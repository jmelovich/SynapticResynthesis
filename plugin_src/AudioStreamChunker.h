#pragma once

#include <vector>
#include <algorithm>
#include <cstring>

#include "IPlug_include_in_plug_hdr.h"
#include "Window.h"

namespace synaptic
{
  using namespace iplug;

  struct AudioChunk
  {
    std::vector<std::vector<sample>> channelSamples; // [channel][frame]
    int numFrames = 0;
    double inRMS = 0;
  };

  struct PoolEntry
  {
    AudioChunk chunk;
    int refCount = 0; // references held by window/pending/output
  };

  // Fixed-size ring buffer of indices (no allocations at runtime)
  struct IndexRing
  {
    std::vector<int> data;
    int head = 0;
    int tail = 0;
    int count = 0;

    void Init(int capacity)
    {
      data.assign(capacity, -1);
      head = tail = count = 0;
    }

    int Capacity() const { return (int) data.size(); }
    bool Empty() const { return count == 0; }
    bool Full() const { return count == Capacity(); }

    bool Push(int v)
    {
      if (Full()) return false;
      data[tail] = v;
      tail = (tail + 1) % Capacity();
      ++count;
      return true;
    }

    bool Pop(int& out)
    {
      if (Empty()) return false;
      out = data[head];
      head = (head + 1) % Capacity();
      --count;
      return true;
    }

    int PeekOldest() const
    {
      if (Empty()) return -1;
      return data[head];
    }
  };

  class AudioStreamChunker
  {
  public:
    explicit AudioStreamChunker(int numChannels)
      : mNumChannels(numChannels)
    {
      Configure(mNumChannels, mChunkSize, mBufferWindowSize);
    }

    void Configure(int numChannels, int chunkSize, int windowSize)
    {
      const int newNumChannels = std::max(1, numChannels);
      const int newChunkSize = std::max(1, chunkSize);
      const int newBufferWindowSize = std::max(1, windowSize);
      const int newPoolCapacity = newBufferWindowSize + mExtraPool;

      // Only reallocate if dimensions actually changed
      const bool needsReallocation = (newNumChannels != mNumChannels ||
                                     newChunkSize != mChunkSize ||
                                     newPoolCapacity != mPoolCapacity);

      mNumChannels = newNumChannels;
      mChunkSize = newChunkSize;
      mBufferWindowSize = newBufferWindowSize;
      mPoolCapacity = newPoolCapacity;

      if (needsReallocation)
      {
      // Pre-size accumulation scratch
      mAccumulation.assign(mNumChannels, std::vector<sample>(mChunkSize, 0.0));

      // Pool sizing: window + extra headroom for pending/output
      mPool.assign(mPoolCapacity, {});
      for (int i = 0; i < mPoolCapacity; ++i)
      {
        auto& e = mPool[i];
        e.refCount = 0;
        e.chunk.numFrames = mChunkSize;
        e.chunk.channelSamples.assign(mNumChannels, std::vector<sample>(mChunkSize, 0.0));
      }

        // Overlap-add buffer
        mOutputOverlapBuffer.assign(mNumChannels, std::vector<sample>(mChunkSize * 2, 0.0));
      }

      // Always reset state
      mAccumulatedFrames = 0;
      mOutputFrontFrameIndex = 0;
      mOutputOverlapValidSamples = 0;
      mTotalInputSamplesPushed = 0;
      mTotalOutputSamplesRendered = 0;

      // Initialize rings
      mFree.Init(mPoolCapacity);
      mPending.Init(mPoolCapacity);
      mOutput.Init(mPoolCapacity);
      mWindow.Init(mBufferWindowSize);

      // All indices free initially
      for (int i = 0; i < mPoolCapacity; ++i)
        mFree.Push(i);
    }

    void SetChunkSize(int chunkSize)
    {
      Configure(mNumChannels, chunkSize, mBufferWindowSize);
    }

    void SetBufferWindowSize(int windowSize)
    {
      Configure(mNumChannels, mChunkSize, windowSize);
    }

    void SetNumChannels(int numChannels)
    {
      Configure(numChannels, mChunkSize, mBufferWindowSize);
    }

    void EnableOverlap(bool enable)
    {
      if (mEnableOverlap != enable)
      {
        mEnableOverlap = enable;
        Reset();
      }
    }

    void SetOutputWindow(const synaptic::Window& w)
    {
      // If window type is changing, reset overlap buffer to prevent artifacts
      if (mOutputWindow.GetType() != w.GetType())
      {
        ResetOverlapBuffer();
      }
      mOutputWindow = w;
    }

