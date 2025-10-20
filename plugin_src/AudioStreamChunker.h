#pragma once

#include <vector>
#include <algorithm>
#include <cstring>

#include "IPlug_include_in_plug_hdr.h"
#include "Window.h"

#include "Morph.h"

namespace synaptic
{
  using namespace iplug;

  struct AudioChunk
  {
    std::vector<std::vector<sample>> channelSamples; // [channel][frame]
    int numFrames = 0;
    double rms = 0.0;  // RMS of this chunk's audio
    int64_t startSample = -1;     // timeline position for alignment (could be useful)
    // NOTE: sourceInputPoolIdx removed - no longer needed with dual-chunk pool entries
  };

  struct PoolEntry
  {
    AudioChunk inputChunk;   // Original input audio from stream
    AudioChunk outputChunk;  // Transformer-generated output
    int refCount = 0;        // references held by window/pending/output
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

      mMorph.Configure(Morph::Type::None, mChunkSize);  // Disabled for now

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
        // Initialize both input and output chunks
        e.inputChunk.numFrames = mChunkSize;
        e.inputChunk.channelSamples.assign(mNumChannels, std::vector<sample>(mChunkSize, 0.0));
        e.outputChunk.numFrames = mChunkSize;
        e.outputChunk.channelSamples.assign(mNumChannels, std::vector<sample>(mChunkSize, 0.0));
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
          // Copy scratch into pool input chunk
          for (int ch = 0; ch < mNumChannels; ++ch)
          {
            std::memcpy(entry.inputChunk.channelSamples[ch].data(), mAccumulation[ch].data(), sizeof(sample) * mChunkSize);
          }
          entry.inputChunk.numFrames = mChunkSize;

          // Set timeline position for this input chunk
          entry.inputChunk.startSample = mTotalInputSamplesPushed - mAccumulatedFrames;

          // Calculate RMS for this input chunk
          float chunkRMS = 0;
          for (int ch = 0; ch < mNumChannels; ch++)
          {
            sample* __restrict chunk = entry.inputChunk.channelSamples[ch].data();
            for (int i = 0; i < entry.inputChunk.numFrames; i++)
            {
              chunkRMS += chunk[i] * chunk[i]; // gather Sum of Squares
            }
          }
          chunkRMS = sqrt(chunkRMS / (entry.inputChunk.numFrames * mNumChannels)); // get Root Mean Square
          entry.inputChunk.rms = chunkRMS;

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

    // NEW API: Get input chunk for reading (const access)
    const AudioChunk* GetInputChunk(int idx) const
    {
      if (idx < 0 || idx >= mPoolCapacity) return nullptr;
      return &mPool[idx].inputChunk;
    }

    // NEW API: Get output chunk for writing (mutable access)
    AudioChunk* GetOutputChunk(int idx)
    {
      if (idx < 0 || idx >= mPoolCapacity) return nullptr;
      return &mPool[idx].outputChunk;
    }

    // NEW API: Commit output chunk (simplified - no separate source tracking needed)
    // Input and output are in the same entry, so source is implicitly tracked.
    void CommitOutputChunk(int idx, int numFrames)
    {
      if (idx < 0 || idx >= mPoolCapacity) return;
      PoolEntry& e = mPool[idx];
      if (numFrames < 0) numFrames = 0;
      if (numFrames > mChunkSize) numFrames = mChunkSize;
      e.outputChunk.numFrames = numFrames;

      // Calculate RMS for this output chunk
      double chunkRMS = 0.0;
      for (int ch = 0; ch < mNumChannels && ch < (int)e.outputChunk.channelSamples.size(); ch++)
      {
        const auto& channelData = e.outputChunk.channelSamples[ch];
        for (int i = 0; i < numFrames && i < (int)channelData.size(); i++)
        {
          chunkRMS += channelData[i] * channelData[i];
        }
      }
      if (numFrames > 0 && mNumChannels > 0)
        chunkRMS = sqrt(chunkRMS / (numFrames * mNumChannels));
      e.outputChunk.rms = chunkRMS;

      // No separate source tracking needed - input and output are co-located!
      // The entry is already referenced by window, which keeps input alive.

      // add output ref and enqueue
      ++e.refCount;
      mOutput.Push(idx);
    }

