#pragma once

#include "IPlugStructs.h"
#include <cstdint>

// Forward declarations
namespace synaptic
{
  class Brain;
  class BrainManager;
}

namespace synaptic
{
  /**
   * @brief Handles plugin state serialization/deserialization
   *
   * Manages saving and loading of brain state (inline or external reference),
   * coordinating with BrainManager for external file handling.
   */
  class StateSerializer
  {
  public:
    StateSerializer() = default;

    /**
     * @brief Serialize brain state to chunk
     *
     * Called after Plugin::SerializeState() to append brain section.
     * Handles both inline brain data and external file references.
     * If external mode with dirty brain, writes brain to external file.
     *
     * @param chunk Chunk to append brain state to
     * @param brain Brain instance to serialize
     * @param brainMgr BrainManager for state info
     * @return true on success
     */
    bool SerializeBrainState(iplug::IByteChunk& chunk,
                            const Brain& brain,
                            const BrainManager& brainMgr) const;

    /**
     * @brief Deserialize brain state from chunk
     *
     * Called after Plugin::UnserializeState() to read brain section.
     * Handles both inline brain data and external file references.
     * Loads brain from external file if path is valid.
     *
     * @param chunk Chunk to read brain state from
     * @param startPos Starting position in chunk
     * @param brain Brain instance to deserialize into
     * @param brainMgr BrainManager to update state
     * @return New position after reading, or -1 on error
     */
    int DeserializeBrainState(const iplug::IByteChunk& chunk,
                             int startPos,
                             Brain& brain,
                             BrainManager& brainMgr);

  private:
    static constexpr uint32_t kBrainSectionTag = 0x42524E53; // 'BRNS'
  };
}

