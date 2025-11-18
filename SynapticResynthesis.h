#pragma once

#include "IPlug_include_in_plug_hdr.h"

#if !SR_USE_WEB_UI && IPLUG_EDITOR
  #include "IGraphics_include_in_plug_hdr.h"
  #include "IControls.h"
#endif

#include "Oscillator.h"
#include "Smoothers.h"
#include "plugin_src/modules/AudioStreamChunker.h"
#include "plugin_src/transformers/BaseTransformer.h"
#include "plugin_src/brain/Brain.h"
#include "plugin_src/audio/Window.h"
#include "plugin_src/morph/IMorph.h"
#include "plugin_src/modules/DSPConfig.h"
#include "plugin_src/ui_bridge/UIBridge.h"
#include "plugin_src/params/ParameterManager.h"
#include "plugin_src/brain/BrainManager.h"
#include "plugin_src/serialization/StateSerializer.h"
#include "plugin_src/ui_bridge/UIMessageHandler.h"
#include "plugin_src/ui/core/ProgressOverlayManager.h"
#include <atomic>

using namespace iplug;

const int kNumPresets = 3;

// Bitflags for pending deferred updates
enum class PendingUpdate : uint32_t {
  None = 0,
  BrainSummary = 1 << 0,
  DSPConfig = 1 << 1,
  MarkDirty = 1 << 2,
  RebuildTransformer = 1 << 3,
  RebuildMorph = 1 << 4,
  SuppressAnalysisReanalyze = 1 << 5
};

// Bitwise operators for PendingUpdate flags
inline PendingUpdate operator|(PendingUpdate a, PendingUpdate b) {
  return static_cast<PendingUpdate>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline PendingUpdate operator&(PendingUpdate a, PendingUpdate b) {
  return static_cast<PendingUpdate>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline PendingUpdate operator~(PendingUpdate a) {
  return static_cast<PendingUpdate>(~static_cast<uint32_t>(a));
}
inline uint32_t operator|(uint32_t a, PendingUpdate b) {
  return a | static_cast<uint32_t>(b);
}
inline uint32_t operator&(uint32_t a, PendingUpdate b) {
  return a & static_cast<uint32_t>(b);
}

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
  kAutotuneBlend,
  kAutotuneMode,
  kAutotuneToleranceOctaves,
  kMorphMode,
  kWindowLock,
  // Dynamic transformer parameters are indexed after this sentinel
  kNumParams
};

// Message tags are now defined in plugin_src/ui_bridge/MessageTags.h
// Include it here for backward compatibility
#include "plugin_src/ui_bridge/MessageTags.h"

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
  bool HandleSetOutputWindowMsg(int mode);
  bool HandleSetAnalysisWindowMsg(int mode);
  bool HandleSetAlgorithmMsg(int algorithmId);
  bool HandleTransformerSetParamMsg(const void* jsonData, int dataSize);
  bool HandleBrainAddFileMsg(int dataSize, const void* pData);
  bool HandleBrainRemoveFileMsg(int fileId);
  bool HandleBrainExportMsg();
  bool HandleBrainImportMsg();
  bool HandleBrainEjectMsg();
  bool HandleBrainDetachMsg();
  bool HandleBrainCreateNewMsg();
  bool HandleBrainSetCompactModeMsg(int enabled);
  bool HandleCancelOperationMsg();
  bool HandleResizeToFitMsg(int dataSize, const void* pData);

  // === Helper Methods ===
  void UpdateChunkerWindowing();
  void MarkHostStateDirty();
  void DrainUiQueueOnMainThread();
  void SyncAndSendDSPConfig();
  void SetParameterFromUI(int paramIdx, double value);
  void UpdateBrainAnalysisWindow();
  void SyncWindowControls(); // Sync window controls with their parameter values
  void SyncControlToParameter(int paramIdx); // Sync a specific control with its parameter value
  void TriggerBrainReanalysisAsync(); // Start async brain reanalysis with progress overlay
  void SyncAnalysisToOutputWindow(); // Sync analysis window to match output window
  void SyncOutputToAnalysisWindow(); // Sync output window to match analysis window

  // Cancellation support helpers
  synaptic::BrainManager::ProgressFn MakeProgressCallback();
  synaptic::BrainManager::CompletionFn MakeStandardCompletionCallback();
  void RollbackParameter(int paramIdx, double oldValue, const char* debugName);

  // UI state synchronization helpers (C++ UI only)
  void SyncBrainUIState();
  void SyncAllUIState();

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
  LogParamSmooth<sample, 2> mOutGainSmoother;
  synaptic::AudioStreamChunker mChunker {2};
  std::shared_ptr<synaptic::IChunkBufferTransformer> mTransformer;
  std::shared_ptr<synaptic::IChunkBufferTransformer> mPendingTransformer; // For thread-safe swapping
  synaptic::Window mOutputWindow;
  std::shared_ptr<synaptic::IMorph> mMorph; // dynamic morph owner (for params)
  std::shared_ptr<synaptic::IMorph> mPendingMorph; // For thread-safe swapping

  // Utility methods
  int ComputeLatencySamples() const { return mDSPConfig.chunkSize + (mTransformer ? mTransformer->GetAdditionalLatencySamples(mDSPConfig.chunkSize, mDSPConfig.bufferWindowSize) : 0); }

  // Helper methods for pending update flags
  void SetPendingUpdate(PendingUpdate flag) { mPendingUpdates.fetch_or(static_cast<uint32_t>(flag)); }
  bool CheckAndClearPendingUpdate(PendingUpdate flag) {
    uint32_t expected = mPendingUpdates.load();
    uint32_t mask = static_cast<uint32_t>(flag);
    while ((expected & mask) && !mPendingUpdates.compare_exchange_weak(expected, expected & ~mask));
    return (expected & mask) != 0;
  }
  bool HasPendingUpdate(PendingUpdate flag) const { return (mPendingUpdates.load() & static_cast<uint32_t>(flag)) != 0; }

  // Atomic bitfield for deferred updates
  std::atomic<uint32_t> mPendingUpdates { 0 };

  // C++ UI initialization flag
  bool mNeedsInitialUIRebuild { true };

  // === Pending file-drop batching for async import ===
  std::vector<synaptic::BrainManager::FileData> mPendingImportFiles;
  std::atomic<bool> mPendingImportScheduled { false };
  int mPendingImportIdleTicks { 0 }; // countdown in idle ticks before starting batch

  // === Progress overlay management ===
  mutable synaptic::ui::ProgressOverlayManager mProgressOverlayMgr;
};
