#include "SynapticResynthesis.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"
#include "json.hpp"
#include "Extras/WebView/IPlugWebViewEditorDelegate.h"
#include "plugin_src/TransformerFactory.h"
#include "plugin_src/PlatformFileDialogs.h"
#include <thread>
#include <mutex>
#ifdef AAX_API
#include "IPlugAAX.h"
#endif

namespace {
  static int ComputeTotalParams()
  {
    // Fixed params are enumerated in EParams up to kNumParams.
    // Dynamic transformer params are appended after kNumParams.
    // Use ParameterManager's method to get union count
    std::vector<synaptic::IChunkBufferTransformer::ExposedParamDesc> unionDescs;
    synaptic::ParameterManager tempMgr;
    // We need to compute this statically, so we replicate the union logic here
    for (const auto& info : synaptic::TransformerFactory::GetAll())
    {
      auto t = info.create();
      std::vector<synaptic::IChunkBufferTransformer::ExposedParamDesc> tmp;
      t->GetParamDescs(tmp);
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

  mEditorInitFunc = [&]()
  {
    LoadIndexHtml(__FILE__, GetBundleID());
    EnableScroll(false);
  };

  MakePreset("One", -70.);
  MakePreset("Two", -30.);
  MakePreset("Three", 0.);

  // Default transformer = first UI-visible entry
  mTransformer = synaptic::TransformerFactory::CreateByUiIndex(mDSPConfig.algorithmId);
  if (auto sb = dynamic_cast<synaptic::SimpleSampleBrainTransformer*>(mTransformer.get()))
    sb->SetBrain(&mBrain);

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
    mUIBridge.SendBrainSummary(mBrain);
  if (mPendingSendDSPConfig.exchange(false))
  {
    SyncAndSendDSPConfig();
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

  for (int ch = 0; ch < outChans; ch++)
  {
    for (int s = 0; s < nFrames; s++)
    {
      inputs[ch][s] *= inGain;
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
    const double smoothedGain = mGainSmoother.Process(outGain);
    for (int ch = 0; ch < outChans; ++ch)
      outputs[ch][s] *= smoothedGain;
  }
}

void SynapticResynthesis::OnReset()
{
  auto sr = GetSampleRate();
  mGainSmoother.SetSmoothTime(20., sr);

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

  // After reset, apply IParam values to transformer implementation using ParameterManager
  mParamManager.ApplyBindingsToTransformer(this, mTransformer.get());

  // When audio engine resets, leave brain state intact; just resend summary to UI
  mUIBridge.SendBrainSummary(mBrain);
  mUIBridge.SendTransformerParams(mTransformer.get());

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
  mUIBridge.SendTransformerParams(mTransformer.get());

  SyncAndSendDSPConfig();

  mUIBridge.SendBrainSummary(mBrain);
}

void SynapticResynthesis::OnIdle()
{
  // Drain any UI messages queued by background threads
  DrainUiQueueOnMainThread();
}

void SynapticResynthesis::OnRestoreState()
{
  Plugin::OnRestoreState();
  mUIBridge.SendTransformerParams(mTransformer.get());

  SyncAndSendDSPConfig();

  mUIBridge.SendBrainSummary(mBrain);
}

void SynapticResynthesis::OnParamChange(int paramIdx)
{
  if (paramIdx == kInGain)
  {
    DBGMSG("input gain %f\n", GetParam(paramIdx)->Value());
    return;
  }

  if (paramIdx == kOutGain)
  {
    DBGMSG("output gain %f\n", GetParam(paramIdx)->Value());
    return;
  }

  // Handle chunk size parameter (coordinated by ParameterManager)
  if (paramIdx == mParamManager.GetChunkSizeParamIdx())
  {
    mParamManager.HandleChunkSizeChange(paramIdx, GetParam(paramIdx), mDSPConfig, this, mChunker, mAnalysisWindow);
    UpdateChunkerWindowing();
    SetLatency(ComputeLatencySamples());
    return;
  }

  // Handle buffer window parameter
  if (paramIdx == mParamManager.GetBufferWindowParamIdx())
  {
    mParamManager.HandleCoreParameterChange(paramIdx, GetParam(paramIdx), mDSPConfig);
    mChunker.SetBufferWindowSize(mDSPConfig.bufferWindowSize);
    return;
  }

  // Handle algorithm change (coordinated by ParameterManager)
  if (paramIdx == mParamManager.GetAlgorithmParamIdx())
  {
    mTransformer = mParamManager.HandleAlgorithmChange(paramIdx, GetParam(paramIdx), mDSPConfig,
                                                       this, mBrain, GetSampleRate(), NInChansConnected());
    UpdateChunkerWindowing();
    SetLatency(ComputeLatencySamples());
    return;
  }

  // Handle output window
  if (paramIdx == mParamManager.GetOutputWindowParamIdx())
  {
    mParamManager.HandleCoreParameterChange(paramIdx, GetParam(paramIdx), mDSPConfig);
    UpdateChunkerWindowing();
    return;
  }

  // Handle analysis window (coordinated by ParameterManager, with background reanalysis)
  if (paramIdx == mParamManager.GetAnalysisWindowParamIdx())
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
    return;
  }

  // Handle overlap enable
  if (paramIdx == mParamManager.GetEnableOverlapParamIdx())
  {
    mParamManager.HandleCoreParameterChange(paramIdx, GetParam(paramIdx), mDSPConfig);
    UpdateChunkerWindowing();
    return;
  }

  // Handle transformer parameters using ParameterManager
  if (mParamManager.HandleTransformerParameterChange(paramIdx, GetParam(paramIdx), mTransformer.get()))
  {
    // Parameter was handled by ParameterManager
    return;
  }
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
  mUIBridge.SendDSPConfigWithAlgorithms(mDSPConfig);
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

  mUIBridge.SendTransformerParams(mTransformer.get());
  mUIBridge.SendExternalRefInfo(mBrainManager.UseExternal(), mBrainManager.ExternalPath());

  return pos;
}
