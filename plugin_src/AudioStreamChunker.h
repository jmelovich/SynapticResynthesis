#pragma once

#include <vector>
#include <algorithm>
#include <cstring>

#include "IPlug_include_in_plug_hdr.h"

namespace synaptic
{
  using namespace iplug;

  struct AudioChunk
  {
    std::vector<std::vector<sample>> channelSamples; // [channel][frame]
    int numFrames = 0;
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
      mNumChannels = std::max(1, numChannels);
      mChunkSize = std::max(1, chunkSize);
      mBufferWindowSize = std::max(1, windowSize);

      // Pre-size accumulation scratch
      mAccumulation.assign(mNumChannels, std::vector<sample>(mChunkSize, 0.0));
      mAccumulatedFrames = 0;

      // Pool sizing: window + extra headroom for pending/output
      mPoolCapacity = mBufferWindowSize + mExtraPool;
      mPool.assign(mPoolCapacity, {});
      for (int i = 0; i < mPoolCapacity; ++i)
      {
        auto& e = mPool[i];
        e.refCount = 0;
        e.chunk.numFrames = mChunkSize;
        e.chunk.channelSamples.assign(mNumChannels, std::vector<sample>(mChunkSize, 0.0));
      }

      // Initialize rings
      mFree.Init(mPoolCapacity);
      mPending.Init(mPoolCapacity);
      mOutput.Init(mPoolCapacity);
      mWindow.Init(mBufferWindowSize);

      // All indices free initially
      for (int i = 0; i < mPoolCapacity; ++i)
        mFree.Push(i);

      mOutputFrontFrameIndex = 0;
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

    void Reset()
    {
      Configure(mNumChannels, mChunkSize, mBufferWindowSize);
    }

    int GetChunkSize() const { return mChunkSize; }

    void PushAudio(sample** inputs, int nFrames)
    {
      if (!inputs || nFrames <= 0 || mNumChannels <= 0) return;

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

        if (mAccumulatedFrames >= mChunkSize)
        {
          int poolIdx;
          if (!mFree.Pop(poolIdx))
          {
            // No space; drop this chunk silently
            mAccumulatedFrames = 0;
            continue;
          }

          PoolEntry& entry = mPool[poolIdx];
          // Copy scratch into pool chunk
          for (int ch = 0; ch < mNumChannels; ++ch)
          {
            std::memcpy(entry.chunk.channelSamples[ch].data(), mAccumulation[ch].data(), sizeof(sample) * mChunkSize);
          }
          entry.chunk.numFrames = mChunkSize;

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

          mAccumulatedFrames = 0;
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
    void CommitWritableChunkIndex(int idx, int numFrames)
    {
      if (idx < 0 || idx >= mPoolCapacity) return;
      PoolEntry& e = mPool[idx];
      if (numFrames < 0) numFrames = 0;
      if (numFrames > mChunkSize) numFrames = mChunkSize;
      e.chunk.numFrames = numFrames;
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

    void RenderOutput(sample** outputs, int nFrames, int outChans)
    {
      if (!outputs || nFrames <= 0 || outChans <= 0) return;

      const int chansToWrite = std::min(outChans, mNumChannels);
      for (int s = 0; s < nFrames; ++s)
      {
        for (int ch = 0; ch < outChans; ++ch)
          if (outputs[ch]) outputs[ch][s] = 0.0;

        if (!mOutput.Empty())
        {
          int idx = mOutput.PeekOldest();
          if (idx >= 0 && idx < mPoolCapacity)
          {
            PoolEntry& e = mPool[idx];
            if (mOutputFrontFrameIndex < e.chunk.numFrames)
            {
              for (int ch = 0; ch < chansToWrite; ++ch)
              {
                if (outputs[ch] && ch < (int)e.chunk.channelSamples.size() &&
                    mOutputFrontFrameIndex < (int)e.chunk.channelSamples[ch].size())
                  outputs[ch][s] = e.chunk.channelSamples[ch][mOutputFrontFrameIndex];
              }
            }

            ++mOutputFrontFrameIndex;
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

    // Map a pool index to a read-only chunk pointer (nullptr if invalid)
    const AudioChunk* GetChunkConstByIndex(int idx) const
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
    int mExtraPool = 8; // additional capacity beyond window size
    int mPoolCapacity = 0;

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
  };
}


