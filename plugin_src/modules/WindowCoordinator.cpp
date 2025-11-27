/**
 * @file WindowCoordinator.cpp
 * @brief Implementation of window coordination across the plugin
 */

#include "WindowCoordinator.h"
#include "WindowModeHelpers.h"
#include "SynapticResynthesis.h"
#include "plugin_src/modules/AudioStreamChunker.h"
#include "plugin_src/audio/Window.h"
#include "plugin_src/brain/Brain.h"
#include "plugin_src/brain/BrainManager.h"
#include "plugin_src/params/ParameterManager.h"
#include "plugin_src/transformers/BaseTransformer.h"
#include "plugin_src/modules/DSPConfig.h"
#include "plugin_src/ui/core/ProgressOverlayManager.h"
#include "plugin_src/ui/core/UIConstants.h"
#include "plugin_src/Structs.h"

#if IPLUG_EDITOR
  #include "plugin_src/ui/controls/UIControls.h"
#endif

namespace synaptic {

WindowCoordinator::WindowCoordinator(
  Window* analysisWindow,
  Window* outputWindow,
  Brain* brain,
  AudioStreamChunker* chunker,
  ParameterManager* paramManager,
  BrainManager* brainManager,
  ui::ProgressOverlayManager* progressOverlayMgr
)
  : mAnalysisWindow(analysisWindow)
  , mOutputWindow(outputWindow)
  , mBrain(brain)
  , mChunker(chunker)
  , mParamManager(paramManager)
  , mBrainManager(brainManager)
  , mProgressOverlayMgr(progressOverlayMgr)
{
}

void WindowCoordinator::UpdateChunkerWindowing(const DSPConfig& config, IChunkBufferTransformer* transformer)
{
  if (config.chunkSize <= 0)
  {
    DBGMSG("Warning: Invalid chunk size %d in WindowCoordinator\n", config.chunkSize);
    return;
  }

  mOutputWindow->Set(Window::IntToType(config.outputWindowMode), config.chunkSize);

  const bool transformerWantsOverlap = transformer ? transformer->WantsOverlapAdd() : true;
  const bool shouldUseOverlap = config.enableOverlapAdd && transformerWantsOverlap;

  mChunker->EnableOverlap(shouldUseOverlap);
  mChunker->SetOutputWindow(*mOutputWindow);
  mChunker->SetInputAnalysisWindow(*mAnalysisWindow);

  DBGMSG("Window config: type=%d, userEnabled=%s, shouldUseOverlap=%s, chunkSize=%d\n",
         config.outputWindowMode, config.enableOverlapAdd ? "true" : "false",
         shouldUseOverlap ? "true" : "false", config.chunkSize);
}

void WindowCoordinator::UpdateBrainAnalysisWindow(const DSPConfig& config)
{
  mAnalysisWindow->Set(Window::IntToType(config.analysisWindowMode), config.chunkSize);
  mBrain->SetWindow(mAnalysisWindow);
}

void WindowCoordinator::SyncAnalysisToOutputWindow(
  void* pluginPtr,
  DSPConfig& config,
  bool triggerReanalysis)
{
  auto* plugin = static_cast<iplug::Plugin*>(pluginPtr);
  const int outputWindowIdx = plugin->GetParam(kOutputWindow)->Int();

  // Update analysis window to match output window
  config.analysisWindowMode = WindowMode::ParamToConfig(outputWindowIdx);
  this->UpdateBrainAnalysisWindow(config);
  ParameterManager::SetParameterFromUI(plugin, kAnalysisWindow, (double)outputWindowIdx);

  if (triggerReanalysis)
  {
    this->TriggerBrainReanalysisAsync((int)plugin->GetSampleRate(), [](bool){});
  }

  this->SyncWindowControls(plugin->GetUI());
}

void WindowCoordinator::SyncOutputToAnalysisWindow(
  void* pluginPtr,
  DSPConfig& config)
{
  auto* plugin = static_cast<iplug::Plugin*>(pluginPtr);
  const int analysisWindowIdx = plugin->GetParam(kAnalysisWindow)->Int();

  config.outputWindowMode = WindowMode::ParamToConfig(analysisWindowIdx);

  ParameterManager::SetParameterFromUI(plugin, kOutputWindow, (double)analysisWindowIdx);
  this->SyncWindowControls(plugin->GetUI());
}

void WindowCoordinator::SyncWindowControls(void* graphicsPtr)
{
#if IPLUG_EDITOR
  auto* graphics = static_cast<iplug::igraphics::IGraphics*>(graphicsPtr);
  if (!graphics) return;

  int numControls = graphics->NControls();

  for (int i = 0; i < numControls; ++i)
  {
    auto* ctrl = graphics->GetControl(i);
    if (!ctrl) continue;

    const int paramIdx = ctrl->GetParamIdx();
    if (paramIdx == kOutputWindow || paramIdx == kAnalysisWindow || paramIdx == kWindowLock)
    {
      if (auto* plugin = dynamic_cast<iplug::Plugin*>(graphics->GetDelegate()))
      {
        if (const iplug::IParam* pParam = plugin->GetParam(paramIdx))
        {
          ctrl->SetValueFromDelegate(pParam->GetNormalized());
          ctrl->SetDirty(false);
        }
      }
    }
  }

  graphics->SetAllControlsDirty();
#endif
}

void WindowCoordinator::TriggerBrainReanalysisAsync(
  int sampleRate,
  std::function<void(bool wasCancelled)> completion)
{
  if (mProgressOverlayMgr)
  {
    mProgressOverlayMgr->Show("Reanalyzing", "Starting...", 0.0f, true);
  }

  mBrainManager->ReanalyzeAllChunksAsync(
    sampleRate,
    MakeProgressCallback(),
    [this, completion](bool wasCancelled)
    {
      if (mProgressOverlayMgr)
      {
        mProgressOverlayMgr->Hide();
      }
      if (completion)
      {
        completion(wasCancelled);
      }
    }
  );
}

void WindowCoordinator::HandleWindowLockToggle(
  bool isLocked,
  int clickedWindowParam,
  void* pluginPtr,
  DSPConfig& config)
{
  auto* plugin = static_cast<iplug::Plugin*>(pluginPtr);

  // Only sync when lock is toggled ON
  if (!isLocked)
    return;

  const int analysisWindowIdx = plugin->GetParam(kAnalysisWindow)->Int();
  const int outputWindowIdx = plugin->GetParam(kOutputWindow)->Int();

  if (analysisWindowIdx == outputWindowIdx)
    return;

  if (clickedWindowParam == kOutputWindow)
  {
    this->SyncOutputToAnalysisWindow(plugin, config);
  }
  else if (clickedWindowParam == kAnalysisWindow)
  {
    this->SyncAnalysisToOutputWindow(plugin, config);
  }
  else
  {
    this->SyncOutputToAnalysisWindow(plugin, config);
  }
}

std::function<void(const std::string&, int, int)> WindowCoordinator::MakeProgressCallback()
{
  return [this](const std::string& fileName, int current, int total)
  {
    if (!mProgressOverlayMgr)
      return;

    const float p = (total > 0)
      ? ((float)current / (float)total * ui::Progress::kMaxProgress)
      : ui::Progress::kDefaultProgress;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s (chunk %d/%d)", fileName.c_str(), current, total);
    mProgressOverlayMgr->Update(buf, p);
  };
}

} // namespace synaptic
