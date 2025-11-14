#pragma once

#include <vector>
#include <cmath>

#include "Window.h"
#include "../Structs.h" // for synaptic::AudioChunk

// PFFFT ordered API
#include "../exdeps/pffft/pffft.h"


namespace synaptic
{
  /**
   * Simple wrapper around PFFFT for ordered real FFT/IFFT.
   * Stores/consumes spectra in PFFFT "ordered" layout with length Nfft.
   */
  class FFTProcessor
  {
  public:
    FFTProcessor() = default;
    ~FFTProcessor() { Destroy(); }

    void Configure(int fftSize)
    {
      if (fftSize == mFFTSize) return;
      Destroy();
      mFFTSize = fftSize;
      if (mFFTSize > 0)
      {
        mSetup = pffft_new_setup(mFFTSize, PFFFT_REAL);
        mScratch.resize((size_t) mFFTSize, 0.0f);
      }
    }

    // Compute spectral energy from ordered real spectrum (unique bins).
    // Uses DC^2 + Nyquist^2 + 2 * sum_{k=1..N/2-1} (Re^2 + Im^2)
    static double SpectrumEnergyOrdered(const float* ordered, int Nfft)
    {
      if (!ordered || Nfft <= 0) return 0.0;
      double energy = 0.0;
      // DC and Nyquist (if present)
      energy += (double) ordered[0] * (double) ordered[0];
      if (Nfft >= 2)
        energy += (double) ordered[1] * (double) ordered[1];
      // Interior bins
      for (int k = 1; k < Nfft/2; ++k)
      {
        const float re = ordered[2*k + 0];
        const float im = ordered[2*k + 1];
        energy += 2.0 * ((double) re * (double) re + (double) im * (double) im);
      }
      return energy;
    }

    // Compute total spectral energy across channels for a chunk (expects spectrum present)
    static double ComputeChunkSpectralEnergy(const AudioChunk& chunk)
    {
      const int chans = (int) chunk.complexSpectrum.size();
      if (chans <= 0 || chunk.fftSize <= 0) return 0.0;
      double total = 0.0;
      for (int ch = 0; ch < chans; ++ch)
      {
        const float* spec = chunk.complexSpectrum[ch].empty() ? nullptr : chunk.complexSpectrum[ch].data();
        if (!spec) continue;
        total += SpectrumEnergyOrdered(spec, chunk.fftSize);
      }
      return total;
    }

    static int NextValidFFTSize(int minN)
    {
      return Window::NextValidFFTSize(minN);
    }

    // In-place forward with windowing: time[N] * w[0..M-1] -> freq[Nfft] (ordered)
    void ForwardWindowed(const float* timeIn, int N, const float* w, int M, float* freqOut) const
    {
      if (!mSetup || !timeIn || !freqOut || N <= 0)
        return;

      for (int i = 0; i < mFFTSize; ++i)
      {
        float x = (i < N) ? timeIn[i] : 0.0f;
        if (w && i < M) x *= w[i];
        mScratch[i] = x;
      }
      pffft_transform_ordered(mSetup, mScratch.data(), freqOut, nullptr, PFFFT_FORWARD);
    }

    // In-place inverse: freq[N] (ordered) -> time[Nout]
    void Inverse(const float* freqIn, int Nfft, float* timeOut, int Nout) const
    {
      if (!mSetup || !freqIn || !timeOut || Nfft != mFFTSize || Nout <= 0)
        return;

      pffft_transform_ordered(mSetup, freqIn, mScratch.data(), nullptr, PFFFT_BACKWARD);

      // Copy out first Nout samples (PFFFT is not normalized; we divide by Nfft)
      const float invN = (mFFTSize > 0) ? (1.0f / (float)mFFTSize) : 1.0f;
      const int copyN = std::min(Nout, mFFTSize);
      for (int i = 0; i < copyN; ++i)
        timeOut[i] = mScratch[i] * invN;
      for (int i = copyN; i < Nout; ++i)
        timeOut[i] = 0.0f;
    }

    // Compute/ensure per-channel spectrum for chunk in-place
    // Removed non-windowed spectrum API to avoid duplicated code paths

