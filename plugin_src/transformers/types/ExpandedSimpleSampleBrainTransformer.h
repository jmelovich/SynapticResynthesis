#pragma once

#include "../BaseTransformer.h"
#include "plugin_src/audio/FeatureAnalysis.h"
#include "plugin_src/audio/FFT.h"

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

        // Analyze input chunk using precomputed spectra and FeatureAnalysis
        std::vector<std::vector<float>> inFeatures(numChannels, std::vector<float>(7, 0.0f));
        std::vector<float> inFeaturesAvg(7, 0.0f);
        std::vector<double> inFftDominantHz(numChannels, 0.0);
        double inFftDominantHzAvg = 0.0;

        if (in->fftSize > 0)
        {
          for (int ch = 0; ch < numChannels; ++ch)
          {
            if (ch >= (int)in->complexSpectrum.size() || in->complexSpectrum[ch].empty())
              continue;

            const float* ordered = in->complexSpectrum[ch].data();
            // Dominant freq
            double domHz = FFTProcessor::DominantFreqHzFromOrderedSpectrum(ordered, in->fftSize, mSampleRate);
            if (domHz < 20.0) domHz = 20.0;
            if (domHz > nyquist - 20.0) domHz = nyquist - 20.0;
            inFftDominantHz[ch] = domHz;
            inFftDominantHzAvg += domHz;

            // Extended features from ordered spectrum
            auto features = FeatureAnalysis::GetFeatures((float*)ordered, in->fftSize, (int)mSampleRate);
            inFeatures[ch] = features;
            for (int f = 0; f < 7; ++f)
              inFeaturesAvg[f] += features[f];
          }
          for (int f = 0; f < 7; ++f)
            inFeaturesAvg[f] /= (numChannels > 0) ? (float)numChannels : 1.0f;
          inFftDominantHzAvg /= (numChannels > 0) ? (double)numChannels : 1.0;
        }

        // Prepare output chunk (already allocated in same entry as input)
        const int chunkSize = chunker.GetChunkSize();
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
                double da = std::abs(in->rms - br);
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
              std::vector<int> src = { bestSrcCh };
              std::vector<int> dst = { ch };
              CopyBrainChannelsToOutput(match, chunkSize, numChannels, *out, src, dst);
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
            double da = std::abs(in->rms - (double)bc->avgRms);
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
          CopyBrainChannelsToOutput(match, chunkSize, numChannels, *out);
          chunker.CommitOutputChunk(idx, std::min(chunkSize, match->audio.numFrames));
        }
      }
    }

    // Exposed parameters implementation
    void GetParamDescs(std::vector<ExposedParamDesc>& out, bool /*includeAll*/) const override
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

