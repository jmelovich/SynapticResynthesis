/**
 * @file ParameterIds.h
 * @brief Central definition of all plugin parameter IDs
 *
 * This header defines the EParams enum which is the single source of truth
 * for all parameter indices in the plugin. All parameter-related code should
 * include this header rather than defining their own constants.
 */

#pragma once

namespace synaptic
{
  /**
   * @brief Fixed (non-dynamic) parameter indices
   *
   * Dynamic transformer/morph parameters are indexed starting at kNumParams.
   */
  enum EParams
  {
    kInGain = 0,
    kChunkSize,
    kBufferWindow,
    kAlgorithm,
    kOutputWindow,
    kDirtyFlag,
    kAnalysisWindow,
    kEnableOverlap,
    kOutGain,
    kAGC,
    kAutotuneBlend,
    kAutotuneMode,
    kAutotuneToleranceOctaves,
    kMorphMode,
    kWindowLock,
    // Dynamic transformer/morph parameters are indexed after this sentinel
    kNumParams
  };
}

// Also expose at global scope for backward compatibility with existing code
using synaptic::EParams;
using synaptic::kInGain;
using synaptic::kChunkSize;
using synaptic::kBufferWindow;
using synaptic::kAlgorithm;
using synaptic::kOutputWindow;
using synaptic::kDirtyFlag;
using synaptic::kAnalysisWindow;
using synaptic::kEnableOverlap;
using synaptic::kOutGain;
using synaptic::kAGC;
using synaptic::kAutotuneBlend;
using synaptic::kAutotuneMode;
using synaptic::kAutotuneToleranceOctaves;
using synaptic::kMorphMode;
using synaptic::kWindowLock;
using synaptic::kNumParams;

