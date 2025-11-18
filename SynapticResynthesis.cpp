#include "SynapticResynthesis.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"
#include "json.hpp"
#if SR_USE_WEB_UI
#include "Extras/WebView/IPlugWebViewEditorDelegate.h"
#else
#include "plugin_src/ui/IGraphicsUI.h"
#include "plugin_src/ui/controls/UIControls.h"
#endif
#include "plugin_src/transformers/TransformerFactory.h"
#include "plugin_src/PlatformFileDialogs.h"
#include "plugin_src/params/DynamicParamSchema.h"
#include "plugin_src/morph/MorphFactory.h"
#include "plugin_src/modules/WindowCoordinator.h"
#include <thread>
#include <mutex>
#ifdef AAX_API
#include "IPlugAAX.h"
#endif

namespace {
  static int ComputeTotalParams()
  {
    // Fixed params are enumerated in EParams up to kNumParams.
    // Dynamic transformer and morph params are appended after kNumParams.
    std::vector<synaptic::ExposedParamDesc> unionDescs;
    // Replicate union logic across both factories
    for (const auto& info : synaptic::TransformerFactory::GetAll())
    {
      auto t = info.create();
      std::vector<synaptic::ExposedParamDesc> tmp;
      t->GetParamDescs(tmp, true);  // includeAll=true to get ALL params for binding
      for (const auto& d : tmp)
      {
        auto it = std::find_if(unionDescs.begin(), unionDescs.end(), [&](const auto& e){ return e.id == d.id; });
        if (it == unionDescs.end()) unionDescs.push_back(d);
      }
    }
    for (const auto& info : synaptic::MorphFactory::GetAll())
    {
      auto m = info.create();
      std::vector<synaptic::ExposedParamDesc> tmp;
      m->GetParamDescs(tmp, true);  // includeAll=true to get ALL params for binding
      for (const auto& d : tmp)
      {
        auto it = std::find_if(unionDescs.begin(), unionDescs.end(), [&](const auto& e){ return e.id == d.id; });
        if (it == unionDescs.end()) unionDescs.push_back(d);
      }
    }
    return kNumParams + (int) unionDescs.size();
  }

#if !SR_USE_WEB_UI && IPLUG_EDITOR
  // Helper to get the active C++ UI instance for this plugin's IGraphics.
  // Ensures we only talk to a SynapticUI whose graphics() matches this
  // instance's GetUI(), which avoids use-after-free and cross-instance access.
  static synaptic::ui::SynapticUI* GetActiveCppUIForGraphics(iplug::igraphics::IGraphics* graphics)
  {
    if (!graphics)
      return nullptr;

    if (auto* ui = synaptic::GetSynapticUI())
    {
      if (ui->graphics() == graphics)
        return ui;
    }

    return nullptr;
  }
#endif
}

