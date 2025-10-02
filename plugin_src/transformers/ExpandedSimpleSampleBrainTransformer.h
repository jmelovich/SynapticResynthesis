#pragma once

#include "../ChunkBufferTransformer.h"
#include "../FeatureAnalysis.h"

namespace synaptic
{
  // Expanded SampleBrain transformer with extended feature analysis
  class ExpandedSimpleSampleBrainTransformer final : public BaseSampleBrainTransformer
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

        // Analyze input chunk using FFT and FeatureAnalysis
        std::vector<std::vector<float>> inFeatures(numChannels, std::vector<float>(7, 0.0f));
        std::vector<float> inFeaturesAvg(7, 0.0f);
        std::vector<double> inFftDominantHz(numChannels, 0.0);
        double inFftDominantHzAvg = 0.0;

        // Compute features for each channel
        const int Nfft = Window::NextValidFFTSize(N);
        PFFFT_Setup* setup = pffft_new_setup(Nfft, PFFFT_REAL);
        if (setup)
        {
          float* inAligned = (float*) pffft_aligned_malloc(sizeof(float) * Nfft);
          float* outAligned = (float*) pffft_aligned_malloc(sizeof(float) * Nfft);
          if (inAligned && outAligned)
          {
            EnsureInputWindowBuilt(N);
            const auto& w = mInputWindow.Coeffs();
            const int W = (int) w.size();
            const int M = std::min(N, W);

            for (int ch = 0; ch < numChannels; ++ch)
            {
              if (ch >= (int) in->channelSamples.size() || in->channelSamples[ch].empty())
                continue;

              const auto& buf = in->channelSamples[ch];

              // Copy and window
              for (int i = 0; i < Nfft; ++i)
              {
                float x = 0.0f;
                if (i < N && i < (int)buf.size())
                {
                  const float wi = (i < M) ? w[i] : 1.0f;
                  x = (float)(buf[i] * wi);
                }
                inAligned[i] = x;
              }

              // FFT
              pffft_transform_ordered(setup, inAligned, outAligned, nullptr, PFFFT_FORWARD);

              // Compute FFT dominant frequency (same as Brain.cpp)
              int bestK = 0;
              float bestMag = -1e9f;
              // DC
              float mag0 = std::abs(outAligned[0]);
              if (mag0 > bestMag) { bestMag = mag0; bestK = 0; }
              // Nyquist
              float magNy = std::abs(outAligned[1]);
              if (magNy > bestMag) { bestMag = magNy; bestK = Nfft/2; }
              // Other bins
              for (int k = 1; k < Nfft/2; ++k)
              {
                float re = outAligned[2*k + 0];
                float im = outAligned[2*k + 1];
                float mag = std::sqrt(re*re + im*im);
                if (mag > bestMag) { bestMag = mag; bestK = k; }
              }
              double domHz = (double)bestK * mSampleRate / (double)Nfft;
              if (domHz < 20.0) domHz = 20.0;
              if (domHz > nyquist - 20.0) domHz = nyquist - 20.0;
              inFftDominantHz[ch] = domHz;
              inFftDominantHzAvg += domHz;

              // Get extended features from FeatureAnalysis
              auto features = FeatureAnalysis::GetFeatures(outAligned, Nfft, (float)mSampleRate);
              if (features.size() >= 7)
              {
                inFeatures[ch] = features;
                for (int f = 0; f < 7; ++f)
                  inFeaturesAvg[f] += features[f];
              }
            }

            // Average features across channels
            for (int f = 0; f < 7; ++f)
              inFeaturesAvg[f] /= (numChannels > 0) ? (float)numChannels : 1.0f;
            inFftDominantHzAvg /= (numChannels > 0) ? (double)numChannels : 1.0;
          }
          if (inAligned) pffft_aligned_free(inAligned);
          if (outAligned) pffft_aligned_free(outAligned);
          pffft_destroy_setup(setup);
        }

        // Allocate output chunk
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
          out->channelSamples.assign(numChannels, std::vector<iplug::sample>(chunkSize, 0.0));
        for (int ch = 0; ch < numChannels; ++ch)
          if ((int) out->channelSamples[ch].size() < chunkSize)
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
                // Get brain chunk features for this channel
                const auto& bFeatures = (bch < (int)bc->extendedFeaturesPerChannel.size())
                  ? bc->extendedFeaturesPerChannel[bch]
                  : bc->avgExtendedFeatures;

