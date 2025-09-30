#pragma once

#include "json.hpp"
#include <string>
#include <algorithm>

namespace synaptic
{
  /**
   * @brief Configuration state for DSP parameters
   *
   * Central struct holding all configurable DSP settings.
   * Provides serialization to/from JSON for UI communication.
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

    // External brain storage info (for UI display)
    bool useExternalBrain = false;
    std::string externalPath;

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
     * @brief Serialize to JSON for UI communication
     */
    nlohmann::json ToJSON() const
    {
      nlohmann::json j;
      j["chunkSize"] = chunkSize;
      j["bufferWindowSize"] = bufferWindowSize;
      j["outputWindowMode"] = outputWindowMode;
      j["analysisWindowMode"] = analysisWindowMode;
      j["algorithmId"] = algorithmId;
      j["enableOverlapAdd"] = enableOverlapAdd;
      j["useExternalBrain"] = useExternalBrain;
      j["externalPath"] = externalPath;
      return j;
    }

    /**
     * @brief Deserialize from JSON (used when receiving from UI)
     */
    void FromJSON(const nlohmann::json& j)
    {
      if (j.contains("chunkSize")) chunkSize = j["chunkSize"].get<int>();
      if (j.contains("bufferWindowSize")) bufferWindowSize = j["bufferWindowSize"].get<int>();
      if (j.contains("outputWindowMode")) outputWindowMode = j["outputWindowMode"].get<int>();
      if (j.contains("analysisWindowMode")) analysisWindowMode = j["analysisWindowMode"].get<int>();
      if (j.contains("algorithmId")) algorithmId = j["algorithmId"].get<int>();
      if (j.contains("enableOverlapAdd")) enableOverlapAdd = j["enableOverlapAdd"].get<bool>();
      if (j.contains("useExternalBrain")) useExternalBrain = j["useExternalBrain"].get<bool>();
      if (j.contains("externalPath")) externalPath = j["externalPath"].get<std::string>();

      Validate();
    }
  };
}