SynapticResynthesis::SynapticResynthesis(const InstanceInfo& info)
: Plugin(info, MakeConfig(ComputeTotalParams(), kNumPresets))
, mUIBridge(this)
, mBrainManager(&mBrain, &mAnalysisWindow, &mUIBridge)
, mWindowCoordinator(&mAnalysisWindow, &mOutputWindow, &mBrain, &mChunker, &mParamManager, &mBrainManager, &mProgressOverlayMgr)
#if SR_USE_WEB_UI
, mProgressOverlayMgr(&mUIBridge)
#else
, mProgressOverlayMgr(nullptr) // C++ UI mode - no UIBridge needed
#endif
{
  GetParam(kInGain)->InitGain("Input Gain", 0.0, -70, 12.);
  GetParam(kOutGain)->InitGain("Output Gain", 0.0, -70, 12.);
  GetParam(kAGC)->InitBool("AGC", false);
  GetParam(kWindowLock)->InitBool("Window Lock", true); // Default to locked (synchronized)

  // Initialize DSP config with defaults
  mDSPConfig.chunkSize = 3000;
  mDSPConfig.bufferWindowSize = 1;
  mDSPConfig.outputWindowMode = 1;
  mDSPConfig.analysisWindowMode = 1;
  mDSPConfig.algorithmId = 0;
  mDSPConfig.enableOverlapAdd = true;

#if SR_USE_WEB_UI
#ifdef DEBUG
  SetEnableDevTools(true);
  #endif
#endif

#if SR_USE_WEB_UI
static std::atomic<bool> inited { false };
mEditorInitFunc = [this]() {
  bool expected = false;
  if (!inited.compare_exchange_strong(expected, true)) return;
  LoadIndexHtml(__FILE__, GetBundleID());
  EnableScroll(false);
};
#else
  #if IPLUG_EDITOR
  // IGraphics UI setup
  mMakeGraphicsFunc = [&]() {
    return igraphics::MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](igraphics::IGraphics* pGraphics) {
    synaptic::BuildIGraphicsLayout(pGraphics);
  };
  #endif
#endif

  MakePreset("One", -70.);
  MakePreset("Two", -30.);
  MakePreset("Three", 0.);

  // Default transformer = first UI-visible entry
  mTransformer = synaptic::TransformerFactory::CreateByUiIndex(mDSPConfig.algorithmId);
  if (auto sb = dynamic_cast<synaptic::BaseSampleBrainTransformer*>(mTransformer.get()))
    sb->SetBrain(&mBrain);

  // Default morph = first UI-visible entry
  mMorph = synaptic::MorphFactory::CreateByUiIndex(0);
  mChunker.SetMorph(mMorph);

  // Initialize analysis window with default Hann window
  mAnalysisWindow.Set(synaptic::Window::Type::Hann, mDSPConfig.chunkSize);

  // Set the window reference in the Brain
  mBrain.SetWindow(&mAnalysisWindow);

  // Note: OnReset will be called later with proper channel counts

  // Initialize parameters using ParameterManager
  mParamManager.InitializeCoreParameters(this, mDSPConfig);
  mParamManager.InitializeTransformerParameters(this);
}

void SynapticResynthesis::DrainUiQueueOnMainThread()
{
  // Coalesce structured resend flags first
#if SR_USE_WEB_UI
  // For WebUI, handle BrainSummary here
  if (CheckAndClearPendingUpdate(PendingUpdate::BrainSummary))
  {
    mUIBridge.SendBrainSummary(mBrain);
  }
  if (CheckAndClearPendingUpdate(PendingUpdate::DSPConfig))
  {
    SyncAndSendDSPConfig();
  }
#endif
  // MarkDirty is shared by both UI modes
  if (CheckAndClearPendingUpdate(PendingUpdate::MarkDirty))
    MarkHostStateDirty();

  // Drain UIBridge queue
  mUIBridge.DrainQueue();

  // Apply any pending imported settings (chunk size + analysis window) on main thread
  {
      const int impCS = mBrainManager.GetPendingImportedChunkSize();
    const int impAW = mBrainManager.GetPendingImportedAnalysisWindow();
    if (impCS > 0 || impAW > 0)
    {
      const int chunkSizeIdx = mParamManager.GetChunkSizeParamIdx();
      const int analysisWindowIdx = mParamManager.GetAnalysisWindowParamIdx();

        if (impCS > 0 && chunkSizeIdx >= 0)
        {
          synaptic::ParameterManager::SetParameterFromUI(this, chunkSizeIdx, (double) impCS);
          mDSPConfig.chunkSize = impCS;
          mChunker.SetChunkSize(mDSPConfig.chunkSize);
        }
        if (impAW > 0 && analysisWindowIdx >= 0)
        {
          const int idx = std::clamp(impAW - 1, 0, 3);

          // Check if we need to unlock BEFORE setting the analysis window parameter
          // (to prevent OnParamChange from syncing output window)
          if (GetParam(kWindowLock)->Bool())
          {
            const int currentOutputWindowIdx = GetParam(kOutputWindow)->Int();

            // If imported analysis window differs from current output window, unlock FIRST
            if (idx != currentOutputWindowIdx)
            {
              // Unlock the windows since they're now different
              GetParam(kWindowLock)->Set(0.0); // Set to unlocked directly
              synaptic::ParameterManager::SetParameterFromUI(this, kWindowLock, 0.0); // Also notify UI
              MarkHostStateDirty(); // Mark state as dirty so unlock gets saved
            }
          }

          // Now set the analysis window parameter (won't trigger sync if we just unlocked)
          SetPendingUpdate(PendingUpdate::SuppressAnalysisReanalyze);
          synaptic::ParameterManager::SetParameterFromUI(this, analysisWindowIdx, (double) idx);
          mDSPConfig.analysisWindowMode = impAW;
        }
      // Update analysis window instance and Brain pointer, but suppress auto-reanalysis because data already analyzed in file
      mWindowCoordinator.UpdateBrainAnalysisWindow(mDSPConfig);

      // Force UI controls to update immediately after import
      mWindowCoordinator.SyncWindowControls(GetUI());

      // Refresh windowing and latency to reflect new settings
      mWindowCoordinator.UpdateChunkerWindowing(mDSPConfig, mTransformer.get());
      SetLatency(ComputeLatencySamples());

#if SR_USE_WEB_UI
      // Also update web UI's brain chunk size label explicitly
      {
        nlohmann::json j; j["id"] = "brainChunkSize"; j["size"] = mDSPConfig.chunkSize;
        const std::string payload = j.dump();
        SendArbitraryMsgFromDelegate(-1, (int)payload.size(), payload.c_str());
      }
#endif

      // Send DSP config to UI
      SyncAndSendDSPConfig();

#if !SR_USE_WEB_UI && IPLUG_EDITOR
      // Trigger full UI rebuild to sync all controls with imported parameters
      SetPendingUpdate(PendingUpdate::RebuildTransformer);
#endif
    }
  }
}


