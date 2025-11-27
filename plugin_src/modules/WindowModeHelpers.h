/**
 * @file WindowModeHelpers.h
 * @brief Helper functions for window mode conversions
 *
 * Window modes are stored as 1-4 in DSPConfig (Hann=1, Hamming=2, Blackman=3, Rectangular=4)
 * but displayed as 0-3 in UI parameters/controls.
 * These helpers centralize the conversion logic.
 */

#pragma once

#include <algorithm>

namespace synaptic
{
  /**
   * @brief Window mode conversion utilities
   *
   * Converts between DSPConfig window modes (1-4) and UI parameter indices (0-3).
   */
  namespace WindowMode
  {
    /**
     * @brief Convert UI parameter value (0-3) to DSPConfig mode (1-4)
     */
    inline int ParamToConfig(int paramValue)
    {
      return 1 + std::clamp(paramValue, 0, 3);
    }

    /**
     * @brief Convert DSPConfig mode (1-4) to UI parameter value (0-3)
     */
    inline int ConfigToParam(int configValue)
    {
      return std::clamp(configValue - 1, 0, 3);
    }

    /**
     * @brief Validate and clamp a config mode value to valid range (1-4)
     */
    inline int ClampConfig(int configValue)
    {
      return std::clamp(configValue, 1, 4);
    }

    /**
     * @brief Validate and clamp a param value to valid range (0-3)
     */
    inline int ClampParam(int paramValue)
    {
      return std::clamp(paramValue, 0, 3);
    }
  }
}

