#pragma once

#include "../Structs.h" // for AudioChunk
#include "FFT.h" // for FFTProcessor
#include "FeatureAnalysis.h"
#include <vector>
#include <cmath>
#include <cstring>

namespace synaptic
{
  /**
   * @brief Settings for autotune processing
   */
  struct AutotuneSettings
  {
    float blend = 0.0f;              // 0.0 = disabled, 1.0 = full autotune
    bool useHPS = false;             // true = HPS detection, false = FFT peak
    int toleranceOctaves = 3;        // Range: 1-5 octaves
  };

  /**
   * @brief Autotune processor that repitches output chunks to match input pitch
   *
   * Handles pitch detection, tolerance normalization, and spectral pitch shifting.
   * Uses preallocated scratch buffers to avoid runtime allocations.
   */
  class AutotuneProcessor
  {
  public:
    AutotuneProcessor() = default;

    /**
     * @brief Initialize/reset processor with new audio configuration
     * @param sampleRate Sample rate in Hz
     * @param fftSize FFT size (must match chunk fftSize)
     * @param numChannels Number of audio channels
     */
    void OnReset(double sampleRate, int fftSize, int numChannels)
    {
      mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
      mFFTSize = fftSize;
      mNumChannels = numChannels;

      // Preallocate scratch buffers to avoid runtime allocations
      if (mFFTSize > 0 && mNumChannels > 0)
      {
        mScratchSpectrum.assign(mNumChannels, std::vector<float>(mFFTSize, 0.0f));
        mShiftedSpectrum.assign(mNumChannels, std::vector<float>(mFFTSize, 0.0f));
      }

      // Initialize tolerance guards
      UpdateToleranceGuards();
    }

    /**
     * @brief Update autotune settings
     */
    void SetSettings(const AutotuneSettings& settings)
    {
      mSettings = settings;
      UpdateToleranceGuards();
    }

    /**
     * @brief Get current settings
     */
    const AutotuneSettings& GetSettings() const { return mSettings; }

    /**
     * @brief Check if autotune is active (blend > 0)
     */
    bool IsActive() const { return mSettings.blend > 0.0001f; }

    /**
     * @brief Get current sample rate
     */
    double GetSampleRate() const { return mSampleRate; }

    /**
     * @brief Set blend amount (0.0 = disabled, 1.0 = full autotune)
     */
    void SetBlend(float blend)
    {
      mSettings.blend = std::clamp(blend, 0.0f, 1.0f);
    }

    /**
     * @brief Set pitch detection mode (true = HPS, false = FFT peak)
     */
    void SetMode(bool useHPS)
    {
      mSettings.useHPS = useHPS;
    }

    /**
     * @brief Set tolerance octaves (1-5)
     */
    void SetToleranceOctaves(int octaves)
    {
      mSettings.toleranceOctaves = std::clamp(octaves, 1, 5);
      UpdateToleranceGuards();
    }

    /**
     * @brief Process autotune on input/output chunks
     *
     * Detects pitch from input chunk, repitches output chunk to match,
     * respecting tolerance and blend settings.
     *
     * @param inputChunk Input audio chunk (for pitch reference)
     * @param outputChunk Output audio chunk (modified in-place)
     * @param fft FFT processor for spectrum operations
     */
    void Process(const AudioChunk& inputChunk, AudioChunk& outputChunk, FFTProcessor& fft)
    {
      if (mSettings.blend <= 0.0001f) return; // Skip if disabled
      if (mFFTSize <= 0 || mNumChannels <= 0) return;
      if (inputChunk.fftSize != mFFTSize || outputChunk.fftSize != mFFTSize) return;

      // Detect pitches
      const float inputPitch = DetectPitch(inputChunk);
      const float outputPitch = DetectPitch(outputChunk);

      if (inputPitch <= 0.0f || outputPitch <= 0.0f) return; // Invalid pitch detection

      const float ratio = inputPitch / outputPitch;

      if (mSettings.blend >= 0.9999f)
      {
        // Full autotune: directly shift output spectrum
        ApplyPitchShift(outputChunk, ratio);
      }
      else
      {
        // Blend: shift into scratch buffer, then blend with original
        // Copy output spectrum to scratch
        for (int ch = 0; ch < mNumChannels && ch < (int)outputChunk.complexSpectrum.size(); ++ch)
        {
          if (ch < (int)mShiftedSpectrum.size() && (int)mShiftedSpectrum[ch].size() == mFFTSize)
          {
            std::memcpy(mShiftedSpectrum[ch].data(), outputChunk.complexSpectrum[ch].data(),
                       sizeof(float) * mFFTSize);
          }
        }

        // Create temporary chunk with shifted spectrum
        AudioChunk tempChunk = outputChunk;
        tempChunk.complexSpectrum = mShiftedSpectrum;

        ApplyPitchShift(tempChunk, ratio);

        // Blend original and shifted spectra
        for (int ch = 0; ch < mNumChannels && ch < (int)outputChunk.complexSpectrum.size(); ++ch)
        {
          if (ch >= (int)tempChunk.complexSpectrum.size()) continue;
          if ((int)tempChunk.complexSpectrum[ch].size() != mFFTSize) continue;
          for (int i = 0; i < mFFTSize; ++i)
          {
            outputChunk.complexSpectrum[ch][i] =
              (1.0f - mSettings.blend) * outputChunk.complexSpectrum[ch][i] +
              mSettings.blend * tempChunk.complexSpectrum[ch][i];
          }
        }
      }
    }

