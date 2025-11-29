/**
 * @file ProgressOverlayManager.h
 * @brief Thread-safe manager for progress overlay operations
 *
 * Provides centralized access to progress overlay functionality with
 * multi-instance support for DAW environments running multiple plugin instances.
 *
 * Access patterns:
 * - ProgressOverlayManager::GetFor(plugin) - Preferred when plugin pointer is available
 * - ProgressOverlayManager::Get() - Returns current context (set via SetCurrentContext)
 */

#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>

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
 * Multi-instance support:
 * - Each plugin instance owns its own ProgressOverlayManager
 * - Use Register() during plugin init, Unregister() during destruction
 * - Use GetFor(plugin) when you have a plugin pointer
 * - Use Get() for the current thread context (set via SetCurrentContext)
 */
class ProgressOverlayManager
{
public:
  /**
   * @brief Construct manager
   */
  explicit ProgressOverlayManager() = default;

  // === Multi-Instance Registry ===

  /**
   * @brief Register a plugin's overlay manager in the global registry
   * @param pluginPtr Plugin instance pointer (used as key, type-erased to void*)
   * @param manager The overlay manager for this plugin
   *
   * Call this once during plugin construction.
   */
  static void Register(void* pluginPtr, ProgressOverlayManager* manager);

  /**
   * @brief Unregister a plugin's overlay manager
   * @param pluginPtr Plugin instance to unregister
   *
   * Call this during plugin destruction to clean up.
   */
  static void Unregister(void* pluginPtr);

  /**
   * @brief Get the overlay manager for a specific plugin instance
   * @param pluginPtr Plugin instance pointer
   * @return Pointer to manager, or nullptr if not registered
   *
   * Preferred method when you have access to the plugin pointer.
   */
  static ProgressOverlayManager* GetFor(void* pluginPtr);

  /**
   * @brief Set the current active overlay manager
   * @param manager The manager to use as current context
   *
   * Thread-safe. Use this before starting operations that will call Get()
   * from background threads without a plugin pointer.
   */
  static void SetCurrentContext(ProgressOverlayManager* manager);

  /**
   * @brief Get the current active overlay manager
   * @return Pointer to current manager, or nullptr if none set
   *
   * Thread-safe. Returns the manager set via SetCurrentContext().
   * Works from any thread (main or background).
   * Use GetFor(plugin) when possible for clearer multi-instance behavior.
   */
  static ProgressOverlayManager* Get();

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
  // Multi-instance registry (uses void* to avoid forward declaration issues with iplug::Plugin)
  static std::mutex sRegistryMutex;
  static std::unordered_map<void*, ProgressOverlayManager*> sRegistry;
  // Atomic pointer for thread-safe access from any thread (main or background)
  static std::atomic<ProgressOverlayManager*> sCurrentContext;

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
