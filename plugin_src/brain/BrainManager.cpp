#include "BrainManager.h"
#include "plugin_src/PlatformFileDialogs.h"
#include "SynapticResynthesis.h"
#include "IPlugStructs.h"
#include "../../exdeps/miniaudio/miniaudio.h"
#include <thread>
#include <cstdio>
#include <algorithm>

namespace synaptic
{
  BrainManager::BrainManager(Brain* brain, Window* analysisWindow)
    : mBrain(brain)
    , mAnalysisWindow(analysisWindow)
  {
  }

  BrainManager::~BrainManager()
  {
    // Ensure all background threads are joined before destruction
    RequestCancellation();

    std::lock_guard<std::mutex> lock(mThreadMutex);
    for (auto& t : mActiveThreads)
    {
      if (t.joinable())
        t.join();
    }
  }

  void BrainManager::LaunchThread(std::function<void()> task)
  {
    std::lock_guard<std::mutex> lock(mThreadMutex);

    // Store the task in the active threads vector to avoid unbounded growth
    mActiveThreads.emplace_back(std::move(task));
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
  }

  void BrainManager::Detach()
  {
    // Stop referencing the external file and reset brain to empty state
    mUseExternalBrain = false;
    mExternalBrainPath.clear();

    // Reset brain to empty state (clear all files and chunks)
    if (mBrain)
    {
      mBrain->Reset();
      mBrain->SetWindow(mAnalysisWindow);
    }

    mBrainDirty = false;
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
      if (onComplete) onComplete(false);
      return;
    }

    // Check if already running
    if (mOperationInProgress.exchange(true))
    {
      DBGMSG("Rechunk request ignored: already running.\n");
      return;
    }

    // Reset cancellation flag before starting
    ResetCancellationFlag();