  private:
    /**
     * @brief Detect pitch from audio chunk spectrum
     * @return Pitch in Hz, or -1.0f if detection failed
     */
    float DetectPitch(const AudioChunk& chunk) const
    {
      if (chunk.fftSize <= 0 || chunk.complexSpectrum.empty()) return -1.0f;
      if (mSampleRate <= 0.0) return -1.0f;

      float totalPitch = 0.0f;
      int validChannels = 0;

      for (int ch = 0; ch < (int)chunk.complexSpectrum.size() && ch < mNumChannels; ++ch)
      {
        const float* spectrum = chunk.complexSpectrum[ch].data();
        if (!spectrum) continue;

        float pitch = -1.0f;
        if (mSettings.useHPS)
        {
          // Use HPS-based fundamental frequency detection
          auto result = FeatureAnalysis::FundamentalFrequency(spectrum, chunk.fftSize, (float)mSampleRate, 6);
          pitch = result.first; // frequency in Hz
        }
        else
        {
          // Use FFT peak detection
          pitch = (float)FFTProcessor::DominantFreqHzFromOrderedSpectrum(spectrum, chunk.fftSize, mSampleRate);
        }

        if (pitch > 0.0f)
        {
          totalPitch += pitch;
          ++validChannels;
        }
      }

      if (validChannels > 0)
        return totalPitch / (float)validChannels;
      return -1.0f;
    }

