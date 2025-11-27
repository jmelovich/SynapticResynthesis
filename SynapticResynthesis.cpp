/**
 * @file SynapticResynthesis.cpp
 * @brief Implementation of the main plugin class
 */

#include "SynapticResynthesis.h"
#include "IPlugPaths.h"
#include "json.hpp"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
  #include "plugin_src/ui/IGraphicsUI.h"
  #include "plugin_src/ui/controls/UIControls.h"
#endif
#include "plugin_src/transformers/TransformerFactory.h"
#include "plugin_src/PlatformFileDialogs.h"
#include "plugin_src/params/DynamicParamSchema.h"
#include "plugin_src/morph/MorphFactory.h"
#include "plugin_src/modules/WindowCoordinator.h"
#include "plugin_src/modules/WindowModeHelpers.h"
#include <thread>
#include <mutex>
#ifdef AAX_API
#include "IPlugAAX.h"
#endif

SynapticResynthesis::SynapticResynthesis(const InstanceInfo& info)
: Plugin(info, MakeConfig(synaptic::ParameterManager::GetTotalParams(), kNumPresets))
, mBrainManager(&mBrain, &mAnalysisWindow)
, mDSPContext(2)
, mWindowCoordinator(&mAnalysisWindow, &mDSPContext.GetOutputWindow(), &mBrain, &mDSPContext.GetChunker(), &mParamManager, &mBrainManager, &mProgressOverlayMgr)
, mUISyncManager(this, &mBrain, &mBrainManager, &mParamManager, &mWindowCoordinator, &mDSPConfig, &mProgressOverlayMgr)
{
  GetParam(kInGain)->InitGain("Input Gain", 0.0, -70, 12.);
  GetParam(kOutGain)->InitGain("Output Gain", 0.0, -70, 12.);
  GetParam(kAGC)->InitBool("AGC", false);
  GetParam(kWindowLock)->InitBool("Window Lock", true);

  // Initialize DSP config with defaults
  mDSPConfig.chunkSize = 3000;
  mDSPConfig.bufferWindowSize = 1;
  mDSPConfig.outputWindowMode = 1;
  mDSPConfig.analysisWindowMode = 1;
  mDSPConfig.algorithmId = 0;
  mDSPConfig.enableOverlapAdd = true;

#if IPLUG_EDITOR
  // IGraphics UI setup
  mMakeGraphicsFunc = [&]() {
    return igraphics::MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [this](igraphics::IGraphics* pGraphics) {
    if (pGraphics)
    {
      mUI = std::make_unique<synaptic::ui::SynapticUI>(pGraphics);
      mUI->build();
    }
  };
#endif

  MakePreset("One", -70.);
  MakePreset("Two", -30.);
  MakePreset("Three", 0.);

  // Initialize DSP Context
  mDSPContext.Init(this, &mParamManager, &mBrain, mDSPConfig);

  // Initialize analysis window
  mAnalysisWindow.Set(synaptic::Window::Type::Hann, mDSPConfig.chunkSize);
  mBrain.SetWindow(&mAnalysisWindow);

  // Configure UISyncManager with DSP context reference
  mUISyncManager.SetDSPContext(&mDSPContext, &mDSPContext.GetChunker());

  // Initialize parameters
  mParamManager.InitializeCoreParameters(this, mDSPConfig);
  mParamManager.InitializeTransformerParameters(this);
}

void SynapticResynthesis::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  mDSPContext.ProcessBlock(inputs, outputs, nFrames, this, mDSPConfig, &mParamManager);
}

void SynapticResynthesis::OnReset()
{
  auto sr = GetSampleRate();

  // Pull current values from IParams into DSPConfig
  const int chunkSizeIdx = mParamManager.GetChunkSizeParamIdx();
  const int bufferWindowIdx = mParamManager.GetBufferWindowParamIdx();
  const int algorithmIdx = mParamManager.GetAlgorithmParamIdx();
  const int outputWindowIdx = mParamManager.GetOutputWindowParamIdx();
  const int analysisWindowIdx = mParamManager.GetAnalysisWindowParamIdx();
  const int enableOverlapIdx = mParamManager.GetEnableOverlapParamIdx();

  if (chunkSizeIdx >= 0) mDSPConfig.chunkSize = std::max(1, GetParam(chunkSizeIdx)->Int());
  if (bufferWindowIdx >= 0) mDSPConfig.bufferWindowSize = std::max(1, GetParam(bufferWindowIdx)->Int());
  if (algorithmIdx >= 0) mDSPConfig.algorithmId = GetParam(algorithmIdx)->Int();
  if (outputWindowIdx >= 0) mDSPConfig.outputWindowMode = synaptic::WindowMode::ParamToConfig(GetParam(outputWindowIdx)->Int());
  if (analysisWindowIdx >= 0) mDSPConfig.analysisWindowMode = synaptic::WindowMode::ParamToConfig(GetParam(analysisWindowIdx)->Int());
  if (enableOverlapIdx >= 0) mDSPConfig.enableOverlapAdd = GetParam(enableOverlapIdx)->Bool();

  mWindowCoordinator.UpdateBrainAnalysisWindow(mDSPConfig);

  mDSPContext.OnReset(sr, GetBlockSize(), NInChansConnected(), this, mDSPConfig, &mParamManager, &mBrain);

  mWindowCoordinator.UpdateChunkerWindowing(mDSPConfig, mDSPContext.GetTransformerRaw());

  SetLatency(mDSPContext.ComputeLatencySamples(mDSPConfig.chunkSize, mDSPConfig.bufferWindowSize));

  // Schedule UI updates
  mUISyncManager.SetPendingUpdate(synaptic::PendingUpdate::BrainSummary);
  mUISyncManager.SetPendingUpdate(synaptic::PendingUpdate::RebuildTransformer);
  mUISyncManager.SetPendingUpdate(synaptic::PendingUpdate::RebuildMorph);
  mUISyncManager.SetPendingUpdate(synaptic::PendingUpdate::DSPConfig);
}