void SynapticResynthesis::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  // Thread-safe transformer swap: check if there's a pending transformer and swap it on audio thread
  if (mPendingTransformer)
  {
    mTransformer = std::move(mPendingTransformer);
    mPendingTransformer.reset();
    // Update latency after swap
    SetLatency(ComputeLatencySamples());

    // Also apply bindings to the newly swapped transformer
    mParamManager.ApplyBindingsToOwners(this, mTransformer.get(), mMorph.get());
  }

  // Thread-safe morph swap: check if there's a pending morph and swap it on audio thread
  if (mPendingMorph)
  {
    mMorph = std::move(mPendingMorph);
    mPendingMorph.reset();
    mChunker.SetMorph(mMorph);

    // Also apply bindings to the newly swapped morph
    mParamManager.ApplyBindingsToOwners(this, mTransformer.get(), mMorph.get());
  }

  const double inGain = GetParam(kInGain)->DBToAmp();
  const double outGain = GetParam(kOutGain)->DBToAmp();
  const double agcEnabled = GetParam(kAGC)->Bool();

  // Safety check for valid inputs/outputs
  const int inChans = NInChansConnected();
  const int outChans = NOutChansConnected();
  if (inChans <= 0 || outChans <= 0 || !inputs || !outputs)
  {
    // Clear outputs and return
    for (int ch = 0; ch < outChans; ++ch)
      if (outputs[ch])
        std::memset(outputs[ch], 0, sizeof(sample) * nFrames);
    return;
  }

  for (int s = 0; s < nFrames; s++)
  {
    const double smoothedInGain = mInGainSmoother.Process(inGain);
    for (int ch = 0; ch < inChans; ch++)
    {
      inputs[ch][s] *= smoothedInGain;
    }
  }

  // Feed the input into the chunker
  mChunker.PushAudio(inputs, nFrames);

  // Transform pending input chunks -> output queue (gate by lookahead)
  if (mTransformer)
  {
    const int required = mTransformer->GetRequiredLookaheadChunks();
    if (mChunker.GetWindowCount() >= required)
      mTransformer->Process(mChunker);
  }

  // Render queued output to the host buffers
  mChunker.RenderOutput(outputs, nFrames, outChans, agcEnabled);

  // Apply gain
  for (int s = 0; s < nFrames; s++)
  {
    const double smoothedOutGain = mOutGainSmoother.Process(outGain);
    for (int ch = 0; ch < outChans; ++ch)
      outputs[ch][s] *= smoothedOutGain;
  }
}

