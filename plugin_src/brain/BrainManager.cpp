#include "BrainManager.h"
#include "plugin_src/PlatformFileDialogs.h"
#include "SynapticResynthesis.h"
#include "IPlugStructs.h"
#include <thread>
#include <cstdio>

namespace synaptic
{
  BrainManager::BrainManager(Brain* brain, Window* analysisWindow, UIBridge* uiBridge)
    : mBrain(brain)
    , mAnalysisWindow(analysisWindow)
    , mUIBridge(uiBridge)
  {
  }

  bool BrainManager::HandleMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
  {
    switch (msgTag)
    {
      case ::kMsgTagBrainAddFile:
      {
        // pData holds raw bytes: [uint16_t nameLenLE][name bytes UTF-8][file bytes]
        if (!pData || dataSize <= 2)
          return false;

        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(pData);
        uint16_t nameLen = (uint16_t)(bytes[0] | (bytes[1] << 8));
        if (2 + nameLen > dataSize)
          return false;

        std::string name(reinterpret_cast<const char*>(bytes + 2),
                        reinterpret_cast<const char*>(bytes + 2 + nameLen));
        const void* fileData = bytes + 2 + nameLen;
        size_t fileSize = static_cast<size_t>(dataSize - (2 + nameLen));

        // AddFileFromMemory will handle overlay and return result
        // We need sample rate, channels, and chunk size from caller context
        // For now, return false - caller will handle inline
        // (This is a synchronous operation that needs plugin context)
        return false;  // Let caller handle this one inline
      }

      case ::kMsgTagBrainRemoveFile:
      {
        RemoveFile(ctrlTag);
        return true;
      }

      case ::kMsgTagBrainExport:
      {
        ExportToFileAsync([](const std::string&, int, int){}, [](){});  // Empty progress and completion callbacks
        return true;
      }

      case ::kMsgTagBrainImport:
      {
        ImportFromFileAsync([](const std::string&, int, int){}, [](){});  // Empty progress and completion callbacks
        return true;
      }

      case ::kMsgTagBrainReset:
      {
        Reset();
        return true;
      }

      case ::kMsgTagBrainDetach:
      {
        Detach();
        return true;
      }

      default:
        return false;
    }
  }

  int BrainManager::AddFileFromMemory(const void* data, size_t size, const std::string& name,
                                      int sampleRate, int channels, int chunkSize)
  {
    if (!mBrain) return -1;

    mUIBridge->ShowOverlay(std::string("Importing ") + name);
    int newId = mBrain->AddAudioFileFromMemory(data, size, name, sampleRate, channels, chunkSize);

    if (newId >= 0)
    {
      mBrainDirty = true;
    }

    mUIBridge->HideOverlay();
    return newId;
  }

  void BrainManager::RemoveFile(int fileId)
  {
    if (!mBrain) return;

    mBrain->RemoveFile(fileId);
    mBrainDirty = true;
  }

  void BrainManager::Reset()
  {
    if (!mBrain) return;

    mBrain->Reset();
    mBrain->SetWindow(mAnalysisWindow);
    mUseExternalBrain = false;
    mExternalBrainPath.clear();
    mBrainDirty = false;

    // Send external ref update to UI
    mUIBridge->SendExternalRefInfo(false, std::string());
  }

  void BrainManager::Detach()
  {
    // Stop referencing the external file; keep current in-memory brain and save inline next
    mUseExternalBrain = false;
    mExternalBrainPath.clear();
    mBrainDirty = true;

    // Send external ref update to UI
    mUIBridge->SendExternalRefInfo(false, std::string());
  }

  void BrainManager::SetExternalRef(const std::string& path, bool useExternal)
  {
    mExternalBrainPath = path;
    mUseExternalBrain = useExternal;
  }

