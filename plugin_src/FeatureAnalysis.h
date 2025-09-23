#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "SynapticResynthesis.h"

class FeatureAnalysis
{
  static std::pair<float, float> FundamentalFrequency(float* input, int inputSize)
  {
    return {-1.0, -1.0}; // TODO
  }

  static float Affinity(float* input, int inputSize, int sampleRate, std::pair<float, float> fund) {
    float frequencyStep = (float)sampleRate / inputSize;

    float aifi = input[1] * (sampleRate / 2);
    for (int i = 1; i < inputSize; i++)
    {
      float mag = std::sqrt(input[2 * i] * input[2 * i] + input[2 * i + 1] * input[2 * i + 1]);
      aifi += mag * frequencyStep * i;
    }

    float ai = 0;
    for (int i = 1; i < inputSize; i++)
    {
      float mag = std::sqrt(input[2 * i] * input[2 * i] + input[2 * i + 1] * input[2 * i + 1]);
      ai += mag;
    }

    return aifi / (fund.first * ai);
  }

  static float Sharpness(float* input, int inputSize, int sampleRate, std::pair<float, float> fund) {
    return -1.0; // TODO
  }

  static float Harmonicity(float* input, int inputSize, int sampleRate, std::pair<float, float> fund) {
    return -1.0; // TODO
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
};
