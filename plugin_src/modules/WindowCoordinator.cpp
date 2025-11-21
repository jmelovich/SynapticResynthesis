#include "WindowCoordinator.h"
#include "SynapticResynthesis.h"
#include "plugin_src/modules/AudioStreamChunker.h"
#include "plugin_src/audio/Window.h"
#include "plugin_src/brain/Brain.h"
#include "plugin_src/brain/BrainManager.h"
#include "plugin_src/params/ParameterManager.h"
#include "plugin_src/transformers/BaseTransformer.h"
#include "plugin_src/modules/DSPConfig.h"
#include "plugin_src/ui/core/ProgressOverlayManager.h"

#if !SR_USE_WEB_UI && IPLUG_EDITOR
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
  // Validate chunk size
  if (config.chunkSize <= 0)
  {
    DBGMSG("Warning: Invalid chunk size %d in WindowCoordinator\n", config.chunkSize);
    return;
  }

  // Set up output window first
  mOutputWindow->Set(Window::IntToType(config.outputWindowMode), config.chunkSize);

  // Configure overlap behavior based on user setting and transformer capabilities
  // Note: Rectangular windows have mOverlap=0.0, so they'll use hopSize=chunkSize in the OLA path
  // This still works correctly but uses the more efficient bulk processing code
  const bool transformerWantsOverlap = transformer ? transformer->WantsOverlapAdd() : true;
  const bool shouldUseOverlap = config.enableOverlapAdd && transformerWantsOverlap;

  mChunker->EnableOverlap(shouldUseOverlap);
  mChunker->SetOutputWindow(*mOutputWindow);

  // Keep the chunker's input analysis window aligned with Brain analysis window
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
  config.analysisWindowMode = 1 + std::clamp(outputWindowIdx, 0, 3);
  this->UpdateBrainAnalysisWindow(config);
  ParameterManager::SetParameterFromUI(plugin, kAnalysisWindow, (double)outputWindowIdx);

  // Trigger reanalysis and UI update
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

  // Update output window to match analysis window
  config.outputWindowMode = 1 + std::clamp(analysisWindowIdx, 0, 3);

  // Note: UpdateChunkerWindowing needs to be called by plugin after this
  // because it needs access to the current transformer
  ParameterManager::SetParameterFromUI(plugin, kOutputWindow, (double)analysisWindowIdx);

  // UI update only (no reanalysis needed for output window)
  this->SyncWindowControls(plugin->GetUI());
}

void WindowCoordinator::SyncWindowControls(void* graphicsPtr)
{
#if !SR_USE_WEB_UI && IPLUG_EDITOR
  auto* graphics = static_cast<iplug::igraphics::IGraphics*>(graphicsPtr);
  if (!graphics) return;

  // Find and sync all controls bound to window parameters
  int numControls = graphics->NControls();

  for (int i = 0; i < numControls; ++i)
  {
    auto* ctrl = graphics->GetControl(i);
    if (!ctrl) continue;

    const int paramIdx = ctrl->GetParamIdx();
    if (paramIdx == kOutputWindow || paramIdx == kAnalysisWindow || paramIdx == kWindowLock)
    {
      // Access param through graphics delegate (the plugin)
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

  // Also mark all controls as dirty to force redraw
  graphics->SetAllControlsDirty();
#endif
}

void WindowCoordinator::TriggerBrainReanalysisAsync(
  int sampleRate,
  std::function<void(bool wasCancelled)> completion)
{
#if !SR_USE_WEB_UI
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
#else
  // Web UI: silent background reanalysis
  mBrainManager->ReanalyzeAllChunksAsync(
    sampleRate,
    [](const std::string&, int, int){},  // Empty progress callback
    completion
  );
#endif
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

  // If windows are already in sync, nothing to do
  if (analysisWindowIdx == outputWindowIdx)
    return;

  // Sync based on which window's lock button was clicked
  if (clickedWindowParam == kOutputWindow)
  {
    // Output window lock was clicked - sync output to analysis
    this->SyncOutputToAnalysisWindow(plugin, config);
  }
  else if (clickedWindowParam == kAnalysisWindow)
  {
    // Analysis window lock was clicked - sync analysis to output
    this->SyncAnalysisToOutputWindow(plugin, config);
  }
  else
  {
    // Fallback: sync output to analysis
    this->SyncOutputToAnalysisWindow(plugin, config);
  }
}

// === Private Helper Methods ===

std::function<void(const std::string&, int, int)> WindowCoordinator::MakeProgressCallback()
{
  return [this](const std::string& fileName, int current, int total)
  {
    if (!mProgressOverlayMgr)
      return;

    const float p = (total > 0) ? ((float)current / (float)total * 100.0f) : 50.0f;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s (chunk %d/%d)", fileName.c_str(), current, total);
    mProgressOverlayMgr->Update(buf, p);
  };
}

} // namespace synaptic

