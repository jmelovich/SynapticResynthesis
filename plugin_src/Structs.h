/**
 * @file Structs.h
 * @brief Core data structures for audio processing
 *
 * Defines the AudioChunk structure used throughout the plugin
 * for representing chunks of audio data with spectral information.
 */

#pragma once

#include <vector>
#include <cstdint>

#include "IPlug_include_in_plug_hdr.h"
#include "plugin_src/params/ParameterIds.h"

namespace synaptic
{
  /**
   * @brief A chunk of audio data with optional spectral representation
   */
  struct AudioChunk
  {
    std::vector<std::vector<iplug::sample>> channelSamples; // [channel][frame]
    int numFrames = 0;
    double rms = 0.0;  // RMS of this chunk's audio
    int64_t startSample = -1;     // timeline position for alignment
    // Spectral data: PFFFT-ordered complex spectrum, length fftSize per channel
    int fftSize = 0;
    std::vector<std::vector<float>> complexSpectrum; // [channel][fftSize]
  };
}
