#pragma once

#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>

#include "IPlug_include_in_plug_hdr.h"
#include "Window.h"

#include "FFT.h"
#include "Structs.h"
#include "Morph.h"

namespace synaptic
{
  using namespace iplug;

  // Audio Chunk is now defined in Structs.h

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
        e.inputChunk.fftSize = 0;
        e.inputChunk.complexSpectrum.clear();
        e.outputChunk.numFrames = mChunkSize;
        e.outputChunk.channelSamples.assign(mNumChannels, std::vector<sample>(mChunkSize, 0.0));
        e.outputChunk.fftSize = 0;
        e.outputChunk.complexSpectrum.clear();
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

      // Configure FFT size
      mFFTSize = Window::NextValidFFTSize(mChunkSize);
      mFFT.Configure(mFFTSize);

      // Keep input analysis window size in sync with chunk size
      mInputAnalysisWindow.Set(mInputAnalysisWindow.GetType(), mChunkSize);

      // Recompute spectral OLA rescale based on analysis window and current chunk size
      {
        const float ovl = mInputAnalysisWindow.GetOverlap();
        const int hop = std::max(1, (int) std::lround(mChunkSize * (1.0 - ovl)));
        mSpectralOLARescale = ComputeSpectralOLARescale(mInputAnalysisWindow, mChunkSize, hop);
      }
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