void SynapticResynthesis::OnReset()
{
  auto sr = GetSampleRate();
  mInGainSmoother.SetSmoothTime(20., sr);
  mOutGainSmoother.SetSmoothTime(20., sr);

  // Pull current values from IParams into DSPConfig using ParameterManager
  const int chunkSizeIdx = mParamManager.GetChunkSizeParamIdx();
  const int bufferWindowIdx = mParamManager.GetBufferWindowParamIdx();
  const int algorithmIdx = mParamManager.GetAlgorithmParamIdx();
  const int outputWindowIdx = mParamManager.GetOutputWindowParamIdx();
  const int analysisWindowIdx = mParamManager.GetAnalysisWindowParamIdx();
  const int enableOverlapIdx = mParamManager.GetEnableOverlapParamIdx();
  const int autotuneBlendIdx = mParamManager.GetAutotuneBlendParamIdx();

  if (chunkSizeIdx >= 0) mDSPConfig.chunkSize = std::max(1, GetParam(chunkSizeIdx)->Int());
  if (bufferWindowIdx >= 0) mDSPConfig.bufferWindowSize = std::max(1, GetParam(bufferWindowIdx)->Int());
  if (algorithmIdx >= 0) mDSPConfig.algorithmId = GetParam(algorithmIdx)->Int();
  if (outputWindowIdx >= 0) mDSPConfig.outputWindowMode = 1 + std::clamp(GetParam(outputWindowIdx)->Int(), 0, 3);
  if (analysisWindowIdx >= 0) mDSPConfig.analysisWindowMode = 1 + std::clamp(GetParam(analysisWindowIdx)->Int(), 0, 3);
  if (enableOverlapIdx >= 0) mDSPConfig.enableOverlapAdd = GetParam(enableOverlapIdx)->Bool();

  mWindowCoordinator.UpdateBrainAnalysisWindow(mDSPConfig);

  mChunker.SetChunkSize(mDSPConfig.chunkSize);
  mChunker.SetBufferWindowSize(mDSPConfig.bufferWindowSize);
  // Ensure chunker channel count matches current connection
  mChunker.SetNumChannels(NInChansConnected());
  auto& autotune = mChunker.GetAutotuneProcessor();
  autotune.OnReset(sr, mChunker.GetFFTSize(), mChunker.GetNumChannels());
  if (autotuneBlendIdx >= 0)
  {
    const double blendPercent = GetParam(autotuneBlendIdx)->Value();
    autotune.SetBlend((float)(blendPercent / 100.0));
  }
  {
    const int modeIdx = mParamManager.GetAutotuneModeParamIdx();
    if (modeIdx >= 0)
      autotune.SetMode(GetParam(modeIdx)->Int() == 1);
    const int tolIdx = mParamManager.GetAutotuneToleranceOctavesParamIdx();
    if (tolIdx >= 0)
    {
      // Convert enum index (0-4) to octave value (1-5)
      const int enumIdx = std::clamp(GetParam(tolIdx)->Int(), 0, 4);
      autotune.SetToleranceOctaves(enumIdx + 1);
    }
  }
  mChunker.Reset();

  mWindowCoordinator.UpdateChunkerWindowing(mDSPConfig, mTransformer.get());

  // Report algorithmic latency to the host (in samples)
  SetLatency(ComputeLatencySamples());

  if (mTransformer)
    mTransformer->OnReset(sr, mDSPConfig.chunkSize, mDSPConfig.bufferWindowSize, NInChansConnected());

  if (mMorph)
    mMorph->OnReset(sr, mDSPConfig.chunkSize, NInChansConnected());
  mChunker.SetMorph(mMorph);

  // After reset, apply IParam values to transformer implementation using ParameterManager
  mParamManager.ApplyBindingsToOwners(this, mTransformer.get(), mMorph.get());

  // When audio engine resets, leave brain state intact; just resend summary to UI
  mUIBridge.SendBrainSummary(mBrain);
  mUIBridge.SendTransformerParams(mTransformer);
  mUIBridge.SendMorphParams(mMorph);

  // Update and send DSP config to UI
  SyncAndSendDSPConfig();
}

bool SynapticResynthesis::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  return synaptic::UIMessageRouter::Route(this, msgTag, ctrlTag, dataSize, pData);
}

// Message handler implementations are in plugin_src/ui_bridge/MessageHandlers.cpp