    /**
     * @brief Apply pitch shift to chunk spectrum with tolerance normalization
     * @param chunk Audio chunk (spectrum modified in-place)
     * @param pitchRatio Desired pitch ratio (inputPitch / outputPitch)
     */
    void ApplyPitchShift(AudioChunk& chunk, float pitchRatio)
    {
      if (chunk.fftSize <= 0 || chunk.complexSpectrum.empty()) return;

      // Normalize to nearest octave-equivalent within tolerance,
      // minimizing distance to the original ratio (NOT distance to 1.0),
      // and preferring to preserve the direction (above/below 1.0).
      const float origRatio = pitchRatio;
      const bool preferUp = (origRatio >= 1.0f);
      float bestRatio = -1.0f;
      float bestScore = 1e9f;

      // Search octave-shifted candidates of the original ratio within guard rails
      for (int k = -12; k <= 12; ++k)
      {
        float candidate = origRatio * (float)std::pow(2.0, (double)k);
        if (candidate < mMinGuard || candidate > mMaxGuard)
          continue;
        // Primary score: closeness to original in log2 domain (octaves of change)
        float score = (float)std::fabs(std::log2(std::max(candidate / std::max(origRatio, 1e-12f), 1e-12f)));
        // Tie-breaker: small penalty if the candidate flips direction across 1.0
        const bool sameSide = preferUp ? (candidate >= 1.0f) : (candidate <= 1.0f);
        if (!sameSide) score += 0.05f;
        if (score < bestScore)
        {
          bestScore = score;
          bestRatio = candidate;
        }
      }

      if (bestRatio > 0.0f)
      {
        pitchRatio = bestRatio;
      }
      else
      {
        // Fallback: clamp original ratio to nearest boundary, preserving direction
        pitchRatio = preferUp
          ? std::min(std::max(origRatio, 1.0f), mMaxGuard)
          : std::max(std::min(origRatio, 1.0f), mMinGuard);
      }

      // Apply pitch shift to each channel
      for (int ch = 0; ch < (int)chunk.complexSpectrum.size() && ch < mNumChannels; ++ch)
      {
        float* spec = chunk.complexSpectrum[ch].data();
        if (!spec) continue;

        // Use preallocated scratch buffer instead of allocating
        if (ch >= (int)mScratchSpectrum.size() || (int)mScratchSpectrum[ch].size() != chunk.fftSize)
          continue;

        std::memcpy(mScratchSpectrum[ch].data(), spec, sizeof(float) * chunk.fftSize);

        // DC and Nyquist bins remain unchanged
        spec[0] = mScratchSpectrum[ch][0]; // DC
        if (chunk.fftSize >= 2)
          spec[1] = mScratchSpectrum[ch][1]; // Nyquist

        // Shift frequency bins with linear interpolation
        for (int k = 1; k < chunk.fftSize / 2; ++k)
        {
          // Target frequency bin in original spectrum
          float srcBin = (float)k / pitchRatio;

          if (srcBin < 0.5f || srcBin >= (float)(chunk.fftSize / 2 - 1))
          {
            // Out of range: zero out
            spec[2 * k + 0] = 0.0f;
            spec[2 * k + 1] = 0.0f;
            continue;
          }

          // Find integer bin indices for interpolation
          int bin0 = (int)std::floor(srcBin);
          int bin1 = bin0 + 1;
          float frac = srcBin - (float)bin0;

          // Clamp bins to valid range
          bin0 = std::max(1, std::min(bin0, chunk.fftSize / 2 - 1));
          bin1 = std::max(1, std::min(bin1, chunk.fftSize / 2 - 1));

          // Interpolate magnitude and phase
          float mag0 = std::sqrt(mScratchSpectrum[ch][2 * bin0 + 0] * mScratchSpectrum[ch][2 * bin0 + 0] +
                                 mScratchSpectrum[ch][2 * bin0 + 1] * mScratchSpectrum[ch][2 * bin0 + 1]);
          float mag1 = std::sqrt(mScratchSpectrum[ch][2 * bin1 + 0] * mScratchSpectrum[ch][2 * bin1 + 0] +
                                 mScratchSpectrum[ch][2 * bin1 + 1] * mScratchSpectrum[ch][2 * bin1 + 1]);
          float mag = (1.0f - frac) * mag0 + frac * mag1;

          float phase0 = std::atan2(mScratchSpectrum[ch][2 * bin0 + 1], mScratchSpectrum[ch][2 * bin0 + 0]);
          float phase1 = std::atan2(mScratchSpectrum[ch][2 * bin1 + 1], mScratchSpectrum[ch][2 * bin1 + 0]);

          // Handle phase wrapping
          float phaseDiff = phase1 - phase0;
          if (phaseDiff > 3.14159265359f) phaseDiff -= 6.28318530718f;
          if (phaseDiff < -3.14159265359f) phaseDiff += 6.28318530718f;
          float phase = phase0 + frac * phaseDiff;

          // Reconstruct complex value
          spec[2 * k + 0] = mag * std::cos(phase);
          spec[2 * k + 1] = mag * std::sin(phase);
        }
      }
    }

  private:
    AutotuneSettings mSettings;
    double mSampleRate = 48000.0;
    int mFFTSize = 0;
    int mNumChannels = 0;

    // Precomputed tolerance guard rails
    float mMinGuard = 0.125f;  // 1/2^3 (default 3 octaves)
    float mMaxGuard = 8.0f;   // 2^3

    // Preallocated scratch buffers (no runtime allocations)
    std::vector<std::vector<float>> mScratchSpectrum;  // For pitch shifting
    std::vector<std::vector<float>> mShiftedSpectrum;   // For blend mode

    /**
     * @brief Update tolerance guard rails from current settings
     */
    void UpdateToleranceGuards()
    {
      if (mSettings.toleranceOctaves >= 1 && mSettings.toleranceOctaves <= 5)
      {
        mMaxGuard = (float)std::pow(2.0, (double)mSettings.toleranceOctaves);
        mMinGuard = 1.0f / mMaxGuard;
      }
    }
  };
}

