/**
 * @file ChunkPool.h
 * @brief Pool-based memory management for audio chunks
 *
 * Manages a fixed-size pool of audio chunk entries with reference counting.
 * Extracted from AudioStreamChunker to improve code organization.
 */

#pragma once

#include <vector>
#include <algorithm>
#include <cstring>
#include "IPlug_include_in_plug_hdr.h"
#include "../Structs.h"

namespace synaptic
{

/**
 * @brief A pool entry containing input and output audio chunks
 */
struct PoolEntry
{
  AudioChunk inputChunk;   ///< Original input audio from stream
  AudioChunk outputChunk;  ///< Transformer-generated output
  int refCount = 0;        ///< References held by window/pending/output
};

/**
 * @brief Fixed-size ring buffer of indices (no allocations at runtime)
 */
class IndexRing
{
public:
  void Init(int capacity)
  {
    data.assign(capacity, -1);
    head = tail = count = 0;
  }

  int Capacity() const { return static_cast<int>(data.size()); }
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

  int GetAt(int ordinalFromHead) const
  {
    if (ordinalFromHead < 0 || ordinalFromHead >= count) return -1;
    const int cap = Capacity();
    const int pos = (head + ordinalFromHead) % cap;
    return data[pos];
  }

  std::vector<int> data;
  int head = 0;
  int tail = 0;
  int count = 0;
};

/**
 * @brief Manages a pool of audio chunk entries with reference counting
 *
 * Provides allocation-free chunk management using pre-allocated pool entries
 * and ring buffers for tracking free, pending, output, and window indices.
 */
class ChunkPool
{
public:
  ChunkPool() = default;

  /**
   * @brief Configure the pool with specified dimensions
   * @param numChannels Number of audio channels
   * @param chunkSize Size of each chunk in samples
   * @param windowSize Number of chunks in lookahead window
   * @param extraPool Additional pool capacity beyond window size
   */
  void Configure(int numChannels, int chunkSize, int windowSize, int extraPool = 8)
  {
    const int newNumChannels = std::max(1, numChannels);
    const int newChunkSize = std::max(1, chunkSize);
    const int newWindowSize = std::max(1, windowSize);
    const int newPoolCapacity = newWindowSize + extraPool;

    const bool needsReallocation = (newNumChannels != mNumChannels ||
                                    newChunkSize != mChunkSize ||
                                    newPoolCapacity != mPoolCapacity);

    mNumChannels = newNumChannels;
    mChunkSize = newChunkSize;
    mWindowSize = newWindowSize;
    mPoolCapacity = newPoolCapacity;

    if (needsReallocation)
    {
      mPool.assign(mPoolCapacity, {});
      for (int i = 0; i < mPoolCapacity; ++i)
      {
        auto& e = mPool[i];
        e.refCount = 0;
        InitializeChunk(e.inputChunk);
        InitializeChunk(e.outputChunk);
      }
    }

    // Always reset state
    mFree.Init(mPoolCapacity);
    mPending.Init(mPoolCapacity);
    mOutput.Init(mPoolCapacity);
    mWindow.Init(mWindowSize);

    // Reset ref counts
    for (auto& e : mPool)
    {
      e.refCount = 0;
      e.inputChunk.numFrames = mChunkSize;
      e.outputChunk.numFrames = mChunkSize;
    }

    // All indices free initially
    for (int i = 0; i < mPoolCapacity; ++i)
      mFree.Push(i);
  }

  // === Pool Access ===

  int GetPoolCapacity() const { return mPoolCapacity; }
  int GetChunkSize() const { return mChunkSize; }
  int GetNumChannels() const { return mNumChannels; }

  PoolEntry* GetEntry(int idx)
  {
    if (idx < 0 || idx >= mPoolCapacity) return nullptr;
    return &mPool[idx];
  }

  const PoolEntry* GetEntry(int idx) const
  {
    if (idx < 0 || idx >= mPoolCapacity) return nullptr;
    return &mPool[idx];
  }

  const AudioChunk* GetInputChunk(int idx) const
  {
    if (idx < 0 || idx >= mPoolCapacity) return nullptr;
    return &mPool[idx].inputChunk;
  }

  AudioChunk* GetOutputChunk(int idx)
  {
    if (idx < 0 || idx >= mPoolCapacity) return nullptr;
    return &mPool[idx].outputChunk;
  }

  // === Ring Buffer Access ===

  IndexRing& Free() { return mFree; }
  IndexRing& Pending() { return mPending; }
  IndexRing& Output() { return mOutput; }
  IndexRing& Window() { return mWindow; }

  const IndexRing& Free() const { return mFree; }
  const IndexRing& Pending() const { return mPending; }
  const IndexRing& Output() const { return mOutput; }
  const IndexRing& Window() const { return mWindow; }

  // === Reference Counting ===

  void IncRef(int idx)
  {
    if (idx >= 0 && idx < mPoolCapacity)
      ++mPool[idx].refCount;
  }

  void DecRefAndMaybeFree(int idx)
  {
    if (idx < 0 || idx >= mPoolCapacity) return;
    auto& e = mPool[idx];
    --e.refCount;
    if (e.refCount <= 0)
    {
      e.refCount = 0;
      mFree.Push(idx);
    }
  }

private:
  void InitializeChunk(AudioChunk& chunk)
  {
    chunk.numFrames = mChunkSize;
    chunk.channelSamples.assign(mNumChannels, std::vector<iplug::sample>(mChunkSize, 0.0));
    chunk.fftSize = 0;
    chunk.complexSpectrum.clear();
  }

  int mNumChannels = 2;
  int mChunkSize = 3000;
  int mWindowSize = 1;
  int mPoolCapacity = 0;

  std::vector<PoolEntry> mPool;
  IndexRing mFree;
  IndexRing mPending;
  IndexRing mOutput;
  IndexRing mWindow;
};

} // namespace synaptic

