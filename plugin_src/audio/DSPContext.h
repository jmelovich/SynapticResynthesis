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

// Forward declarations
namespace synaptic {

class ParameterManager;
class Brain;
struct IMorph;

/**
 * @brief Encapsulates the real-time audio processing context
 *
 * Handles audio buffering, chunking, transformation, gain, and 
 * thread-safe component swapping.
 */
class DSPContext
{
public:
  DSPContext(int nChannels);

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

  // Accessors for UISyncManager/Coordinators
  AudioStreamChunker& GetChunker() { return mChunker; }
  Window& GetOutputWindow() { return mOutputWindow; }
  
  std::shared_ptr<IChunkBufferTransformer>* GetTransformerPtr() { return &mTransformer; }
  std::shared_ptr<IMorph>* GetMorphPtr() { return &mMorph; }
  std::shared_ptr<IChunkBufferTransformer>* GetPendingTransformerPtr() { return &mPendingTransformer; }
  std::shared_ptr<IMorph>* GetPendingMorphPtr() { return &mPendingMorph; }

private:
  // Components
  iplug::LogParamSmooth<iplug::sample, 1> mInGainSmoother;
  iplug::LogParamSmooth<iplug::sample, 2> mOutGainSmoother;
  
  AudioStreamChunker mChunker;
  Window mOutputWindow;
  
  // Dynamic DSP objects
  std::shared_ptr<IChunkBufferTransformer> mTransformer;
  std::shared_ptr<IChunkBufferTransformer> mPendingTransformer; // For thread-safe swapping
  
  std::shared_ptr<IMorph> mMorph;
  std::shared_ptr<IMorph> mPendingMorph; // For thread-safe swapping
};

} // namespace synaptic

