#pragma once

#include "plugin_src/brain/Brain.h"
#include "plugin_src/audio/Window.h"
#include "plugin_src/ui_bridge/UIBridge.h"
#include <atomic>
#include <string>
#include <functional>

namespace synaptic
{
  /**
   * @brief Manages all brain-related operations
   *
   * Handles brain add/remove/export/import/rechunk/reanalyze operations,
   * manages external brain file references, and coordinates background threading
   * for long-running operations.
   */
  class BrainManager
  {
  public:
    /**
     * @brief Construct BrainManager
     * @param brain Reference to brain instance (owned by plugin)
     * @param analysisWindow Reference to analysis window (owned by plugin)
     * @param uiBridge Reference to UI bridge for messages/overlays
     */
    explicit BrainManager(Brain* brain, Window* analysisWindow, UIBridge* uiBridge);

    // === Message Handling ===

    /**
     * @brief Handle brain-related messages from UI
     * @param msgTag Message tag (kMsgTagBrainXxx)
     * @param ctrlTag Control tag (fileId for remove, etc.)
     * @param dataSize Data size for binary messages
     * @param pData Data pointer for binary messages
     * @return true if message was handled
     */
    bool HandleMessage(int msgTag, int ctrlTag, int dataSize, const void* pData);

    // === Direct Operations (Synchronous) ===

    /**
     * @brief Add audio file from memory buffer
     * @param data Audio file data (any format supported by miniaudio)
     * @param size Data size in bytes
     * @param name Display name for the file
     * @param sampleRate Target sample rate
     * @param channels Target channel count
     * @param chunkSize Chunk size in samples
     * @return New file ID on success, -1 on failure
     */
    int AddFileFromMemory(const void* data, size_t size, const std::string& name,
                         int sampleRate, int channels, int chunkSize);

    /**
     * @brief Remove a file and all its chunks from brain
     * @param fileId File ID to remove
     */
    void RemoveFile(int fileId);

    /**
     * @brief Reset brain (clear all files and chunks)
     */
    void Reset();

    /**
     * @brief Detach external brain reference (keep in-memory data, switch to inline storage)
     */
    void Detach();

    // === Asynchronous Operations (with callbacks) ===

    using CompletionFn = std::function<void(bool wasCancelled)>;
    using ProgressFn = std::function<void(const std::string& message, int current, int total)>;

    /**
     * @brief Rechunk all brain files to new chunk size (background thread)
     * @param newChunkSize New chunk size in samples
     * @param sampleRate Sample rate for analysis
     * @param onProgress Progress callback (message, current, total)
     * @param onComplete Callback when rechunking completes (on main thread via OnIdle)
     */
    void RechunkAllFilesAsync(int newChunkSize, int sampleRate, ProgressFn onProgress, CompletionFn onComplete);

    /**
     * @brief Reanalyze all chunks with current window (background thread)
     * @param sampleRate Sample rate for analysis
     * @param onProgress Progress callback (message, current, total)
     * @param onComplete Callback when reanalysis completes (on main thread via OnIdle)
     */
    void ReanalyzeAllChunksAsync(int sampleRate, ProgressFn onProgress, CompletionFn onComplete);

    /**
     * @brief Export brain to file with native save dialog (background thread)
     * @param onProgress Progress callback (message, current, total)
     * @param onComplete Callback when export completes
     */
    void ExportToFileAsync(ProgressFn onProgress, CompletionFn onComplete);

    /**
     * @brief Import brain from file with native open dialog (background thread)
     * @param onProgress Progress callback (message, current, total)
     * @param onComplete Callback when import completes
     * @return Imported chunk size (for syncing UI params), or -1 if not imported
     */
    void ImportFromFileAsync(ProgressFn onProgress, CompletionFn onComplete);

    /**
     * @brief Create new empty brain file with native save dialog (background thread)
     * @param onProgress Progress callback (message, current, total)
     * @param onComplete Callback when creation completes
     */
    void CreateNewBrainAsync(ProgressFn onProgress, CompletionFn onComplete);

    // === State Management ===

    /**
     * @brief Check if brain has unsaved changes
     */
    bool IsDirty() const { return mBrainDirty; }

    /**
     * @brief Mark brain as dirty (has unsaved changes)
     */
    void SetDirty(bool dirty) const { mBrainDirty = dirty; }

    /**
     * @brief Check if using external brain file
     */
    bool UseExternal() const { return mUseExternalBrain; }

    /**
     * @brief Get external brain file path
     */
    const std::string& ExternalPath() const { return mExternalBrainPath; }

    /**
     * @brief Set external brain reference
     * @param path File path
     * @param useExternal Whether to use external mode
     */
    void SetExternalRef(const std::string& path, bool useExternal);

    /**
     * @brief Check if background operation is in progress
     */
    bool IsOperationInProgress() const { return mOperationInProgress; }

    /**
     * @brief Request cancellation of current operation
     */
    void RequestCancellation() { mCancellationRequested = true; }

    /**
     * @brief Check if cancellation has been requested
     */
    bool IsCancellationRequested() const { return mCancellationRequested; }

    /**
     * @brief Reset cancellation flag before starting new operation
     */
    void ResetCancellationFlag() { mCancellationRequested = false; }

    /**
     * @brief Get pending imported chunk size (for UI param sync)
     * @return Chunk size, or -1 if none pending
     */
    int GetPendingImportedChunkSize() { return mPendingImportedChunkSize.exchange(-1); }

    /**
     * @brief Get pending imported analysis window mode (for UI param sync)
     * @return Window mode (1-4), or -1 if none pending
     */
    int GetPendingImportedAnalysisWindow() { return mPendingImportedAnalysisWindow.exchange(-1); }

    // === Multi-File Import ===

    /**
     * @brief Data for a single file to import
     */
    struct FileData
    {
      std::vector<uint8_t> data;
      std::string name;
    };

    /**
     * @brief Add multiple files asynchronously with progress reporting
     * @param files Vector of file data to import
     * @param sampleRate Target sample rate
     * @param channels Target channel count
     * @param chunkSize Chunk size in samples
     * @param onProgress Progress callback (message, current, total)
     * @param onComplete Callback when all files are imported
     */
    void AddMultipleFilesAsync(std::vector<FileData> files, int sampleRate, int channels,
                               int chunkSize, ProgressFn onProgress, CompletionFn onComplete);

  private:
    // Core references (not owned)
    Brain* mBrain;
    Window* mAnalysisWindow;
    UIBridge* mUIBridge;

    // External brain state
    bool mUseExternalBrain = false;
    std::string mExternalBrainPath;
    mutable bool mBrainDirty = false;

    // Threading coordination
    std::atomic<bool> mOperationInProgress{false};
    std::atomic<bool> mCancellationRequested{false};

    // Import coordination (for param sync)
    std::atomic<int> mPendingImportedChunkSize{-1};
    std::atomic<int> mPendingImportedAnalysisWindow{-1};
  };
}