    void ResetOverlapBuffer()
    {
      mOutputOverlapValidSamples = 0;
      for (int ch = 0; ch < std::min(mNumChannels, (int)mOutputOverlapBuffer.size()); ++ch)
      {
        std::fill(mOutputOverlapBuffer[ch].begin(), mOutputOverlapBuffer[ch].end(), 0.0f);
      }
    }

    void Reset()
    {
      Configure(mNumChannels, mChunkSize, mBufferWindowSize);
    }

    int GetChunkSize() const { return mChunkSize; }

    void PushAudio(sample** inputs, int nFrames)
    {
      if (!inputs || nFrames <= 0 || mNumChannels <= 0) return;

      // Track total input samples for exact latency alignment
      mTotalInputSamplesPushed += nFrames;

      int frameIndex = 0;
      while (frameIndex < nFrames)
      {
        const int framesToCopy = std::min(mChunkSize - mAccumulatedFrames, nFrames - frameIndex);
        for (int ch = 0; ch < mNumChannels; ++ch)
        {
          // Bounds check
          if (ch >= (int)mAccumulation.size() || !inputs[ch]) continue;
          if (mAccumulation[ch].size() < (size_t)(mAccumulatedFrames + framesToCopy)) continue;

          // copy into scratch without reallocations
          sample* dst = mAccumulation[ch].data() + mAccumulatedFrames;
          const sample* src = inputs[ch] + frameIndex;
          std::memcpy(dst, src, sizeof(sample) * framesToCopy);
        }
        mAccumulatedFrames += framesToCopy;
        frameIndex += framesToCopy;

        // Use appropriate hop size for input processing
        const int inputHopSize = (mEnableOverlap && mOutputWindow.GetOverlap() > 0.0f)
          ? mChunkSize / 2  // 50% hop only when overlap-add is enabled
          : mChunkSize;     // 100% hop when overlap-add is disabled or rectangular window
        while (mAccumulatedFrames >= mChunkSize)
        {
          int poolIdx;
          if (!mFree.Pop(poolIdx))
          {
            // No space; drop oldest part of accumulation buffer and continue
            mAccumulatedFrames -= inputHopSize;
            if (mAccumulatedFrames > 0)
            {
              for (int ch = 0; ch < mNumChannels; ++ch)
                std::memmove(mAccumulation[ch].data(), mAccumulation[ch].data() + inputHopSize, sizeof(sample) * mAccumulatedFrames);
            }
            else {
            mAccumulatedFrames = 0;
            }
            continue;
          }

          PoolEntry& entry = mPool[poolIdx];
          // Copy scratch into pool chunk
          for (int ch = 0; ch < mNumChannels; ++ch)
          {
            std::memcpy(entry.chunk.channelSamples[ch].data(), mAccumulation[ch].data(), sizeof(sample) * mChunkSize);
          }
          entry.chunk.numFrames = mChunkSize;

          // Get RMS of current frame and store in AudioChunk
          float inChunkRMS = 0;
          for (int ch = 0; ch < mNumChannels; ch++)
          {
            sample* __restrict chunk = entry.chunk.channelSamples[ch].data();
            for (int i = 0; i < entry.chunk.numFrames; i++)
            {
              inChunkRMS += chunk[i] * chunk[i]; // gather Sum of Squares
            }
          }
          inChunkRMS = sqrt(inChunkRMS / entry.chunk.numFrames * 2); // get Root Mean Square (rms)
          entry.chunk.inRMS = inChunkRMS;

          // Insert into lookahead window
          if (mWindow.Full())
          {
            int oldIdx;
            mWindow.Pop(oldIdx);
            DecRefAndMaybeFree(oldIdx);
          }
          mWindow.Push(poolIdx);
          ++entry.refCount; // window ref

          // Insert into pending queue
          if (!mPending.Push(poolIdx))
          {
            // If pending overflows, drop oldest pending
            int dropped;
            if (mPending.Pop(dropped))
              DecRefAndMaybeFree(dropped);
            mPending.Push(poolIdx);
          }
          ++entry.refCount; // pending ref

          // Shift accumulation buffer by consistent hop size
          mAccumulatedFrames -= inputHopSize;
          if (mAccumulatedFrames > 0)
          {
            for (int ch = 0; ch < mNumChannels; ++ch)
              std::memmove(mAccumulation[ch].data(), mAccumulation[ch].data() + inputHopSize, sizeof(sample) * mAccumulatedFrames);
          }
          else
          {
          mAccumulatedFrames = 0;
          }
        }
      }
    }