                if (bFeatures.size() < 7) continue;

                // Compute weighted distance
                double score = 0.0;

                // FFT Dominant Frequency (like Simple SampleBrain)
                const double bFftFreq = (bch < (int)bc->fftDominantHzPerChannel.size())
                  ? bc->fftDominantHzPerChannel[bch]
                  : bc->avgFftDominantHz;
                const double inFftFreq = (ch < (int)inFftDominantHz.size()) ? inFftDominantHz[ch] : 0.0;
                double dFft = std::abs(inFftFreq - bFftFreq) / nyquist;
                score += mWeightFftFrequency * dFft;

                // Feature 0: Fundamental Frequency (f0 from Harmonic Product Spectrum)
                double df0 = std::abs(inFeatures[ch][0] - bFeatures[0]) / nyquist;
                score += mWeightFundFrequency * df0;

                // Amplitude (use RMS)
                const double br = (bch < (int) bc->rmsPerChannel.size()) ? (double) bc->rmsPerChannel[bch] : (double) bc->avgRms;
                double da = std::abs(in->inRMS - br);
                if (da > 1.0) da = 1.0;
                score += mWeightAmplitude * da;

                // Features 1-6: Affinity, Sharpness, Harmonicity, Monotony, MeanAffinity, MeanContrast
                const double weights[6] = {
                  mWeightAffinity, mWeightSharpness, mWeightHarmonicity,
                  mWeightMonotony, mWeightMeanAffinity, mWeightMeanContrast
                };

                for (int f = 1; f < 7; ++f)
                {
                  double diff = std::abs(inFeatures[ch][f] - bFeatures[f]);
                  // Normalize by a reasonable range (features are already mostly normalized)
                  score += weights[f-1] * std::min(1.0, diff);
                }

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

