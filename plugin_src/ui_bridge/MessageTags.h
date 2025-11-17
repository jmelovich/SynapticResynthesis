#pragma once

// Message tags for UI-to-C++ communication
// This enum is separated from SynapticResynthesis.h to avoid circular dependencies
enum EMsgTags
{
  kMsgTagSetChunkSize = 4,
  // kMsgTagSetBufferWindowSize = 5, // DEPRECATED - removed
  kMsgTagSetAlgorithm = 6,
  kMsgTagSetOutputWindowMode = 7,
  // Analysis window used for offline brain analysis (non-automatable IParam mirrors this)
  kMsgTagSetAnalysisWindowMode = 8,
  // Brain UI -> C++ messages
  kMsgTagBrainAddFile = 100,
  kMsgTagBrainRemoveFile = 101,
  // Transformer params UI -> C++
  kMsgTagTransformerSetParam = 102,
  // UI lifecycle
  kMsgTagUiReady = 103,
  // Brain snapshot external IO
  kMsgTagBrainExport = 104,
  kMsgTagBrainImport = 105,
  kMsgTagBrainEject = 106,
  kMsgTagBrainDetach = 107,
  kMsgTagBrainCreateNew = 109,
  kMsgTagBrainSetCompactMode = 110,
  // Window resize
  kMsgTagResizeToFit = 108,
  // C++ -> UI JSON updates use msgTag = -1, with id fields "brainSummary"
};

