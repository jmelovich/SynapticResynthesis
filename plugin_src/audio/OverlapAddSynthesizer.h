/**
 * @file OverlapAddSynthesizer.h
 * @brief Overlap-add synthesis for audio reconstruction
 *
 * Handles the overlap-add (OLA) synthesis process for combining
 * overlapping audio chunks into a continuous output stream.
 * Extracted from AudioStreamChunker to improve code organization.
 */

#pragma once

#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "IPlug_include_in_plug_hdr.h"
#include "Window.h"
#include "../Structs.h"

namespace synaptic
{

/**
 * @brief Manages overlap-add synthesis for audio reconstruction
 *
 * Accumulates windowed audio chunks and produces a continuous output stream
 * with proper overlap handling and rescaling.
 */
class OverlapAddSynthesizer
{
public:
  /**
   * @brief Configure the synthesizer
   * @param numChannels Number of audio channels
   * @param chunkSize Size of each chunk in samples
   */
  void Configure(int numChannels, int chunkSize)
  {
    mNumChannels = std::max(1, numChannels);
    mChunkSize = std::max(1, chunkSize);
    mOverlapBuffer.assign(mNumChannels, std::vector<iplug::sample>(mChunkSize * 2, 0.0));
    Reset();
  }

  /**
   * @brief Reset the synthesizer state
   */
  void Reset()
  {
    mValidSamples = 0;
    for (auto& ch : mOverlapBuffer)
      std::fill(ch.begin(), ch.end(), 0.0);
  }

  /**
   * @brief Add a chunk to the overlap buffer with windowing
   * @param chunk Audio chunk to add
   * @param windowCoeffs Window coefficients (nullptr for no windowing)
   * @param gain AGC gain to apply
   * @param hopSize Hop size for overlap positioning
   */
  void AddChunk(const AudioChunk& chunk, const std::vector<float>* windowCoeffs,
                float gain, int hopSize)
  {
    const int frames = chunk.numFrames;
    if (frames <= 0) return;

    // Compute add position based on settled samples
    const int settledStride = std::max(0, mChunkSize - hopSize);
    const int addPos = (mValidSamples >= settledStride) ? (mValidSamples - settledStride) : 0;
    const int requiredSize = addPos + frames;

    // Ensure buffer capacity
    EnsureCapacity(requiredSize);

    // Add windowed samples
    const int chans = std::min(mNumChannels, static_cast<int>(chunk.channelSamples.size()));
    for (int ch = 0; ch < chans; ++ch)
    {
      const auto& src = chunk.channelSamples[ch];
      auto& dst = mOverlapBuffer[ch];

      for (int i = 0; i < frames && i < static_cast<int>(src.size()); ++i)
      {
        if (addPos + i < static_cast<int>(dst.size()))
        {
          float w = 1.0f;
          if (windowCoeffs && i < static_cast<int>(windowCoeffs->size()))
            w = (*windowCoeffs)[i];
          dst[addPos + i] += src[i] * w * gain;
        }
      }
    }

    mValidSamples = requiredSize;
  }

  /**
   * @brief Render output samples from the overlap buffer
   * @param outputs Output buffer pointers [channel][sample]
   * @param nFrames Number of frames to render
   * @param outChans Number of output channels
   * @param rescale Rescale factor for normalization
   * @param maxSamplesToRender Maximum samples that can be rendered (latency control)
   * @return Number of frames actually rendered
   */
  int RenderOutput(iplug::sample** outputs, int nFrames, int outChans,
                   float rescale, int64_t maxSamplesToRender)
  {
    const int chansToWrite = std::min(outChans, mNumChannels);
    const int framesToCopy = static_cast<int>(std::min({
      static_cast<int64_t>(nFrames),
      static_cast<int64_t>(mValidSamples),
      maxSamplesToRender
    }));

    if (framesToCopy > 0)
    {
      // Copy with rescaling
      for (int ch = 0; ch < chansToWrite; ++ch)
      {
        for (int i = 0; i < framesToCopy; ++i)
          outputs[ch][i] = mOverlapBuffer[ch][i] * rescale;
      }

      // Shift buffer
      ShiftBuffer(framesToCopy);
    }

    return framesToCopy;
  }

  int GetValidSamples() const { return mValidSamples; }

private:
  void EnsureCapacity(int required)
  {
    if (required > static_cast<int>(mOverlapBuffer[0].size()))
    {
      for (auto& ch : mOverlapBuffer)
        ch.resize(required, 0.0);
    }
  }

  void ShiftBuffer(int samples)
  {
    const int newValid = mValidSamples - samples;
    if (newValid > 0)
    {
      for (auto& ch : mOverlapBuffer)
        std::memmove(ch.data(), ch.data() + samples, newValid * sizeof(iplug::sample));
    }
    mValidSamples = std::max(0, newValid);

    // Zero tail
    const int tailStart = mValidSamples;
    for (auto& ch : mOverlapBuffer)
    {
      const int tailSize = static_cast<int>(ch.size()) - tailStart;
      if (tailSize > 0)
        std::memset(ch.data() + tailStart, 0, sizeof(iplug::sample) * tailSize);
    }
  }

  int mNumChannels = 2;
  int mChunkSize = 3000;
  std::vector<std::vector<iplug::sample>> mOverlapBuffer;
  int mValidSamples = 0;
};

/**
 * @brief Computes the OLA rescale factor for a given window and hop size
 * @param window Window to compute rescale for
 * @param chunkSize Chunk size in samples
 * @param hopSize Hop size in samples
 * @return Rescale factor to normalize overlapped output
 */
inline float ComputeOLARescale(const Window& window, int chunkSize, int hopSize)
{
  const auto& a = window.Coeffs();
  if (a.empty() || chunkSize <= 0) return 1.0f;

  const int hop = (hopSize <= 0) ? chunkSize : hopSize;
  double sum = 0.0;
  int count = 0;

  for (int n = 0; n < chunkSize; ++n)
  {
    double s = 0.0;
    int jmin = static_cast<int>(std::floor((n - (chunkSize - 1)) / static_cast<double>(hop)));
    int jmax = static_cast<int>(std::floor(n / static_cast<double>(hop)));

    for (int j = jmin; j <= jmax; ++j)
    {
      const int idx = n - j * hop;
      if (idx >= 0 && idx < static_cast<int>(a.size()))
        s += a[idx];
    }
    sum += s;
    ++count;
  }

  const double mean = (count > 0) ? (sum / static_cast<double>(count)) : 1.0;
  return (mean > 1e-9) ? static_cast<float>(1.0 / mean) : 1.0f;
}

} // namespace synaptic
