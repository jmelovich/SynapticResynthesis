#pragma once

#include <string>
#include <algorithm>

namespace synaptic
{
  /**
   * @brief Default values for DSP configuration
   *
   * All DSP-related defaults are centralized here for easy modification
   * and to eliminate magic numbers throughout the codebase.
   */
  namespace DSPDefaults
  {
    constexpr int kChunkSize = 3000;              ///< Default chunk size in samples
    constexpr int kBufferWindowSize = 1;          ///< Default lookahead window count
    constexpr int kOutputWindowMode = 1;          ///< 1=Hann (default)
    constexpr int kAnalysisWindowMode = 1;        ///< 1=Hann (default)
    constexpr int kAlgorithmId = 0;               ///< First transformer in UI list
    constexpr bool kEnableOverlapAdd = true;      ///< Overlap-add enabled by default

    constexpr int kMinChunkSize = 1;
    constexpr int kMaxChunkSize = 262144;
    constexpr int kMinBufferWindow = 1;
    constexpr int kMaxBufferWindow = 1024;
    constexpr int kMinWindowMode = 1;
    constexpr int kMaxWindowMode = 4;
  }

  /**
   * @brief Configuration state for DSP parameters
   *
   * Contains only DSP-related settings. Brain storage state has been
   * moved to BrainManager to maintain single responsibility.
   */
  struct DSPConfig
  {
    // Core DSP parameters (initialized with defaults)
    int chunkSize = DSPDefaults::kChunkSize;
    int bufferWindowSize = DSPDefaults::kBufferWindowSize;
    int outputWindowMode = DSPDefaults::kOutputWindowMode;      // 1=Hann, 2=Hamming, 3=Blackman, 4=Rectangular
    int analysisWindowMode = DSPDefaults::kAnalysisWindowMode;  // Same encoding as output window
    int algorithmId = DSPDefaults::kAlgorithmId;                // Index into transformer factory UI list
    bool enableOverlapAdd = DSPDefaults::kEnableOverlapAdd;

    /**
     * @brief Validate and clamp parameters to safe ranges
     */
    void Validate()
    {
      chunkSize = std::clamp(chunkSize, DSPDefaults::kMinChunkSize, DSPDefaults::kMaxChunkSize);
      bufferWindowSize = std::clamp(bufferWindowSize, DSPDefaults::kMinBufferWindow, DSPDefaults::kMaxBufferWindow);
      outputWindowMode = std::clamp(outputWindowMode, DSPDefaults::kMinWindowMode, DSPDefaults::kMaxWindowMode);
      analysisWindowMode = std::clamp(analysisWindowMode, DSPDefaults::kMinWindowMode, DSPDefaults::kMaxWindowMode);
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
