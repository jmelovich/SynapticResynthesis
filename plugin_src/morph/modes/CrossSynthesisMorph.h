#pragma once

#include "../IMorph.h"
#include "../MorphUtils.h"

namespace synaptic
{
  class CrossSynthesisMorph final : public IMorph
  {
  public:
    enum class MorphDomain
    {
      Log,
      Cepstral
    };

    void OnReset(double /*sampleRate*/, int fftSize, int /*numChannels*/) override
    {
      mCepstralScratch.EnsureSize(fftSize);
    }

    void Process(AudioChunk& a, AudioChunk& b, FFTProcessor& fft) override
    {
      if (b.fftSize <= 0) return;

      if (mDomain == MorphDomain::Log)
      {
        LogApply(a.complexSpectrum, b.complexSpectrum, b.fftSize,
                            (float) mMorphAmount, (float) mPhaseMorphAmount);
      }
      else
      {
        CepstralApply(a.complexSpectrum, b.complexSpectrum, b.fftSize,
                      (float) mMorphAmount, (float) mPhaseMorphAmount, (float) mEmphasis,
                      fft, mCepstralScratch);
      }
    }

    void GetParamDescs(std::vector<ExposedParamDesc>& out, bool includeAll = false) const override
    {
      out.clear();
      ExposedParamDesc p1;
      p1.id = "morphAmount";
      p1.label = "Morph Amount";
      p1.tooltip = "Blends magnitude spectrum between source and transformed chunks. 0 = source only, 1 = transformed only.";
      p1.type = ParamType::Number;
      p1.control = ControlType::Slider;
      p1.minValue = 0.0; p1.maxValue = 1.0; p1.step = 0.01; p1.defaultNumber = 1.0;
      out.push_back(p1);

      ExposedParamDesc p2;
      p2.id = "phaseMorphAmount";
      p2.label = "Phase Morph Amount";
      p2.tooltip = "Blends phase spectrum between source and transformed chunks. Affects timing and transient preservation.";
      p2.type = ParamType::Number;
      p2.control = ControlType::Slider;
      p2.minValue = 0.0; p2.maxValue = 1.0; p2.step = 0.01; p2.defaultNumber = 1.0;
      out.push_back(p2);

      ExposedParamDesc p3;
      p3.id = "morphDomain";
      p3.label = "Morph Domain";
      p3.tooltip = "Choose morphing domain: Log (logarithmic magnitude) or Cepstral (cepstral coefficients). Cepstral allows finer control with Emphasis parameter.";
      p3.type = ParamType::Enum;
      p3.control = ControlType::Select;
      p3.options.push_back({"log", "Log"});
      p3.options.push_back({"cepstral", "Cepstral"});
      p3.defaultString = "log";
      out.push_back(p3);

      // Only show Emphasis when domain is Cepstral (unless includeAll is true)3
      // 'includeAll' is used when getting all params for binding
      // so when 'includeAll' is true, we should make sure all possible parameters are returned
      if(includeAll || mDomain == MorphDomain::Cepstral)
      {
        ExposedParamDesc p4;
        p4.id = "emphasis";
        p4.label = "Emphasis";
        p4.tooltip = "Emphasis factor for cepstral morphing. Higher values emphasize formant structure and timbral characteristics.";
        p4.type = ParamType::Number;
        p4.control = ControlType::Slider;
        p4.minValue = 0.0; p4.maxValue = 1.0; p4.step = 0.01; p4.defaultNumber = 0.0;
        out.push_back(p4);
      }
    }

    bool SetParamFromNumber(const std::string& id, double v) override
    {
      if (id == "morphAmount") { mMorphAmount = v; return true; }
      if (id == "phaseMorphAmount") { mPhaseMorphAmount = v; return true; }
      if (id == "emphasis") { mEmphasis = v; return true; }
      return false;
    }

    bool GetParamAsNumber(const std::string& id, double& out) const override
    {
      if (id == "morphAmount") { out = mMorphAmount; return true; }
      if (id == "phaseMorphAmount") { out = mPhaseMorphAmount; return true; }
      if (id == "emphasis") { out = mEmphasis; return true; }
      return false;
    }

    bool SetParamFromString(const std::string& id, const std::string& v) override
    {
      if (id == "morphDomain")
      {
        if (v == "log") mDomain = MorphDomain::Log;
        else if (v == "cepstral") mDomain = MorphDomain::Cepstral;
        return true;
      }
      return false;
    }

    bool GetParamAsString(const std::string& id, std::string& out) const override
    {
      if (id == "morphDomain")
      {
        out = (mDomain == MorphDomain::Log) ? "log" : "cepstral";
        return true;
      }
      return false;
    }

    bool ParamChangeRequiresUIRebuild(const std::string& id) const override
    {
      // Changing morph domain requires UI rebuild because it controls visibility of the Emphasis parameter
      return (id == "morphDomain");
    }

  private:
    double mMorphAmount = 1.0;
    double mPhaseMorphAmount = 1.0;
    double mEmphasis = 0.0;
    MorphDomain mDomain = MorphDomain::Log;
    CepstralScratch mCepstralScratch;
  };
}


