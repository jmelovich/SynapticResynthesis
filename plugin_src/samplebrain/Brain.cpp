#include "Brain.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#define MINIAUDIO_IMPLEMENTATION
#include "../../exdeps/miniaudio/miniaudio.h"
// Use PFFFT for FFT analysis
#include "../../exdeps/pffft/pffft.h"
#include "../Window.h"
#include "../FeatureAnalysis.h"
#include "../FFT.h"

namespace synaptic
{
  static constexpr uint32_t kSnapshotMagic = 0x53424252; // 'SBBR' Synaptic Brain BRain
  static constexpr uint16_t kSnapshotVersion = 3; // v3: added extended features

  static void PutString(iplug::IByteChunk& chunk, const std::string& s)
  {
    int32_t len = (int32_t) s.size();
    chunk.Put(&len);
    if (len > 0) chunk.PutBytes(s.data(), len);
  }

  static bool GetString(const iplug::IByteChunk& chunk, int& pos, std::string& out)
  {
    int32_t len = 0;
    pos = chunk.Get(&len, pos);
    if (pos < 0 || len < 0) return false;
    out.resize((size_t) len);
    if (len > 0)
    {
      pos = chunk.GetBytes(out.data(), len, pos);
      if (pos < 0) return false;
    }
    return true;
  }
  static void InterleaveToPlanar(const float* interleaved,
                                 int frames,
                                 int channels,
                                 std::vector<std::vector<iplug::sample>>& out)
  {
    out.assign(channels, std::vector<iplug::sample>(frames, 0.0));
    for (int ch = 0; ch < channels; ++ch)
    {
      for (int i = 0; i < frames; ++i)
      {
        out[ch][i] = (iplug::sample) interleaved[i * channels + ch];
      }
    }
  }