    LaunchThread([this, newChunkSize, sampleRate, onProgress, onComplete]()
    {
      // Brain's RechunkAllFiles now reports per-chunk progress with (fileName, currentChunk, totalChunks)
      auto stats = mBrain->RechunkAllFiles(newChunkSize, sampleRate,
        [onProgress](const std::string& displayName, int current, int total)
        {
          if (onProgress)
            onProgress(displayName, current, total);
        },
        &mCancellationRequested);

      if (stats.wasCancelled)
      {
        DBGMSG("Brain Rechunk: CANCELLED by user\n");
      }
      else
      {
        DBGMSG("Brain Rechunk: processed=%d, rechunked=%d, totalChunks=%d\n",
               stats.filesProcessed, stats.filesRechunked, stats.newTotalChunks);
      }

      if (!stats.wasCancelled)
      {
        mBrainDirty = true;
      }

      // Call completion callback with cancellation status
      // Note: Keep mOperationInProgress true during callback to prevent OnParamChange from retriggering operations during rollback
      if (onComplete)
        onComplete(stats.wasCancelled);

      mOperationInProgress = false;  // Clear flag AFTER callback completes
    });
  }

  void BrainManager::ReanalyzeAllChunksAsync(int sampleRate, ProgressFn onProgress, CompletionFn onComplete)
  {
    if (!mBrain) return;

    // Skip if brain is empty (no files to reanalyze)
    if (mBrain->GetTotalChunks() == 0)
    {
      DBGMSG("Reanalyze skipped: brain is empty\n");
      if (onComplete) onComplete(false);
      return;
    }

    // Check if already running
    if (mOperationInProgress.exchange(true))
    {
      DBGMSG("Reanalyze request ignored: already running.\n");
      return;
    }

    // Reset cancellation flag before starting
    ResetCancellationFlag();

    LaunchThread([this, sampleRate, onProgress, onComplete]()
    {
      // Brain's ReanalyzeAllChunks now reports per-chunk progress with (fileName, currentChunk, totalChunks)
      auto stats = mBrain->ReanalyzeAllChunks(sampleRate,
        [onProgress](const std::string& displayName, int current, int total)
        {
          if (onProgress)
            onProgress(displayName, current, total);
        },
        &mCancellationRequested);

      if (stats.wasCancelled)
      {
        DBGMSG("Brain Reanalyze: CANCELLED by user\n");
      }
      else
      {
        DBGMSG("Brain Reanalyze: files=%d chunks=%d\n", stats.filesProcessed, stats.chunksProcessed);
      }

      if (!stats.wasCancelled)
      {
        mBrainDirty = true;
      }

      // Call completion callback with cancellation status
      // Note: Keep mOperationInProgress true during callback to prevent OnParamChange from retriggering operations during rollback
      if (onComplete)
        onComplete(stats.wasCancelled);

      mOperationInProgress = false;  // Clear flag AFTER callback completes
    });
  }

  void BrainManager::ExportToFileAsync(ProgressFn onProgress, CompletionFn onComplete)
  {
    if (!mBrain) return;

    // Move to background thread to avoid blocking the UI thread with native dialogs
    LaunchThread([this, onProgress, onComplete]()
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
          onComplete(false);
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
      }

      if (onComplete)
        onComplete(false);  // Export doesn't support cancellation yet

    });
  }

  void BrainManager::ImportFromFileAsync(ProgressFn onProgress, CompletionFn onComplete)
  {
    if (!mBrain) return;

    // Native Open dialog; C++ reads file directly
    LaunchThread([this, onProgress, onComplete]()
    {
      // Show initial progress (0 of 2) - waiting for file selection
      if (onProgress)
        onProgress("Waiting for file selection...", 0, 2);

      std::string openPath;
      if (!platform::GetOpenFilePath(openPath, L"Synaptic Brain (*.sbrain)\0*.sbrain\0All Files (*.*)\0*.*\0\0"))
      {
        // User cancelled
        if (onComplete)
          onComplete(false);
        return;
      }

      // File selected - update progress (1 of 3 = ~33%)
      if (onProgress)
        onProgress("Reading brain file...", 1, 3);

      // Read file
      FILE* fp = fopen(openPath.c_str(), "rb");
      if (!fp)
      {
        if (onComplete)
          onComplete(false);
        return;
      }

      fseek(fp, 0, SEEK_END);
      long sz = ftell(fp);
      fseek(fp, 0, SEEK_SET);
      std::vector<char> data;
      data.resize((size_t)sz);
      fread(data.data(), 1, (size_t)sz, fp);
      fclose(fp);

      // Show initial loading progress
      if (onProgress)
        onProgress("Loading brain data...", 2, 3);

      // Deserialize with progress callback for compact brain rechunking/analysis
      iplug::IByteChunk in;
      in.PutBytes(data.data(), (int)data.size());
      mBrain->DeserializeSnapshotFromChunk(in, 0,
        [onProgress](const std::string& fileName, int current, int total)
        {
          // For compact brains, this shows rechunking and analysis progress
          if (onProgress)
            onProgress("Rechunking & Analyzing: " + fileName, current, total);
        });
      mBrain->SetWindow(mAnalysisWindow);

      mExternalBrainPath = openPath;
      mUseExternalBrain = true;
      mBrainDirty = false;

      // Extract imported settings for UI param sync
      const int importedChunkSize = mBrain->GetChunkSize();
      const int importedWindowMode = Window::TypeToInt(mBrain->GetSavedAnalysisWindowType());

      mPendingImportedChunkSize = importedChunkSize;
      mPendingImportedAnalysisWindow = importedWindowMode;

      if (onComplete)
        onComplete(false);  // Import doesn't support cancellation yet

    });
  }

  void BrainManager::CreateNewBrainAsync(ProgressFn onProgress, CompletionFn onComplete)
  {
    if (!mBrain) return;

    // Move to background thread to avoid blocking the UI thread with native dialogs
    LaunchThread([this, onProgress, onComplete]()
    {
      // Show initial progress (0 of 2) - waiting for file selection
      if (onProgress)
        onProgress("Waiting for file selection...", 0, 2);

      std::string savePath;
      const bool chose = platform::GetSaveFilePath(savePath,
                                                   L"Synaptic Brain (*.sbrain)\0*.sbrain\0All Files (*.*)\0*.*\0\0",
                                                   L"NewBrain.sbrain");
      if (!chose)
      {
        // User cancelled - call completion without progress
        if (onComplete)
          onComplete(false);
        return;
      }

      // File selected - update progress (1 of 2 = 50%)
      if (onProgress)
        onProgress("Creating empty brain...", 1, 2);

      // Reset brain to empty state (clear all files and chunks)
      mBrain->Reset();
      mBrain->SetWindow(mAnalysisWindow);

      // Serialize empty brain to chunk
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
      }

      if (onComplete)
        onComplete(false);  // Create new doesn't support cancellation yet

    });
  }

  void BrainManager::AddMultipleFilesAsync(std::vector<FileData> files, int sampleRate, int channels,
                                           int chunkSize, ProgressFn onProgress, CompletionFn onComplete)
  {
    if (!mBrain) return;

    const int totalFiles = (int)files.size();
    if (totalFiles == 0)
    {
      if (onComplete) onComplete(false);
      return;
    }

    // Check if already running
    if (mOperationInProgress.exchange(true))
    {
      DBGMSG("AddMultipleFiles request ignored: already running.\n");
      return;
    }

    // Reset cancellation flag before starting
    ResetCancellationFlag();

    LaunchThread([this, files = std::move(files), sampleRate, channels, chunkSize, totalFiles, onProgress, onComplete]() mutable
    {
      // Pre-scan files to estimate total chunks for cumulative progress tracking
      int estimatedTotalChunks = 0;

      for (auto& fileData : files)
      {
        // Decode to get length without full processing
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, (ma_uint32)channels, (ma_uint32)sampleRate);
        ma_decoder decoder;
        ma_uint64 frameCount = 0;

        if (ma_decoder_init_memory(fileData.data.data(), fileData.data.size(), &config, &decoder) == MA_SUCCESS)
        {
          if (ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount) == MA_SUCCESS)
          {
            estimatedTotalChunks += Brain::EstimateChunkCount((int)frameCount, chunkSize);
          }
          else
          {
            estimatedTotalChunks += 10; // fallback estimate if frame count unavailable
          }
          ma_decoder_uninit(&decoder);
        }
        else
        {
          estimatedTotalChunks += 10; // fallback estimate if decode fails
        }
      }

      int cumulativeChunks = 0;

      // Import files with cumulative progress tracking
      for (auto& fileData : files)
      {
        // Check for cancellation before processing each file
        if (mCancellationRequested.load())
        {
          DBGMSG("Multi-file import CANCELLED by user after %d files\n", (int)(&fileData - &files[0]));
          break;
        }

        // Import file with per-chunk progress callback that reports cumulative progress
        int newId = mBrain->AddAudioFileFromMemory(
          fileData.data.data(),
          fileData.data.size(),
          fileData.name,
          sampleRate,
          channels,
          chunkSize,
          [&fileData, &cumulativeChunks, estimatedTotalChunks, onProgress](const std::string& fileName, int currentChunk, int totalChunksInFile)
          {
            // Report cumulative progress across all files
            if (onProgress)
            {
              ++cumulativeChunks;
              onProgress(fileData.name, cumulativeChunks, estimatedTotalChunks);
            }
          },
          &mCancellationRequested
        );

        if (newId >= 0)
        {
          mBrainDirty = true;
        }

        DBGMSG("Imported file: %s (id=%d)\n", fileData.name.c_str(), newId);
      }

      // Check if any file was cancelled (if we broke out of loop early due to cancellation)
      bool wasCancelled = mCancellationRequested.load();

      // Call completion callback with cancellation status
      if (onComplete)
        onComplete(wasCancelled);

      mOperationInProgress = false;
    });
  }
}
