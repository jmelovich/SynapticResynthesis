/**
 * @file TransformerFactory.h
 * @brief Factory for audio chunk transformers
 *
 * Provides registration and creation of transformer implementations.
 * Uses GenericFactory template for common factory operations.
 */

#pragma once

#include <memory>
#include <vector>

#include "../common/GenericFactory.h"
#include "BaseTransformer.h"
#include "types/SimpleSampleBrainTransformer.h"
#include "types/ExpandedSimpleSampleBrainTransformer.h"

namespace synaptic
{

// Type alias for backward compatibility
using TransformerInfo = FactoryEntry<IChunkBufferTransformer>;

/**
 * @brief Factory for creating transformer instances
 *
 * Registers all available transformer implementations and provides
 * methods for UI integration and instance creation.
 */
class TransformerFactory : public GenericFactory<IChunkBufferTransformer, TransformerFactory>
{
public:
  /**
   * @brief Get all registered transformer implementations
   *
   * This is the single source of truth for transformer registrations.
   * Order defines UI dropdown order for entries with includeInUI=true.
   */
  static const std::vector<TransformerInfo>& GetAllEntries()
  {
    static const std::vector<TransformerInfo> kAll = {
      { "passthrough", "Passthrough",
        []{ return std::make_shared<PassthroughTransformer>(); }, true },
      { "sinematch", "Simple Sine Match",
        []{ return std::make_shared<SineMatchTransformer>(); }, true },
      { "samplebrain", "Simple SampleBrain",
        []{ return std::make_shared<SimpleSampleBrainTransformer>(); }, true },
      { "expandedsamplebrain", "Expanded SampleBrain",
        []{ return std::make_shared<ExpandedSimpleSampleBrainTransformer>(); }, true },
    };
    return kAll;
  }
};

} // namespace synaptic