void SynapticResynthesis::OnUIOpen()
{
  // Ensure UI gets current values when window opens
  Plugin::OnUIOpen();

#if !SR_USE_WEB_UI && IPLUG_EDITOR
  // Bind this instance's progress overlay manager to the active C++ UI, if any.
  if (auto* ui = GetActiveCppUIForGraphics(GetUI()))
  {
    mProgressOverlayMgr.SetSynapticUI(ui);
  }
  else
  {
    mProgressOverlayMgr.SetSynapticUI(nullptr);
  }
#endif
#if SR_USE_WEB_UI
  mUIBridge.SendTransformerParams(mTransformer);
  mUIBridge.SendMorphParams(mMorph);
  SyncAndSendDSPConfig();
  mUIBridge.SendBrainSummary(mBrain);
#else
  // For C++ UI, rebuild dynamic parameter controls and brain file list
  #if IPLUG_EDITOR
  // Small delay to ensure UI is fully initialized
  static int initCounter = 0;
  initCounter = 0; // Reset on UI open
  #endif
#endif
}

void SynapticResynthesis::OnIdle()
{
  // Drain any UI messages queued by background threads
  DrainUiQueueOnMainThread();

  // Update C++ UI on first idle call after UI open, and process any queued UI work
#if !SR_USE_WEB_UI && IPLUG_EDITOR
  if (auto* ui = GetActiveCppUIForGraphics(GetUI()))
  {
    if (mNeedsInitialUIRebuild)
    {
      SyncAllUIState();
      mNeedsInitialUIRebuild = false;
    }

    // Update C++ UI brain file list if needed
    if (CheckAndClearPendingUpdate(PendingUpdate::BrainSummary))
    {
      SyncBrainUIState();
    }

    // Process progress overlay updates from background threads
    mProgressOverlayMgr.ProcessPendingUpdates(ui);

    // Handle transformer/morph parameter UI rebuild on UI thread
    // Simply rebuild the entire UI - this is reliable and always works correctly
    if (HasPendingUpdate(PendingUpdate::RebuildTransformer) || HasPendingUpdate(PendingUpdate::RebuildMorph))
    {
      // Use pending transformer/morph if available (swap hasn't happened yet), otherwise use current
      // Pass shared_ptr copies to keep objects alive during UI rebuild (prevents race with audio thread)
      std::shared_ptr<const synaptic::IChunkBufferTransformer> currentTransformer =
        mPendingTransformer ? mPendingTransformer : mTransformer;
      std::shared_ptr<const synaptic::IMorph> currentMorph =
        mPendingMorph ? mPendingMorph : mMorph;

      // Update cached context with shared_ptr copies before rebuilding
      ui->setDynamicParamContext(currentTransformer, currentMorph, &mParamManager, this);
      ui->rebuild();

      // Re-sync brain UI state after rebuild (brain controls were wiped and need to be repopulated)
      SyncBrainUIState();

      // Sync window controls after rebuild (they get recreated during rebuild and need current values)
      mWindowCoordinator.SyncWindowControls(GetUI());

      CheckAndClearPendingUpdate(PendingUpdate::RebuildTransformer);
      CheckAndClearPendingUpdate(PendingUpdate::RebuildMorph);
    }
  }

  // Coalesce pending dropped files and start async batch import
  if (mPendingImportScheduled.load())
  {
    if (mPendingImportIdleTicks > 0)
      --mPendingImportIdleTicks;

    if (mPendingImportIdleTicks <= 0)
    {
      // Only launch if no other operation is in progress
      if (!mBrainManager.IsOperationInProgress())
      {
        auto files = std::move(mPendingImportFiles);
        mPendingImportFiles.clear();
        mPendingImportScheduled = false;

        if (!files.empty())
        {
          mProgressOverlayMgr.Show("Importing Files", "Starting...", 0.0f, true);
          mBrainManager.AddMultipleFilesAsync(std::move(files), (int)GetSampleRate(), NInChansConnected(), mDSPConfig.chunkSize,
            MakeProgressCallback(),
            [this](bool wasCancelled)
            {
              mProgressOverlayMgr.Hide();
              if (!wasCancelled)
              {
                SetPendingUpdate(PendingUpdate::BrainSummary);
                MarkHostStateDirty();
              }
              else
              {
                DBGMSG("Multi-file import CANCELLED - partial files may have been imported\n");
                // Note: Partial imports are intentional - successfully imported files are kept
              }
            }
          );
        }
      }
      else
      {
        // Keep scheduled; try again next idle
        mPendingImportIdleTicks = 1;
      }
    }
  }

  if (!GetUI())
  {
    mNeedsInitialUIRebuild = true;
  }
#endif
}

