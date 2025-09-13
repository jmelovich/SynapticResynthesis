#pragma once

#include "AudioStreamChunker.h"

namespace synaptic
{
  // Base interface for all chunk-buffer transformers.
  // Implementations can declare additional algorithmic latency (in samples),
  // beyond the intrinsic chunk accumulation delay.
  class IChunkBufferTransformer
  {
  public:
    virtual ~IChunkBufferTransformer() {}

    // Lifecycle hook for reinitialization on SR/size changes.
    virtual void OnReset(double sampleRate, int chunkSize, int bufferWindowSize, int numChannels) = 0;

    // Called from audio thread each block to consume pending input chunks
    // and push transformed output chunks.
    virtual void Process(AudioStreamChunker& chunker) = 0;

    // Additional algorithmic latency in samples (not including chunk accumulation).
    // Useful when algorithms require extra buffering/lookahead.
    virtual int GetAdditionalLatencySamples(int chunkSize, int bufferWindowSize) const = 0;

    // Required lookahead in chunks before processing (to gate scheduling).
    virtual int GetRequiredLookaheadChunks() const = 0;
  };

  // Simple passthrough transformer: no additional latency and no lookahead.
  class PassthroughTransformer final : public IChunkBufferTransformer
  {
  public:
    void OnReset(double /*sampleRate*/, int /*chunkSize*/, int /*bufferWindowSize*/, int /*numChannels*/) override {}

    void Process(AudioStreamChunker& chunker) override
    {
      int idx;
      while (chunker.PopPendingInputChunkIndex(idx))
      {
        chunker.EnqueueOutputChunkIndex(idx);
      }
    }

    int GetAdditionalLatencySamples(int /*chunkSize*/, int /*bufferWindowSize*/) const override
    {
      return 0;
    }

    int GetRequiredLookaheadChunks() const override
    {
      return 0;
    }
  };

  // Demonstration transformer: for each input chunk, synthesize a sine chunk
  // with roughly matched frequency (via zero-crossing rate) and amplitude (via RMS).
  class SineMatchTransformer final : public IChunkBufferTransformer
  {
  public:
    void OnReset(double sampleRate, int /*chunkSize*/, int /*bufferWindowSize*/, int /*numChannels*/) override
    {
      mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    }

    void Process(AudioStreamChunker& chunker) override
    {
      const int chunkSize = chunker.GetChunkSize();
      const int numChannels = chunker.GetNumChannels();

      int inIdx;
      while (chunker.PopPendingInputChunkIndex(inIdx))
      {
        const AudioChunk* in = chunker.GetChunkConstByIndex(inIdx);
        if (!in)
          continue;

        // Estimate amplitude (RMS on mono sum) and frequency (zero-crossing rate)
        const int N = in->numFrames;
        if (N <= 0)
          continue;

        // Build a simple mono proxy by averaging channels
        double rmsAcc = 0.0;
        int zc = 0;
        auto sampleAt = [&](int i) -> double {
          double sum = 0.0;
          int chans = (int) in->channelSamples.size();
          for (int ch = 0; ch < chans; ++ch)
          {
            if (i < (int) in->channelSamples[ch].size())
              sum += (double) in->channelSamples[ch][i];
          }
          return (chans > 0) ? (sum / (double) chans) : 0.0;
        };

        double prev = sampleAt(0);
        rmsAcc += prev * prev;
        for (int i = 1; i < N; ++i)
        {
          double x = sampleAt(i);
          rmsAcc += x * x;
          if ((prev <= 0.0 && x > 0.0) || (prev >= 0.0 && x < 0.0))
            ++zc;
          prev = x;
        }
        const double rms = std::sqrt(rmsAcc / std::max(1, N));
        double freq = (double) zc * mSampleRate / (2.0 * std::max(1, N));
        if (!(freq > 0.0)) freq = 440.0;
        const double nyquist = 0.5 * mSampleRate;
        if (freq < 20.0) freq = 20.0;
        if (freq > nyquist - 20.0) freq = nyquist - 20.0;

        // Allocate an output chunk and synthesize sine
        int outIdx;
        if (!chunker.AllocateWritableChunkIndex(outIdx))
          continue;
        AudioChunk* out = chunker.GetWritableChunkByIndex(outIdx);
        if (!out)
          continue;

        // amplitude: convert RMS to peak for a sine (sqrt(2) factor)
        const double amp = std::min(1.0, rms * 1.41421356237);
        double phase = 0.0;
        const double dphase = 2.0 * 3.14159265358979323846 * freq / mSampleRate;

        // Ensure channels sized
        if ((int) out->channelSamples.size() != numChannels)
          out->channelSamples.assign(numChannels, std::vector<sample>(chunkSize, 0.0));
        for (int ch = 0; ch < numChannels; ++ch)
        {
          if ((int) out->channelSamples[ch].size() < chunkSize)
            out->channelSamples[ch].assign(chunkSize, 0.0);
        }

        const int framesToWrite = std::min(chunkSize, N);
        for (int i = 0; i < framesToWrite; ++i)
        {
          const sample v = (sample) (amp * std::sin(phase));
          for (int ch = 0; ch < numChannels; ++ch)
            out->channelSamples[ch][i] = v;
          phase += dphase;
        }
        // Zero-fill remainder if any
        for (int i = framesToWrite; i < chunkSize; ++i)
        {
          for (int ch = 0; ch < numChannels; ++ch)
            out->channelSamples[ch][i] = 0.0;
        }

        chunker.CommitWritableChunkIndex(outIdx, framesToWrite);
      }
    }

    int GetAdditionalLatencySamples(int /*chunkSize*/, int /*bufferWindowSize*/) const override
    {
      return 0; // synthesis done per input chunk, no extra latency beyond chunk accumulation
    }

    int GetRequiredLookaheadChunks() const override
    {
      return 0; // no extra lookahead needed
    }

  private:
    double mSampleRate = 48000.0;
  };
}


