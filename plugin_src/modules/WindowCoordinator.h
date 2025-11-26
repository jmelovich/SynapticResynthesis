#pragma once

#include <functional>
#include <string>
#include "IPlug_include_in_plug_hdr.h"

// Forward declarations
// Use namespace forward declarations to avoid type conflicts
namespace synaptic {

// Forward declarations
class Window;
class Brain;
class AudioStreamChunker;
class IChunkBufferTransformer;
class ParameterManager;
class BrainManager;
struct DSPConfig;

namespace ui {
  class ProgressOverlayManager;
}

/**
 * @brief Coordinates window operations across the plugin
 *
 * Manages the relationship between analysis windows (used for Brain feature extraction)
 * and output windows (used for audio reconstruction). Handles:
 * - Window synchronization when "Window Lock" is enabled
 * - Windowing configuration for the chunker
 * - Triggering reanalysis when analysis window changes
 * - UI control synchronization
 */
class WindowCoordinator
{
public:
  /**
   * @brief Construct WindowCoordinator with dependencies
   * @param analysisWindow Reference to analysis window (owned by plugin)
   * @param outputWindow Reference to output window (owned by plugin)
   * @param brain Reference to brain instance
   * @param chunker Reference to audio stream chunker
   * @param paramManager Reference to parameter manager
   * @param brainManager Reference to brain manager
   * @param progressOverlayMgr Reference to progress overlay manager (can be nullptr)
   */
  WindowCoordinator(
    Window* analysisWindow,
    Window* outputWindow,
    Brain* brain,
    AudioStreamChunker* chunker,
    ParameterManager* paramManager,
    BrainManager* brainManager,
    ui::ProgressOverlayManager* progressOverlayMgr
  );

  /**
   * @brief Update chunker windowing configuration
   *
   * Configures the chunker's output window and overlap-add behavior based on:
   * - Current window type (Hann, Hamming, Blackman, Rectangular)
   * - User's overlap enable preference
   * - Transformer's overlap requirements
   * - Also updates the chunker's input analysis window reference
   *
   * @param config Current DSP configuration
   * @param transformer Current transformer (can be nullptr)
   */
  void UpdateChunkerWindowing(const DSPConfig& config, IChunkBufferTransformer* transformer);

  /**
   * @brief Update brain's analysis window from config
   *
   * Synchronizes the analysis window instance with the current config and
   * updates the Brain's window pointer.
   *
   * @param config Current DSP configuration
   */
  void UpdateBrainAnalysisWindow(const DSPConfig& config);

  /**
   * @brief Sync analysis window to match output window
   *
   * Called when window lock is enabled and output window changes.
   * Updates analysis window mode, triggers reanalysis, and syncs UI.
   *
   * @param plugin Plugin instance for parameter access (void* to avoid header dependency)
   * @param config DSP configuration to update
   * @param triggerReanalysis If true, starts async reanalysis operation
   */
  void SyncAnalysisToOutputWindow(
    void* plugin,
    DSPConfig& config,
    bool triggerReanalysis = true
  );

  /**
   * @brief Sync output window to match analysis window
   *
   * Called when window lock is enabled and analysis window changes.
   * Updates output window mode and syncs UI.
   *
   * @param plugin Plugin instance for parameter access (void* to avoid header dependency)
   * @param config DSP configuration to update
   */
  void SyncOutputToAnalysisWindow(
    void* plugin,
    DSPConfig& config
  );

  /**
   * @brief Sync window controls with their parameter values (C++ UI only)
   *
   * Updates all window-related controls (kOutputWindow, kAnalysisWindow, kWindowLock)
   * to reflect their current parameter values. Marks all controls dirty to force redraw.
   *
   * @param graphics IGraphics instance (can be nullptr, void* to avoid header dependency)
   */
  void SyncWindowControls(void* graphics);

  /**
   * @brief Trigger async brain reanalysis with progress overlay
   *
   * Starts a background reanalysis operation with cancellation support.
   * Shows progress overlay (C++ UI only), updates on progress, hides on completion.
   *
   * @param sampleRate Current sample rate
   * @param completion Completion callback (wasCancelled flag)
   */
  void TriggerBrainReanalysisAsync(
    int sampleRate,
    std::function<void(bool wasCancelled)> completion
  );

  /**
   * @brief Handle window lock parameter toggle
   *
   * When window lock is enabled, synchronizes the two windows based on which
   * control's lock button was clicked. Uses LockButtonControl's last clicked state.
   *
   * @param isLocked New lock state
   * @param clickedWindowParam Which window param's lock was clicked (kOutputWindow or kAnalysisWindow)
   * @param plugin Plugin instance for parameter access (void* to avoid header dependency)
   * @param config DSP configuration to update
   */
  void HandleWindowLockToggle(
    bool isLocked,
    int clickedWindowParam,
    void* plugin,
    DSPConfig& config
  );

private:
  // Helper: Create progress callback for brain operations
  std::function<void(const std::string&, int, int)> MakeProgressCallback();

  // Dependencies (non-owning references)
  Window* mAnalysisWindow;
  Window* mOutputWindow;
  Brain* mBrain;
  AudioStreamChunker* mChunker;
  ParameterManager* mParamManager;
  BrainManager* mBrainManager;
  ui::ProgressOverlayManager* mProgressOverlayMgr;
};

} // namespace synaptic
