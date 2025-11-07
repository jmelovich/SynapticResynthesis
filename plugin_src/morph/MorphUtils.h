#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "../Structs.h" // for AudioChunk::ComplexSpectrum
#include <cmath>
#include <algorithm> // for std::min

namespace synaptic
{
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
