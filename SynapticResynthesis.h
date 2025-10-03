#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "Oscillator.h"
#include "Smoothers.h"
#include "plugin_src/AudioStreamChunker.h"
#include "plugin_src/ChunkBufferTransformer.h"
#include "plugin_src/samplebrain/Brain.h"
#include "plugin_src/Window.h"
#include "plugin_src/modules/DSPConfig.h"
#include "plugin_src/modules/UIBridge.h"
#include "plugin_src/modules/ParameterManager.h"
#include "plugin_src/modules/BrainManager.h"
#include "plugin_src/modules/StateSerializer.h"
#include "plugin_src/modules/UIMessageHandler.h"
#include <atomic>

using namespace iplug;

const int kNumPresets = 3;

enum EParams
{
  // Fixed, non-dynamic parameters
  kInGain = 0,
  kChunkSize,
  kBufferWindow,
  kAlgorithm,
  kOutputWindow,
  kDirtyFlag,
  kAnalysisWindow,
  kEnableOverlap,
  kOutGain,
  kAGC,
  // Dynamic transformer parameters are indexed after this sentinel
  kNumParams
};

enum EMsgTags
{
  kMsgTagSetChunkSize = 4,
  kMsgTagSetBufferWindowSize = 5,
  kMsgTagSetAlgorithm = 6,
  kMsgTagSetOutputWindowMode = 7,
  // Analysis window used for offline brain analysis (non-automatable IParam mirrors this)
  kMsgTagSetAnalysisWindowMode = 8,
  // Brain UI -> C++ messages
  kMsgTagBrainAddFile = 100,
  kMsgTagBrainRemoveFile = 101,
  // Transformer params UI -> C++
  kMsgTagTransformerSetParam = 102,
  // UI lifecycle
  kMsgTagUiReady = 103,
  // Brain snapshot external IO
  kMsgTagBrainExport = 104,
  kMsgTagBrainImport = 105,
  kMsgTagBrainReset = 106,
  kMsgTagBrainDetach = 107,
  // Window resize
  kMsgTagResizeToFit = 108,
  // C++ -> UI JSON updates use msgTag = -1, with id fields "brainSummary"

};

class SynapticResynthesis final : public Plugin
{
  // Allow UIMessageRouter to call private handler methods
  friend class synaptic::UIMessageRouter;

public:
  SynapticResynthesis(const InstanceInfo& info);

  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
  void OnUIOpen() override;
  void OnIdle() override;
  void OnRestoreState() override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
  void OnParamChange(int paramIdx) override;
  // state serialization
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

private:
  // === Message Handlers (called by UIMessageRouter) ===
  bool HandleUiReadyMsg();
  bool HandleSetChunkSizeMsg(int newSize);
  bool HandleSetBufferWindowSizeMsg(int newSize);
  bool HandleSetOutputWindowMsg(int mode);
  bool HandleSetAnalysisWindowMsg(int mode);
  bool HandleSetAlgorithmMsg(int algorithmId);
  bool HandleTransformerSetParamMsg(const void* jsonData, int dataSize);
  bool HandleBrainAddFileMsg(int dataSize, const void* pData);
  bool HandleBrainRemoveFileMsg(int fileId);
  bool HandleBrainExportMsg();
  bool HandleBrainImportMsg();
  bool HandleBrainResetMsg();
  bool HandleBrainDetachMsg();
  bool HandleResizeToFitMsg(int dataSize, const void* pData);

  // === Helper Methods ===
  void UpdateChunkerWindowing();
  void MarkHostStateDirty();
  void DrainUiQueueOnMainThread();
  void SyncAndSendDSPConfig();
  void SetParameterFromUI(int paramIdx, double value);
  void UpdateBrainAnalysisWindow();

  // === Brain State (must be declared before BrainManager) ===
  synaptic::Brain mBrain;
  synaptic::Window mAnalysisWindow;  // For brain analysis

  // === Modules ===
  synaptic::DSPConfig mDSPConfig;
  synaptic::UIBridge mUIBridge;
  synaptic::ParameterManager mParamManager;
  synaptic::BrainManager mBrainManager;
  synaptic::StateSerializer mStateSerializer;

  // === DSP Components ===
  LogParamSmooth<sample, 1> mInGainSmoother;
  LogParamSmooth<sample, 1> mOutGainSmoother;
  synaptic::AudioStreamChunker mChunker {2};
  std::shared_ptr<synaptic::IChunkBufferTransformer> mTransformer;
  std::shared_ptr<synaptic::IChunkBufferTransformer> mPendingTransformer; // For thread-safe swapping
  synaptic::Window mOutputWindow;

  // Utility methods
  int ComputeLatencySamples() const { return mDSPConfig.chunkSize + (mTransformer ? mTransformer->GetAdditionalLatencySamples(mDSPConfig.chunkSize, mDSPConfig.bufferWindowSize) : 0); }

  // Atomic flags for deferred updates
  std::atomic<bool> mPendingSendBrainSummary { false };
  std::atomic<bool> mPendingSendDSPConfig { false };
  std::atomic<bool> mPendingMarkDirty { false };
  std::atomic<bool> mSuppressNextAnalysisReanalyze { false };
};