void SynapticResynthesis::OnRestoreState()
{
  Plugin::OnRestoreState();
#if SR_USE_WEB_UI
  mUIBridge.SendTransformerParams(mTransformer);
  mUIBridge.SendMorphParams(mMorph);
  SyncAndSendDSPConfig();
  mUIBridge.SendBrainSummary(mBrain);
#else
  // For C++ UI, rebuild dynamic parameter controls and brain file list
  SyncAllUIState();
#endif
}

void SynapticResynthesis::OnParamChange(int paramIdx)
{
  // Create context for centralized parameter coordination
  synaptic::ParameterChangeContext ctx;
  ctx.plugin = this;
  ctx.config = &mDSPConfig;
  ctx.chunker = &mChunker;
  ctx.brain = &mBrain;
  ctx.analysisWindow = &mAnalysisWindow;
  ctx.currentTransformer = &mTransformer;
  ctx.pendingTransformer = &mPendingTransformer;
  ctx.currentMorph = &mMorph;
  ctx.pendingMorph = &mPendingMorph;
  ctx.windowCoordinator = &mWindowCoordinator;
  ctx.brainManager = &mBrainManager;
  ctx.progressOverlayMgr = &mProgressOverlayMgr;

  // Bind callbacks
  ctx.setPendingUpdate = [this](uint32_t flag) { SetPendingUpdate((PendingUpdate)flag); };
  ctx.checkAndClearPendingUpdate = [this](uint32_t flag) { return CheckAndClearPendingUpdate((PendingUpdate)flag); };
  ctx.computeLatency = [this]() { return ComputeLatencySamples(); };
  ctx.setLatency = [this](int latency) { SetLatency(latency); };

  // Delegate to ParameterManager for all coordination
  mParamManager.OnParamChange(paramIdx, ctx);

  // For all parameters (including kAGC, kInGain, kOutGain, and any others),
  // the base Plugin class will notify parameter-bound controls automatically.
  // By not returning early, we let the default notification mechanism work.
}

void SynapticResynthesis::ProcessMidiMsg(const IMidiMsg& msg)
{
  TRACE;

  msg.PrintMsg();
  SendMidiMsg(msg);
}

void SynapticResynthesis::MarkHostStateDirty()
{
  // Cross-API lightweight dirty notification.
  // AAX: bump compare state so the compare light turns on.
#ifdef AAX_API
  if (auto* aax = dynamic_cast<IPlugAAX*>(this))
  {
    aax->DirtyPTCompareState();
  }
#endif
  // For VST3/others, ping a single parameter (use hidden non-automatable dirty flag)
  int idx = mParamManager.GetDirtyFlagParamIdx();
  if (idx < 0) idx = mParamManager.GetBufferWindowParamIdx();
  if (idx < 0) idx = 0;

  if (idx >= 0 && GetParam(idx))
  {
    // Toggle value quickly to ensure a host-visible delta without semantic changes
    const bool cur = GetParam(idx)->Bool();
    const double norm = GetParam(idx)->ToNormalized(cur ? 0.0 : 1.0);
    BeginInformHostOfParamChangeFromUI(idx);
    SendParameterValueFromUI(idx, norm);
    EndInformHostOfParamChangeFromUI(idx);
  }
}

void SynapticResynthesis::SyncAndSendDSPConfig()
{
  mDSPConfig.useExternalBrain = mBrainManager.UseExternal();
  mDSPConfig.externalPath = mBrainManager.UseExternal() ? mBrainManager.ExternalPath() : std::string();
  int morphIdx = 0;
  {
    const int morphModeParamIdx = mParamManager.GetMorphModeParamIdx();
    if (morphModeParamIdx >= 0) morphIdx = GetParam(morphModeParamIdx)->Int();
  }
  mUIBridge.SendDSPConfigWithAlgorithms(mDSPConfig, morphIdx);
}


synaptic::BrainManager::ProgressFn SynapticResynthesis::MakeProgressCallback()
{
  return [this](const std::string& fileName, int current, int total)
  {
    const float p = (total > 0) ? ((float)current / (float)total * 100.0f) : 50.0f;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s (chunk %d/%d)", fileName.c_str(), current, total);
    mProgressOverlayMgr.Update(buf, p);
  };
}

