/**
 * @file ParameterIds.h
 * @brief Central definition of all plugin parameter IDs
 *
 * This header defines the EParams enum which is the single source of truth
 * for all parameter indices in the plugin. All parameter-related code should
 * include this header rather than defining their own constants.
 *
 * Usage:
 *   - Within synaptic namespace code: use kChunkSize, kAlgorithm, etc. directly
 *   - Outside synaptic namespace: add `using namespace synaptic;` or qualify
 *     with `synaptic::kChunkSize`, etc.
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
