#include "SynapticResynthesis.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"
#include "json.hpp"
#if SR_USE_WEB_UI
#include "Extras/WebView/IPlugWebViewEditorDelegate.h"
#else
#include "plugin_src/ui/IGraphicsUI.h"
#endif
#include "plugin_src/TransformerFactory.h"
#include "plugin_src/PlatformFileDialogs.h"
#include "plugin_src/params/DynamicParamSchema.h"
#include "plugin_src/morph/MorphFactory.h"
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
      t->GetParamDescs(tmp);
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
      m->GetParamDescs(tmp);
      for (const auto& d : tmp)
      {
        auto it = std::find_if(unionDescs.begin(), unionDescs.end(), [&](const auto& e){ return e.id == d.id; });
        if (it == unionDescs.end()) unionDescs.push_back(d);
      }
    }
    return kNumParams + (int) unionDescs.size();
  }
}

SynapticResynthesis::SynapticResynthesis(const InstanceInfo& info)
: Plugin(info, MakeConfig(ComputeTotalParams(), kNumPresets))
, mUIBridge(this)
, mBrainManager(&mBrain, &mAnalysisWindow, &mUIBridge)
{
  GetParam(kInGain)->InitGain("Input Gain", 0.0, -70, 0.);
  GetParam(kOutGain)->InitGain("Output Gain", 0.0, -70, 0.);
  GetParam(kAGC)->InitBool("AGC", false);

  // Initialize DSP config with defaults
  mDSPConfig.chunkSize = 3000;
  mDSPConfig.bufferWindowSize = 1;
  mDSPConfig.outputWindowMode = 1;
  mDSPConfig.analysisWindowMode = 1;
  mDSPConfig.algorithmId = 0;
  mDSPConfig.enableOverlapAdd = true;

#ifdef DEBUG
  SetEnableDevTools(true);
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
  if (mPendingSendBrainSummary.exchange(false))
  {
#if SR_USE_WEB_UI
    mUIBridge.SendBrainSummary(mBrain);
#endif
  }
  if (mPendingSendDSPConfig.exchange(false))
  {
#if SR_USE_WEB_UI
    SyncAndSendDSPConfig();
#endif
  }
  if (mPendingMarkDirty.exchange(false))
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
        SetParameterFromUI(chunkSizeIdx, (double) impCS);
        mDSPConfig.chunkSize = impCS;
        mChunker.SetChunkSize(mDSPConfig.chunkSize);
      }
      if (impAW > 0 && analysisWindowIdx >= 0)
      {
        const int idx = std::clamp(impAW - 1, 0, 3);
        mSuppressNextAnalysisReanalyze = true;
        SetParameterFromUI(analysisWindowIdx, (double) idx);
        mDSPConfig.analysisWindowMode = impAW;
      }
      // Update analysis window instance and Brain pointer, but suppress auto-reanalysis because data already analyzed in file
      UpdateBrainAnalysisWindow();

      // Send DSP config to UI
      SyncAndSendDSPConfig();
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

  if (chunkSizeIdx >= 0) mDSPConfig.chunkSize = std::max(1, GetParam(chunkSizeIdx)->Int());
  if (bufferWindowIdx >= 0) mDSPConfig.bufferWindowSize = std::max(1, GetParam(bufferWindowIdx)->Int());
  if (algorithmIdx >= 0) mDSPConfig.algorithmId = GetParam(algorithmIdx)->Int();
  if (outputWindowIdx >= 0) mDSPConfig.outputWindowMode = 1 + std::clamp(GetParam(outputWindowIdx)->Int(), 0, 3);
  if (analysisWindowIdx >= 0) mDSPConfig.analysisWindowMode = 1 + std::clamp(GetParam(analysisWindowIdx)->Int(), 0, 3);
  if (enableOverlapIdx >= 0) mDSPConfig.enableOverlapAdd = GetParam(enableOverlapIdx)->Bool();

  UpdateBrainAnalysisWindow();

  mChunker.SetChunkSize(mDSPConfig.chunkSize);
  mChunker.SetBufferWindowSize(mDSPConfig.bufferWindowSize);
  // Ensure chunker channel count matches current connection
  mChunker.SetNumChannels(NInChansConnected());
  mChunker.Reset();

  UpdateChunkerWindowing();

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

