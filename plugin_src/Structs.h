#pragma once

#include <vector>
#include <cstdint>

#include "IPlug_include_in_plug_hdr.h"

namespace synaptic
{
  struct AudioChunk
  {
    std::vector<std::vector<iplug::sample>> channelSamples; // [channel][frame]
    int numFrames = 0;
    double rms = 0.0;  // RMS of this chunk's audio
    int64_t startSample = -1;     // timeline position for alignment (could be useful)
    // Spectral data: PFFFT-ordered complex spectrum, length fftSize per channel
    int fftSize = 0;
    std::vector<std::vector<float>> complexSpectrum; // [channel][fftSize]
  };

}

// Global parameter enum
enum EParams
{
  // Fixed, non-dynamic parameters
  kInGain = 0,
  kChunkSize,
  kBufferWindow,
  kAlgorithm,
  kOutputWindow,
  kDirtyFlag,
  kAnalysisWindow,
  kEnableOverlap,
  kOutGain,
  kAGC,
  kAutotuneBlend,
  kAutotuneMode,
  kAutotuneToleranceOctaves,
  kMorphMode,
  kWindowLock,
  // Dynamic transformer parameters are indexed after this sentinel
  kNumParams
};
