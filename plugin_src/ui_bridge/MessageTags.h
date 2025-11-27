/**
 * @file MessageTags.h
 * @brief Message tags for UI-to-C++ communication
 *
 * This enum defines all message types used for communication between
 * the UI layer and the C++ backend. Tags are organized by category
 * with contiguous IDs within each category.
 */

#pragma once

namespace synaptic
{
  /**
   * @brief Message tag categories and their base offsets
   */
  namespace MsgTagCategory
  {
    constexpr int kDSP = 0;       // DSP configuration messages
    constexpr int kBrain = 100;   // Brain management messages
    constexpr int kUI = 200;      // UI lifecycle messages
  }

  /**
   * @brief All message tags for UI-to-C++ communication
   */
  enum EMsgTags
  {
    // === DSP Configuration Messages (0-99) ===
    kMsgTagSetChunkSize = MsgTagCategory::kDSP + 0,
    kMsgTagSetAlgorithm = MsgTagCategory::kDSP + 1,
    kMsgTagSetOutputWindowMode = MsgTagCategory::kDSP + 2,
    kMsgTagSetAnalysisWindowMode = MsgTagCategory::kDSP + 3,
    kMsgTagTransformerSetParam = MsgTagCategory::kDSP + 4,

    // === Brain Management Messages (100-199) ===
    kMsgTagBrainAddFile = MsgTagCategory::kBrain + 0,
    kMsgTagBrainRemoveFile = MsgTagCategory::kBrain + 1,
    kMsgTagBrainExport = MsgTagCategory::kBrain + 2,
    kMsgTagBrainImport = MsgTagCategory::kBrain + 3,
    kMsgTagBrainEject = MsgTagCategory::kBrain + 4,
    kMsgTagBrainDetach = MsgTagCategory::kBrain + 5,
    kMsgTagBrainCreateNew = MsgTagCategory::kBrain + 6,
    kMsgTagBrainSetCompactMode = MsgTagCategory::kBrain + 7,
    kMsgTagCancelOperation = MsgTagCategory::kBrain + 8,

    // === UI Lifecycle Messages (200-299) ===
    kMsgTagUiReady = MsgTagCategory::kUI + 0,
    kMsgTagResizeToFit = MsgTagCategory::kUI + 1
  };
}

// Expose at global scope for backward compatibility
using synaptic::EMsgTags;
using synaptic::kMsgTagSetChunkSize;
using synaptic::kMsgTagSetAlgorithm;
using synaptic::kMsgTagSetOutputWindowMode;
using synaptic::kMsgTagSetAnalysisWindowMode;
using synaptic::kMsgTagTransformerSetParam;
using synaptic::kMsgTagBrainAddFile;
using synaptic::kMsgTagBrainRemoveFile;
using synaptic::kMsgTagBrainExport;
using synaptic::kMsgTagBrainImport;
using synaptic::kMsgTagBrainEject;
using synaptic::kMsgTagBrainDetach;
using synaptic::kMsgTagBrainCreateNew;
using synaptic::kMsgTagBrainSetCompactMode;
using synaptic::kMsgTagCancelOperation;
using synaptic::kMsgTagUiReady;
using synaptic::kMsgTagResizeToFit;