    // Helper to clear output chunk to a value (default 0).
    void ClearOutputChunk(int idx, sample value = 0.0)
    {
      if (idx < 0 || idx >= mPoolCapacity) return;
      PoolEntry& e = mPool[idx];
      for (int ch = 0; ch < (int)e.outputChunk.channelSamples.size(); ++ch)
      {
        std::fill(e.outputChunk.channelSamples[ch].begin(), e.outputChunk.channelSamples[ch].end(), value);
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

          if (e.outputChunk.numFrames > 0)
          {
            // Compute AGC first (uses original output chunk RMS)
            const float agc = ComputeAGC(idx, agcEnabled);

            // Apply morph processing AFTER AGC but BEFORE windowing/OLA
            // This modifies the output chunk in-place, blending with co-located input
            const AudioChunk* sourceChunk = GetSourceChunkForOutput(idx);
            if (sourceChunk) {
              mMorph.Process(sourceChunk->channelSamples, e.outputChunk.channelSamples, e.outputChunk.channelSamples);
            }

            if (mOutputWindow.Size() != e.outputChunk.numFrames)
              mOutputWindow.Set(mOutputWindow.GetType(), e.outputChunk.numFrames);

            const auto& coeffs = mOutputWindow.Coeffs();
            const int frames = e.outputChunk.numFrames;
            const int addPos = (mOutputOverlapValidSamples >= hopSize) ? (mOutputOverlapValidSamples - hopSize) : 0;
            const int requiredSize = addPos + frames;

            if ((int) mOutputOverlapBuffer[0].size() < requiredSize)
            {
              for (int ch = 0; ch < mNumChannels; ++ch)
                mOutputOverlapBuffer[ch].resize(requiredSize, 0.0);
            }

            for (int ch = 0; ch < std::min(mNumChannels, (int)mOutputOverlapBuffer.size()); ++ch)
            {
              if (ch < (int)e.outputChunk.channelSamples.size())
              {

                for (int i = 0; i < frames; ++i)
                {
                  if (addPos + i < (int)mOutputOverlapBuffer[ch].size() && i < (int)coeffs.size())
                  {
                    mOutputOverlapBuffer[ch][addPos + i] += e.outputChunk.channelSamples[ch][i] * coeffs[i] * agc;
                  }
                }
              }
            }
            mOutputOverlapValidSamples = requiredSize;
          }

          // Release the pool entry (input and output are co-located)
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

            // Access the corresponding input chunk for this output chunk (if available)
            // const AudioChunk* sourceChunk = GetSourceChunkForOutput(idx);
            // if (sourceChunk && mOutputFrontFrameIndex < sourceChunk->numFrames)
            // {
            //   // sourceChunk->channelSamples[ch][mOutputFrontFrameIndex] = input sample
            //   // sourceChunk->rms = input chunk RMS
            //   // e.chunk.channelSamples[ch][mOutputFrontFrameIndex] = output sample
            //   // e.chunk.rms = output chunk RMS
            //   // You can blend/morph them here before writing to outputs[ch][s]
            // }
            if (mOutputFrontFrameIndex < e.outputChunk.numFrames)
            {
                const float agc = ComputeAGC(idx, agcEnabled);

                // Apply individual chunk windowing if window type has overlap > 0
                float windowCoeff = 1.0f;
                if (mOutputWindow.GetOverlap() > 0.0f && mOutputFrontFrameIndex < mOutputWindow.Size())
                {
                  const auto& coeffs = mOutputWindow.Coeffs();
                  windowCoeff = coeffs[mOutputFrontFrameIndex];
                }

              for (int ch = 0; ch < chansToWrite; ++ch)
              {
                if (outputs[ch] && ch < (int)e.outputChunk.channelSamples.size() &&
                    mOutputFrontFrameIndex < (int)e.outputChunk.channelSamples[ch].size())
                  {
                    outputs[ch][s] = e.outputChunk.channelSamples[ch][mOutputFrontFrameIndex] * windowCoeff * agc;
                  }
                }
            }

            ++mOutputFrontFrameIndex;
            ++mTotalOutputSamplesRendered;  // Track output for latency control

            if (mOutputFrontFrameIndex >= e.outputChunk.numFrames)
            {
              // finished this chunk
              int finished;
              mOutput.Pop(finished);
              DecRefAndMaybeFree(finished);
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

    // Current output head (if any) and its frame index
    bool PeekCurrentOutput(int& outPoolIdx, int& outFrameIndex) const
    {
      if (mOutput.Empty()) return false;
      outPoolIdx = mOutput.PeekOldest();
      outFrameIndex = mOutputFrontFrameIndex;
      return true;
    }

    int GetNumChannels() const { return mNumChannels; }

    // Get the source input chunk for a given output chunk index.
    // SIMPLIFIED: Input and output are co-located, so just return inputChunk from same entry.
    const AudioChunk* GetSourceChunkForOutput(int outputPoolIdx) const
    {
      if (outputPoolIdx < 0 || outputPoolIdx >= mPoolCapacity)
      {
        return nullptr;
      }
      // Input and output are in the same pool entry!
      return &mPool[outputPoolIdx].inputChunk;
    }

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

    float ComputeAGC(int outputIdx, bool agcEnabled) const
    {
      if (!agcEnabled) return 1.0f;

      if (outputIdx < 0 || outputIdx >= mPoolCapacity) return 1.0f;

      const AudioChunk* sourceChunk = GetSourceChunkForOutput(outputIdx);
      const PoolEntry& e = mPool[outputIdx];

      if (sourceChunk && e.outputChunk.rms > 1e-9) // avoid divide by zero
      {
        return (float)(sourceChunk->rms / e.outputChunk.rms);
      }

      return 1.0f;
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

    Morph mMorph;

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


