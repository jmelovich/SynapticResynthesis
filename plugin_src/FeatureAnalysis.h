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

  static float Affinity(float* input, int inputSize, int sampleRate, std::pair<float, float> fund) {
    return sum_aifi(input, inputSize, sampleRate) / (fund.first * sum_ai(input, inputSize));
  }

  static float Sharpness(float* input, int inputSize, int sampleRate, std::pair<float, float> fund) {
    return fund.second / sum_ai(input, inputSize);
  }

  static float Harmonicity(float* input, int inputSize, int sampleRate, std::pair<float, float> fund) {
    float frequencyStep = (float)sampleRate / inputSize;

    float m = ((sampleRate / 2) / fund.first);
    float harmonicity = m - std::floor(m);

    for (int i = 1; i < inputSize; i++)
    {
      m = frequencyStep*i / fund.first;
      harmonicity += m - std::floor(m);
    }

    return harmonicity;
  }

  static float Monotony(float* input, int inputSize, int sampleRate, std::pair<float, float> fund) {
    return -1.0; // TODO
  }

  static float MeanAffinity(float* input, int inputSize, int sampleRate, std::pair<float, float> fund) {
    return -1.0; // TODO
  }

  static float MeanContrast(float* input, int inputSize, int sampleRate, std::pair<float, float> fund) {
    return -1.0; // TODO
  }

  private:
  static float sum_ai(float* input, int inputSize)
  {
    float ai = input[1]; // nyquist (per pffft implementation)
    for (int i = 1; i < inputSize; i++)
    {
      float mag = std::sqrt(input[2 * i] * input[2 * i] + input[2 * i + 1] * input[2 * i + 1]);
      ai += mag;
    }

    return ai;
  }

  static float sum_aifi(float* input, int inputSize, int sampleRate)
  {
    float frequencyStep = (float)sampleRate / inputSize;

    float aifi = input[1] * (sampleRate / 2); // nyquist (per pffft implementation)
    for (int i = 1; i < inputSize; i++)
    {
      float mag = std::sqrt(input[2 * i] * input[2 * i] + input[2 * i + 1] * input[2 * i + 1]);
      aifi += mag * frequencyStep * i;
    }

    return aifi;
  }
};
