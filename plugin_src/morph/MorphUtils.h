#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "../Structs.h" // for AudioChunk::ComplexSpectrum
#include "../audio/FFT.h"
#include <cmath>
#include <algorithm> // for std::min

namespace synaptic
{
  struct CepstralScratch
  {
    std::vector<float> logMagA;
    std::vector<float> logMagB;
    std::vector<float> cepA;
    std::vector<float> cepB;
    std::vector<float> cepC;
    std::vector<float> logMagC;

    void EnsureSize(int N)
    {
      if ((int) logMagA.size() != N) logMagA.assign((size_t) N, 0.0f);
      if ((int) logMagB.size() != N) logMagB.assign((size_t) N, 0.0f);
      if ((int) cepA.size()    != N) cepA.assign((size_t) N, 0.0f);
      if ((int) cepB.size()    != N) cepB.assign((size_t) N, 0.0f);
      if ((int) cepC.size()    != N) cepC.assign((size_t) N, 0.0f);
      if ((int) logMagC.size() != N) logMagC.assign((size_t) N, 0.0f);
    }
  };

  // Shared cross-synthesis implementation, migrated from legacy Morph class
  inline void CrossSynthesisApply(
    std::vector<std::vector<float>>& a,
    std::vector<std::vector<float>>& b,
    int fftSize, float morphAmount, float phaseMorphAmount)
  {
    const int numChannels = (int) std::min(a.size(), b.size());

    const float magAmt = morphAmount;
    const float phaseAmt = phaseMorphAmount;
    const float oneMinusMagAmt = 1.0f - morphAmount;
    const float oneMinusPhaseAmt = 1.0f - phaseMorphAmount;

    for (int c = 0; c < numChannels; c++)
    {
      const float* __restrict aptr = a[c].data();
      float* __restrict bptr = b[c].data();

      bptr[0] = bptr[0] * magAmt + aptr[0] * oneMinusMagAmt; // dc
      bptr[1] = bptr[1] * magAmt + aptr[1] * oneMinusMagAmt; // nyquist

      for (int i = 2; i < fftSize; i += 2) // for interleaved real and imaginary
      {
        const float ma = sqrtf(aptr[i] * aptr[i] + aptr[i + 1] * aptr[i + 1]);
        const float mb = sqrtf(bptr[i] * bptr[i] + bptr[i + 1] * bptr[i + 1]);

        const float m = expf(oneMinusMagAmt * logf(ma + 1e-20f) + magAmt * logf(mb + 1e-20f));

        const float inv_ma = ma > 1e-12f ? 1.0f / ma : 0.0f;
        const float inv_mb = mb > 1e-12f ? 1.0f / mb : 0.0f;

        const float ua_r = aptr[i    ] * inv_ma;
        const float ua_i = aptr[i + 1] * inv_ma;
        const float ub_r = bptr[i    ] * inv_mb;
        const float ub_i = bptr[i + 1] * inv_mb;

        float u_r = oneMinusPhaseAmt * ua_r + phaseAmt * ub_r;
        float u_i = oneMinusPhaseAmt * ua_i + phaseAmt * ub_i;

        const float norm = 1.0f / sqrtf(u_r * u_r + u_i * u_i + 1e-20f);
        u_r *= norm;
        u_i *= norm;

        bptr[i    ] = m * u_r;
        bptr[i + 1] = m * u_i;
      }
    }
  }