    // Compute spectrum using a provided analysis window (Rectangular if empty)
    void ComputeChunkSpectrum(AudioChunk& chunk, const Window& window) const
    {
      const int chans = (int) chunk.channelSamples.size();
      if (chans <= 0 || mFFTSize <= 0) return;
      if (chunk.fftSize != mFFTSize)
      {
        chunk.fftSize = mFFTSize;
        chunk.complexSpectrum.assign(chans, std::vector<float>(mFFTSize, 0.0f));
      }
      else
      {
        if ((int)chunk.complexSpectrum.size() != chans)
          chunk.complexSpectrum.assign(chans, std::vector<float>(mFFTSize, 0.0f));
      }

      const auto& coeffs = window.Coeffs();
      const int M = (int) coeffs.size();

      for (int ch = 0; ch < chans; ++ch)
      {
        const auto& time = chunk.channelSamples[ch];
        float* spec = chunk.complexSpectrum[ch].data();
        const int N = std::min((int) time.size(), chunk.numFrames);
        for (int i = 0; i < mFFTSize; ++i)
        {
          float x = (i < N) ? (float) time[i] : 0.0f;
          if (M > 0 && i < M) x *= coeffs[i];
          mScratch[i] = x;
        }
        pffft_transform_ordered(mSetup, mScratch.data(), spec, nullptr, PFFFT_FORWARD);
      }
    }

    // IFFT back into the same chunk's samples, using its spectrum
    void ComputeChunkIFFT(AudioChunk& chunk) const
    {
      const int chans = (int) chunk.channelSamples.size();
      if (chans <= 0 || mFFTSize <= 0) return;

      double sumSquares = 0.0;
      int totalCount = 0;
      for (int ch = 0; ch < chans; ++ch)
      {
        const float* spec = (ch < (int)chunk.complexSpectrum.size())
          ? chunk.complexSpectrum[ch].data() : nullptr;
        if (!spec) continue;
        // Inverse into temp float buffer, then copy/convert to sample type
        std::vector<float> tmp((size_t) chunk.numFrames, 0.0f);
        Inverse(spec, mFFTSize, tmp.data(), chunk.numFrames);
        auto& out = chunk.channelSamples[ch];
        const int N = std::min((int)out.size(), chunk.numFrames);
        for (int i = 0; i < N; ++i)
        {
          const float v = tmp[i];
          out[i] = (iplug::sample) v;
          sumSquares += (double) v * (double) v;
        }
        totalCount += N;
      }
      chunk.rms = (totalCount > 0) ? std::sqrt(sumSquares / (double) totalCount) : 0.0;
    }

    static double DominantFreqHzFromOrderedSpectrum(const float* ordered, int Nfft, double sampleRate)
    {
      if (!ordered || Nfft <= 0 || sampleRate <= 0.0) return 0.0;
      int bestK = 0;
      float bestMag = -1.0f;
      // DC
      float mag0 = std::fabs(ordered[0]);
      if (mag0 > bestMag) { bestMag = mag0; bestK = 0; }
      // Nyquist
      float magNy = (Nfft >= 2) ? std::fabs(ordered[1]) : 0.0f;
      if (magNy > bestMag) { bestMag = magNy; bestK = Nfft/2; }
      // Other bins
      for (int k = 1; k < Nfft/2; ++k)
      {
        float re = ordered[2*k + 0];
        float im = ordered[2*k + 1];
        float mag = std::sqrt(re*re + im*im);
        if (mag > bestMag) { bestMag = mag; bestK = k; }
      }
      double hz = (double) bestK * sampleRate / (double) Nfft;
      const double ny = 0.5 * sampleRate;
      if (hz < 20.0) hz = 20.0;
      if (hz > ny - 20.0) hz = ny - 20.0;
      return hz;
    }

  private:
    void Destroy()
    {
      if (mSetup)
      {
        pffft_destroy_setup(mSetup);
        mSetup = nullptr;
      }
      mScratch.clear();
      mFFTSize = 0;
    }

  private:
    int mFFTSize = 0;
    PFFFT_Setup* mSetup = nullptr;
    mutable std::vector<float> mScratch; // reused across calls
  };
}