    // Transformer API (index-based)
    bool PopPendingInputChunkIndex(int& outIdx)
    {
      if (!mPending.Pop(outIdx))
        return false;
      // pending ref removed
      DecRefAndMaybeFree(outIdx);
      return true;
    }

    void EnqueueOutputChunkIndex(int idx)
    {
      // add output ref
      ++mPool[idx].refCount;
      mOutput.Push(idx);
    }

    // Allocate a fresh, writable chunk for synthesized output. Returns false if no pool entries are free.
    bool AllocateWritableChunkIndex(int& outIdx)
    {
      if (!mFree.Pop(outIdx))
        return false;
      PoolEntry& e = mPool[outIdx];
      e.refCount = 0; // no refs yet
      e.chunk.numFrames = mChunkSize;
      // leave channelSamples as-is (pre-sized); caller will write
      return true;
    }

    // Get a writable pointer for a chunk index obtained via AllocateWritableChunkIndex().
    AudioChunk* GetWritableChunkByIndex(int idx)
    {
      if (idx < 0 || idx >= mPoolCapacity) return nullptr;
      return &mPool[idx].chunk;
    }

    // Commit a synthesized chunk to output. numFrames will be clamped to [0, mChunkSize].
    // inRMS should be the RMS of the corresponding input chunk for AGC to work correctly.
    void CommitWritableChunkIndex(int idx, int numFrames, double inRMS = 0.0)
    {
      if (idx < 0 || idx >= mPoolCapacity) return;
      PoolEntry& e = mPool[idx];
      if (numFrames < 0) numFrames = 0;
      if (numFrames > mChunkSize) numFrames = mChunkSize;
      e.chunk.numFrames = numFrames;
      e.chunk.inRMS = inRMS;  // Set input RMS for AGC
      // add output ref and enqueue
      ++e.refCount;
      mOutput.Push(idx);
    }

    // Optional helper to clear a writable chunk to a value (default 0).
    void ClearWritableChunkIndex(int idx, sample value = 0.0)
    {
      if (idx < 0 || idx >= mPoolCapacity) return;
      PoolEntry& e = mPool[idx];
      for (int ch = 0; ch < (int)e.chunk.channelSamples.size(); ++ch)
      {
        std::fill(e.chunk.channelSamples[ch].begin(), e.chunk.channelSamples[ch].end(), value);
      }
    }

