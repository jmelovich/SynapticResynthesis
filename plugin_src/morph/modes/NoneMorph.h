#pragma once

#include "../IMorph.h"

namespace synaptic
{
  class NoneMorph final : public IMorph
  {
  public:
    void OnReset(double /*sampleRate*/, int /*fftSize*/, int /*numChannels*/) override {}

    void Process(AudioChunk& /*a*/, AudioChunk& /*b*/, FFTProcessor& /*fft*/) override {}

    bool IsActive() const override { return false; }
  };
}


