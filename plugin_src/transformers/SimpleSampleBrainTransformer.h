#pragma once

#include "../ChunkBufferTransformer.h"
#include "../samplebrain/Brain.h"
#include "../Window.h"
#include "../../exdeps/pffft/pffft.h"

namespace synaptic
{
  // Simple Samplebrain transformer: match input chunk to closest Brain chunk by freq & amplitude.
  class SimpleSampleBrainTransformer final : public BaseSampleBrainTransformer
  {
  public:
    void Process(AudioStreamChunker& chunker) override
    {
      if (!mBrain)
      {
        // fallback passthrough
        int idx;
        while (chunker.PopPendingInputChunkIndex(idx))
        {
          // Simple passthrough when no brain
          const AudioChunk* in = chunker.GetInputChunk(idx);
          AudioChunk* out = chunker.GetOutputChunk(idx);
          if (in && out)
          {
            const int numChannels = (int)in->channelSamples.size();
            const int chunkSize = chunker.GetChunkSize();
            if ((int)out->channelSamples.size() != numChannels)
              out->channelSamples.assign(numChannels, std::vector<iplug::sample>(chunkSize, 0.0));
            for (int ch = 0; ch < numChannels; ++ch)
            {
              const int copyN = std::min((int)in->channelSamples[ch].size(), chunkSize);
              if (copyN > 0)
                std::memcpy(out->channelSamples[ch].data(), in->channelSamples[ch].data(),
                           sizeof(iplug::sample) * copyN);
            }
            chunker.CommitOutputChunk(idx, in->numFrames);
          }
        }
        return;
      }

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
            std::vector<iplug::sample> temp;
            temp.resize(N);
            for (int i = 0; i < N; ++i)
            {
              const float wi = (i < M) ? w[i] : 0.0f;
              temp[i] = (iplug::sample) (buf[i] * wi);
            }
            inFftFreq[ch] = ComputeDominantFftHz(temp, N, mSampleRate);
          }
        }

        // Channel-independent matching or average-based matching
        const int chunkSize = chunker.GetChunkSize();

        // Ensure output chunk is properly sized
        if ((int)out->channelSamples.size() != numChannels)
          out->channelSamples.assign(numChannels, std::vector<iplug::sample>(chunkSize, 0.0));
        for (int ch = 0; ch < numChannels; ++ch)
          if ((int)out->channelSamples[ch].size() < chunkSize)
            out->channelSamples[ch].assign(chunkSize, 0.0);

        if (mChannelIndependent)
        {
          // For each output channel, independently pick best brain chunk+channel
          const int total = mBrain->GetTotalChunks();
          bool foundAnyMatch = false;
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
                double da = std::abs(in->rms - br);
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
              foundAnyMatch = true;
              const BrainChunk* match = mBrain->GetChunkByGlobalIndex(bestChunk);
              const int framesToWrite = std::min(chunkSize, match ? match->audio.numFrames : chunkSize);
              const int srcChans = match ? (int) match->audio.channelSamples.size() : 0;
              const int sch = (bestSrcCh < srcChans) ? bestSrcCh : 0;
              for (int i = 0; i < framesToWrite; ++i)
                out->channelSamples[ch][i] = match->audio.channelSamples[sch][i];
              for (int i = framesToWrite; i < chunkSize; ++i)
                out->channelSamples[ch][i] = 0.0;
            }
            else
            {
              // No match found for this channel - output silence
              for (int i = 0; i < chunkSize; ++i)
                out->channelSamples[ch][i] = 0.0;
            }
          }

          // Commit output chunk (RMS calculated automatically)
          chunker.CommitOutputChunk(idx, chunkSize);
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
            double da = std::abs(in->rms - br);
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
            // No match found - output silence
            for (int ch = 0; ch < numChannels; ++ch)
              for (int i = 0; i < chunkSize; ++i)
                out->channelSamples[ch][i] = 0.0;
            chunker.CommitOutputChunk(idx, chunkSize);
            continue;
          }

          const BrainChunk* match = mBrain->GetChunkByGlobalIndex(bestIdx);
          if (!match)
          {
            // No match found - output silence
            for (int ch = 0; ch < numChannels; ++ch)
              for (int i = 0; i < chunkSize; ++i)
                out->channelSamples[ch][i] = 0.0;
            chunker.CommitOutputChunk(idx, chunkSize);
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

          chunker.CommitOutputChunk(idx, framesToWrite);
        }
      }
    }

    // Exposed parameters implementation
    void GetParamDescs(std::vector<ExposedParamDesc>& out) const override
    {
      out.clear();
      AddCommonParamDescs(out);

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

    bool SetParamFromNumber(const std::string& id, double v) override
    {
      if (id == "weightFreq") { mWeightFreq = v; return true; }
      if (id == "weightAmp") { mWeightAmp = v; return true; }
      return false;
    }

  protected:
    bool GetDerivedParamAsBool(const std::string& id, bool& out) const override
    {
      if (id == "useFftFreq") { out = mUseFftFreq; return true; }
      return false;
    }

    bool SetDerivedParamFromBool(const std::string& id, bool v) override
    {
      if (id == "useFftFreq") { mUseFftFreq = v; return true; }
      return false;
    }

  private:
    double mWeightFreq = 1.0;
    double mWeightAmp = 1.0;
    bool mUseFftFreq = false;

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

    static double ComputeDominantFftHz(const std::vector<iplug::sample>& buf, int validFrames, double sampleRate)
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