    void RenderOutput(sample** outputs, int nFrames, int outChans, bool agcEnabled = false)
    {
      if (!outputs || nFrames <= 0 || outChans <= 0) return;

      const int chansToWrite = std::min(outChans, mNumChannels);

      // Use overlap-add only when there's actual overlap (not rectangular window)
      const bool useOverlapAdd = mEnableOverlap && (mOutputWindow.GetOverlap() > 0.0f);

      if (useOverlapAdd)
      {
        // Use consistent hop size for all windows to maintain timing
        // Mathematical overlap properties are handled by coefficients and scaling only
        const int hopSize = mChunkSize / 2;  // Consistent 50% hop for all windows
        const float rescale = mOutputWindow.GetOverlapRescale();

        // First, process any queued chunks and overlap-add them to our buffer
        while (!mOutput.Empty())
        {
          int idx;
          mOutput.Pop(idx);
          PoolEntry& e = mPool[idx];

          if (e.chunk.numFrames > 0)
          {
            float agc = 1.0f;
            if (agcEnabled)
            {
              // Get RMS of current frame and store in AudioChunk
              float outChunkRMS = 0;
              for (int ch = 0; ch < mNumChannels; ch++)
              {
                sample* __restrict chunk = e.chunk.channelSamples[ch].data();
                for (int i = 0; i < e.chunk.numFrames; i++)
                {
                  outChunkRMS += chunk[i] * chunk[i]; // gather Sum of Squares
                }
              }
              outChunkRMS = sqrt(outChunkRMS / e.chunk.numFrames * 2); // get Root Mean Square (rms)

              agc = e.chunk.inRMS / outChunkRMS;
            }

            if (mOutputWindow.Size() != e.chunk.numFrames)
              mOutputWindow.Set(mOutputWindow.GetType(), e.chunk.numFrames);

            const auto& coeffs = mOutputWindow.Coeffs();
            const int frames = e.chunk.numFrames;
            const int addPos = (mOutputOverlapValidSamples >= hopSize) ? (mOutputOverlapValidSamples - hopSize) : 0;
            const int requiredSize = addPos + frames;

            if ((int) mOutputOverlapBuffer[0].size() < requiredSize)
            {
              for (int ch = 0; ch < mNumChannels; ++ch)
                mOutputOverlapBuffer[ch].resize(requiredSize, 0.0);
            }

            for (int ch = 0; ch < std::min(mNumChannels, (int)mOutputOverlapBuffer.size()); ++ch)
            {
              if (ch < (int)e.chunk.channelSamples.size())
              {
                for (int i = 0; i < frames; ++i)
                {
                  if (addPos + i < (int)mOutputOverlapBuffer[ch].size() && i < (int)coeffs.size())
                  {
                    mOutputOverlapBuffer[ch][addPos + i] += e.chunk.channelSamples[ch][i] * coeffs[i] * agc;
                  }
                }
              }
            }
            mOutputOverlapValidSamples = requiredSize;
          }
          DecRefAndMaybeFree(idx);
        }

        // Now, copy from our buffer to the output
        // Maintain exactly chunkSize samples of latency
        const int64_t samplesAvailableToRender = mTotalInputSamplesPushed - mChunkSize - mTotalOutputSamplesRendered;
        const int64_t maxToRender = std::max((int64_t)0, std::min((int64_t)mOutputOverlapValidSamples, samplesAvailableToRender));
        const int framesToCopy = std::min((int64_t)nFrames, maxToRender);

        if (framesToCopy > 0)
        {
          // Copy available samples with rescaling

          for (int ch = 0; ch < chansToWrite; ++ch)
          {
            for (int i = 0; i < framesToCopy; ++i)
            {
              outputs[ch][i] = mOutputOverlapBuffer[ch][i] * rescale;
            }
          }

          // Shift our internal buffer
          const int newValidSamples = mOutputOverlapValidSamples - framesToCopy;
          if (newValidSamples > 0)
          {
            for (int ch = 0; ch < mNumChannels; ++ch)
            {
              std::memmove(mOutputOverlapBuffer[ch].data(), mOutputOverlapBuffer[ch].data() + framesToCopy, newValidSamples * sizeof(sample));
            }
          }
          mOutputOverlapValidSamples = newValidSamples;

          // Zero out the tail of our buffer to avoid stale audio
          const int tailStart = mOutputOverlapValidSamples > 0 ? mOutputOverlapValidSamples : 0;
          const int tailSize = (int)mOutputOverlapBuffer[0].size() - tailStart;
          if (tailSize > 0)
          {
            for (int ch = 0; ch < mNumChannels; ++ch)
              std::memset(mOutputOverlapBuffer[ch].data() + tailStart, 0, sizeof(sample) * tailSize);
          }

          // Track output samples
          mTotalOutputSamplesRendered += framesToCopy;
        }

        // Zero out remainder of host buffer if we didn't render enough
        if (framesToCopy < nFrames)
        {
          for (int ch = 0; ch < outChans; ++ch)
            std::memset(outputs[ch] + framesToCopy, 0, sizeof(sample) * (nFrames - framesToCopy));
        }
      }
      else
      {
        // Sequential playback with exact latency control
        // Only render samples when we maintain exactly chunkSize latency
        for (int s = 0; s < nFrames; ++s)
      {
        // Check if we can output this specific sample
        const bool canOutputThisSample = (mTotalOutputSamplesRendered < mTotalInputSamplesPushed - mChunkSize);

        for (int ch = 0; ch < outChans; ++ch)
          if (outputs[ch]) outputs[ch][s] = 0.0;

        if (canOutputThisSample && !mOutput.Empty())
        {
          int idx = mOutput.PeekOldest();
          if (idx >= 0 && idx < mPoolCapacity)
          {
            PoolEntry& e = mPool[idx];
            if (mOutputFrontFrameIndex < e.chunk.numFrames)
            {
                // Apply individual chunk windowing if window type has overlap > 0
                float windowCoeff = 1.0f;
                if (mOutputWindow.GetOverlap() > 0.0f && mOutputFrontFrameIndex < mOutputWindow.Size())
                {
                  const auto& coeffs = mOutputWindow.Coeffs();
                  windowCoeff = coeffs[mOutputFrontFrameIndex];
                }

              for (int ch = 0; ch < chansToWrite; ++ch)
              {
                if (outputs[ch] && ch < (int)e.chunk.channelSamples.size() &&
                    mOutputFrontFrameIndex < (int)e.chunk.channelSamples[ch].size())
                  {
                    outputs[ch][s] = e.chunk.channelSamples[ch][mOutputFrontFrameIndex] * windowCoeff;
                  }
                }
            }

            ++mOutputFrontFrameIndex;
            ++mTotalOutputSamplesRendered;  // Track output for latency control

            if (mOutputFrontFrameIndex >= e.chunk.numFrames)
            {
              // finished this chunk
              int finished;
              mOutput.Pop(finished);
              DecRefAndMaybeFree(finished); // drop output ref
              mOutputFrontFrameIndex = 0;
              }
            }
          }
        }
      }
    }

