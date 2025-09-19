#include "Brain.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#define MINIAUDIO_IMPLEMENTATION
#include "../../exdeps/miniaudio/miniaudio.h"
// Use PFFFT for FFT analysis
#include "../../exdeps/pffft/pffft.h"

namespace synaptic
{
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

    // Compute FFT magnitudes per channel using PFFFT and dominant frequency per channel
    auto isGoodN = [](int n) -> bool {
      if (n <= 0) return false;
      int m = n;
      // Require multiple of 32 for SIMD-friendly real transform
      if ((m % 32) != 0) return false;
      // Factorize by 2, 3, 5 only
      for (int p : {2,3,5})
      {
        while ((m % p) == 0) m /= p;
      }
      return m == 1;
    };

    auto nextGoodN = [&](int minN) -> int {
      int n = std::max(32, minN);
      // Round up to at least 32
      if (n < 32) n = 32;
      for (;; ++n)
      {
        if (isGoodN(n)) return n;
      }
    };

    const int framesForFft = std::max(1, validFrames);
    const int Nfft = nextGoodN(framesForFft);
    chunk.fftSize = Nfft;
    chunk.fftMagnitudePerChannel.assign(chCount, std::vector<float>(Nfft/2 + 1, 0.0f));
    chunk.fftDominantHzPerChannel.assign(chCount, 0.0);

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

          // Ordered forward transform to get canonical interleaved output
          pffft_transform_ordered(setup, inAligned, outAligned, nullptr, PFFFT_FORWARD);

          // Extract magnitudes for bins 0..N/2
          auto& mags = chunk.fftMagnitudePerChannel[ch];
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
    int numChunks = (totalFrames + chunkSizeSamples - 1) / chunkSizeSamples;
    fileRec.chunkIndices.reserve(numChunks);

    for (int c = 0; c < numChunks; ++c)
    {
      const int start = c * chunkSizeSamples;
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
      AnalyzeChunk(chunk, framesInChunk, (double) targetSampleRate);

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
    std::lock_guard<std::mutex> lock(mutex_);
    // Rebuild each file by concatenating its chunks' valid frames, then rechunk
    std::vector<BrainChunk> newChunks;
    newChunks.reserve(chunks_.size());

    for (auto& f : files_)
    {
      ++stats.filesProcessed;
      if (onProgress) onProgress(f.displayName);
      // Concatenate valid frames across this file's chunks
      int totalValidFrames = 0;
      int numChannels = 0;
      // Determine channels from first valid chunk
      for (int gi : f.chunkIndices)
      {
        if (gi < 0 || gi >= (int)chunks_.size()) continue;
        const BrainChunk& bc = chunks_[gi];
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

      // Gather into planar buffer
      std::vector<std::vector<sample>> planar(numChannels);
      for (int ch = 0; ch < numChannels; ++ch) planar[ch].clear();

      // Determine original (old) chunk size from first chunk
      int oldChunkSize = mChunkSize > 0 ? mChunkSize : newChunkSizeSamples;

      for (int gi : f.chunkIndices)
      {
        if (gi < 0 || gi >= (int)chunks_.size()) continue;
        const BrainChunk& bc = chunks_[gi];
        // valid frames for non-last chunks = full chunk size; last chunk subtract tail padding
        int frames = std::max(0, oldChunkSize);
        const bool isLast = (bc.chunkIndexInFile == (f.chunkCount - 1));
        if (isLast)
        {
          frames = std::max(0, oldChunkSize - f.tailPaddingFrames);
        }
        const int srcChans = (int) bc.audio.channelSamples.size();
        for (int ch = 0; ch < numChannels; ++ch)
        {
          if (ch < srcChans)
          {
            const auto& src = bc.audio.channelSamples[ch];
            const int copyN = std::min(frames, (int)src.size());
            planar[ch].insert(planar[ch].end(), src.begin(), src.begin() + copyN);
          }
          else
          {
            // missing channel: append zeros
            planar[ch].insert(planar[ch].end(), frames, 0.0);
          }
        }
        totalValidFrames += frames;
      }

      // Rechunk the reconstructed buffer
      f.chunkIndices.clear();
      const int totalFrames = totalValidFrames;
      const int numChunks = (totalFrames + newChunkSizeSamples - 1) / newChunkSizeSamples;
      for (int c = 0; c < numChunks; ++c)
      {
        const int start = c * newChunkSizeSamples;
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
        // Analyze metrics for this chunk over valid frames
        AnalyzeChunk(out, framesInChunk, (double) targetSampleRate);

        const int globalIdx = (int) newChunks.size();
        newChunks.push_back(std::move(out));
        f.chunkIndices.push_back(globalIdx);
      }
      f.chunkCount = (int) f.chunkIndices.size();
      // Recompute tail padding for new chunk size
      if (f.chunkCount > 0)
      {
        const int totalFramesMod = totalValidFrames % newChunkSizeSamples;
        f.tailPaddingFrames = (totalFramesMod == 0) ? 0 : (newChunkSizeSamples - totalFramesMod);
      }
      else
      {
        f.tailPaddingFrames = 0;
      }
      if (f.chunkCount > 0) ++stats.filesRechunked;
      stats.newTotalChunks += f.chunkCount;
    }

    chunks_.swap(newChunks);
    mChunkSize = newChunkSizeSamples;
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
}



