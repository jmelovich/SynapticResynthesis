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

  // Simple Samplebrain transformer: match input chunk to closest Brain chunk by freq & amplitude.
  class SimpleSampleBrainTransformer final : public IChunkBufferTransformer
  {
  public:
    void OnReset(double sampleRate, int /*chunkSize*/, int /*bufferWindowSize*/, int /*numChannels*/) override
    {
      mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
      mLastChunkSize = 0; // force window rebuild on next use
    }

    void SetBrain(const Brain* brain) { mBrain = brain; }

    void Process(AudioStreamChunker& chunker) override
    {
      if (!mBrain)
      {
        // fallback passthrough
        int idx;
        while (chunker.PopPendingInputChunkIndex(idx))
        {
          CopyInputToOutput(chunker, idx);
        }
        return;
      }

      const int numChannels = chunker.GetNumChannels();

      int inIdx;
      while (chunker.PopPendingInputChunkIndex(inIdx))
      {
        const AudioChunk* in = chunker.GetChunkConstByIndex(inIdx);
        if (!in || in->numFrames <= 0)
        {
          chunker.EnqueueOutputChunkIndex(inIdx);
          continue;
        }

        const int N = in->numFrames;
        const double nyquist = 0.5 * mSampleRate;

        // Analyze all channels (frequency only, RMS already computed by chunker)
        std::vector<double> inFreq(numChannels, 440.0);
        std::vector<double> inFftFreq(numChannels, 440.0);
        for (int ch = 0; ch < numChannels; ++ch)
        {
          if (ch >= (int) in->channelSamples.size() || in->channelSamples[ch].empty())
            continue;
          const auto& buf = in->channelSamples[ch];
          int zc = 0;
          double prev = buf[0];
          for (int i = 1; i < N; ++i)
          {
            double x = buf[i];
            if ((prev <= 0.0 && x > 0.0) || (prev >= 0.0 && x < 0.0))
              ++zc;
            prev = x;
          }
          double f = (double) zc * mSampleRate / (2.0 * (double) N);
          if (!(f > 0.0)) f = 440.0;
          if (f < 20.0) f = 20.0;
          if (f > nyquist - 20.0) f = nyquist - 20.0;
          inFreq[ch] = f;

          if (mUseFftFreq)
          {
            // Apply selected input window (Rectangular = no-op)
            EnsureInputWindowBuilt(N);
            const auto& w = mInputWindow.Coeffs();
            const int W = (int) w.size();
            const int M = std::min(N, W);
            std::vector<sample> temp;
            temp.resize(N);
            for (int i = 0; i < N; ++i)
            {
              const float wi = (i < M) ? w[i] : 0.0f;
              temp[i] = (sample) (buf[i] * wi);
            }
            inFftFreq[ch] = ComputeDominantFftHz(temp, N, mSampleRate);
          }
        }

        // Channel-independent matching or average-based matching
        int outIdx;
        if (!chunker.AllocateWritableChunkIndex(outIdx))
        {
          chunker.EnqueueOutputChunkIndex(inIdx);
          continue;
        }
        AudioChunk* out = chunker.GetWritableChunkByIndex(outIdx);
        if (!out)
        {
          chunker.EnqueueOutputChunkIndex(inIdx);
          continue;
        }

        const int chunkSize = chunker.GetChunkSize();
        if ((int) out->channelSamples.size() != numChannels)
          out->channelSamples.assign(numChannels, std::vector<sample>(chunkSize, 0.0));
        for (int ch = 0; ch < numChannels; ++ch)
          if ((int) out->channelSamples[ch].size() < chunkSize)
            out->channelSamples[ch].assign(chunkSize, 0.0);

        if (mChannelIndependent)
        {
          // For each output channel, independently pick best brain chunk+channel
          const int total = mBrain->GetTotalChunks();
          for (int ch = 0; ch < numChannels; ++ch)
          {
            int bestChunk = -1;
            int bestSrcCh = 0;
            double bestScore = 1e9;
            for (int bi = 0; bi < total; ++bi)
            {
              const BrainChunk* bc = mBrain->GetChunkByGlobalIndex(bi);
              if (!bc) continue;
              const int bChans = (int) bc->audio.channelSamples.size();
              for (int bch = 0; bch < bChans; ++bch)
              {
                const double bf = (!mUseFftFreq)
                  ? ((bch < (int) bc->freqHzPerChannel.size() && bc->freqHzPerChannel[bch] > 0.0)
                      ? bc->freqHzPerChannel[bch]
                      : (bc->avgFreqHz > 0.0 ? bc->avgFreqHz : 440.0))
                  : ((bch < (int) bc->fftDominantHzPerChannel.size() && bc->fftDominantHzPerChannel[bch] > 0.0)
                      ? bc->fftDominantHzPerChannel[bch]
                      : (bc->avgFftDominantHz > 0.0 ? bc->avgFftDominantHz : 440.0));
                const double br = (bch < (int) bc->rmsPerChannel.size()) ? (double) bc->rmsPerChannel[bch] : (double) bc->avgRms;
                const double inFeatureF = mUseFftFreq ? inFftFreq[ch] : inFreq[ch];
                double df = std::abs(inFeatureF - bf) / nyquist;
                double da = std::abs(in->inRMS - br);
                if (da > 1.0) da = 1.0;
                double score = mWeightFreq * df + mWeightAmp * da;
                if (score < bestScore)
                {
                  bestScore = score;
                  bestChunk = bi;
                  bestSrcCh = bch;
                }
              }
            }

            if (bestChunk >= 0)
            {
              const BrainChunk* match = mBrain->GetChunkByGlobalIndex(bestChunk);
              const int framesToWrite = std::min(chunkSize, match ? match->audio.numFrames : chunkSize);
              const int srcChans = match ? (int) match->audio.channelSamples.size() : 0;
              const int sch = (bestSrcCh < srcChans) ? bestSrcCh : 0;
              for (int i = 0; i < framesToWrite; ++i)
                out->channelSamples[ch][i] = match->audio.channelSamples[sch][i];
              for (int i = framesToWrite; i < chunkSize; ++i)
                out->channelSamples[ch][i] = 0.0;
            }
          }
          chunker.CommitWritableChunkIndex(outIdx, chunkSize, in->inRMS);
        }
        else
        {
          // Average-based: pick one brain chunk, copy its channels
          int bestIdx = -1;
          double bestScore = 1e9;
          const int total = mBrain->GetTotalChunks();
          const double inFreqAvg = (numChannels > 0) ? std::accumulate(inFreq.begin(), inFreq.end(), 0.0) / (double) numChannels : 440.0;
          const double inFftAvg = (numChannels > 0) ? std::accumulate(inFftFreq.begin(), inFftFreq.end(), 0.0) / (double) numChannels : 440.0;
          for (int bi = 0; bi < total; ++bi)
          {
            const BrainChunk* bc = mBrain->GetChunkByGlobalIndex(bi);
            if (!bc) continue;
            const double bf = (!mUseFftFreq)
              ? ((bc->avgFreqHz > 0.0) ? bc->avgFreqHz : 440.0)
              : ((bc->avgFftDominantHz > 0.0) ? bc->avgFftDominantHz : 440.0);
            const double br = (double) bc->avgRms;
            const double inFeatureAvg = mUseFftFreq ? inFftAvg : inFreqAvg;
            double df = std::abs(inFeatureAvg - bf) / nyquist;
            double da = std::abs(in->inRMS - br);
            if (da > 1.0) da = 1.0;
            double score = mWeightFreq * df + mWeightAmp * da;
            if (score < bestScore)
            {
              bestScore = score;
              bestIdx = bi;
            }
          }

          if (bestIdx < 0)
          {
            chunker.EnqueueOutputChunkIndex(inIdx);
            continue;
          }

          const BrainChunk* match = mBrain->GetChunkByGlobalIndex(bestIdx);
          if (!match)
          {
            chunker.EnqueueOutputChunkIndex(inIdx);
            continue;
          }

          const int framesToWrite = std::min(chunkSize, match->audio.numFrames);
          const int srcChans = (int) match->audio.channelSamples.size();
          for (int ch = 0; ch < numChannels; ++ch)
          {
            const int sch = (ch < srcChans) ? ch : 0;
            for (int i = 0; i < framesToWrite; ++i)
              out->channelSamples[ch][i] = match->audio.channelSamples[sch][i];
            for (int i = framesToWrite; i < chunkSize; ++i)
              out->channelSamples[ch][i] = 0.0;
          }

          chunker.CommitWritableChunkIndex(outIdx, framesToWrite, in->inRMS);
        }
      }
    }

    int GetAdditionalLatencySamples(int /*chunkSize*/, int /*bufferWindowSize*/) const override { return 0; }
    int GetRequiredLookaheadChunks() const override { return 0; }

    void SetWeights(double wFreq, double wAmp) { mWeightFreq = wFreq; mWeightAmp = wAmp; }
    void SetChannelIndependent(bool enabled) { mChannelIndependent = enabled; }
    void SetUseFftFreq(bool enabled) { mUseFftFreq = enabled; }

    // Exposed parameters implementation
    void GetParamDescs(std::vector<ExposedParamDesc>& out) const override
    {
      out.clear();
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

      ExposedParamDesc p1b;
      p1b.id = "useFftFreq";
      p1b.label = "Use FFT Frequency";
      p1b.type = ParamType::Boolean;
      p1b.control = ControlType::Checkbox;
      p1b.defaultBool = false;
      out.push_back(p1b);

      ExposedParamDesc p2;
      p2.id = "weightFreq";
      p2.label = "Frequency Weight";
      p2.type = ParamType::Number;
      p2.control = ControlType::Slider;
      p2.minValue = 0.0;
      p2.maxValue = 2.0;
      p2.step = 0.01;
      p2.defaultNumber = 1.0;
      out.push_back(p2);

      ExposedParamDesc p3;
      p3.id = "weightAmp";
      p3.label = "Amplitude Weight";
      p3.type = ParamType::Number;
      p3.control = ControlType::Slider;
      p3.minValue = 0.0;
      p3.maxValue = 2.0;
      p3.step = 0.01;
      p3.defaultNumber = 1.0;
      out.push_back(p3);
    }

    bool GetParamAsNumber(const std::string& id, double& out) const override
    {
      if (id == "weightFreq") { out = mWeightFreq; return true; }
      if (id == "weightAmp") { out = mWeightAmp; return true; }
      return false;
    }

    bool GetParamAsBool(const std::string& id, bool& out) const override
    {
      if (id == "channelIndependent") { out = mChannelIndependent; return true; }
      if (id == "useFftFreq") { out = mUseFftFreq; return true; }
      return false;
    }

    bool GetParamAsString(const std::string& id, std::string& out) const override
    {
      if (id == "inputWindow") { out = InputWindowModeToString(mInputWinMode); return true; }
      return false;
    }

    bool SetParamFromNumber(const std::string& id, double v) override
    {
      if (id == "weightFreq") { mWeightFreq = v; return true; }
      if (id == "weightAmp") { mWeightAmp = v; return true; }
      return false;
    }

    bool SetParamFromBool(const std::string& id, bool v) override
    {
      if (id == "channelIndependent") { mChannelIndependent = v; return true; }
      if (id == "useFftFreq") { mUseFftFreq = v; return true; }
      return false;
    }

    bool SetParamFromString(const std::string& id, const std::string& v) override
    {
      if (id == "inputWindow")
      {
        mInputWinMode = StringToInputWindowMode(v);
        mLastChunkSize = 0; // rebuild at next use
        return true;
      }
      return false;
    }

  private:
    const Brain* mBrain = nullptr;
    double mSampleRate = 48000.0;
    double mWeightFreq = 1.0;
    double mWeightAmp = 1.0;
    bool mChannelIndependent = false;
    bool mUseFftFreq = false;
    enum class InputWindowMode { Hann, Hamming, Blackman, Rectangular };
    InputWindowMode mInputWinMode = InputWindowMode::Hann;
    Window mInputWindow;
    int mLastChunkSize = 0;

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

    void EnsureInputWindowBuilt(int size)
    {
      if (size <= 0) return;
      if (mLastChunkSize != size)
      {
        mInputWindow.Set(InputWindowModeToWindowType(mInputWinMode), size);
        mLastChunkSize = size;
      }
    }

    static bool IsGoodFftN(int n)
    {
      if (n <= 0) return false;
      if ((n % 32) != 0) return false;
      int m = n;
      for (int p : {2,3,5})
      {
        while ((m % p) == 0) m /= p;
      }
      return m == 1;
    }

    static int NextGoodFftN(int minN)
    {
      int n = (minN < 32) ? 32 : minN;
      for (;; ++n) if (IsGoodFftN(n)) return n;
    }

    static double ComputeDominantFftHz(const std::vector<sample>& buf, int validFrames, double sampleRate)
    {
      if (validFrames <= 0 || sampleRate <= 0.0) return 0.0;
      const int Nfft = NextGoodFftN(validFrames);
      PFFFT_Setup* setup = pffft_new_setup(Nfft, PFFFT_REAL);
      if (!setup) return 0.0;
      float* inAligned = (float*) pffft_aligned_malloc(sizeof(float) * Nfft);
      float* outAligned = (float*) pffft_aligned_malloc(sizeof(float) * Nfft);
      if (!inAligned || !outAligned)
      {
        if (inAligned) pffft_aligned_free(inAligned);
        if (outAligned) pffft_aligned_free(outAligned);
        pffft_destroy_setup(setup);
        return 0.0;
      }
      const int N = std::min((int) buf.size(), validFrames);
      for (int i = 0; i < Nfft; ++i)
        inAligned[i] = (i < N) ? (float) buf[i] : 0.0f;
      pffft_transform_ordered(setup, inAligned, outAligned, nullptr, PFFFT_FORWARD);

      int bestK = 0;
      float bestMag = -1.0f;
      // DC and Nyquist in out[0], out[1]
      float mag0 = std::abs(outAligned[0]);
      if (mag0 > bestMag) { bestMag = mag0; bestK = 0; }
      float magNy = std::abs(outAligned[1]);
      if (magNy > bestMag) { bestMag = magNy; bestK = Nfft/2; }
      for (int k = 1; k < Nfft/2; ++k)
      {
        float re = outAligned[2*k + 0];
        float im = outAligned[2*k + 1];
        float mag = std::sqrt(re*re + im*im);
        if (mag > bestMag) { bestMag = mag; bestK = k; }
      }
      double hz = (double) bestK * sampleRate / (double) Nfft;
      const double ny = 0.5 * sampleRate;
      if (hz < 20.0) hz = 20.0;
      if (hz > ny - 20.0) hz = ny - 20.0;
      pffft_aligned_free(inAligned);
      pffft_aligned_free(outAligned);
      pffft_destroy_setup(setup);
      return hz;
    }
  };
}