    // Lookahead window info (read-only access for transformers)
    int GetWindowCapacity() const { return mBufferWindowSize; }
    int GetWindowCount() const { return mWindow.count; }

    // Get pool index at ordinal from oldest (0 = oldest, count-1 = newest). Returns -1 if OOR.
    int GetWindowIndexFromOldest(int ordinal) const
    {
      if (ordinal < 0 || ordinal >= mWindow.count) return -1;
      const int cap = mWindow.Capacity();
      const int pos = (mWindow.head + ordinal) % cap;
      return mWindow.data[pos];
    }

    // Get pool index at ordinal from newest (0 = newest, count-1 = oldest). Returns -1 if OOR.
    int GetWindowIndexFromNewest(int ordinal) const
    {
      if (ordinal < 0 || ordinal >= mWindow.count) return -1;
      const int cap = mWindow.Capacity();
      int newestPos = (mWindow.head + mWindow.count - 1) % cap;
      int pos = (newestPos - ordinal);
      while (pos < 0) pos += cap;
      pos %= cap;
      return mWindow.data[pos];
    }

    // Output queue info (read-only indexing for transformers)
    int GetOutputCount() const { return mOutput.count; }

    // Get output pool index at ordinal from oldest (0 = oldest, count-1 = newest). Returns -1 if OOR.
    int GetOutputIndexFromOldest(int ordinal) const
    {
      if (ordinal < 0 || ordinal >= mOutput.count) return -1;
      const int cap = mOutput.Capacity();
      const int pos = (mOutput.head + ordinal) % cap;
      return mOutput.data[pos];
    }

    // Map a pool index to a read-only chunk pointer (nullptr if invalid)
    const AudioChunk* GetChunkConstByIndex(int idx) const
    {
      if (idx < 0 || idx >= mPoolCapacity) return nullptr;
      return &mPool[idx].chunk;
    }

    // Map a pool index to a writable chunk pointer (nullptr if invalid)
    AudioChunk* GetMutableChunkByIndex(int idx)
    {
      if (idx < 0 || idx >= mPoolCapacity) return nullptr;
      return &mPool[idx].chunk;
    }

    // Current output head (if any) and its frame index
    bool PeekCurrentOutput(int& outPoolIdx, int& outFrameIndex) const
    {
      if (mOutput.Empty()) return false;
      outPoolIdx = mOutput.PeekOldest();
      outFrameIndex = mOutputFrontFrameIndex;
      return true;
    }

    int GetNumChannels() const { return mNumChannels; }

  private:
    void DecRefAndMaybeFree(int idx)
    {
      if (idx < 0) return;
      PoolEntry& e = mPool[idx];
      --e.refCount;
      if (e.refCount <= 0)
      {
        e.refCount = 0;
        mFree.Push(idx);
      }
    }

  private:
    int mNumChannels = 2;
    int mChunkSize = 3000;
    int mBufferWindowSize = 1;
    bool mEnableOverlap = true;
    int mExtraPool = 8; // additional capacity beyond window size
    int mPoolCapacity = 0;
    int64_t mTotalInputSamplesPushed = 0; // Track total input for exact latency alignment
    int64_t mTotalOutputSamplesRendered = 0; // Track total output to maintain exact latency

    // Accumulation scratch (per-channel, size chunkSize)
    std::vector<std::vector<sample>> mAccumulation;
    int mAccumulatedFrames = 0;

    // Pool and rings
    std::vector<PoolEntry> mPool;
    IndexRing mFree;
    IndexRing mPending;
    IndexRing mOutput;
    IndexRing mWindow; // lookahead window (indices only), capped at mBufferWindowSize

    // Output streaming state
    int mOutputFrontFrameIndex = 0;
    // Overlap-add state
    synaptic::Window mOutputWindow;
    std::vector<std::vector<sample>> mOutputOverlapBuffer;
    int mOutputOverlapValidSamples = 0;
  };
}


