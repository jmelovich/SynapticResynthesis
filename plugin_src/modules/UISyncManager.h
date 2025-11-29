/**
 * @file UISyncManager.h
 * @brief Manages UI synchronization and message handling
 *
 * Handles communication between the audio thread (or background threads) and UI thread.
 * Manages pending updates, deferred actions, and UI message routing.
 */

#pragma once

#include <atomic>
#include <vector>
#include <memory>
#include <functional>
#include "plugin_src/brain/BrainManager.h"
#include "IPlug_include_in_plug_hdr.h"

namespace synaptic {

class Brain;
class ParameterManager;
class WindowCoordinator;
class IChunkBufferTransformer;
struct IMorph;
class AudioStreamChunker;
class DSPContext;
struct DSPConfig;

namespace ui {
  class SynapticUI;
  class ProgressOverlayManager;
}

/**
 * @brief Bitflags for pending deferred updates
 */
enum class PendingUpdate : uint32_t {
  None = 0,
  BrainSummary = 1 << 0,
  DSPConfig = 1 << 1,
  MarkDirty = 1 << 2,
  RebuildTransformer = 1 << 3,
  RebuildMorph = 1 << 4,
  SuppressAnalysisReanalyze = 1 << 5
};

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
 */
class UISyncManager {
public:
  UISyncManager(iplug::Plugin* plugin,
                Brain* brain,
                BrainManager* brainManager,
                ParameterManager* paramManager,
                WindowCoordinator* windowCoordinator,
                DSPConfig* dspConfig);

  // === Configuration ===

  /** @brief Set DSP context and chunker references */
  void SetDSPContext(DSPContext* dspContext, AudioStreamChunker* chunker);

  /** @brief Set UI instance (called when UI opens) */
  void SetUI(ui::SynapticUI* ui);
  ui::SynapticUI* GetUI() const { return mUI; }

  // === Update Flags ===

  void SetPendingUpdate(PendingUpdate flag) { mPendingUpdates.fetch_or(static_cast<uint32_t>(flag)); }
  bool CheckAndClearPendingUpdate(PendingUpdate flag);
  bool HasPendingUpdate(PendingUpdate flag) const { return (mPendingUpdates.load() & static_cast<uint32_t>(flag)) != 0; }

  // === Main Loop Handlers ===

  void OnIdle();
  void OnUIClose();
  void OnRestoreState();

  // === Message Handling ===

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData);

  void MarkHostStateDirty();

private:
  void DrainUiQueue();
  void SyncBrainUIState();
  void SyncAllUIState();

  // Message handlers
  bool HandleBrainAddFileMsg(int dataSize, const void* pData);
  bool HandleBrainRemoveFileMsg(int fileId);
  bool HandleBrainExportMsg();
  bool HandleBrainImportMsg();
  bool HandleBrainEjectMsg();
  bool HandleBrainDetachMsg();
  bool HandleBrainCreateNewMsg();
  bool HandleBrainSetCompactModeMsg(int enabled);
  bool HandleCancelOperationMsg();

  // Callbacks - take overlay manager for multi-instance safety
  synaptic::BrainManager::ProgressFn MakeProgressCallback(ui::ProgressOverlayManager* overlayMgr);
  synaptic::BrainManager::CompletionFn MakeStandardCompletionCallback(ui::ProgressOverlayManager* overlayMgr);

  // Dependencies
  iplug::Plugin* mPlugin;
  Brain* mBrain;
  BrainManager* mBrainManager;
  ParameterManager* mParamManager;
  WindowCoordinator* mWindowCoordinator;
  DSPConfig* mDSPConfig;
  ui::SynapticUI* mUI = nullptr;

  // DSP Context reference
  DSPContext* mDSPContext = nullptr;
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