          // If brain was completely empty, just output silence
          if (!foundAnyMatch && total == 0)
          {
            chunker.CommitWritableChunkIndex(outIdx, chunkSize, 0.0);
          }
          else
          {
            chunker.CommitWritableChunkIndex(outIdx, chunkSize, in->inRMS);
          }
        }
        else
        {
          // Average-based: pick one brain chunk, copy its channels
          int bestIdx = -1;
          double bestScore = 1e9;
          const int total = mBrain->GetTotalChunks();

          for (int bi = 0; bi < total; ++bi)
          {
            const BrainChunk* bc = mBrain->GetChunkByGlobalIndex(bi);
            if (!bc) continue;

            const auto& bFeatures = bc->avgExtendedFeatures;
            if (bFeatures.size() < 7) continue;

            // Compute weighted distance using average features
            double score = 0.0;

            // FFT Dominant Frequency
            double dFft = std::abs(inFftDominantHzAvg - bc->avgFftDominantHz) / nyquist;
            score += mWeightFftFrequency * dFft;

            // Feature 0: Fundamental Frequency (f0)
            double df0 = std::abs(inFeaturesAvg[0] - bFeatures[0]) / nyquist;
            score += mWeightFundFrequency * df0;

            // Amplitude
            double da = std::abs(in->inRMS - (double)bc->avgRms);
            if (da > 1.0) da = 1.0;
            score += mWeightAmplitude * da;

            // Features 1-6
            const double weights[6] = {
              mWeightAffinity, mWeightSharpness, mWeightHarmonicity,
              mWeightMonotony, mWeightMeanAffinity, mWeightMeanContrast
            };

            for (int f = 1; f < 7; ++f)
            {
              double diff = std::abs(inFeaturesAvg[f] - bFeatures[f]);
              score += weights[f-1] * std::min(1.0, diff);
            }

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

    // Exposed parameters implementation
    void GetParamDescs(std::vector<ExposedParamDesc>& out) const override
    {
      out.clear();
      AddCommonParamDescs(out);

      ExposedParamDesc pFft;
      pFft.id = "weightFftFrequency";
      pFft.label = "FFT Frequency Weight";
      pFft.type = ParamType::Number;
      pFft.control = ControlType::Slider;
      pFft.minValue = 0.0;
      pFft.maxValue = 2.0;
      pFft.step = 0.01;
      pFft.defaultNumber = 1.0;
      out.push_back(pFft);

      ExposedParamDesc pFund;
      pFund.id = "weightFundFrequency";
      pFund.label = "Fund Frequency Weight";
      pFund.type = ParamType::Number;
      pFund.control = ControlType::Slider;
      pFund.minValue = 0.0;
      pFund.maxValue = 2.0;
      pFund.step = 0.01;
      pFund.defaultNumber = 0.0;
      out.push_back(pFund);

      ExposedParamDesc pAmp;
      pAmp.id = "weightAmplitude";
      pAmp.label = "Amplitude Weight";
      pAmp.type = ParamType::Number;
      pAmp.control = ControlType::Slider;
      pAmp.minValue = 0.0;
      pAmp.maxValue = 2.0;
      pAmp.step = 0.01;
      pAmp.defaultNumber = 1.0;
      out.push_back(pAmp);

      // Feature weights
      const char* featureNames[] = {
        "Affinity", "Sharpness", "Harmonicity", "Monotony", "Mean Affinity", "Mean Contrast"
      };
      const char* featureIds[] = {
        "weightAffinity", "weightSharpness", "weightHarmonicity",
        "weightMonotony", "weightMeanAffinity", "weightMeanContrast"
      };

      for (int i = 0; i < 6; ++i)
      {
        ExposedParamDesc p;
        p.id = featureIds[i];
        p.label = std::string(featureNames[i]) + " Weight";
        p.type = ParamType::Number;
        p.control = ControlType::Slider;
        p.minValue = 0.0;
        p.maxValue = 2.0;
        p.step = 0.01;
        p.defaultNumber = 0.0;
        out.push_back(p);
      }
    }

    bool GetParamAsNumber(const std::string& id, double& out) const override
    {
      if (id == "weightFftFrequency") { out = mWeightFftFrequency; return true; }
      if (id == "weightFundFrequency") { out = mWeightFundFrequency; return true; }
      if (id == "weightAmplitude") { out = mWeightAmplitude; return true; }
      if (id == "weightAffinity") { out = mWeightAffinity; return true; }
      if (id == "weightSharpness") { out = mWeightSharpness; return true; }
      if (id == "weightHarmonicity") { out = mWeightHarmonicity; return true; }
      if (id == "weightMonotony") { out = mWeightMonotony; return true; }
      if (id == "weightMeanAffinity") { out = mWeightMeanAffinity; return true; }
      if (id == "weightMeanContrast") { out = mWeightMeanContrast; return true; }
      return false;
    }

  protected:
    // No derived bool/string params in this transformer
    // (base class handles inputWindow and channelIndependent)

    bool SetParamFromNumber(const std::string& id, double v) override
    {
      if (id == "weightFftFrequency") { mWeightFftFrequency = v; return true; }
      if (id == "weightFundFrequency") { mWeightFundFrequency = v; return true; }
      if (id == "weightAmplitude") { mWeightAmplitude = v; return true; }
      if (id == "weightAffinity") { mWeightAffinity = v; return true; }
      if (id == "weightSharpness") { mWeightSharpness = v; return true; }
      if (id == "weightHarmonicity") { mWeightHarmonicity = v; return true; }
      if (id == "weightMonotony") { mWeightMonotony = v; return true; }
      if (id == "weightMeanAffinity") { mWeightMeanAffinity = v; return true; }
      if (id == "weightMeanContrast") { mWeightMeanContrast = v; return true; }
      return false;
    }

  private:
    double mWeightFftFrequency = 1.0;       // FFT dominant frequency (like Simple SampleBrain)
    double mWeightFundFrequency = 0.0;      // Fundamental frequency (f0 from Harmonic Product Spectrum)
    double mWeightAmplitude = 1.0;
    double mWeightAffinity = 0.0;
    double mWeightSharpness = 0.0;
    double mWeightHarmonicity = 0.0;
    double mWeightMonotony = 0.0;
    double mWeightMeanAffinity = 0.0;
    double mWeightMeanContrast = 0.0;
  };
}

