/**
 * @file MorphFactory.h
 * @brief Factory for spectral morph modes
 *
 * Provides registration and creation of morph implementations.
 * Uses GenericFactory template for common factory operations.
 */

#pragma once

#include <memory>
#include <vector>

#include "../common/GenericFactory.h"
#include "IMorph.h"
#include "modes/NoneMorph.h"
#include "modes/CrossSynthesisMorph.h"
#include "modes/SpectralVocoderMorph.h"
#include "modes/WaveMorph.h"

namespace synaptic
{

// Type alias for backward compatibility
using MorphInfo = FactoryEntry<IMorph>;

/**
 * @brief Factory for creating morph instances
 *
 * Registers all available morph implementations and provides
 * methods for UI integration and instance creation.
 */
class MorphFactory : public GenericFactory<IMorph, MorphFactory>
{
public:
  /**
   * @brief Get all registered morph implementations
   *
   * This is the single source of truth for morph registrations.
   * Order defines UI dropdown order for entries with includeInUI=true.
   */
  static const std::vector<MorphInfo>& GetAllEntries()
  {
    static const std::vector<MorphInfo> kAll = {
      { "none", "None",
        []{ return std::make_shared<NoneMorph>(); }, true },
      { "cross", "Cross Synthesis",
        []{ return std::make_shared<CrossSynthesisMorph>(); }, true },
      { "vocoder", "Spectral Vocoder",
        []{ return std::make_shared<SpectralVocoderMorph>(); }, true },
      { "wave", "Wave Morph",
        []{ return std::make_shared<WaveMorph>(); }, true },
    };
    return kAll;
  }
};

} // namespace synaptic
