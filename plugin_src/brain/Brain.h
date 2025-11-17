#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>

#include "plugin_src/modules/AudioStreamChunker.h"
#include "IPlugStructs.h"

// Forward declare miniaudio types to avoid including the large header here.
// We will include it in the .cpp only.

namespace synaptic
{
  struct BrainChunk
  {
    AudioChunk audio; // same format as realtime chunks (now includes complexSpectrum and fftSize)
    int fileId = -1;
    int chunkIndexInFile = -1;
    // Per-channel analysis
    std::vector<float> rmsPerChannel;     // RMS per channel
    std::vector<double> freqHzPerChannel; // ZCR-based frequency per channel
    // FFT analysis (per channel)
    // Magnitude spectrum per channel (length = fftSize/2 + 1), computed via PFFFT
    std::vector<std::vector<float>> complexSpectrum;
    // Dominant frequency (Hz) derived from FFT magnitude peak per channel
    std::vector<double> fftDominantHzPerChannel;
    // FFT size actually used for analysis. PFFFT has strict size constraints:
    // - N must be factorable by 2/3/5 only, and for real transforms on SSE it must be a multiple of 32.
    // Therefore we may zero-pad each chunk up to the next valid N (>= audio.numFrames), so this can
    // differ from the logical chunk size. We store it to make the analysis explicit and reproducible.
    int fftSize = 0;
    // Aggregate (averages across channels)
    float avgRms = 0.0f;
    double avgFreqHz = 0.0;
    double avgFftDominantHz = 0.0;
    // Extended feature analysis (per channel)
    std::vector<std::vector<float>> extendedFeaturesPerChannel; // 7 features per channel: [f0, affinity, sharpness, harmonicity, monotony, meanAffinity, meanContrast]
    std::vector<float> avgExtendedFeatures; // averaged across channels
  };

  struct BrainFile
  {
    int id = -1;
    std::string displayName; // filename (no path)
    int chunkCount = 0;
    std::vector<int> chunkIndices; // indices into mChunks
    int tailPaddingFrames = 0; // number of padded frames in the final chunk
  };

  class Brain
  {
  public:
    /**
     * @brief Global flag to enable compact brain format
     *
     * When true, .sbrain files save only reconstructed original audio + metadata.
     * This dramatically reduces file size (~100MB input = ~100MB output vs 800MB).
     * On load, files are automatically re-chunked with saved settings.
     *
     * When false (default), saves full chunked data with all analysis (faster load, larger files).
     *
     * Usage:
     *   Brain::sUseCompactBrainFormat = true;   // Enable compact mode
     *   brain.SerializeSnapshotToChunk(chunk);  // Saves in compact format
     *   brain.DeserializeSnapshotFromChunk(...);// Auto-detects and re-chunks
     */
    static bool sUseCompactBrainFormat;

    void Reset()
    {
      std::lock_guard<std::mutex> lock(mutex_);
      nextFileId_ = 1;
      files_.clear();
      idToFileIndex_.clear();
      chunks_.clear();
      mLastLoadedWasCompact = false; // Reset format tracking
    }

    // Set the window to use for FFT analysis
    void SetWindow(const class Window* window) { mWindow = window; }

    // Progress callback: (fileName, currentChunk, totalChunks)
    using ProgressFn = std::function<void(const std::string& /*fileName*/, int /*current*/, int /*total*/)>;

    // Decode an entire audio file from memory and split into chunks.
    // Returns the new fileId on success, or -1 on failure.
    // Optional progress callback reports per-chunk progress.
    int AddAudioFileFromMemory(const void* data,
                               size_t dataSize,
                               const std::string& displayName,
                               int targetSampleRate,
                               int targetChannels,
                               int chunkSizeSamples,
                               ProgressFn onProgress = nullptr);

    // Remove a previously-added file and all of its chunks.
    void RemoveFile(int fileId);

    // Build a compact summary for UI: vector of {id, name, chunkCount}
    struct FileSummary { int id; std::string name; int chunkCount; };
    std::vector<FileSummary> GetSummary() const;

    // Read-only access for transformers
    int GetTotalChunks() const;
    const BrainChunk* GetChunkByGlobalIndex(int idx) const;

    // Re-chunk all files to a new chunk size
    struct RechunkStats { int filesProcessed = 0; int filesRechunked = 0; int newTotalChunks = 0; };
    RechunkStats RechunkAllFiles(int newChunkSizeSamples, int targetSampleRate, ProgressFn onProgress = nullptr);
    int GetChunkSize() const { return mChunkSize; }

    // Re-analyze all existing chunks (no rechunking). Uses current window (SetWindow) and provided sampleRate.
    struct ReanalyzeStats { int filesProcessed = 0; int chunksProcessed = 0; };
    ReanalyzeStats ReanalyzeAllChunks(int targetSampleRate, ProgressFn onProgress = nullptr);

    // Helper: Estimate chunk count from audio length
    // Formula: (totalFrames * 2) / chunkSize - 1 (accounts for 50% overlap)
    static int EstimateChunkCount(int totalFrames, int chunkSize)
    {
      if (chunkSize <= 0) return 0;
      return std::max(1, (totalFrames * 2) / chunkSize - 1);
    }

    // Snapshot serialization (unified for project state and .sbrain files)
    bool SerializeSnapshotToChunk(iplug::IByteChunk& out) const;
    int DeserializeSnapshotFromChunk(const iplug::IByteChunk& in, int startPos, ProgressFn onProgress = nullptr);

    // Accessor for saved analysis window type as stored in snapshot
    Window::Type GetSavedAnalysisWindowType() const { return mSavedAnalysisWindowType; }

    // Check if the last loaded brain was in compact format
    bool WasLastLoadedInCompactFormat() const { return mLastLoadedWasCompact; }

  private:
    static float ComputeRMS(const std::vector<iplug::sample>& buffer, int offset, int count);
    static double ComputeZeroCrossingFreq(const std::vector<iplug::sample>& buffer, int offset, int count, double sampleRate);
    // Analyze the provided chunk over validFrames (<= chunk.audio.numFrames) and fill per-channel and average metrics
    void AnalyzeChunk(BrainChunk& chunk, int validFrames, double sampleRate);

  private:
    mutable std::mutex mutex_;
    int nextFileId_ = 1;
    std::vector<BrainFile> files_;
    std::unordered_map<int, int> idToFileIndex_;
    std::vector<BrainChunk> chunks_;
    int mChunkSize = 0;
    const class Window* mWindow = nullptr;
    // Saved in snapshot for import; defaults to Hann if unknown
    Window::Type mSavedAnalysisWindowType = Window::Type::Hann;
    // Track if the last loaded brain was in compact format (for UI sync)
    bool mLastLoadedWasCompact = false;
  };
}