  float Brain::ComputeRMS(const std::vector<iplug::sample>& buffer, int offset, int count)
  {
    if (count <= 0 || offset < 0 || offset + count > (int) buffer.size())
      return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < count; ++i)
    {
      double x = buffer[offset + i];
      acc += x * x;
    }
    return (float) std::sqrt(acc / (double) std::max(1, count));
  }

  double Brain::ComputeZeroCrossingFreq(const std::vector<iplug::sample>& buffer, int offset, int count, double sampleRate)
  {
    if (count <= 1 || offset < 0 || offset + count > (int) buffer.size() || sampleRate <= 0.0)
      return 0.0;
    int zc = 0;
    double prev = buffer[offset];
    for (int i = 1; i < count; ++i)
    {
      double x = buffer[offset + i];
      if ((prev <= 0.0 && x > 0.0) || (prev >= 0.0 && x < 0.0))
        ++zc;
      prev = x;
    }
    double freq = (double) zc * sampleRate / (2.0 * (double) count);
    if (!(freq > 0.0)) return 0.0;
    const double nyquist = 0.5 * sampleRate;
    if (freq < 20.0) freq = 20.0;
    if (freq > nyquist - 20.0) freq = nyquist - 20.0;
    return freq;
  }

  void Brain::AnalyzeChunk(BrainChunk& chunk, int validFrames, double sampleRate)
  {
    const int chCount = (int) chunk.audio.channelSamples.size();
    if (validFrames <= 0 || chCount <= 0)
    {
      chunk.rmsPerChannel.assign(chCount, 0.0f);
      chunk.freqHzPerChannel.assign(chCount, 0.0);
      chunk.avgRms = 0.0f;
      chunk.avgFreqHz = 0.0;
      return;
    }

    chunk.rmsPerChannel.assign(chCount, 0.0f);
    chunk.freqHzPerChannel.assign(chCount, 0.0);
    double rmsSum = 0.0;
    double freqSum = 0.0;
    for (int ch = 0; ch < chCount; ++ch)
    {
      const auto& buf = chunk.audio.channelSamples[ch];
      float crms = ComputeRMS(buf, 0, validFrames);
      double cf = ComputeZeroCrossingFreq(buf, 0, validFrames, sampleRate);
      chunk.rmsPerChannel[ch] = crms;
      chunk.freqHzPerChannel[ch] = cf;
      rmsSum += crms;
      freqSum += cf;
    }
    chunk.avgRms = (chCount > 0) ? (float) (rmsSum / (double) chCount) : 0.0f;
    chunk.avgFreqHz = (chCount > 0) ? (freqSum / (double) chCount) : 0.0;

    // Use the chunk's nominal size for FFT (we zero-pad anyway) to match the chunker
    const int framesForFft = std::max(1, chunk.audio.numFrames);
    const int Nfft = Window::NextValidFFTSize(framesForFft);
    chunk.fftSize = Nfft;
    chunk.complexSpectrum.assign(chCount, std::vector<float>(Nfft/2 + 1, 0.0f));
    chunk.fftDominantHzPerChannel.assign(chCount, 0.0);

    // Initialize extended features
    chunk.extendedFeaturesPerChannel.assign(chCount, std::vector<float>(7, 0.0f));
    chunk.avgExtendedFeatures.assign(7, 0.0f);

    // Prepare PFFFT setup once per chunk
    PFFFT_Setup* setup = pffft_new_setup(Nfft, PFFFT_REAL);
    if (setup)
    {
      // Aligned buffers
      float* inAligned = (float*) pffft_aligned_malloc(sizeof(float) * Nfft);
      float* outAligned = (float*) pffft_aligned_malloc(sizeof(float) * Nfft);
      if (inAligned && outAligned)
      {
        for (int ch = 0; ch < chCount; ++ch)
        {
          // Copy valid frames and zero-pad
          for (int i = 0; i < Nfft; ++i)
          {
            float x = 0.0f;
            if (i < framesForFft && i < (int) chunk.audio.channelSamples[ch].size())
              x = (float) chunk.audio.channelSamples[ch][i];
            inAligned[i] = x;
          }

          // Apply windowing before FFT
          const Window& window = *mWindow;
          window(inAligned);

          // Ordered forward transform to get canonical interleaved output
          pffft_transform_ordered(setup, inAligned, outAligned, nullptr, PFFFT_FORWARD);

          // Extract magnitudes for bins 0..N/2
          auto& mags = chunk.complexSpectrum[ch];
          if ((int) mags.size() != (Nfft/2 + 1)) mags.assign(Nfft/2 + 1, 0.0f);
          // DC and Nyquist packed in first complex slot: out[0]=F0(real), out[1]=F(N/2)(real)
          mags[0] = std::abs(outAligned[0]);
          mags[Nfft/2] = std::abs(outAligned[1]);
          for (int k = 1; k < Nfft/2; ++k)
          {
            float re = outAligned[2 * k + 0];
            float im = outAligned[2 * k + 1];
            mags[k] = std::sqrt(re * re + im * im);
          }

          // Dominant bin (exclude DC if desired; keep it simple and include all)
          int bestK = 0;
          float bestMag = -std::numeric_limits<float>::infinity();
          for (int k = 0; k <= Nfft/2; ++k)
          {
            if (mags[k] > bestMag)
            {
              bestMag = mags[k];
              bestK = k;
            }
          }
          double domHz = (double) bestK * sampleRate / (double) Nfft;
          // Clamp to [20, nyquist-20]
          const double ny = 0.5 * sampleRate;
          if (domHz < 20.0) domHz = 20.0;
          if (domHz > ny - 20.0) domHz = ny - 20.0;
          chunk.fftDominantHzPerChannel[ch] = domHz;

          // Store full ordered spectrum into chunk.audio
          if (chunk.audio.fftSize != Nfft)
          {
            chunk.audio.fftSize = Nfft;
            chunk.audio.complexSpectrum.assign(chCount, std::vector<float>(Nfft, 0.0f));
          }
          if (ch < (int)chunk.audio.complexSpectrum.size())
          {
            std::memcpy(chunk.audio.complexSpectrum[ch].data(), outAligned, sizeof(float) * Nfft);
          }

          // Compute extended features using FeatureAnalysis (outAligned has ordered FFT output)
          auto features = FeatureAnalysis::GetFeatures(outAligned, Nfft, (float)sampleRate);
          if (features.size() >= 7)
          {
            chunk.extendedFeaturesPerChannel[ch] = features;
            for (int f = 0; f < 7; ++f)
              chunk.avgExtendedFeatures[f] += features[f];
          }
        }
      }
      if (inAligned) pffft_aligned_free(inAligned);
      if (outAligned) pffft_aligned_free(outAligned);
      pffft_destroy_setup(setup);
    }

    // Average FFT dominant Hz across channels
    double fftSum = 0.0;
    for (int ch = 0; ch < chCount; ++ch) fftSum += chunk.fftDominantHzPerChannel[ch];
    chunk.avgFftDominantHz = (chCount > 0) ? (fftSum / (double) chCount) : 0.0;

    // Average extended features across channels
    for (int f = 0; f < 7; ++f)
      chunk.avgExtendedFeatures[f] /= (chCount > 0) ? (float)chCount : 1.0f;
  }

  int Brain::AddAudioFileFromMemory(const void* data,
                                    size_t dataSize,


                                    const std::string& displayName,
                                    int targetSampleRate,
                                    int targetChannels,
                                    int chunkSizeSamples)
  {
    if (!data || dataSize == 0 || targetSampleRate <= 0 || targetChannels <= 0 || chunkSizeSamples <= 0)
      return -1;

    // Decode entire file using miniaudio to target format
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, (ma_uint32) targetChannels, (ma_uint32) targetSampleRate);
    ma_decoder decoder;
    if (ma_decoder_init_memory(data, dataSize, &config, &decoder) != MA_SUCCESS)
      return -1;

    ma_uint64 frameCount = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount) != MA_SUCCESS)
    {
      ma_decoder_uninit(&decoder);
      return -1;
    }

    std::vector<float> interleaved;
    interleaved.resize((size_t) frameCount * (size_t) targetChannels);
    ma_uint64 framesRead = 0;
    ma_result rr = ma_decoder_read_pcm_frames(&decoder, interleaved.data(), frameCount, &framesRead);
    if (rr != MA_SUCCESS) framesRead = 0;
    ma_decoder_uninit(&decoder);
    if (framesRead == 0)
      return -1;

    // Convert to planar channel-major layout
    std::vector<std::vector<iplug::sample>> planar;
    InterleaveToPlanar(interleaved.data(), (int) framesRead, targetChannels, planar);

    // Prepare file record
    std::lock_guard<std::mutex> lock(mutex_);
    mChunkSize = chunkSizeSamples;
    const int fileId = nextFileId_++;
    BrainFile fileRec;
    fileRec.id = fileId;
    fileRec.displayName = displayName;

    // Chunking
    const int totalFrames = (int) framesRead;
    int numChunks = 2*totalFrames / chunkSizeSamples - 1;
    fileRec.chunkIndices.reserve(numChunks);

    for (int c = 0; c < numChunks; ++c)
    {
      const int start = c * chunkSizeSamples/2;
      const int framesInChunk = std::min(chunkSizeSamples, totalFrames - start);
      if (framesInChunk <= 0)
        break;

      BrainChunk chunk;
      chunk.fileId = fileId;
      chunk.chunkIndexInFile = c;
      chunk.audio.numFrames = chunkSizeSamples;
      chunk.audio.channelSamples.assign(targetChannels, std::vector<sample>(chunkSizeSamples, 0.0));

      // Copy audio frames (zero-pad tail)
      for (int ch = 0; ch < targetChannels; ++ch)
      {
        for (int i = 0; i < framesInChunk; ++i)
          chunk.audio.channelSamples[ch][i] = planar[ch][start + i];
        for (int i = framesInChunk; i < chunkSizeSamples; ++i)
          chunk.audio.channelSamples[ch][i] = 0.0f;
      }

      // Analyze metrics for this chunk over valid frames
      if (mWindow) {
        AnalyzeChunk(chunk, framesInChunk, (double) targetSampleRate);
      }

      const int chunkGlobalIndex = (int) chunks_.size();
      chunks_.push_back(std::move(chunk));
      fileRec.chunkIndices.push_back(chunkGlobalIndex);
    }

    fileRec.chunkCount = (int) fileRec.chunkIndices.size();
    // Compute tail padding for last chunk
    const int totalFramesMod = (int) framesRead % chunkSizeSamples;
    if (fileRec.chunkCount > 0)
    {
      fileRec.tailPaddingFrames = (totalFramesMod == 0) ? 0 : (chunkSizeSamples - totalFramesMod);
    }
    else
    {
      fileRec.tailPaddingFrames = 0;
    }

    idToFileIndex_[fileId] = (int) files_.size();
    files_.push_back(std::move(fileRec));
    return fileId;
  }

  void Brain::RemoveFile(int fileId)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = idToFileIndex_.find(fileId);
    if (it == idToFileIndex_.end()) return;
    const int fileIdx = it->second;

    // Mark to remove: rebuild chunks_ compactly excluding this file's chunks
    std::vector<int> toRemove = files_[fileIdx].chunkIndices;
    std::vector<BrainChunk> newChunks;
    newChunks.reserve(chunks_.size());

    std::vector<int> indexMap(chunks_.size(), -1);
    std::vector<char> isRemoved(chunks_.size(), 0);
    for (int idx : toRemove)
      if (idx >= 0 && idx < (int) isRemoved.size()) isRemoved[idx] = 1;

    for (int i = 0; i < (int) chunks_.size(); ++i)
    {
      if (!isRemoved[i])
      {
        indexMap[i] = (int) newChunks.size();
        newChunks.push_back(std::move(chunks_[i]));
      }
    }
    chunks_.swap(newChunks);

    // Rebuild all files' chunkIndices using indexMap and drop the removed file
    std::vector<BrainFile> newFiles;
    newFiles.reserve(files_.size());
    idToFileIndex_.clear();

    for (int i = 0; i < (int) files_.size(); ++i)
    {
      if (i == fileIdx) continue;
      BrainFile f = std::move(files_[i]);
      std::vector<int> newIdxs;
      newIdxs.reserve(f.chunkIndices.size());
      for (int oldIdx : f.chunkIndices)
      {
        int mapped = (oldIdx >= 0 && oldIdx < (int) indexMap.size()) ? indexMap[oldIdx] : -1;
        if (mapped >= 0) newIdxs.push_back(mapped);
      }
      f.chunkIndices.swap(newIdxs);
      f.chunkCount = (int) f.chunkIndices.size();

      idToFileIndex_[f.id] = (int) newFiles.size();
      newFiles.push_back(std::move(f));
    }
    files_.swap(newFiles);
  }

  std::vector<Brain::FileSummary> Brain::GetSummary() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FileSummary> v;
    v.reserve(files_.size());
    for (const auto& f : files_)
      v.push_back({f.id, f.displayName, f.chunkCount});
    return v;
  }

  Brain::RechunkStats Brain::RechunkAllFiles(int newChunkSizeSamples, int targetSampleRate, RechunkProgressFn onProgress)
  {
    RechunkStats stats;
    if (newChunkSizeSamples <= 0 || targetSampleRate <= 0) return stats;

    // Snapshot current state under lock, then perform heavy work without holding the mutex.
    std::vector<BrainFile> filesSnapshot;
    std::vector<BrainChunk> chunksSnapshot;
    int oldChunkSize = 0;
    const Window& window = *mWindow;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      filesSnapshot = files_;
      chunksSnapshot = chunks_;
      oldChunkSize = (mChunkSize > 0) ? mChunkSize : newChunkSizeSamples;
    }

    // Rebuild each file by concatenating its chunks' valid frames, then rechunk
    std::vector<BrainFile> newFiles = filesSnapshot; // will mutate per-file indices/counts/padding
    std::vector<BrainChunk> newChunks;
    newChunks.reserve(chunksSnapshot.size());

    for (auto& f : newFiles)
    {
      ++stats.filesProcessed;
      if (onProgress) onProgress(f.displayName);

      // Concatenate valid frames across this file's chunks
      int totalValidFrames = 0;
      int numChannels = 0;
      // Determine channels from first valid chunk
      for (int gi : f.chunkIndices)
      {
        if (gi < 0 || gi >= (int)chunksSnapshot.size()) continue;
        const BrainChunk& bc = chunksSnapshot[gi];
        if ((int)bc.audio.channelSamples.size() > 0)
        {
          numChannels = (int)bc.audio.channelSamples.size();
          break;
        }
      }
      if (numChannels <= 0)
      {
        f.chunkCount = 0;
        f.chunkIndices.clear();
        continue;
      }

      // Reconstruct a contiguous buffer via overlap-add using original hopSize = oldChunkSize/2
      const int hop = std::max(1, oldChunkSize / 2);
      const int lastValidFrames = std::max(0, oldChunkSize - f.tailPaddingFrames);
      const int totalLen = (int) f.chunkIndices.size() > 0
        ? (int) ((int) f.chunkIndices.size() - 1) * hop + lastValidFrames
        : 0;
      std::vector<std::vector<sample>> planar(numChannels, std::vector<sample>(std::max(0, totalLen), 0.0));

      for (int ord = 0; ord < (int) f.chunkIndices.size(); ++ord)
      {
        const int gi = f.chunkIndices[ord];
        if (gi < 0 || gi >= (int)chunksSnapshot.size()) continue;
        const BrainChunk& bc = chunksSnapshot[gi];
        const bool isLast = (bc.chunkIndexInFile == (f.chunkCount - 1));
        const int valid = isLast ? lastValidFrames : oldChunkSize;
        const int start = ord * hop;
        const int srcChans = (int) bc.audio.channelSamples.size();
        for (int ch = 0; ch < numChannels; ++ch)
        {
          const auto& src = (ch < srcChans) ? bc.audio.channelSamples[ch] : std::vector<sample>();
          auto& dst = planar[ch];
          const int copyN = std::min(valid, (int) src.size());
          const int maxWrite = std::min(copyN, (int) dst.size() - start);
          if (maxWrite > 0)
          {
            std::memcpy(dst.data() + start, src.data(), sizeof(sample) * maxWrite);
          }
        }
      }
      totalValidFrames = totalLen;

      // Rechunk the reconstructed buffer
      f.chunkIndices.clear();
      const int totalFrames = totalValidFrames;
      const int numChunks = 2 * totalFrames / newChunkSizeSamples - 1;
      for (int c = 0; c < numChunks; ++c)
      {
        const int start = c * newChunkSizeSamples / 2;
        const int framesInChunk = std::min(newChunkSizeSamples, totalFrames - start);
        if (framesInChunk <= 0) break;

        BrainChunk out;
        out.fileId = f.id;
        out.chunkIndexInFile = c;
        out.audio.numFrames = newChunkSizeSamples;
        out.audio.channelSamples.assign(numChannels, std::vector<sample>(newChunkSizeSamples, 0.0));

        // Copy audio
        for (int ch = 0; ch < numChannels; ++ch)
        {
          auto& dst = out.audio.channelSamples[ch];
          const auto& src = planar[ch];
          const int copyN = framesInChunk;
          if (start < (int)src.size())
          {
            const int clampedCopy = std::min(copyN, (int)src.size() - start);
            std::memcpy(dst.data(), src.data() + start, sizeof(sample) * clampedCopy);
          }
        }

        // Analyze metrics for this chunk over valid frames (use captured window pointer)
          // Temporarily set the analysis window pointer (thread-local) by calling member that reads mWindow.
          // Since AnalyzeChunk only reads mWindow, this is safe as we captured the pointer value.
          AnalyzeChunk(out, framesInChunk, (double) targetSampleRate);

        const int globalIdx = (int) newChunks.size();
        newChunks.push_back(std::move(out));
        f.chunkIndices.push_back(globalIdx);
      }

      f.chunkCount = (int) f.chunkIndices.size();
      // Recompute tail padding for new chunk size
      if (f.chunkCount > 0)
      {
        const int totalFramesMod = totalFrames % newChunkSizeSamples;
        f.tailPaddingFrames = (totalFramesMod == 0) ? 0 : (newChunkSizeSamples - totalFramesMod);
      }
      else
      {
        f.tailPaddingFrames = 0;
      }

      if (f.chunkCount > 0) ++stats.filesRechunked;
      stats.newTotalChunks += f.chunkCount;
    }

    // Commit new state under lock in one short critical section
    {
      std::unique_lock<std::mutex> lock(mutex_);
      chunks_.swap(newChunks);
      files_.swap(newFiles);
      idToFileIndex_.clear();
      for (int i = 0; i < (int) files_.size(); ++i)
        idToFileIndex_[files_[i].id] = i;
      mChunkSize = newChunkSizeSamples;
    }

    return stats;
  }

  Brain::ReanalyzeStats Brain::ReanalyzeAllChunks(int targetSampleRate, RechunkProgressFn onProgress)
  {
    ReanalyzeStats stats;
    if (targetSampleRate <= 0) return stats;
    // Snapshot file list under lock
    std::vector<BrainFile> filesSnapshot;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      filesSnapshot = files_;
    }
    // Iterate files and re-run analysis on their chunks
    for (const auto& f : filesSnapshot)
    {
      ++stats.filesProcessed;
      if (onProgress) onProgress(f.displayName);
      // For each chunk index, reanalyze its audio
      std::vector<int> idxs = f.chunkIndices;
      for (int gi : idxs)
      {
        BrainChunk* cptr = nullptr;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          if (gi >= 0 && gi < (int) chunks_.size()) cptr = &chunks_[gi];
        }
        if (!cptr) continue;
        BrainChunk local;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          local = *cptr; // copy to avoid holding lock during analysis
        }
        const int validFrames = std::min(local.audio.numFrames, (int) (local.audio.channelSamples.empty() ? 0 : local.audio.channelSamples[0].size()));
        AnalyzeChunk(local, validFrames, (double) targetSampleRate);
        {
          std::unique_lock<std::mutex> lock(mutex_);
          if (gi >= 0 && gi < (int) chunks_.size()) chunks_[gi] = std::move(local);
        }
        ++stats.chunksProcessed;
      }
    }
    return stats;
  }

  int Brain::GetTotalChunks() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return (int) chunks_.size();
  }

  const BrainChunk* Brain::GetChunkByGlobalIndex(int idx) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (idx < 0 || idx >= (int) chunks_.size()) return nullptr;
    return &chunks_[idx];
  }

  bool Brain::SerializeSnapshotToChunk(iplug::IByteChunk& out) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    out.Put(&kSnapshotMagic);
    out.Put(&kSnapshotVersion);
    int32_t chunkSize = mChunkSize;
    out.Put(&chunkSize);
    // Window type for analysis (store as int for simplicity)
    int32_t winMode = mWindow ? Window::TypeToInt(mWindow->GetType()) : 1;
    out.Put(&winMode);

    int32_t nFiles = (int32_t) files_.size();
    out.Put(&nFiles);
    // We store per-file name and number of chunks referencing it to rebuild mapping
    for (const auto& f : files_)
    {
      int32_t fid = f.id;
      out.Put(&fid);
      PutString(out, f.displayName);
      int32_t tailPad = f.tailPaddingFrames;
      out.Put(&tailPad);
      int32_t nIdx = (int32_t) f.chunkIndices.size();
      out.Put(&nIdx);
      for (int32_t gi : f.chunkIndices)
        out.Put(&gi);
    }

    // Store all chunks with audio and analysis
    int32_t nChunks = (int32_t) chunks_.size();
    out.Put(&nChunks);
    for (const auto& c : chunks_)
    {
      out.Put(&c.fileId);
      out.Put(&c.chunkIndexInFile);
      // Audio
      int32_t chans = (int32_t) c.audio.channelSamples.size();
      out.Put(&chans);
      out.Put(&c.audio.numFrames);
      for (int ch = 0; ch < chans; ++ch)
      {
        int32_t frames = (int32_t) c.audio.channelSamples[ch].size();
        out.Put(&frames);
        if (frames > 0)
          out.PutBytes(c.audio.channelSamples[ch].data(), (int) (sizeof(iplug::sample) * frames));
      }
      // Analysis
      int32_t rmsc = (int32_t) c.rmsPerChannel.size(); out.Put(&rmsc);
      for (int i = 0; i < rmsc; ++i) out.Put(&c.rmsPerChannel[i]);
      int32_t fzc = (int32_t) c.freqHzPerChannel.size(); out.Put(&fzc);
      for (int i = 0; i < fzc; ++i) out.Put(&c.freqHzPerChannel[i]);
      int32_t fftSize = c.fftSize; out.Put(&fftSize);
      int32_t fftc = (int32_t)c.complexSpectrum.size();
      out.Put(&fftc);
      for (int ch = 0; ch < fftc; ++ch)
      {
        int32_t bins = (int32_t)c.complexSpectrum[ch].size();
        out.Put(&bins);
        if (bins > 0)
          out.PutBytes(c.complexSpectrum[ch].data(), (int)(sizeof(float) * bins));
      }
      int32_t domc = (int32_t) c.fftDominantHzPerChannel.size(); out.Put(&domc);
      for (int i = 0; i < domc; ++i) out.Put(&c.fftDominantHzPerChannel[i]);
      out.Put(&c.avgRms);
      out.Put(&c.avgFreqHz);
      out.Put(&c.avgFftDominantHz);
      // Extended features (v3)
      int32_t extChans = (int32_t) c.extendedFeaturesPerChannel.size(); out.Put(&extChans);
      for (int ch = 0; ch < extChans; ++ch)
      {
        int32_t numFeatures = (int32_t) c.extendedFeaturesPerChannel[ch].size();
        out.Put(&numFeatures);
        if (numFeatures > 0)
          out.PutBytes(c.extendedFeaturesPerChannel[ch].data(), (int)(sizeof(float) * numFeatures));
      }
      int32_t avgFeatCount = (int32_t) c.avgExtendedFeatures.size(); out.Put(&avgFeatCount);
      if (avgFeatCount > 0)
        out.PutBytes(c.avgExtendedFeatures.data(), (int)(sizeof(float) * avgFeatCount));
    }

    return true;
  }

  int Brain::DeserializeSnapshotFromChunk(const iplug::IByteChunk& in, int startPos)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    int pos = startPos;
    uint32_t magic = 0; pos = in.Get(&magic, pos); if (pos < 0 || magic != kSnapshotMagic) return -1;
    uint16_t ver = 0; pos = in.Get(&ver, pos); if (pos < 0 || ver > kSnapshotVersion) return -1;
    int32_t chunkSize = 0; pos = in.Get(&chunkSize, pos); if (pos < 0) return -1; mChunkSize = chunkSize;

    // Handle window type: v1 used string, v2 uses int
    if (ver == 1)
    {
      // Version 1: read string
      std::string win;
      if (!GetString(in, pos, win)) return -1;
      // Convert string to window type
      if (win == "hann") mSavedAnalysisWindowType = Window::Type::Hann;
      else if (win == "hamming") mSavedAnalysisWindowType = Window::Type::Hamming;
      else if (win == "blackman") mSavedAnalysisWindowType = Window::Type::Blackman;
      else if (win == "rectangular") mSavedAnalysisWindowType = Window::Type::Rectangular;
      else mSavedAnalysisWindowType = Window::Type::Hann; // default fallback
    }
    else
    {
      // Version 2: read int
      int32_t winMode = 1;
      pos = in.Get(&winMode, pos);
      if (pos < 0) return -1;
      mSavedAnalysisWindowType = Window::IntToType(winMode);
    }

    files_.clear(); idToFileIndex_.clear(); chunks_.clear();

    int32_t nFiles = 0; pos = in.Get(&nFiles, pos); if (pos < 0 || nFiles < 0) return -1;
    files_.reserve(nFiles);
    for (int i = 0; i < nFiles; ++i)
    {
      BrainFile f;
      pos = in.Get(&f.id, pos); if (pos < 0) return -1;
      std::string name; if (!GetString(in, pos, name)) return -1; f.displayName = name;
      pos = in.Get(&f.tailPaddingFrames, pos); if (pos < 0) return -1;
      int32_t nIdx = 0; pos = in.Get(&nIdx, pos); if (pos < 0 || nIdx < 0) return -1;
      f.chunkIndices.resize(nIdx, -1);
      for (int k = 0; k < nIdx; ++k) pos = in.Get(&f.chunkIndices[k], pos);
      f.chunkCount = nIdx;
      idToFileIndex_[f.id] = (int) files_.size();
      files_.push_back(std::move(f));
    }

    int32_t nChunks = 0; pos = in.Get(&nChunks, pos); if (pos < 0 || nChunks < 0) return -1;
    chunks_.resize(nChunks);
    for (int i = 0; i < nChunks; ++i)
    {
      auto& c = chunks_[i];
      pos = in.Get(&c.fileId, pos); if (pos < 0) return -1;
      pos = in.Get(&c.chunkIndexInFile, pos); if (pos < 0) return -1;
      int32_t chans = 0; pos = in.Get(&chans, pos); if (pos < 0 || chans < 0) return -1;
      pos = in.Get(&c.audio.numFrames, pos); if (pos < 0) return -1;
      c.audio.channelSamples.assign(chans, std::vector<iplug::sample>());
      for (int ch = 0; ch < chans; ++ch)
      {
        int32_t frames = 0; pos = in.Get(&frames, pos); if (pos < 0 || frames < 0) return -1;
        c.audio.channelSamples[ch].resize(frames);
        if (frames > 0)
        {
          pos = in.GetBytes(c.audio.channelSamples[ch].data(), (int) (sizeof(iplug::sample) * frames), pos);
          if (pos < 0) return -1;
        }
      }
      int32_t rmsc = 0; pos = in.Get(&rmsc, pos); if (pos < 0 || rmsc < 0) return -1;
      c.rmsPerChannel.resize(rmsc); for (int k = 0; k < rmsc; ++k) pos = in.Get(&c.rmsPerChannel[k], pos);
      int32_t fzc = 0; pos = in.Get(&fzc, pos); if (pos < 0 || fzc < 0) return -1;
      c.freqHzPerChannel.resize(fzc); for (int k = 0; k < fzc; ++k) pos = in.Get(&c.freqHzPerChannel[k], pos);
      int32_t fftSize = 0; pos = in.Get(&fftSize, pos); c.fftSize = fftSize; if (pos < 0) return -1;
      int32_t fftc = 0; pos = in.Get(&fftc, pos); if (pos < 0 || fftc < 0) return -1;
      c.complexSpectrum.resize(fftc);
      for (int ch = 0; ch < fftc; ++ch)
      {
        int32_t bins = 0; pos = in.Get(&bins, pos); if (pos < 0 || bins < 0) return -1;
        c.complexSpectrum[ch].resize(bins);
        if (bins > 0)
        {
          pos = in.GetBytes(c.complexSpectrum[ch].data(), (int)(sizeof(float) * bins), pos);
          if (pos < 0) return -1;
        }
      }
      int32_t domc = 0; pos = in.Get(&domc, pos); if (pos < 0 || domc < 0) return -1;
      c.fftDominantHzPerChannel.resize(domc); for (int k = 0; k < domc; ++k) pos = in.Get(&c.fftDominantHzPerChannel[k], pos);
      pos = in.Get(&c.avgRms, pos); if (pos < 0) return -1;
      pos = in.Get(&c.avgFreqHz, pos); if (pos < 0) return -1;
      pos = in.Get(&c.avgFftDominantHz, pos); if (pos < 0) return -1;
      // Extended features (v3+)
      if (ver >= 3)
      {
        int32_t extChans = 0; pos = in.Get(&extChans, pos); if (pos < 0 || extChans < 0) return -1;
        c.extendedFeaturesPerChannel.resize(extChans);
        for (int ch = 0; ch < extChans; ++ch)
        {
          int32_t numFeatures = 0; pos = in.Get(&numFeatures, pos); if (pos < 0 || numFeatures < 0) return -1;
          c.extendedFeaturesPerChannel[ch].resize(numFeatures);
          if (numFeatures > 0)
          {
            pos = in.GetBytes(c.extendedFeaturesPerChannel[ch].data(), (int)(sizeof(float) * numFeatures), pos);
            if (pos < 0) return -1;
          }
        }
        int32_t avgFeatCount = 0; pos = in.Get(&avgFeatCount, pos); if (pos < 0 || avgFeatCount < 0) return -1;
        c.avgExtendedFeatures.resize(avgFeatCount);
        if (avgFeatCount > 0)
        {
          pos = in.GetBytes(c.avgExtendedFeatures.data(), (int)(sizeof(float) * avgFeatCount), pos);
          if (pos < 0) return -1;
        }
      }
    }

    // Update nextFileId_ to be one more than the maximum file ID we just loaded
    // This prevents duplicate IDs when adding new files after import
    nextFileId_ = 1;
    for (const auto& f : files_)
    {
      if (f.id >= nextFileId_)
        nextFileId_ = f.id + 1;
    }

    return pos;
  }
}



