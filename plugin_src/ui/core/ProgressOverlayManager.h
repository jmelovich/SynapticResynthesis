/**
 * @file ProgressOverlayManager.h
 * @brief Thread-safe manager for progress overlay operations
 *
 * Handles showing/updating/hiding progress overlays from background threads,
 * queuing updates to be applied on the main UI thread.
 */

#pragma once

#include <string>
#include <atomic>
#include <mutex>

namespace synaptic {

// Forward declaration
class UIBridge;

namespace ui {

// Forward declaration
class SynapticUI;

/**
 * @brief Manages progress overlay state and thread-safe updates
 *
 * This class provides a simple interface for showing progress overlays
 * from any thread (including background threads), while ensuring all
 * actual UI updates happen on the main thread.
 */
class ProgressOverlayManager
{
public:
  /**
   * @brief Construct manager
   * @param uiBridge Pointer to UIBridge for WebUI mode (can be nullptr for C++ UI)
   */
  explicit ProgressOverlayManager(UIBridge* uiBridge = nullptr);

  /**
   * @brief Show progress overlay (thread-safe)
   * @param title Operation title
   * @param message Current progress message
   * @param progress Progress value (0-100)
   */
  void Show(const std::string& title, const std::string& message, float progress);

  /**
   * @brief Update progress overlay (thread-safe)
   * @param message Current progress message
   * @param progress Progress value (0-100)
   */
  void Update(const std::string& message, float progress);

  /**
   * @brief Hide progress overlay (thread-safe)
   */
  void Hide();

  /**
   * @brief Process pending updates on main thread
   * @param ui Pointer to SynapticUI (nullptr in WebUI mode)
   *
   * Call this from OnIdle() to apply queued updates on the UI thread.
   */
  void ProcessPendingUpdates(SynapticUI* ui);

private:
  UIBridge* mUIBridge;

  enum class UpdateType { None, Show, Update, Hide };

  struct PendingUpdate {
    UpdateType type = UpdateType::None;
    std::string title;
    std::string message;
    float progress = 0.0f;
  };

  PendingUpdate mPendingUpdate;
  std::mutex mMutex;
  std::atomic<bool> mHasUpdate { false };
};

} // namespace ui
} // namespace synaptic

