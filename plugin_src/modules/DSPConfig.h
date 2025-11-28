#pragma once

#include <string>
#include <algorithm>

namespace synaptic
{
  /**
   * @brief Configuration state for DSP parameters
   *
   * Contains only DSP-related settings. Brain storage state has been
   * moved to BrainManager to maintain single responsibility.
   */
  struct DSPConfig
  {
    // Core DSP parameters
    int chunkSize = 3000;
    int bufferWindowSize = 1;
    int outputWindowMode = 1;      // 1=Hann, 2=Hamming, 3=Blackman, 4=Rectangular
    int analysisWindowMode = 1;    // Same encoding as output window
    int algorithmId = 0;           // Index into transformer factory UI list
    bool enableOverlapAdd = true;

    /**
     * @brief Validate and clamp parameters to safe ranges
     */
    void Validate()
    {
      chunkSize = std::max(1, chunkSize);
      bufferWindowSize = std::max(1, bufferWindowSize);
      outputWindowMode = std::clamp(outputWindowMode, 1, 4);
      analysisWindowMode = std::clamp(analysisWindowMode, 1, 4);
      algorithmId = std::max(0, algorithmId);
    }

    /**
     * @brief Check if two configs have equivalent DSP settings
     */
    bool operator==(const DSPConfig& other) const
    {
      return chunkSize == other.chunkSize &&
             bufferWindowSize == other.bufferWindowSize &&
             outputWindowMode == other.outputWindowMode &&
             analysisWindowMode == other.analysisWindowMode &&
             algorithmId == other.algorithmId &&
             enableOverlapAdd == other.enableOverlapAdd;
    }

    bool operator!=(const DSPConfig& other) const { return !(*this == other); }
  };
}
