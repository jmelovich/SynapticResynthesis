#pragma once

#include "../IMorph.h"

namespace synaptic
{
  class SpectralVocoderMorph final : public IMorph
  {
  public:
    void OnReset(double /*sampleRate*/, int /*fftSize*/, int /*numChannels*/) override {}

    void Process(AudioChunk& /*a*/, AudioChunk& /*b*/, FFTProcessor& /*fft*/) override {}

    void GetParamDescs(std::vector<ExposedParamDesc>& out) const override
    {
      out.clear();
      ExposedParamDesc p1;
      p1.id = "vocoderSensitivity";
      p1.label = "Vocoder Sensitivity";
      p1.type = ParamType::Number;
      p1.control = ControlType::Slider;
      p1.minValue = 0.0; p1.maxValue = 1.0; p1.step = 0.01; p1.defaultNumber = 1.0;
      out.push_back(p1);
    }

    bool SetParamFromNumber(const std::string& id, double v) override
    {
      if (id == "vocoderSensitivity") { mSensitivity = v; return true; }
      return false;
    }

    bool GetParamAsNumber(const std::string& id, double& out) const override
    {
      if (id == "vocoderSensitivity") { out = mSensitivity; return true; }
      return false;
    }

  private:
    double mSensitivity = 1.0;
  };
}


