#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <memory>
#include "../params/DynamicParamSchema.h"
#include "../Structs.h" // for AudioChunk

namespace synaptic
{
  class FFTProcessor; // fwd decl

  struct IMorph : public IDynamicParamOwner
  {
    virtual ~IMorph() {}

    virtual void OnReset(double sampleRate, int fftSize, int numChannels) = 0;

    // Applies morphing to input/output audio in the spectral domain as needed
    virtual void Process(AudioChunk& a, AudioChunk& b, FFTProcessor& fft) = 0;

    // Whether this morph engages spectral processing (controls windowing/OLA decisions)
    virtual bool IsActive() const { return true; }

    // Default empty dynamic param implementation
    void GetParamDescs(std::vector<ExposedParamDesc>& out) const override { out.clear(); }
    bool GetParamAsNumber(const std::string&, double&) const override { return false; }
    bool GetParamAsBool(const std::string&, bool&) const override { return false; }
    bool GetParamAsString(const std::string&, std::string&) const override { return false; }
    bool SetParamFromNumber(const std::string&, double) override { return false; }
    bool SetParamFromBool(const std::string&, bool) override { return false; }
    bool SetParamFromString(const std::string&, const std::string&) override { return false; }
  };
}