  // Cepstral Morph Apply, used in Cross Synthesis Morph and Wave Morph
  inline void CepstralApply(std::vector<std::vector<float>>& a,
                            std::vector<std::vector<float>>& b,
                            int fftSize,
                            float morphAmount,
                            float phaseMorphAmount,
                            FFTProcessor& fft,
                            CepstralScratch& scratch)
  {
    const int numChannels = (int)std::min(a.size(), b.size());

    const float magAmt = morphAmount;
    const float phaseAmt = phaseMorphAmount;
    const float oneMinusMagAmt = 1.0f - morphAmount;
    const float oneMinusPhaseAmt = 1.0f - phaseMorphAmount;

    if (fftSize <= 0 || numChannels <= 0)
      return;

    scratch.EnsureSize(fftSize);

    for (int c = 0; c < numChannels; c++)
    {
      const float* __restrict aptr = a[c].data();
      float* __restrict bptr = b[c].data();

      // 1) Build Log Magnitude spectra (real-only) for a and b from one-sided complex spectra
      // DC and Nyquist are real-only
      scratch.logMagA[0] = logf(std::fabs(aptr[0]) + 1e-20f);
      scratch.logMagB[0] = logf(std::fabs(bptr[0]) + 1e-20f);
      scratch.logMagA[1] = logf(std::fabs(aptr[1]) + 1e-20f);
      scratch.logMagB[1] = logf(std::fabs(bptr[1]) + 1e-20f);

      for (int i = 2; i < fftSize; i += 2) // interleaved real/imag bins for k=1..N/2-1
      {
        const float a_re = aptr[i    ];
        const float a_im = aptr[i + 1];
        const float b_re = bptr[i    ];
        const float b_im = bptr[i + 1];

        const float ma = sqrtf(a_re * a_re + a_im * a_im);
        const float mb = sqrtf(b_re * b_re + b_im * b_im);

        scratch.logMagA[i    ] = logf(ma + 1e-20f);
        scratch.logMagA[i + 1] = 0.0f; // real-only
        scratch.logMagB[i    ] = logf(mb + 1e-20f);
        scratch.logMagB[i + 1] = 0.0f; // real-only
      }

      // 2) Real IFFT of log magnitude spectra -> cepstra (FFTProcessor::Inverse scales by Nfft)
      fft.Inverse(scratch.logMagA.data(), fftSize, scratch.cepA.data(), fftSize);
      fft.Inverse(scratch.logMagB.data(), fftSize, scratch.cepB.data(), fftSize);

      // 3) Crossfade cepstra based on magnitude morph amount
      for (int n = 0; n < fftSize; ++n)
        scratch.cepC[n] = oneMinusMagAmt * scratch.cepA[n] + magAmt * scratch.cepB[n];

      // 4) Real FFT of crossfaded cepstrum -> combined log magnitude spectrum
      // Use forward without window
      fft.ForwardWindowed(scratch.cepC.data(), fftSize, nullptr, 0, scratch.logMagC.data());

      // 5) Phase crossfade (unit phasor morph) and scale by exp of resulting log magnitude
      // Handle DC and Nyquist magnitudes directly (no phase)
      bptr[0] = expf(scratch.logMagC[0]);
      bptr[1] = expf(scratch.logMagC[1]);

      for (int i = 2; i < fftSize; i += 2)
      {
        const float a_re = aptr[i    ];
        const float a_im = aptr[i + 1];
        const float b_re = bptr[i    ];
        const float b_im = bptr[i + 1];

        const float ma = sqrtf(a_re * a_re + a_im * a_im);
        const float mb = sqrtf(b_re * b_re + b_im * b_im);
        const float inv_ma = (ma > 1e-12f) ? (1.0f / ma) : 0.0f;
        const float inv_mb = (mb > 1e-12f) ? (1.0f / mb) : 0.0f;

        const float ua_r = a_re * inv_ma;
        const float ua_i = a_im * inv_ma;
        const float ub_r = b_re * inv_mb;
        const float ub_i = b_im * inv_mb;

        float u_r = oneMinusPhaseAmt * ua_r + phaseAmt * ub_r;
        float u_i = oneMinusPhaseAmt * ua_i + phaseAmt * ub_i;
        const float norm = 1.0f / sqrtf(u_r * u_r + u_i * u_i + 1e-20f);
        u_r *= norm;
        u_i *= norm;

        const float mCombined = expf(scratch.logMagC[i]); // take real part as log magnitude

        bptr[i    ] = mCombined * u_r;
        bptr[i + 1] = mCombined * u_i;
      }
    }
  }

  // Helper functions for harmonic synthesis (migrated from legacy Morph)
  inline std::pair<double, double> SquareNthHarmonic(double r, double i, int n) {
    if (n % 2 == 0)
      return {0.0, 0.0};

    double amplitude = 1.0 / n;

    return {r * amplitude, i * amplitude};
  }

  inline std::pair<double, double> TriangleNthHarmonic(double r, double i, int n) {
    if (n % 2 == 0)
      return {0.0, 0.0}; // even harmonics are zero

    double rescale = 8.0 / (M_PI * M_PI);
    int k = (n - 1) / 2;                     // 0,1,2,...
    double sign = (k % 2 == 0) ? 1.0 : -1.0; // (-1)^{k} == (-1)^{(n-1)/2}
    double factor = sign * rescale / (double)(n * n);  // 1/n^2 with alternating sign

    return {r * factor, i * factor};
  }

  inline std::pair<double, double> SawtoothNthHarmonic(double r, double i, int n)
  {
    double rescale = 2.0 / M_PI;
    double sign = ((n + 1) % 2 == 0) ? 1.0 : -1.0;
    double factor = sign * rescale / static_cast<double>(n);

    return {r * factor, i * factor};
  }
}
