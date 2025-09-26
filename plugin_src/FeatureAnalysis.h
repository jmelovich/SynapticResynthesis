#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "SynapticResynthesis.h"
#include <cfloat>
#include <cmath>
#include <utility>

class FeatureAnalysis
{
public:
  static std::pair<float, float> FundamentalFrequency(const float* input, int inputSize, float sampleRate, int nHarmonics = 6)
  {
    if (!input || inputSize < 4) // need at least DC, Nyquist, and 1 complex bin
      return {-1.f, -1.f};

    const int nBins = (inputSize + 2) / 2; // DC + Nyquist + (complex bins)
    if (nBins <= 2)
      return {-1.f, -1.f};

    const float binHz = sampleRate / inputSize; // bin resolution

    // Temporary storage on stack (real-time safe).
    // Limit max bins to avoid stack overflow in RT context.
    constexpr int MAX_BINS = 8192;
    if (nBins > MAX_BINS)
      return {-1.f, -1.f};

    float logMag[MAX_BINS];

    // Compute log magnitude spectrum
    const float eps = 1e-12f;
    logMag[0] = std::log(std::fabs(input[0]) + eps);         // DC
    logMag[nBins - 1] = std::log(std::fabs(input[1]) + eps); // Nyquist

    for (int k = 1; k < nBins - 1; k++)
    {
      float re = input[2 * k];
      float im = input[2 * k + 1];
      float mag = std::sqrt(re * re + im * im);
      logMag[k] = std::log(mag + eps);
    }

    // Harmonic Product Spectrum (log domain → sum)
    float bestVal = -FLT_MAX;
    int bestK = -1;

    // skip DC bin
    for (int k = 1; k < nBins; ++k)
    {
      float acc = 0.f;
      for (int h = 1; h <= nHarmonics; ++h)
      {
        int idx = k * h;
        if (idx >= nBins)
          break;
        acc += logMag[idx];
      }
      if (acc > bestVal)
      {
        bestVal = acc;
        bestK = k;
      }
    }

    if (bestK <= 0)
      return {-1.f, -1.f};

    float f0 = bestK * binHz;

    // Amplitude: recover from magnitude spectrum (linear domain, not log)
    float re = (bestK == nBins - 1) ? input[1] : input[2 * bestK];
    float im = (bestK == nBins - 1) ? 0.f : input[2 * bestK + 1];
    float amp = std::sqrt(re * re + im * im);

    return {f0, amp};
  }

  static std::vector<float> GetFeatures(float* input, int inputSize, int sampleRate) {
    auto fund = FundamentalFrequency(input, inputSize, sampleRate);
    auto peaks = GetPeaks(input, inputSize, sampleRate);

    std::vector<float> features(7, 0.0);
    features[0] = fund.first;
    features[1] = Affinity(peaks, fund);
    features[2] = Sharpness(peaks, fund);
    features[3] = Harmonicity(peaks, fund);
    features[4] = Monotony(peaks, fund);
    features[5] = MeanAffinity(peaks, fund);
    features[6] = MeanContrast(peaks, fund);

    return features;
  }

  static float GetAffinity(float* input, int inputSize, int sampleRate)
  {
    auto fund = FundamentalFrequency(input, inputSize, sampleRate);
    auto peaks = GetPeaks(input, inputSize, sampleRate);
    return Affinity(peaks, fund);
  }

  static float GetSharpness(float* input, int inputSize, int sampleRate)
  {
    auto fund = FundamentalFrequency(input, inputSize, sampleRate);
    auto peaks = GetPeaks(input, inputSize, sampleRate);
    return Sharpness(peaks, fund);
  }

  static float GetHarmonicity(float* input, int inputSize, int sampleRate)
  {
    auto fund = FundamentalFrequency(input, inputSize, sampleRate);
    auto peaks = GetPeaks(input, inputSize, sampleRate);
    return Harmonicity(peaks, fund);
  }

