#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "SynapticResynthesis.h"

class FeatureAnalysis
{
public:
  static std::pair<float, float> FundamentalFrequency(float* input, int inputSize)
  {
    return {-1.0, -1.0}; // TODO
  }

  static float GetAffinity(float* input, int inputSize, int sampleRate, std::pair<float, float> fund)
  {
    auto peaks = GetPeaks(input, inputSize, sampleRate);
    return Affinity(peaks, fund);
  }

  static float GetSharpness(float* input, int inputSize, int sampleRate, std::pair<float, float> fund)
  {
    auto peaks = GetPeaks(input, inputSize, sampleRate);
    return Sharpness(peaks, fund);
  }

  static float GetHarmonicity(float* input, int inputSize, int sampleRate, std::pair<float, float> fund)
  {
    auto peaks = GetPeaks(input, inputSize, sampleRate);
    return Harmonicity(peaks, fund);
  }

  static float GetMonotony(float* input, int inputSize, int sampleRate, std::pair<float, float> fund)
  {
    auto peaks = GetPeaks(input, inputSize, sampleRate);
    return Monotony(peaks, fund);
  }

  static float GetMeanAffinity(float* input, int inputSize, int sampleRate, std::pair<float, float> fund)
  {
    auto peaks = GetPeaks(input, inputSize, sampleRate);
    return MeanAffinity(peaks, fund);
  }

  static float GetMeanContrast(float* input, int inputSize, int sampleRate, std::pair<float, float> fund)
  {
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
