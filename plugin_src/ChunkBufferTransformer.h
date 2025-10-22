#pragma once

#include "AudioStreamChunker.h"
#include "samplebrain/Brain.h"
#include <cmath>
#include <string>
#include <vector>
#include <utility>
#include <numeric>
#include <cstring>

// No direct FFT here; transformers consume precomputed spectra from the chunker/brain

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

    // Whether this transformer's output should be overlap-added by the chunker.
    // If false, the chunker will use simple sequential playback.
    virtual bool WantsOverlapAdd() const { return true; }

    // Generic parameter exposure API for UI
    enum class ParamType { Number, Boolean, Enum, Text };
    enum class ControlType { Slider, NumberBox, Select, Checkbox, TextBox };

    struct ParamOption
    {
      std::string value;  // internal value
      std::string label;  // human-readable label
    };

    struct ExposedParamDesc
    {
      std::string id;           // unique, stable identifier
      std::string label;        // display name
      ParamType type = ParamType::Number;
      ControlType control = ControlType::NumberBox;
      // Numeric constraints (for Number)
      double minValue = 0.0;
      double maxValue = 1.0;
      double step = 0.01;
      // Options (for Enum)
      std::vector<ParamOption> options;
      // Defaults
      double defaultNumber = 0.0;
      bool defaultBool = false;
      std::string defaultString;
    };

    // Describe all exposed parameters (schema)
    virtual void GetParamDescs(std::vector<ExposedParamDesc>& out) const { out.clear(); }

    // Get current value by id
    virtual bool GetParamAsNumber(const std::string& /*id*/, double& /*out*/) const { return false; }
    virtual bool GetParamAsBool(const std::string& /*id*/, bool& /*out*/) const { return false; }
    virtual bool GetParamAsString(const std::string& /*id*/, std::string& /*out*/) const { return false; }

    // Set value by id
    virtual bool SetParamFromNumber(const std::string& /*id*/, double /*v*/) { return false; }
    virtual bool SetParamFromBool(const std::string& /*id*/, bool /*v*/) { return false; }
    virtual bool SetParamFromString(const std::string& /*id*/, const std::string& /*v*/) { return false; }

  protected:
    // NOTE: CopyInputToOutput helper removed - no longer needed with dual-chunk pool entries.
    // Use GetInputChunk() / GetOutputChunk() / CommitOutputChunk() instead.
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
        // NEW API: Input and output are in the same pool entry
        const AudioChunk* in = chunker.GetInputChunk(idx);
        AudioChunk* out = chunker.GetOutputChunk(idx);

        if (!in || !out) continue;

        const int numChannels = (int)in->channelSamples.size();
        const int chunkSize = chunker.GetChunkSize();
        const int framesToWrite = std::min(chunkSize, in->numFrames);

        // Ensure output is properly sized
        if ((int)out->channelSamples.size() != numChannels)
          out->channelSamples.assign(numChannels, std::vector<iplug::sample>(chunkSize, 0.0));

        // Copy input to output
        for (int ch = 0; ch < numChannels; ++ch)
        {
          if ((int)out->channelSamples[ch].size() < chunkSize)
            out->channelSamples[ch].assign(chunkSize, 0.0);

          const int copyN = std::min(framesToWrite, (int)in->channelSamples[ch].size());
          if (copyN > 0)
            std::memcpy(out->channelSamples[ch].data(), in->channelSamples[ch].data(),
                       sizeof(iplug::sample) * copyN);

          // Zero-fill remainder
          for (int i = copyN; i < chunkSize; ++i)
            out->channelSamples[ch][i] = 0.0;
        }

        chunker.CommitOutputChunk(idx, framesToWrite);
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

      int idx;
      while (chunker.PopPendingInputChunkIndex(idx))
      {
        // NEW API: Access input and output from same entry
        const AudioChunk* in = chunker.GetInputChunk(idx);
        AudioChunk* out = chunker.GetOutputChunk(idx);

        if (!in || !out || in->numFrames <= 0)
          continue;

        const int N = in->numFrames;

        std::vector<double> freqs(numChannels);
        std::vector<double> amps(numChannels);

        // 1. Analyze each channel independently
        for (int ch = 0; ch < numChannels; ++ch)
        {
          if (ch >= (int)in->channelSamples.size() || in->channelSamples[ch].empty())
          {
            freqs[ch] = 440.0;
            amps[ch] = 0.0;
            continue;
          }

          const auto& channelData = in->channelSamples[ch];

          int zc = 0;
          double prev = channelData[0];

          for (int i = 1; i < N; ++i)
          {
            double x = channelData[i];
            if ((prev <= 0.0 && x > 0.0) || (prev >= 0.0 && x < 0.0))
              ++zc;
            prev = x;
          }

          double freq = (double) zc * mSampleRate / (2.0 * N);

          // Freq clamping
          if (!(freq > 0.0)) freq = 440.0;
          const double nyquist = 0.5 * mSampleRate;
          if (freq < 20.0) freq = 20.0;
          if (freq > nyquist - 20.0) freq = nyquist - 20.0;

          freqs[ch] = freq;
          // Use pre-calculated input RMS from chunker, convert to peak amplitude
          amps[ch] = std::min(1.0, in->rms * 1.41421356237); // RMS to peak
        }

        // 2. Synthesize to output chunk
        // Ensure channels sized
        if ((int)out->channelSamples.size() != numChannels)
          out->channelSamples.assign(numChannels, std::vector<sample>(chunkSize, 0.0));
        for (int ch = 0; ch < numChannels; ++ch)
        {
          if ((int)out->channelSamples[ch].size() < chunkSize)
            out->channelSamples[ch].assign(chunkSize, 0.0);
        }

        const int framesToWrite = std::min(chunkSize, N);

        for (int ch = 0; ch < numChannels; ++ch)
        {
          double phase = 0.0;
          const double dphase = 2.0 * 3.14159265358979323846 * freqs[ch] / mSampleRate;

          for (int i = 0; i < framesToWrite; ++i)
          {
            out->channelSamples[ch][i] = (sample)(amps[ch] * std::sin(phase));
            phase += dphase;
          }
          // Zero-fill remainder if any
          for (int i = framesToWrite; i < chunkSize; ++i)
          {
            out->channelSamples[ch][i] = 0.0;
          }
        }

        chunker.CommitOutputChunk(idx, framesToWrite);
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

  // Forward declare Brain for base class
  class Brain;

  // Base class for SampleBrain-based transformers
  // Provides common functionality for transformers that match input chunks
  // against a Brain database using feature-based similarity.
  class BaseSampleBrainTransformer : public IChunkBufferTransformer
  {
  public:
    void OnReset(double sampleRate, int /*chunkSize*/, int /*bufferWindowSize*/, int /*numChannels*/) override
    {
      mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    }

    void SetBrain(const Brain* brain) { mBrain = brain; }

    int GetAdditionalLatencySamples(int /*chunkSize*/, int /*bufferWindowSize*/) const override
    {
      return 0;
    }

    int GetRequiredLookaheadChunks() const override
    {
      return 0;
    }

    // Common parameter getters/setters
    bool GetParamAsBool(const std::string& id, bool& out) const override
    {
      if (id == "channelIndependent") { out = mChannelIndependent; return true; }
      return GetDerivedParamAsBool(id, out);
    }

    bool GetParamAsString(const std::string& id, std::string& out) const override
    {
      return GetDerivedParamAsString(id, out);
    }

    bool SetParamFromBool(const std::string& id, bool v) override
    {
      if (id == "channelIndependent") { mChannelIndependent = v; return true; }
      return SetDerivedParamFromBool(id, v);
    }

    bool SetParamFromString(const std::string& id, const std::string& v) override
    {
      return SetDerivedParamFromString(id, v);
    }

  protected:
    // Hook methods for derived classes to add their own parameters
    virtual bool GetDerivedParamAsBool(const std::string& /*id*/, bool& /*out*/) const { return false; }
    virtual bool GetDerivedParamAsString(const std::string& /*id*/, std::string& /*out*/) const { return false; }
    virtual bool SetDerivedParamFromBool(const std::string& /*id*/, bool /*v*/) { return false; }
    virtual bool SetDerivedParamFromString(const std::string& /*id*/, const std::string& /*v*/) { return false; }

    // Helper to add common parameter descriptors
    void AddCommonParamDescs(std::vector<ExposedParamDesc>& out) const
    {
      ExposedParamDesc p1;
      p1.id = "channelIndependent";
      p1.label = "Channel Independent";
      p1.type = ParamType::Boolean;
      p1.control = ControlType::Checkbox;
      p1.defaultBool = false;
      out.push_back(p1);
    }

    // Centralized copy helper for matched brain chunks across arbitrary channel mappings.
    // If both vectors are empty, copies all channels 0..numOutChannels-1 from matching brain channels.
    void CopyBrainChannelsToOutput(const BrainChunk* match,
                                   int chunkSize,
                                   int numOutChannels,
                                   AudioChunk& out,
                                   const std::vector<int>& brainSrcChans = std::vector<int>(),
                                   const std::vector<int>& outChans = std::vector<int>()) const
    {
      if (!match || chunkSize <= 0 || numOutChannels <= 0) return;

      const int framesToWrite = std::min(chunkSize, match->audio.numFrames);
      const int srcChans = (int) match->audio.channelSamples.size();

      // Ensure output buffers sized
      if ((int) out.channelSamples.size() != numOutChannels)
        out.channelSamples.assign(numOutChannels, std::vector<iplug::sample>(chunkSize, 0.0));
      for (int ch = 0; ch < numOutChannels; ++ch)
        if ((int) out.channelSamples[ch].size() < chunkSize)
          out.channelSamples[ch].assign(chunkSize, 0.0);

      auto doPair = [&](int sch, int och)
      {
        if (och < 0 || och >= numOutChannels) return;
        const int srcIdx = (sch >= 0 && sch < srcChans) ? sch : 0;
        for (int i = 0; i < framesToWrite; ++i)
          out.channelSamples[och][i] = match->audio.channelSamples[srcIdx][i];
        for (int i = framesToWrite; i < chunkSize; ++i)
          out.channelSamples[och][i] = 0.0;
      };

      if (brainSrcChans.empty() && outChans.empty())
      {
        for (int och = 0; och < numOutChannels; ++och)
          doPair(och, och);
      }
      else
      {
        const int pairs = (int) std::min(brainSrcChans.size(), outChans.size());
        for (int i = 0; i < pairs; ++i)
          doPair(brainSrcChans[i], outChans[i]);
      }

      // Copy spectra if available
      if (match->audio.fftSize > 0)
      {
        out.fftSize = match->audio.fftSize;
        if ((int) out.complexSpectrum.size() != numOutChannels)
          out.complexSpectrum.assign(numOutChannels, std::vector<float>(out.fftSize, 0.0f));

        auto doPairSpec = [&](int sch, int och)
        {
          if (och < 0 || och >= numOutChannels) return;
          const int srcIdx = (sch >= 0 && sch < (int)match->audio.complexSpectrum.size()) ? sch : 0;
          if (och < (int) out.complexSpectrum.size() && srcIdx < (int) match->audio.complexSpectrum.size())
            out.complexSpectrum[och] = match->audio.complexSpectrum[srcIdx];
        };

        if (brainSrcChans.empty() && outChans.empty())
        {
          for (int och = 0; och < numOutChannels; ++och)
            doPairSpec(och, och);
        }
        else
        {
          const int pairs = (int) std::min(brainSrcChans.size(), outChans.size());
          for (int i = 0; i < pairs; ++i)
            doPairSpec(brainSrcChans[i], outChans[i]);
        }
      }
    }

    // Common members accessible to derived classes
    const Brain* mBrain = nullptr;
    double mSampleRate = 48000.0;
    bool mChannelIndependent = false;

  };

  // Simple Samplebrain transformer moved to transformers/SimpleSampleBrainTransformer.h
}


