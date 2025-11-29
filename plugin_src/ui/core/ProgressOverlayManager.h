/**
 * @file ProgressOverlayManager.h
 * @brief Thread-safe manager for progress overlay operations
 *
 * Provides centralized access to progress overlay functionality.
 * Components can access the manager via Get() without needing
 * direct pointer passing through constructors.
 */

#pragma once

#include <string>
#include <atomic>
#include <mutex>

namespace synaptic {
namespace ui {

// Forward declaration
class SynapticUI;

/**
 * @brief Manages progress overlay state and thread-safe updates
 *
 * This class provides a simple interface for showing progress overlays
 * from any thread (including background threads), while ensuring all
 * actual UI updates happen on the main thread.
 *
 * Access via the static Get() method after SetCurrent() has been called.
 */
class ProgressOverlayManager
{
public:
  /**
   * @brief Construct manager
   */
  explicit ProgressOverlayManager() = default;

  // === Centralized Access ===

  /**
   * @brief Get the current progress overlay manager instance
   * @return Pointer to current manager, or nullptr if none set
   *
   * Use this to access the progress overlay from any component without
   * needing to pass pointers through constructors.
   */
  static ProgressOverlayManager* Get() { return sCurrent; }

  /**
   * @brief Set the current progress overlay manager instance
   * @param manager Pointer to manager (owned by caller, typically the plugin)
   *
   * Call this once during plugin initialization.
   */
  static void SetCurrent(ProgressOverlayManager* manager) { sCurrent = manager; }

  // === UI Binding ===

  /**
   * @brief Set the SynapticUI pointer for immediate updates
   * @param ui Pointer to SynapticUI
   *
   * Call this once after UI is created to enable immediate overlay updates
   * for synchronous operations like project save.
   */
  void SetSynapticUI(SynapticUI* ui) { mSynapticUI = ui; }

  // === Thread-Safe Operations ===

  /**
   * @brief Show progress overlay (thread-safe)
   * @param title Operation title
   * @param message Current progress message
   * @param progress Progress value (0-100)
   * @param showCancelButton Whether to show cancel button
   */
  void Show(const std::string& title, const std::string& message,
            float progress = 0.0f, bool showCancelButton = true);

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
   * @param ui Pointer to SynapticUI
   *
   * Call this from OnIdle() to apply queued updates on the UI thread.
   */
  void ProcessPendingUpdates(SynapticUI* ui);

  // === Synchronous Operations ===

  /**
   * @brief Force immediate display of overlay (for synchronous operations)
   * @param title Operation title
   * @param message Current progress message
   *
   * Use this for synchronous blocking operations where the normal queued
   * updates won't be processed until after the operation completes.
   * Requires SetSynapticUI() to have been called.
   */
  void ShowImmediate(const std::string& title, const std::string& message);

  /**
   * @brief Force immediate hiding of overlay (for synchronous operations)
   *
   * Requires SetSynapticUI() to have been called.
   */
  void HideImmediate();

private:
  static inline ProgressOverlayManager* sCurrent = nullptr;

  SynapticUI* mSynapticUI = nullptr;

  enum class UpdateType { None, Show, Update, Hide };

  struct PendingUpdate {
    UpdateType type = UpdateType::None;
    std::string title;
    std::string message;
    float progress = 0.0f;
    bool showCancelButton = true;
  };

  PendingUpdate mPendingUpdate;
  std::mutex mMutex;
  std::atomic<bool> mHasUpdate { false };
};

} // namespace ui
} // namespace synaptic