// Message handler implementations are in plugin_src/modules/MessageHandlers.cpp

void SynapticResynthesis::OnUIOpen()
{
  // Ensure UI gets current values when window opens
  Plugin::OnUIOpen();
#if SR_USE_WEB_UI
  mUIBridge.SendTransformerParams(mTransformer);
  mUIBridge.SendMorphParams(mMorph);
  SyncAndSendDSPConfig();
  mUIBridge.SendBrainSummary(mBrain);
#endif
}

void SynapticResynthesis::OnIdle()
{
  // Drain any UI messages queued by background threads
  DrainUiQueueOnMainThread();
}

void SynapticResynthesis::OnRestoreState()
{
  Plugin::OnRestoreState();
#if SR_USE_WEB_UI
  mUIBridge.SendTransformerParams(mTransformer);
  mUIBridge.SendMorphParams(mMorph);
  SyncAndSendDSPConfig();
  mUIBridge.SendBrainSummary(mBrain);
#endif
}

void SynapticResynthesis::OnParamChange(int paramIdx)
{
  // Handle chunk size parameter (coordinated by ParameterManager)
  if (paramIdx == mParamManager.GetChunkSizeParamIdx())
  {
    mParamManager.HandleChunkSizeChange(paramIdx, GetParam(paramIdx), mDSPConfig, this, mChunker, mAnalysisWindow);
    UpdateChunkerWindowing();
    SetLatency(ComputeLatencySamples());
  }
  // Handle buffer window parameter
  else if (paramIdx == mParamManager.GetBufferWindowParamIdx())
  {
    mParamManager.HandleCoreParameterChange(paramIdx, GetParam(paramIdx), mDSPConfig);
    mChunker.SetBufferWindowSize(mDSPConfig.bufferWindowSize);
  }
  // Handle algorithm change (coordinated by ParameterManager)
  else if (paramIdx == mParamManager.GetAlgorithmParamIdx())
  {
    // Store new transformer in pending slot for thread-safe swap in ProcessBlock
    mPendingTransformer = mParamManager.HandleAlgorithmChange(paramIdx, GetParam(paramIdx), mDSPConfig,
                                                               this, mBrain, GetSampleRate(), NInChansConnected());
    UpdateChunkerWindowing();
    // Note: SetLatency will be called in ProcessBlock after swap
  }
  // Handle output window
  else if (paramIdx == mParamManager.GetOutputWindowParamIdx())
  {
    mParamManager.HandleCoreParameterChange(paramIdx, GetParam(paramIdx), mDSPConfig);
    UpdateChunkerWindowing();
  }
  // Handle analysis window (coordinated by ParameterManager, with background reanalysis)
  else if (paramIdx == mParamManager.GetAnalysisWindowParamIdx())
  {
    mParamManager.HandleCoreParameterChange(paramIdx, GetParam(paramIdx), mDSPConfig);
    UpdateBrainAnalysisWindow();

    // Kick background reanalysis when analysis window changes via host/restore, unless suppressed
    if (!mSuppressNextAnalysisReanalyze.exchange(false))
    {
      mBrainManager.ReanalyzeAllChunksAsync((int)GetSampleRate(), [this]()
      {
        mPendingSendBrainSummary = true;
        mPendingMarkDirty = true;
      });
    }
    mPendingSendDSPConfig = true;
  }
  // Handle overlap enable
  else if (paramIdx == mParamManager.GetEnableOverlapParamIdx())
  {
    mParamManager.HandleCoreParameterChange(paramIdx, GetParam(paramIdx), mDSPConfig);
    UpdateChunkerWindowing();
  }
  // Handle morph mode change
  else if (paramIdx == mParamManager.GetMorphModeParamIdx())
  {
    // Create/reset new IMorph instance and apply bindings
    mMorph = mParamManager.HandleMorphModeChange(paramIdx, GetParam(paramIdx), this,
                                                 GetSampleRate(), mDSPConfig.chunkSize, NInChansConnected());
    mChunker.SetMorph(mMorph);
    mUIBridge.SendMorphParams(mMorph);
  }
  // Handle dynamic parameters using ParameterManager
  else if (mParamManager.HandleDynamicParameterChange(paramIdx, GetParam(paramIdx), mTransformer.get(), mMorph.get()))
  {
    // Parameter was handled by ParameterManager
  }

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

void SynapticResynthesis::UpdateChunkerWindowing()
{
  // Validate chunk size
  if (mDSPConfig.chunkSize <= 0)
  {
    DBGMSG("Warning: Invalid chunk size %d, using default\n", mDSPConfig.chunkSize);
    mDSPConfig.chunkSize = 3000;
  }

  // Set up output window first
  mOutputWindow.Set(synaptic::Window::IntToType(mDSPConfig.outputWindowMode), mDSPConfig.chunkSize);

  // Configure overlap behavior based on user setting, window type, and transformer capabilities
  const bool isRectangular = (mDSPConfig.outputWindowMode == 4); // Rectangular
  const bool transformerWantsOverlap = mTransformer ? mTransformer->WantsOverlapAdd() : true;
  const bool shouldUseOverlap = mDSPConfig.enableOverlapAdd && !isRectangular && transformerWantsOverlap;

  mChunker.EnableOverlap(shouldUseOverlap);
  mChunker.SetOutputWindow(mOutputWindow);

  // Keep the chunker's input analysis window aligned with Brain analysis window
  mChunker.SetInputAnalysisWindow(mAnalysisWindow);

  DBGMSG("Window config: type=%d, userEnabled=%s, shouldUseOverlap=%s, chunkSize=%d\n",
         mDSPConfig.outputWindowMode, mDSPConfig.enableOverlapAdd ? "true" : "false", shouldUseOverlap ? "true" : "false", mDSPConfig.chunkSize);
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

void SynapticResynthesis::SetParameterFromUI(int paramIdx, double value)
{
  const double norm = GetParam(paramIdx)->ToNormalized(value);
  BeginInformHostOfParamChangeFromUI(paramIdx);
  SendParameterValueFromUI(paramIdx, norm);
  EndInformHostOfParamChangeFromUI(paramIdx);
}

void SynapticResynthesis::UpdateBrainAnalysisWindow()
{
  mAnalysisWindow.Set(synaptic::Window::IntToType(mDSPConfig.analysisWindowMode), mDSPConfig.chunkSize);
  mBrain.SetWindow(&mAnalysisWindow);
}

bool SynapticResynthesis::SerializeState(IByteChunk& chunk) const
{
  if (!Plugin::SerializeState(chunk)) return false;

  // Use StateSerializer to append brain state
  return mStateSerializer.SerializeBrainState(chunk, mBrain, mBrainManager);
}

int SynapticResynthesis::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int pos = Plugin::UnserializeState(chunk, startPos);
  if (pos < 0) return pos;

  // Use StateSerializer to deserialize brain state
  pos = mStateSerializer.DeserializeBrainState(chunk, pos, mBrain, mBrainManager);

  // Re-link window pointer and notify UI
  mBrain.SetWindow(&mAnalysisWindow);
  mUIBridge.SendBrainSummary(mBrain);

  SyncAndSendDSPConfig();

  mUIBridge.SendTransformerParams(mTransformer);
  mUIBridge.SendMorphParams(mMorph);
  mUIBridge.SendExternalRefInfo(mBrainManager.UseExternal(), mBrainManager.ExternalPath());

  return pos;
}