  void BrainManager::RechunkAllFilesAsync(int newChunkSize, int sampleRate, ProgressFn onProgress, CompletionFn onComplete)
  {
    if (!mBrain) return;

    // Skip if brain is empty (no files to rechunk)
    if (mBrain->GetTotalChunks() == 0)
    {
      DBGMSG("Rechunk skipped: brain is empty\n");
      if (onComplete) onComplete();
      return;
    }

    // Check if already running
    if (mOperationInProgress.exchange(true))
    {
      DBGMSG("Rechunk request ignored: already running.\n");
      return;
    }

    // Get initial file count for progress tracking
    const int totalFiles = (int)mBrain->GetSummary().size();

    std::thread([this, newChunkSize, sampleRate, totalFiles, onProgress, onComplete]()
    {
      int currentFile = 0;

      auto stats = mBrain->RechunkAllFiles(newChunkSize, sampleRate, [&currentFile, totalFiles, onProgress](const std::string& displayName)
      {
        if (onProgress)
        {
          currentFile++;
          onProgress(displayName, currentFile, totalFiles);
        }
      });

      DBGMSG("Brain Rechunk: processed=%d, rechunked=%d, totalChunks=%d\n",
             stats.filesProcessed, stats.filesRechunked, stats.newTotalChunks);

      mBrainDirty = true;

      // Queue UI updates
      mUIBridge->MarkBrainSummaryPending();

      // Call completion callback
      if (onComplete)
        onComplete();

      mOperationInProgress = false;
    }).detach();
  }

  void BrainManager::ReanalyzeAllChunksAsync(int sampleRate, ProgressFn onProgress, CompletionFn onComplete)
  {
    if (!mBrain) return;

    // Skip if brain is empty (no files to reanalyze)
    if (mBrain->GetTotalChunks() == 0)
    {
      DBGMSG("Reanalyze skipped: brain is empty\n");
      if (onComplete) onComplete();
      return;
    }

    // Check if already running
    if (mOperationInProgress.exchange(true))
    {
      DBGMSG("Reanalyze request ignored: already running.\n");
      return;
    }

    // Get initial file count for progress tracking
    const int totalFiles = (int)mBrain->GetSummary().size();

    std::thread([this, sampleRate, totalFiles, onProgress, onComplete]()
    {
      int currentFile = 0;

      auto stats = mBrain->ReanalyzeAllChunks(sampleRate, [&currentFile, totalFiles, onProgress](const std::string& displayName)
      {
        if (onProgress)
        {
          currentFile++;
          onProgress(displayName, currentFile, totalFiles);
        }
      });

      DBGMSG("Brain Reanalyze: files=%d chunks=%d\n", stats.filesProcessed, stats.chunksProcessed);

      mBrainDirty = true;

      // Queue UI updates
      mUIBridge->MarkBrainSummaryPending();

      // Call completion callback
      if (onComplete)
        onComplete();

      mOperationInProgress = false;
    }).detach();
  }

  void BrainManager::ExportToFileAsync(ProgressFn onProgress, CompletionFn onComplete)
  {
    if (!mBrain) return;

    // Move to background thread to avoid WebView2 re-entrancy when showing native dialogs
    std::thread([this, onProgress, onComplete]()
    {
      // Show initial progress (0 of 2) - waiting for file selection
      if (onProgress)
        onProgress("Waiting for file selection...", 0, 2);

      std::string savePath;
      const bool chose = platform::GetSaveFilePath(savePath,
                                                   L"Synaptic Brain (*.sbrain)\0*.sbrain\0All Files (*.*)\0*.*\0\0",
                                                   L"SynapticResynthesis-Brain.sbrain");
      if (!chose)
      {
        // User cancelled - call completion without progress
        if (onComplete)
          onComplete();
        return;
      }

      // File selected - update progress (1 of 2 = 50%)
      if (onProgress)
        onProgress("Exporting brain...", 1, 2);

      // Serialize brain to chunk
      iplug::IByteChunk blob;
      mBrain->SerializeSnapshotToChunk(blob);

      // Write to file
      FILE* fp = fopen(savePath.c_str(), "wb");
      if (fp)
      {
        fwrite(blob.GetData(), 1, (size_t)blob.Size(), fp);
        fclose(fp);

        mExternalBrainPath = savePath;
        mUseExternalBrain = true;
        mBrainDirty = false;

        // Notify UI about new external ref
        nlohmann::json j;
        j["id"] = "brainExternalRef";
        j["info"] = {{"path", mExternalBrainPath}};
        mUIBridge->EnqueueJSON(j);

        // Refresh DSP config to show storage indicator
        mUIBridge->MarkDSPConfigPending();
      }

      if (onComplete)
        onComplete();

    }).detach();
  }