synaptic::BrainManager::CompletionFn SynapticResynthesis::MakeStandardCompletionCallback()
{
  return [this](bool wasCancelled)
  {
    mProgressOverlayMgr.Hide();
    if (!wasCancelled)
    {
      SetPendingUpdate(PendingUpdate::BrainSummary);
      SetPendingUpdate(PendingUpdate::MarkDirty);
    }
  };
}


void SynapticResynthesis::SyncBrainUIState()
{
#if !SR_USE_WEB_UI && IPLUG_EDITOR
  auto* ui = GetActiveCppUIForGraphics(GetUI());
  if (!ui) return;

  // Convert Brain summary to UI format
  auto brainSummary = mBrain.GetSummary();
  std::vector<synaptic::ui::BrainFileEntry> uiEntries;
  for (const auto& s : brainSummary)
  {
    uiEntries.push_back({s.id, s.name, s.chunkCount});
  }
  ui->updateBrainFileList(uiEntries);

  // Update brain state (storage info, button visibility, control states)
  // Single source of truth: all UI state is derived from UseExternal()
  ui->updateBrainState(mBrainManager.UseExternal(), mBrainManager.ExternalPath());

  // Update the UI toggle control to reflect the current setting
  // Note: We don't change the flag here - we just sync the UI to match it
  auto* compactToggle = ui->getCompactModeToggle();
  if (compactToggle)
  {
    compactToggle->SetValue(synaptic::Brain::sUseCompactBrainFormat ? 1.0 : 0.0);
    compactToggle->SetDirty(false); // Don't trigger change notification
  }
#endif
}

void SynapticResynthesis::SyncAllUIState()
{
#if !SR_USE_WEB_UI && IPLUG_EDITOR
  auto* ui = GetActiveCppUIForGraphics(GetUI());
  if (!ui) return;

  // Update context with shared_ptr copies to prevent race conditions
  ui->setDynamicParamContext(mTransformer, mMorph, &mParamManager, this);

  // Rebuild dynamic params
  ui->rebuildDynamicParams(synaptic::ui::DynamicParamType::Transformer, mTransformer.get(), mParamManager, this);
  ui->rebuildDynamicParams(synaptic::ui::DynamicParamType::Morph, mMorph.get(), mParamManager, this);

  // Sync brain state
  SyncBrainUIState();

  // Resize to fit
  ui->resizeWindowToFitContent();
#endif
}

bool SynapticResynthesis::SerializeState(IByteChunk& chunk) const
{
  if (!Plugin::SerializeState(chunk)) return false;

  // Use StateSerializer to append brain state.
  // For C++ UI mode, only pass the overlay manager if this instance currently
  // has an active graphics/UI bound, to avoid dereferencing stale UI pointers.
#if !SR_USE_WEB_UI && IPLUG_EDITOR
  auto* self = const_cast<SynapticResynthesis*>(this);
  auto* ui = GetActiveCppUIForGraphics(self->GetUI());
  synaptic::ui::ProgressOverlayManager* overlayMgr = ui ? &self->mProgressOverlayMgr : nullptr;
  return mStateSerializer.SerializeBrainState(chunk, mBrain, mBrainManager, overlayMgr);
#else
  return mStateSerializer.SerializeBrainState(chunk, mBrain, mBrainManager, &mProgressOverlayMgr);
#endif
}

int SynapticResynthesis::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int pos = Plugin::UnserializeState(chunk, startPos);
  if (pos < 0) return pos;

  // Use StateSerializer to deserialize brain state
  pos = mStateSerializer.DeserializeBrainState(chunk, pos, mBrain, mBrainManager);

  // When loading project state, sync the compact mode setting from the loaded brain
  // This ensures the toggle matches what format was loaded
  synaptic::Brain::sUseCompactBrainFormat = mBrain.WasLastLoadedInCompactFormat();

  // Re-link window pointer and notify UI
  mBrain.SetWindow(&mAnalysisWindow);
  mUIBridge.SendBrainSummary(mBrain);

  SyncAndSendDSPConfig();

  mUIBridge.SendTransformerParams(mTransformer);
  mUIBridge.SendMorphParams(mMorph);
  mUIBridge.SendExternalRefInfo(mBrainManager.UseExternal(), mBrainManager.ExternalPath());

  return pos;
}
