/**
 * @file DSPContext.h
 * @brief Encapsulates the real-time audio processing context
 *
 * Handles audio buffering, chunking, transformation, gain, and
 * thread-safe component swapping between the audio thread and UI thread.
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IPlugConstants.h"
#include "IPlugMidi.h"
#include "Smoothers.h"
#include "plugin_src/modules/AudioStreamChunker.h"
#include "plugin_src/transformers/BaseTransformer.h"
#include "plugin_src/morph/IMorph.h"
#include "plugin_src/audio/Window.h"
#include "plugin_src/modules/DSPConfig.h"
#include <memory>
#include <vector>
#include <atomic>

namespace synaptic {

class ParameterManager;
class Brain;
struct IMorph;

/**
 * @brief Encapsulates the real-time audio processing context
 *
 * Manages:
 * - Audio chunking and overlap-add processing
 * - Transformer and morph instances with thread-safe swapping
 * - Input/output gain smoothing
 * - Latency calculation
 */
class DSPContext
{
public:
  explicit DSPContext(int nChannels);

  // Initialize components
  void Init(iplug::Plugin* plugin, ParameterManager* paramManager, Brain* brain, DSPConfig& config);
  
  // Main audio processing
  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames, 
                    iplug::Plugin* plugin, DSPConfig& config, ParameterManager* paramManager);

  // Reset state
  void OnReset(double sampleRate, int blockSize, int nChans, 
               iplug::Plugin* plugin, DSPConfig& config, ParameterManager* paramManager, Brain* brain);

  // Latency calculation
  int ComputeLatencySamples(int chunkSize, int bufferWindowSize) const;

  // === Component Accessors ===
  
  AudioStreamChunker& GetChunker() { return mChunker; }
  const AudioStreamChunker& GetChunker() const { return mChunker; }
  
  Window& GetOutputWindow() { return mOutputWindow; }
  const Window& GetOutputWindow() const { return mOutputWindow; }
  
  // === Transformer Access ===
  
  /** @brief Get current transformer (read-only access) */
  std::shared_ptr<IChunkBufferTransformer> GetTransformer() const { return mTransformer; }
  
  /** @brief Get raw pointer to current transformer (for parameter binding) */
  IChunkBufferTransformer* GetTransformerRaw() const { return mTransformer.get(); }
  
  /** @brief Set pending transformer for thread-safe swap in ProcessBlock */
  void SetPendingTransformer(std::shared_ptr<IChunkBufferTransformer> transformer) 
  { 
    mPendingTransformer = std::move(transformer); 
  }
  
  /** @brief Check if there's a pending transformer */
  bool HasPendingTransformer() const { return mPendingTransformer != nullptr; }
  
  /** @brief Get pending transformer (for parameter binding before swap) */
  IChunkBufferTransformer* GetPendingTransformerRaw() const { return mPendingTransformer.get(); }
  
  // === Morph Access ===
  
  /** @brief Get current morph (read-only access) */
  std::shared_ptr<IMorph> GetMorph() const { return mMorph; }
  
  /** @brief Get raw pointer to current morph (for parameter binding) */
  IMorph* GetMorphRaw() const { return mMorph.get(); }
  
  /** @brief Set pending morph for thread-safe swap in ProcessBlock */
  void SetPendingMorph(std::shared_ptr<IMorph> morph) 
  { 
    mPendingMorph = std::move(morph); 
  }
  
  /** @brief Check if there's a pending morph */
  bool HasPendingMorph() const { return mPendingMorph != nullptr; }
  
  /** @brief Get pending morph (for parameter binding before swap) */
  IMorph* GetPendingMorphRaw() const { return mPendingMorph.get(); }

private:
  // Gain smoothers
  iplug::LogParamSmooth<iplug::sample, 1> mInGainSmoother;
  iplug::LogParamSmooth<iplug::sample, 2> mOutGainSmoother;
  
  // Audio processing components
  AudioStreamChunker mChunker;
  Window mOutputWindow;
  
  // Dynamic DSP objects with pending slots for thread-safe swapping
  std::shared_ptr<IChunkBufferTransformer> mTransformer;
  std::shared_ptr<IChunkBufferTransformer> mPendingTransformer;
  
  std::shared_ptr<IMorph> mMorph;
  std::shared_ptr<IMorph> mPendingMorph;
};

} // namespace synaptic