  void BrainManager::ImportFromFileAsync(ProgressFn onProgress, CompletionFn onComplete)
  {
    if (!mBrain) return;

    // Native Open dialog; C++ reads file directly
    std::thread([this, onProgress, onComplete]()
    {
      // Show initial progress (0 of 2) - waiting for file selection
      if (onProgress)
        onProgress("Waiting for file selection...", 0, 2);

      std::string openPath;
      if (!platform::GetOpenFilePath(openPath, L"Synaptic Brain (*.sbrain)\0*.sbrain\0All Files (*.*)\0*.*\0\0"))
      {
        // User cancelled
        if (onComplete)
          onComplete();
        return;
      }

      // File selected - update progress (1 of 2 = 50%)
      if (onProgress)
        onProgress("Importing brain...", 1, 2);

      // Read file
      FILE* fp = fopen(openPath.c_str(), "rb");
      if (!fp)
      {
        if (onComplete)
          onComplete();
        return;
      }

      fseek(fp, 0, SEEK_END);
      long sz = ftell(fp);
      fseek(fp, 0, SEEK_SET);
      std::vector<char> data;
      data.resize((size_t)sz);
      fread(data.data(), 1, (size_t)sz, fp);
      fclose(fp);

      // Deserialize
      iplug::IByteChunk in;
      in.PutBytes(data.data(), (int)data.size());
      mBrain->DeserializeSnapshotFromChunk(in, 0);
      mBrain->SetWindow(mAnalysisWindow);

      mExternalBrainPath = openPath;
      mUseExternalBrain = true;
      mBrainDirty = false;

      // Extract imported settings for UI param sync
      const int importedChunkSize = mBrain->GetChunkSize();
      const int importedWindowMode = Window::TypeToInt(mBrain->GetSavedAnalysisWindowType());

      mPendingImportedChunkSize = importedChunkSize;
      mPendingImportedAnalysisWindow = importedWindowMode;

      // Queue UI updates
      mUIBridge->MarkBrainSummaryPending();
      nlohmann::json j;
      j["id"] = "brainExternalRef";
      j["info"] = {{"path", mExternalBrainPath}};
      mUIBridge->EnqueueJSON(j);

      if (onComplete)
        onComplete();

    }).detach();
  }

  void BrainManager::AddMultipleFilesAsync(std::vector<FileData> files, int sampleRate, int channels,
                                           int chunkSize, ProgressFn onProgress, CompletionFn onComplete)
  {
    if (!mBrain) return;

    const int totalFiles = (int)files.size();
    if (totalFiles == 0)
    {
      if (onComplete) onComplete();
      return;
    }

    // Check if already running
    if (mOperationInProgress.exchange(true))
    {
      DBGMSG("AddMultipleFiles request ignored: already running.\n");
      return;
    }

    std::thread([this, files = std::move(files), sampleRate, channels, chunkSize, totalFiles, onProgress, onComplete]() mutable
    {
      int currentFile = 0;

      for (auto& fileData : files)
      {
        currentFile++;

        // Report progress
        if (onProgress)
        {
          onProgress(fileData.name, currentFile, totalFiles);
        }

        // Import file
        int newId = mBrain->AddAudioFileFromMemory(
          fileData.data.data(),
          fileData.data.size(),
          fileData.name,
          sampleRate,
          channels,
          chunkSize
        );

        if (newId >= 0)
        {
          mBrainDirty = true;
        }

        DBGMSG("Imported file %d/%d: %s (id=%d)\n", currentFile, totalFiles, fileData.name.c_str(), newId);
      }

      // Queue UI updates
      mUIBridge->MarkBrainSummaryPending();

      // Call completion callback
      if (onComplete)
        onComplete();

      mOperationInProgress = false;
    }).detach();
  }
}

