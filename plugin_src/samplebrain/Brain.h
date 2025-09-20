#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>

#include "plugin_src/AudioStreamChunker.h"
#include "IPlugStructs.h"

// Forward declare miniaudio types to avoid including the large header here.
// We will include it in the .cpp only.

namespace synaptic
{
  struct BrainChunk
  {
    AudioChunk audio; // same format as realtime chunks
    int fileId = -1;
    int chunkIndexInFile = -1;
    // Per-channel analysis
    std::vector<float> rmsPerChannel;     // RMS per channel
    std::vector<double> freqHzPerChannel; // ZCR-based frequency per channel
    // FFT analysis (per channel)
    // Magnitude spectrum per channel (length = fftSize/2 + 1), computed via PFFFT
    std::vector<std::vector<float>> fftMagnitudePerChannel;
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
    void Reset()
    {
      std::lock_guard<std::mutex> lock(mutex_);
      nextFileId_ = 1;
      files_.clear();
      idToFileIndex_.clear();
      chunks_.clear();
    }

    // Set the window to use for FFT analysis
    void SetWindow(const class Window* window) { mWindow = window; }

    // Decode an entire audio file from memory and split into chunks.
    // Returns the new fileId on success, or -1 on failure.
    int AddAudioFileFromMemory(const void* data,
                               size_t dataSize,
                               const std::string& displayName,
                               int targetSampleRate,
                               int targetChannels,
                               int chunkSizeSamples);

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
    using RechunkProgressFn = std::function<void(const std::string& /*displayName*/)>;
    RechunkStats RechunkAllFiles(int newChunkSizeSamples, int targetSampleRate, RechunkProgressFn onProgress = nullptr);
    int GetChunkSize() const { return mChunkSize; }

    // Snapshot serialization (unified for project state and .sbrain files)
    bool SerializeSnapshotToChunk(iplug::IByteChunk& out) const;
    int DeserializeSnapshotFromChunk(const iplug::IByteChunk& in, int startPos);

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
  };
}