  static float GetMonotony(float* input, int inputSize, int sampleRate)
  {
    auto fund = FundamentalFrequency(input, inputSize, sampleRate);
    auto peaks = GetPeaks(input, inputSize, sampleRate);
    return Monotony(peaks, fund);
  }

  static float GetMeanAffinity(float* input, int inputSize, int sampleRate)
  {
    auto fund = FundamentalFrequency(input, inputSize, sampleRate);
    auto peaks = GetPeaks(input, inputSize, sampleRate);
    return MeanAffinity(peaks, fund);
  }

  static float GetMeanContrast(float* input, int inputSize, int sampleRate)
  {
    auto fund = FundamentalFrequency(input, inputSize, sampleRate);
    auto peaks = GetPeaks(input, inputSize, sampleRate);
    return MeanContrast(peaks, fund);
  }

private:

  static float Affinity(std::vector<std::pair<float, float>> peaks, std::pair<float, float> fund)
  {
    return sum_aifi(peaks) / (fund.first * sum_ai(peaks));
  }

  static float Sharpness(std::vector<std::pair<float, float>> peaks, std::pair<float, float> fund)
  {
    return fund.second / sum_ai(peaks);
  }

  static float Harmonicity(std::vector<std::pair<float, float>> peaks, std::pair<float, float> fund)
  {
    float harmonicity = 0;

    for (auto peak : peaks)
    {
      float m = peak.first / fund.first;
      harmonicity += m - std::floor(m);
    }

    return harmonicity;
  }

  static float Monotony(std::vector<std::pair<float, float>> peaks, std::pair<float, float> fund)
  {
    float monotony = 0;
    
    for (auto peak = peaks.begin(); peak < peaks.end() - 1; peak++)
    {
      float a_slope = (*(peak + 1)).second - (*peak).second;
      float f_slope = (*(peak + 1)).first - (*peak).first;

      monotony += a_slope / f_slope;
    }

    monotony *= fund.first / peaks.size();

    return monotony;
  }

  static float MeanAffinity(std::vector<std::pair<float, float>> peaks, std::pair<float, float> fund)
  {
    float meanAffinity = 0;

    float avgFreq = AverageFreq(peaks);
    for (auto peak : peaks)
    {
      meanAffinity += abs(peak.first - avgFreq);
    }

    meanAffinity /= peaks.size() * fund.first;

    return meanAffinity;
  }

  static float MeanContrast(std::vector<std::pair<float, float>> peaks, std::pair<float, float> fund)
  {
    float meanContrast = 0;
    for (auto peak : peaks)
    {
      meanContrast += abs(fund.second - peak.second);
    }

    meanContrast /= peaks.size();

    return meanContrast;
  }

  static float AverageFreq(std::vector<std::pair<float, float>> peaks) {
    float avg = 0;
    for (auto peak : peaks)
    {
      avg += peak.first;
    }
    avg /= peaks.size();
    return avg;
  }

  static float sum_ai(std::vector<std::pair<float, float>> peaks)
  {
    float ai = 0;
    for (auto peak : peaks)
    {
      ai += peak.second;
    }

    return ai;
  }

  static float sum_aifi(std::vector<std::pair<float, float>> peaks)
  {
    float aifi = 0;
    for (auto peak : peaks)
    {
      aifi += peak.first * peak.second;
    }

    return aifi;
  }

  // vector of frequency/magnitude pairs
  static std::vector<std::pair<float, float>> GetPeaks(float* input, int inputSize, int sampleRate)
  {
    float frequencyStep = (float)sampleRate / inputSize;
    std::vector<std::pair<float, float>> peaks;

    float prev = input[0];

    for (int i = 1; i <= inputSize/2; i++)
    {
      float mag = (i != inputSize/2)
        ? std::sqrt(input[2 * i] * input[2 * i] + input[2 * i + 1] * input[2 * i + 1])
        : input[1];

      if (mag < prev)
      {
        peaks.push_back({frequencyStep * (i - 1), prev});
      }
      else if (i == inputSize/2)
      {
        peaks.push_back({sampleRate / 2, mag});
      }

      prev = mag;
    }
  }
};
