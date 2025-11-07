#pragma once

#include "../IMorph.h"
#include "../MorphUtils.h"

namespace synaptic
{
  class CrossSynthesisMorph final : public IMorph
  {
  public:
    void OnReset(double /*sampleRate*/, int /*fftSize*/, int /*numChannels*/) override {}

    void Process(AudioChunk& a, AudioChunk& b, FFTProcessor& /*fft*/) override
    {
      if (b.fftSize <= 0) return;
      CrossSynthesisApply(a.complexSpectrum, b.complexSpectrum, b.fftSize,
                          (float) mMorphAmount, (float) mPhaseMorphAmount);
    }

    void GetParamDescs(std::vector<ExposedParamDesc>& out) const override
    {
      out.clear();
      ExposedParamDesc p1;
      p1.id = "morphAmount";
      p1.label = "Morph Amount";
      p1.type = ParamType::Number;
      p1.control = ControlType::Slider;
      p1.minValue = 0.0; p1.maxValue = 1.0; p1.step = 0.01; p1.defaultNumber = 1.0;
      out.push_back(p1);

      ExposedParamDesc p2;
      p2.id = "phaseMorphAmount";
      p2.label = "Phase Morph Amount";
      p2.type = ParamType::Number;
      p2.control = ControlType::Slider;
      p2.minValue = 0.0; p2.maxValue = 1.0; p2.step = 0.01; p2.defaultNumber = 1.0;
      out.push_back(p2);
    }

    bool SetParamFromNumber(const std::string& id, double v) override
    {
      if (id == "morphAmount") { mMorphAmount = v; return true; }
      if (id == "phaseMorphAmount") { mPhaseMorphAmount = v; return true; }
      return false;
    }

    bool GetParamAsNumber(const std::string& id, double& out) const override
    {
      if (id == "morphAmount") { out = mMorphAmount; return true; }
      if (id == "phaseMorphAmount") { out = mPhaseMorphAmount; return true; }
      return false;
    }

  private:
    double mMorphAmount = 1.0;
    double mPhaseMorphAmount = 1.0;
  };
}


