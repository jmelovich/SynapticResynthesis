#pragma once

#include "../IMorph.h"
#include "../MorphUtils.h"
#include <utility>

namespace synaptic
{
  class WaveMorph final : public IMorph
  {
  public:
    enum class MorphDomain
    {
      Log,
      Cepstral
    };

    enum WaveMorphShape
    {
      Square,
      Saw,
      Triangle,
    };

    void OnReset(double /*sampleRate*/, int fftSize, int /*numChannels*/) override
    {
      mCepstralScratch.EnsureSize(fftSize);
    }

    void Process(AudioChunk& a, AudioChunk& b, FFTProcessor& fft) override
    {
      const int fftSize = b.fftSize;
      if (fftSize <= 0) return;

      const int numChannels = (int) std::min(a.complexSpectrum.size(), b.complexSpectrum.size());
      if (numChannels <= 0) return;

      std::pair<double, double> partial = {0, 0};

      int minHarmonic = std::floor(std::max(fftSize * mWaveMorphStart / 2, 1.0));

      for (int c = 0; c < numChannels; c++)
      {
        float* __restrict aptr = a.complexSpectrum[c].data();
        float* __restrict bptr = b.complexSpectrum[c].data();

        for (int i = minHarmonic; i < fftSize / 2; i++)
        {
          for (int k = 2; k < mWaveHarmonics; k++)
          {
            int currHarm = 2 * i * k;
            if (currHarm >= fftSize)
              break;

            partial = GetHarmonic(aptr[2 * i], aptr[2 * i + 1], k);
            aptr[currHarm]     -= partial.first;
            aptr[currHarm + 1] -= partial.second;

            partial = GetHarmonic(bptr[2 * i], bptr[2 * i + 1], k);
            bptr[currHarm]     -= partial.first;
            bptr[currHarm + 1] -= partial.second;
          }
        }
      }

      // Apply cross synthesis after removing harmonics
      if (mDomain == MorphDomain::Log)
      {
        LogApply(a.complexSpectrum, b.complexSpectrum, fftSize, (float)mMorphAmount, (float)mPhaseMorphAmount);
      }
      else
      {
        CepstralApply(a.complexSpectrum, b.complexSpectrum, fftSize,
                      (float) mMorphAmount, (float) mPhaseMorphAmount, (float) mEmphasis,
                      fft, mCepstralScratch);
      }

      for (int c = 0; c < numChannels; c++)
      {
        float* __restrict bptr = b.complexSpectrum[c].data();

        for (int i = fftSize / 2 - 1; i >= minHarmonic; i--)
        {
          for (int k = 2; k < mWaveHarmonics; k++)
          {
            int currHarm = 2 * i * k;
            if (currHarm >= fftSize)
              break;

            partial = GetHarmonic(bptr[2 * i], bptr[2 * i + 1], k);
            bptr[currHarm] += partial.first;
            bptr[currHarm + 1] += partial.second;
          }
        }
      }
    }

