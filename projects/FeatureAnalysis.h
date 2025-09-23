#pragma once

#include "IPlug_include_in_plug_hdr.h"

class FeatureAnalysis
{
  // There is potential for frequecy detection to be done on the spectrum
  // OR it can be done on the live audio, which will offer a much more accurate analysis
  // This function is drafted for the spectrum analysis...
  // but for now the fund frequency is passed as a parameter

  //static float FundamentalFrequency(float* input, int inputSize) {
  //  return -1.0;
  //}

  static float Affinity(float* input, int inputSize, float f0) {
    return -1.0;
  }

  static float Sharpness(float* input, int inputSize, float f0) {
    return -1.0;
  }

  static float Harmonicity(float* input, int inputSize, float f0) {
    return -1.0;
  }

  static float Monotony(float* input, int inputSize, float f0) {
    return -1.0;
  }

  static float MeanAffinity(float* input, int inputSize, float f0) {
    return -1.0;
  }

  static float MeanContrast(float* input, int inputSize, float f0) {
    return -1.0;
  }
};