bool SynapticResynthesis::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  return mUISyncManager.OnMessage(msgTag, ctrlTag, dataSize, pData);
}

void SynapticResynthesis::OnUIOpen()
{
  Plugin::OnUIOpen();

#if IPLUG_EDITOR
  if (mUI)
  {
    mUISyncManager.SetUI(mUI.get());
  }
#endif
}

void SynapticResynthesis::OnUIClose()
{
  Plugin::OnUIClose();

  mUISyncManager.OnUIClose();
#if IPLUG_EDITOR
  mUI.reset();
#endif
}

void SynapticResynthesis::OnIdle()
{
  mUISyncManager.OnIdle();
}

void SynapticResynthesis::OnRestoreState()
{
  Plugin::OnRestoreState();
  mUISyncManager.OnRestoreState();
}

void SynapticResynthesis::OnParamChange(int paramIdx)
{
  synaptic::ParameterChangeContext ctx;
  ctx.plugin = this;
  ctx.config = &mDSPConfig;
  ctx.dspContext = &mDSPContext;
  ctx.chunker = &mDSPContext.GetChunker();
  ctx.brain = &mBrain;
  ctx.analysisWindow = &mAnalysisWindow;
  ctx.windowCoordinator = &mWindowCoordinator;
  ctx.brainManager = &mBrainManager;
  ctx.progressOverlayMgr = &mProgressOverlayMgr;

  ctx.setPendingUpdate = [this](uint32_t flag) { mUISyncManager.SetPendingUpdate((synaptic::PendingUpdate)flag); };
  ctx.checkAndClearPendingUpdate = [this](uint32_t flag) { return mUISyncManager.CheckAndClearPendingUpdate((synaptic::PendingUpdate)flag); };
  ctx.computeLatency = [this]() { return mDSPContext.ComputeLatencySamples(mDSPConfig.chunkSize, mDSPConfig.bufferWindowSize); };
  ctx.setLatency = [this](int latency) { SetLatency(latency); };

  mParamManager.OnParamChange(paramIdx, ctx);
}

void SynapticResynthesis::ProcessMidiMsg(const IMidiMsg& msg)
{
  TRACE;
  msg.PrintMsg();
  SendMidiMsg(msg);
}

bool SynapticResynthesis::SerializeState(IByteChunk& chunk) const
{
  if (!Plugin::SerializeState(chunk)) return false;

#if IPLUG_EDITOR
  synaptic::ui::ProgressOverlayManager* overlayMgr = const_cast<SynapticResynthesis*>(this)->mUISyncManager.GetUI() ? &mProgressOverlayMgr : nullptr;
  return mStateSerializer.SerializeBrainState(chunk, mBrain, const_cast<synaptic::BrainManager&>(mBrainManager), overlayMgr);
#else
  return mStateSerializer.SerializeBrainState(chunk, mBrain, const_cast<synaptic::BrainManager&>(mBrainManager), nullptr);
#endif
}

int SynapticResynthesis::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int pos = Plugin::UnserializeState(chunk, startPos);
  if (pos < 0) return pos;

  pos = mStateSerializer.DeserializeBrainState(chunk, pos, mBrain, mBrainManager);

  synaptic::Brain::sUseCompactBrainFormat = mBrain.WasLastLoadedInCompactFormat();

  mBrain.SetWindow(&mAnalysisWindow);
  mUISyncManager.SetPendingUpdate(synaptic::PendingUpdate::BrainSummary);
  mUISyncManager.SetPendingUpdate(synaptic::PendingUpdate::DSPConfig);
  mUISyncManager.SetPendingUpdate(synaptic::PendingUpdate::RebuildTransformer);
  mUISyncManager.SetPendingUpdate(synaptic::PendingUpdate::RebuildMorph);

  return pos;
}