    void GetParamDescs(std::vector<ExposedParamDesc>& out) const override
    {
      out.clear();
      ExposedParamDesc p1;
      p1.id = "waveMorphStart";
      p1.label = "Wave Start Freq";
      p1.type = ParamType::Number;
      p1.control = ControlType::Slider;
      p1.minValue = 0.0; p1.maxValue = 1.0; p1.step = 0.01; p1.defaultNumber = 0.03;
      out.push_back(p1);

      ExposedParamDesc p2;
      p2.id = "waveHarmonics";
      p2.label = "Wave Harmonics";
      p2.type = ParamType::Number;
      p2.control = ControlType::NumberBox;
      p2.minValue = 2; p2.maxValue = 100; p2.step = 1; p2.defaultNumber = 20;
      out.push_back(p2);

      ExposedParamDesc p3;
      p3.id = "morphAmount";
      p3.label = "Morph Amount";
      p3.type = ParamType::Number;
      p3.control = ControlType::Slider;
      p3.minValue = 0.0; p3.maxValue = 1.0; p3.step = 0.01; p3.defaultNumber = 1.0;
      out.push_back(p3);

      ExposedParamDesc p4;
      p4.id = "phaseMorphAmount";
      p4.label = "Phase Morph Amount";
      p4.type = ParamType::Number;
      p4.control = ControlType::Slider;
      p4.minValue = 0.0; p4.maxValue = 1.0; p4.step = 0.01; p4.defaultNumber = 1.0;
      out.push_back(p4);

      ExposedParamDesc p5;
      p5.id = "waveShape";
      p5.label = "Wave Shape";
      p5.type = ParamType::Enum;
      p5.control = ControlType::Select;
      p5.options.push_back({"square", "Square"});
      p5.options.push_back({"saw", "Sawtooth"});
      p5.options.push_back({"triangle", "Triangle"});
      out.push_back(p5);

      ExposedParamDesc p6;
      p6.id = "morphDomain";
      p6.label = "Morph Domain";
      p6.type = ParamType::Enum;
      p6.control = ControlType::Select;
      p6.options.push_back({"log", "Log"});
      p6.options.push_back({"cepstral", "Cepstral"});
      p6.defaultString = "log";
      out.push_back(p6);
      
      ExposedParamDesc p7;
      p7.id = "emphasis";
      p7.label = "Emphasis";
      p7.type = ParamType::Number;
      p7.control = ControlType::Slider;
      p7.minValue = 0.0; p7.maxValue = 1.0; p7.step = 0.01; p7.defaultNumber = 0.0;
      out.push_back(p7);
    }

    bool SetParamFromNumber(const std::string& id, double v) override
    {
      if (id == "waveMorphStart") { mWaveMorphStart = v; return true; }
      if (id == "waveHarmonics") { mWaveHarmonics = (int)v; return true; }
      if (id == "morphAmount") { mMorphAmount = v; return true; }
      if (id == "phaseMorphAmount") { mPhaseMorphAmount = v; return true; }
      if (id == "emphasis") { mEmphasis = v; return true; }
      return false;
    }

    bool GetParamAsNumber(const std::string& id, double& out) const override
    {
      if (id == "waveMorphStart") { out = mWaveMorphStart; return true; }
      if (id == "waveHarmonics") { out = mWaveHarmonics; return true; }
      if (id == "morphAmount") { out = mMorphAmount; return true; }
      if (id == "phaseMorphAmount") { out = mPhaseMorphAmount; return true; }
      if (id == "emphasis") { out = mEmphasis; return true; }
      return false;
    }

    bool SetParamFromString(const std::string& id, const std::string& v) override
    {
      if (id == "waveShape")
      {
        if (v == "square") mWaveShape = Square;
        else if (v == "saw") mWaveShape = Saw;
        else if (v == "triangle") mWaveShape = Triangle;
        return true;
      }
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
      if (id == "waveShape")
      {
        if (mWaveShape == Square) out = "square";
        else if (mWaveShape == Saw) out = "saw";
        else if (mWaveShape == Triangle) out = "triangle";
        return true;
      }
      if (id == "morphDomain")
      {
        out = (mDomain == MorphDomain::Log) ? "log" : "cepstral";
        return true;
      }
      return false;
    }

  private:
    std::pair<double, double> GetHarmonic(double r, double i, int n)
    {
      switch (mWaveShape)
      {
        case Square: return SquareNthHarmonic(r, i, n);
        case Saw: return SawtoothNthHarmonic(r, i, n);
        case Triangle: return TriangleNthHarmonic(r, i, n);
      }
      return {0.0, 0.0};
    }

    WaveMorphShape mWaveShape = Square;
    double mWaveMorphStart = 0.03;
    int mWaveHarmonics = 20;
    double mMorphAmount = 1.0;
    double mPhaseMorphAmount = 1.0;
    double mEmphasis = 0.0;
    MorphDomain mDomain = MorphDomain::Log;
    CepstralScratch mCepstralScratch;
  };
}
