#pragma once

#include "json.hpp"
#include "plugin_src/modules/DSPConfig.h"
#include "IPlug_include_in_plug_hdr.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

// Forward declarations
namespace synaptic
{
  class Brain;
  class IChunkBufferTransformer;
  struct IMorph;
}

namespace synaptic
{
  /**
   * @brief Handles all UI-to-C++ communication
   *
   * Manages JSON message building, thread-safe queue for background->UI updates,
   * and provides clean interface for sending various state updates to the UI.
   */
  class UIBridge
  {
  public:
    explicit UIBridge(iplug::IEditorDelegate* delegate);

    // === UI Message Senders ===

    /**
     * @brief Send brain file summary to UI
     * Sends JSON with id="brainSummary" containing array of {id, name, chunks}
     */
    void SendBrainSummary(const Brain& brain);

    /**
     * @brief Send current transformer parameter schema and values to UI
     * Sends JSON with id="transformerParams" containing parameter descriptions
     * Takes shared_ptr to prevent transformer from being destroyed during async operations
     */
    void SendTransformerParams(std::shared_ptr<const IChunkBufferTransformer> transformer);

    /**
     * @brief Send current morph parameter schema and values to UI
     * Sends JSON with id="morphParams" containing parameter descriptions
     */
    void SendMorphParams(std::shared_ptr<const IMorph> morph);

    /**
     * @brief Send DSP configuration to UI
     * Sends JSON with id="dspConfig" containing all DSP settings
     */
    void SendDSPConfig(const DSPConfig& config);

    /**
     * @brief Send DSP config with transformer algorithm options
     * Includes the list of available algorithms from the factory
     */
    void SendDSPConfigWithAlgorithms(const DSPConfig& config);

    /**
     * @brief Send DSP config with transformer algorithms and morph modes and current morph index
     */
    void SendDSPConfigWithAlgorithms(const DSPConfig& config, int currentMorphIndex);

    /**
     * @brief Send external brain reference info to UI
     * Sends JSON with id="brainExternalRef"
     */
    void SendExternalRefInfo(bool useExternal, const std::string& path);

    /**
     * @brief Send all state to UI (used on UI ready and state restore)
     */
    void SendAllState(const Brain& brain,
                      std::shared_ptr<const IChunkBufferTransformer> transformer,
                      std::shared_ptr<const IMorph> morph,
                      const DSPConfig& config);

    // === Overlay Controls ===

    /**
     * @brief Show overlay with text (for long operations like rechunking)
     */
    void ShowOverlay(const std::string& text);

    /**
     * @brief Hide overlay
     */
    void HideOverlay();

    // === Thread-Safe Queue Management ===

    /**
     * @brief Enqueue JSON payload for sending from background thread
     * Thread-safe: can be called from any thread
     */
    void EnqueuePayload(const std::string& jsonPayload);

    /**
     * @brief Enqueue JSON object for sending from background thread
     * Thread-safe: can be called from any thread
     */
    void EnqueueJSON(const nlohmann::json& j);

    /**
     * @brief Drain queued messages and send to UI
     * Must be called from main thread (typically in OnIdle)
     */
    void DrainQueue();

    // === Atomic Flags for Deferred Updates ===

    /**
     * @brief Set flag to send brain summary on next DrainQueue
     */
    void MarkBrainSummaryPending() { mPendingSendBrainSummary = true; }

    /**
     * @brief Set flag to send DSP config on next DrainQueue
     */
    void MarkDSPConfigPending() { mPendingDSPConfig = true; }

    /**
     * @brief Get delegate for direct access (used by modules that need it)
     */
    iplug::IEditorDelegate* GetDelegate() { return mDelegate; }

  private:
    /**
     * @brief Send JSON immediately (must be on main thread)
     */
    void SendJSON(const nlohmann::json& j);

  private:
    iplug::IEditorDelegate* mDelegate;

    // Thread-safe queue for background->UI messages
    std::mutex mQueueMutex;
    std::vector<std::string> mQueue;

    // Atomic flags for coalescing repeated updates
    std::atomic<bool> mPendingSendBrainSummary{false};
    std::atomic<bool> mPendingDSPConfig{false};
  };
}