    // Called by plugin to keep input analysis window in sync with Brain
    void SetInputAnalysisWindow(const synaptic::Window& w)
    {
      // If type changed, just copy config (size should match chunk size already)
      if (mInputAnalysisWindow.GetType() != w.GetType() || mInputAnalysisWindow.Size() != w.Size())
      {
        mInputAnalysisWindow = w;
        // Update spectral rescale when analysis window changes
        const float ovl = mInputAnalysisWindow.GetOverlap();
        const int hop = std::max(1, (int) std::lround(mChunkSize * (1.0 - ovl)));
        mSpectralOLARescale = ComputeSpectralOLARescale(mInputAnalysisWindow, mChunkSize, hop);
      }
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

    // Access to morph instance for configuration
    Morph& GetMorph() { return mMorph; }

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
        // When spectral processing is active, key overlap decision off the analysis window
        const bool spectralActive = (mMorph.GetType() != Morph::Type::None);
        const bool overlapActive = mEnableOverlap && (spectralActive
          ? (mInputAnalysisWindow.GetOverlap() > 0.0f)
          : (mOutputWindow.GetOverlap() > 0.0f));
        int inputHopSize = mChunkSize;
        if (overlapActive)
        {
          const float ovl = spectralActive ? mInputAnalysisWindow.GetOverlap() : mOutputWindow.GetOverlap();
          inputHopSize = std::max(1, (int) std::lround(mChunkSize * (1.0 - ovl)));
        }
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

          // Compute input spectrum for transformer consumption (match Brain analysis window)
          if (mFFTSize > 0)
          {
            mFFT.ComputeChunkSpectrum(entry.inputChunk, mInputAnalysisWindow);
          }

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

      // Determine if spectral processing is active
      const bool spectralActive = (mMorph.GetType() != Morph::Type::None);

      // Use overlap-add only when there's actual overlap (not rectangular window)
      // When spectral processing is active, decide based on the analysis window
      const bool useOverlapAdd = mEnableOverlap && (spectralActive
        ? (mInputAnalysisWindow.GetOverlap() > 0.0f)
        : (mOutputWindow.GetOverlap() > 0.0f));

      if (useOverlapAdd)
      {
        // Derive hop size from analysis window overlap (spectral) or output window overlap (non-spectral)
        const float ovl = spectralActive ? mInputAnalysisWindow.GetOverlap() : mOutputWindow.GetOverlap();
        const int hopSize = std::max(1, (int) std::lround(mChunkSize * (1.0 - ovl)));
        const float rescale = spectralActive ? mSpectralOLARescale : mOutputWindow.GetOverlapRescale();

        // First, process any queued chunks and overlap-add them to our buffer
        while (!mOutput.Empty())
        {
          int idx;
          mOutput.Pop(idx);
          PoolEntry& e = mPool[idx];

          if (e.outputChunk.numFrames > 0)
          {
            // Ensure spectral processing before windowing/OLA
            // This function is where morph is applied
            SpectralProcessing(idx);
            // Compute AGC first (uses spectral-aware or RMS-aware calculation)
            const float agc = ComputeAGC(idx, agcEnabled);
            // Maintain output window only for non-spectral path
            const std::vector<float>* coeffsPtr = nullptr;
            if (!spectralActive)
            {
              if (mOutputWindow.Size() != e.outputChunk.numFrames)
                mOutputWindow.Set(mOutputWindow.GetType(), e.outputChunk.numFrames);
              coeffsPtr = &mOutputWindow.Coeffs();
            }
            const int frames = e.outputChunk.numFrames;
            // Generalized OLA positioning: after adding a frame, the number of newly stable samples is hopSize
            // Therefore, add position should be currentValid - (N - hop)
            const int settledStride = std::max(0, mChunkSize - hopSize);
            const int addPos = (mOutputOverlapValidSamples >= settledStride) ? (mOutputOverlapValidSamples - settledStride) : 0;
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
                  if (addPos + i < (int)mOutputOverlapBuffer[ch].size())
                  {
                    float w = 1.0f;
                    if (!spectralActive && coeffsPtr)
                    {
                      const auto& coeffs = *coeffsPtr;
                      if (i < (int) coeffs.size()) w = coeffs[i];
                    }
                    mOutputOverlapBuffer[ch][addPos + i] += e.outputChunk.channelSamples[ch][i] * w * agc;
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

            // Ensure spectral processing once at the start of each chunk
            if (mOutputFrontFrameIndex == 0 && e.outputChunk.numFrames > 0)
            {
              SpectralProcessing(idx);
            }

            if (mOutputFrontFrameIndex < e.outputChunk.numFrames)
            {
                const float agc = ComputeAGC(idx, agcEnabled);

                // Apply individual chunk windowing if window type has overlap > 0 (skip when spectral is active to avoid double windowing)
                float windowCoeff = 1.0f;
                if (!spectralActive && mOutputWindow.GetOverlap() > 0.0f && mOutputFrontFrameIndex < mOutputWindow.Size())
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

    // Spectral-domain hook: ensure output spectrum, run spectral ops, IFFT back to samples
    void SpectralProcessing(int poolIdx)
    {
      if (poolIdx < 0 || poolIdx >= mPoolCapacity) return;
      if (mFFTSize <= 0) return;
      PoolEntry& e = mPool[poolIdx];

      bool needsSpectrumProcessing = false;

      // check if any spectral processing is needed
      // (such as checking if morph is enabled)
      // if so, then set needsSpectrumProcessing to true
      if(mMorph.GetType() != Morph::Type::None){
        needsSpectrumProcessing = true;
      }

      if(!needsSpectrumProcessing){
        return; // avoid unnecessary spectrum processing
      }

      // If transformer didn't provide spectrum, build it from current samples
      EnsureChunkSpectrum(e.outputChunk);

      // TODO: Future spectral processing here (e.g., morph acting on spectra only)
      mMorph.Process(e.inputChunk, e.outputChunk, mFFT);

      // Synthesize back to time domain for rendering
      mFFT.ComputeChunkIFFT(e.outputChunk);

      // 'Polish' the output chunk to avoid artifacts at the edges of the window
      // This tapers the edges of the window to 0, to avoid clicking after OLA resynthesis.
      for (int ch = 0; ch < mNumChannels; ++ch)
        mOutputWindow.Polish(e.outputChunk.channelSamples[ch].data());
    }

  private:
    // Approximate constant rescale for analysis-windowed OLA with arbitrary hop
    static float ComputeSpectralOLARescale(const synaptic::Window& w, int N, int H)
    {
      const auto& a = w.Coeffs();
      if (a.empty() || N <= 0) return 1.0f;
      const int hop = (H <= 0) ? N : H;
      double sum = 0.0;
      int count = 0;
      for (int n = 0; n < N; ++n)
      {
        double s = 0.0;
        // Sum contributions from all overlapping frames at multiples of hop
        // j ranges over integers such that 0 <= n - j*hop < N
        // Compute jmin and jmax and iterate
        int jmin = (int) std::floor((n - (N - 1)) / (double) hop);
        int jmax = (int) std::floor(n / (double) hop);
        for (int j = jmin; j <= jmax; ++j)
        {
          const int idx = n - j * hop;
          if (idx >= 0 && idx < (int) a.size()) s += a[idx];
        }
        sum += s;
        ++count;
      }
      const double mean = (count > 0) ? (sum / (double) count) : 1.0;
      return (mean > 1e-9) ? (float) (1.0 / mean) : 1.0f;
    }

    void EnsureChunkSpectrum(AudioChunk& chunk)
    {
      if (mFFTSize <= 0) return;
      if (chunk.fftSize != mFFTSize || (int)chunk.complexSpectrum.size() != (int)chunk.channelSamples.size())
      {
        chunk.fftSize = 0; // signal to helper to size
      }
      mFFT.ComputeChunkSpectrum(chunk, mInputAnalysisWindow);
    }

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

      // Determine processing mode
      const bool spectralActive = (mMorph.GetType() != Morph::Type::None);
      const bool overlapActive = mEnableOverlap && (spectralActive
        ? (mInputAnalysisWindow.GetOverlap() > 0.0f)
        : (mOutputWindow.GetOverlap() > 0.0f));

      // Default numerator/denominator use RMS
      double num = sourceChunk ? (double) sourceChunk->rms : 0.0;
      double denom = (double) e.outputChunk.rms;

      if (spectralActive && sourceChunk)
      {
        // Compare spectral magnitudes (Parseval-consistent up to a constant that cancels in ratio)
        // Use sqrt of energy to get an RMS-like quantity for gain computation
        const double Ein = FFTProcessor::ComputeChunkSpectralEnergy(*sourceChunk);
        const double Eout = FFTProcessor::ComputeChunkSpectralEnergy(e.outputChunk);
        num = std::sqrt(std::max(0.0, Ein));
        denom = std::sqrt(std::max(0.0, Eout));
      }

      // Make AGC OLA-aware: predict gain introduced after AGC by OLA and final rescale
      if (overlapActive)
      {
        // Predict OLA gain and combine with final rescale
        const float finalRescale = spectralActive ? mSpectralOLARescale : mOutputWindow.GetOverlapRescale();
        const float olaGainBeforeRescale = spectralActive
          ? ((finalRescale > 1e-9f) ? (1.0f / finalRescale) : 1.0f)
          : 1.0f;
        denom *= (double) (olaGainBeforeRescale * finalRescale);
      }

      if (denom > 1e-9)
      {
        return (float) (num / denom);
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
    int mFFTSize = 0;
    FFTProcessor mFFT;

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
    synaptic::Window mInputAnalysisWindow;
    std::vector<std::vector<sample>> mOutputOverlapBuffer;
    int mOutputOverlapValidSamples = 0;
    float mSpectralOLARescale = 1.0f;
  };
}


