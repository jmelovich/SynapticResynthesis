#pragma once

#include "AudioStreamChunker.h"
#include "samplebrain/Brain.h"
#include "Window.h"
#include <cmath>
#include <string>
#include <vector>
#include <utility>
#include <numeric>
#include <cstring>

// FFT support for on-the-fly input analysis
#include "../exdeps/pffft/pffft.h"

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
    // Helper function to copy input chunk to output chunk (common pattern)
    static bool CopyInputToOutput(AudioStreamChunker& chunker, int inIdx)
    {
      int outIdx;
      if (!chunker.AllocateWritableChunkIndex(outIdx))
      {
        chunker.EnqueueOutputChunkIndex(inIdx);
        return false;
      }

      AudioChunk* out = chunker.GetWritableChunkByIndex(outIdx);
      const AudioChunk* in = chunker.GetChunkConstByIndex(inIdx);
      if (!out || !in)
      {
        chunker.EnqueueOutputChunkIndex(inIdx);
        return false;
      }

      const int numChannels = (int) in->channelSamples.size();
      const int outChunkSize = chunker.GetChunkSize();
      const int framesToWrite = std::min(outChunkSize, std::max(0, in->numFrames));

      if ((int) out->channelSamples.size() != numChannels)
        out->channelSamples.assign(numChannels, std::vector<iplug::sample>(outChunkSize, 0.0));

      for (int ch = 0; ch < numChannels; ++ch)
      {
        if ((int) out->channelSamples[ch].size() < outChunkSize)
          out->channelSamples[ch].assign(outChunkSize, 0.0);
        const int copyN = std::min(framesToWrite, (int) in->channelSamples[ch].size());
        if (copyN > 0)
          std::memcpy(out->channelSamples[ch].data(), in->channelSamples[ch].data(), sizeof(iplug::sample) * copyN);
        for (int i = copyN; i < outChunkSize; ++i)
          out->channelSamples[ch][i] = 0.0;
      }

      chunker.CommitWritableChunkIndex(outIdx, framesToWrite, in->inRMS);
      return true;
    }
  };

  // Simple passthrough transformer: no additional latency and no lookahead.
  class PassthroughTransformer final : public IChunkBufferTransformer
  {
  public:
    void OnReset(double /*sampleRate*/, int /*chunkSize*/, int /*bufferWindowSize*/, int /*numChannels*/) override {}

    void Process(AudioStreamChunker& chunker) override
    {
      int inIdx;
      while (chunker.PopPendingInputChunkIndex(inIdx))
      {
        CopyInputToOutput(chunker, inIdx);
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
        if (!in || in->numFrames <= 0)
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
          amps[ch] = std::min(1.0, in->inRMS * 1.41421356237); // RMS to peak
        }

        // 2. Allocate and synthesize
        int outIdx;
        if (!chunker.AllocateWritableChunkIndex(outIdx))
          continue;
        AudioChunk* out = chunker.GetWritableChunkByIndex(outIdx);
        if (!out)
          continue;

        // Ensure channels sized
        if ((int) out->channelSamples.size() != numChannels)
          out->channelSamples.assign(numChannels, std::vector<sample>(chunkSize, 0.0));
        for (int ch = 0; ch < numChannels; ++ch)
        {
          if ((int) out->channelSamples[ch].size() < chunkSize)
            out->channelSamples[ch].assign(chunkSize, 0.0);
        }

        const int framesToWrite = std::min(chunkSize, N);

        for (int ch = 0; ch < numChannels; ++ch)
        {
          double phase = 0.0;
          const double dphase = 2.0 * 3.14159265358979323846 * freqs[ch] / mSampleRate;

          for (int i = 0; i < framesToWrite; ++i)
          {
            out->channelSamples[ch][i] = (sample) (amps[ch] * std::sin(phase));
            phase += dphase;
          }
          // Zero-fill remainder if any
          for (int i = framesToWrite; i < chunkSize; ++i)
          {
            out->channelSamples[ch][i] = 0.0;
          }
        }

        chunker.CommitWritableChunkIndex(outIdx, framesToWrite, in->inRMS);
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
      mLastChunkSize = 0; // force window rebuild on next use
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
      if (id == "inputWindow") { out = InputWindowModeToString(mInputWinMode); return true; }
      return GetDerivedParamAsString(id, out);
    }

    bool SetParamFromBool(const std::string& id, bool v) override
    {
      if (id == "channelIndependent") { mChannelIndependent = v; return true; }
      return SetDerivedParamFromBool(id, v);
    }

    bool SetParamFromString(const std::string& id, const std::string& v) override
    {
      if (id == "inputWindow")
      {
        mInputWinMode = StringToInputWindowMode(v);
        mLastChunkSize = 0; // rebuild at next use
        return true;
      }
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
      ExposedParamDesc p0;
      p0.id = "inputWindow";
      p0.label = "Input Analysis Window";
      p0.type = ParamType::Enum;
      p0.control = ControlType::Select;
      p0.options = {
        { "hann", "Hann" },
        { "hamming", "Hamming" },
        { "blackman", "Blackman" },
        { "rectangular", "Rectangular" }
      };
      p0.defaultString = "hann";
      out.push_back(p0);

      ExposedParamDesc p1;
      p1.id = "channelIndependent";
      p1.label = "Channel Independent";
      p1.type = ParamType::Boolean;
      p1.control = ControlType::Checkbox;
      p1.defaultBool = false;
      out.push_back(p1);
    }

    void EnsureInputWindowBuilt(int size)
    {
      if (size <= 0) return;
      if (mLastChunkSize != size)
      {
        mInputWindow.Set(InputWindowModeToWindowType(mInputWinMode), size);
        mLastChunkSize = size;
      }
    }

    // Common members accessible to derived classes
    const Brain* mBrain = nullptr;
    double mSampleRate = 48000.0;
    bool mChannelIndependent = false;
    Window mInputWindow;
    int mLastChunkSize = 0;

  private:
    enum class InputWindowMode { Hann, Hamming, Blackman, Rectangular };
    InputWindowMode mInputWinMode = InputWindowMode::Hann;

    static std::string InputWindowModeToString(InputWindowMode m)
    {
      switch (m)
      {
        case InputWindowMode::Hann: return "hann";
        case InputWindowMode::Hamming: return "hamming";
        case InputWindowMode::Blackman: return "blackman";
        case InputWindowMode::Rectangular: return "rectangular";
        default: return "hann";
      }
    }

    static InputWindowMode StringToInputWindowMode(const std::string& s)
    {
      if (s == "hann") return InputWindowMode::Hann;
      if (s == "hamming") return InputWindowMode::Hamming;
      if (s == "blackman") return InputWindowMode::Blackman;
      if (s == "rectangular") return InputWindowMode::Rectangular;
      return InputWindowMode::Hann;
    }

    static Window::Type InputWindowModeToWindowType(InputWindowMode m)
    {
      switch (m)
      {
        case InputWindowMode::Hann: return Window::Type::Hann;
        case InputWindowMode::Hamming: return Window::Type::Hamming;
        case InputWindowMode::Blackman: return Window::Type::Blackman;
        case InputWindowMode::Rectangular: return Window::Type::Rectangular;
        default: return Window::Type::Hann;
      }
    }
  };

  // Simple Samplebrain transformer moved to transformers/SimpleSampleBrainTransformer.h
}


