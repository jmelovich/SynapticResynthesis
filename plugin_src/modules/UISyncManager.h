#pragma once

#include <atomic>
#include <vector>
#include <memory>
#include <functional>
#include "plugin_src/brain/BrainManager.h"
#include "IPlug_include_in_plug_hdr.h"

// Forward declarations
// Use namespace forward declarations to avoid type conflicts
namespace synaptic {
  class Brain;
  class ParameterManager;
  class WindowCoordinator;
  class IChunkBufferTransformer;
  struct IMorph;
  class AudioStreamChunker;
  struct DSPConfig;

  namespace ui { class SynapticUI; class ProgressOverlayManager; }
}

namespace synaptic {

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

/**
 * @brief Manages UI synchronization and message handling
 *
 * Handles the communication between the audio thread (or background threads) and the UI thread.
 * Manages pending updates, deferred actions, and UI message routing.
 */
class UISyncManager {
public:
  UISyncManager(iplug::Plugin* plugin,
                Brain* brain,
                BrainManager* brainManager,
                ParameterManager* paramManager,
                WindowCoordinator* windowCoordinator,
                DSPConfig* dspConfig,
                ui::ProgressOverlayManager* overlayMgr);

  // === Configuration ===

  // Set dynamic object references (DSP context)
  void SetDSPContext(std::shared_ptr<IChunkBufferTransformer>* transformer,
                     std::shared_ptr<IMorph>* morph,
                     std::shared_ptr<IChunkBufferTransformer>* pendingTransformer,
                     std::shared_ptr<IMorph>* pendingMorph,
                     AudioStreamChunker* chunker);

  // Set UI instance (called when UI opens)
  void SetUI(ui::SynapticUI* ui);
  ui::SynapticUI* GetUI() const { return mUI; }

  // === Update Flags ===

  void SetPendingUpdate(PendingUpdate flag) { mPendingUpdates.fetch_or(static_cast<uint32_t>(flag)); }
  bool CheckAndClearPendingUpdate(PendingUpdate flag);
  bool HasPendingUpdate(PendingUpdate flag) const { return (mPendingUpdates.load() & static_cast<uint32_t>(flag)) != 0; }

  // === Main Loop Handlers ===

  // Called from Plugin::OnIdle
  void OnIdle();

  // Called when UI is closed
  void OnUIClose();

  // Called to restore state
  void OnRestoreState();

  // === Message Handling ===

  // Called from Plugin::OnMessage
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData);

  // Helper to mark host state dirty safely
  void MarkHostStateDirty();

private:
  // Internal helpers
  void DrainUiQueue();
  void SyncBrainUIState();
  void SyncAllUIState();
  void SyncAndSendDSPConfig();

  // Message handlers
  bool HandleSetChunkSizeMsg(int newSize);
  bool HandleSetOutputWindowMsg(int mode);
  bool HandleSetAnalysisWindowMsg(int mode);
  bool HandleSetAlgorithmMsg(int algorithmId);
  bool HandleBrainAddFileMsg(int dataSize, const void* pData);
  bool HandleBrainRemoveFileMsg(int fileId);
  bool HandleBrainExportMsg();
  bool HandleBrainImportMsg();
  bool HandleBrainEjectMsg();
  bool HandleBrainDetachMsg();
  bool HandleBrainCreateNewMsg();
  bool HandleBrainSetCompactModeMsg(int enabled);
  bool HandleCancelOperationMsg();

  // Callbacks
  synaptic::BrainManager::ProgressFn MakeProgressCallback();
  synaptic::BrainManager::CompletionFn MakeStandardCompletionCallback();

  // Dependencies
  iplug::Plugin* mPlugin;
  Brain* mBrain;
  BrainManager* mBrainManager;
  ParameterManager* mParamManager;
  WindowCoordinator* mWindowCoordinator;
  DSPConfig* mDSPConfig;
  ui::ProgressOverlayManager* mOverlayMgr;
  ui::SynapticUI* mUI = nullptr;

  // DSP Context pointers
  std::shared_ptr<IChunkBufferTransformer>* mTransformer = nullptr;
  std::shared_ptr<IMorph>* mMorph = nullptr;
  std::shared_ptr<IChunkBufferTransformer>* mPendingTransformer = nullptr;
  std::shared_ptr<IMorph>* mPendingMorph = nullptr;
  AudioStreamChunker* mChunker = nullptr;

  // State
  std::atomic<uint32_t> mPendingUpdates { 0 };
  bool mNeedsInitialUIRebuild { true };

  // Pending file import state
  std::vector<synaptic::BrainManager::FileData> mPendingImportFiles;
  std::atomic<bool> mPendingImportScheduled { false };
  int mPendingImportIdleTicks { 0 };
};

} // namespace synaptic
