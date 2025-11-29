/**
 * @file DSPContext.cpp
 * @brief Implementation of the real-time audio processing context
 */

#include "plugin_src/audio/DSPContext.h"
#include "plugin_src/params/ParameterManager.h"
#include "plugin_src/params/ParameterIds.h"
#include "plugin_src/transformers/TransformerFactory.h"
#include "plugin_src/transformers/types/ExpandedSimpleSampleBrainTransformer.h"
#include "plugin_src/morph/MorphFactory.h"
#include "plugin_src/brain/Brain.h"
#include "plugin_src/Structs.h"

namespace synaptic {

DSPContext::DSPContext(int nChannels)
  : mChunker(nChannels)
{
}

void DSPContext::Init(iplug::Plugin* plugin, ParameterManager* paramManager, Brain* brain, DSPConfig& config)
{
  // Default transformer = first UI-visible entry
  mTransformer = TransformerFactory::CreateByUiIndex(config.algorithmId);
  if (auto sb = dynamic_cast<BaseSampleBrainTransformer*>(mTransformer.get()))
    sb->SetBrain(brain);

  // Default morph = first UI-visible entry
  mMorph = MorphFactory::CreateByUiIndex(0);
  mChunker.SetMorph(mMorph);
  
  // Initialize chunker state
  mChunker.SetChunkSize(config.chunkSize);
  mChunker.SetBufferWindowSize(config.bufferWindowSize);
}

int DSPContext::ComputeLatencySamples(int chunkSize, int bufferWindowSize) const
{
  return chunkSize + (mTransformer ? mTransformer->GetAdditionalLatencySamples(chunkSize, bufferWindowSize) : 0);
}

void DSPContext::OnReset(double sampleRate, int blockSize, int nChans, 
                         iplug::Plugin* plugin, DSPConfig& config, ParameterManager* paramManager, Brain* brain)
{
  mInGainSmoother.SetSmoothTime(20., sampleRate);
  mOutGainSmoother.SetSmoothTime(20., sampleRate);

  mChunker.SetChunkSize(config.chunkSize);
  mChunker.SetBufferWindowSize(config.bufferWindowSize);
  mChunker.SetNumChannels(nChans);
  
  auto& autotune = mChunker.GetAutotuneProcessor();
  autotune.OnReset(sampleRate, mChunker.GetFFTSize(), mChunker.GetNumChannels());
  
  const int autotuneBlendIdx = kAutotuneBlend;
  if (plugin->GetParam(autotuneBlendIdx))
  {
    const double blendPercent = plugin->GetParam(autotuneBlendIdx)->Value();
    autotune.SetBlend((float)(blendPercent / 100.0));
  }
  
  {
    const int modeIdx = kAutotuneMode;
    if (plugin->GetParam(modeIdx))
      autotune.SetMode(plugin->GetParam(modeIdx)->Int() == 1);
      
    const int tolIdx = kAutotuneToleranceOctaves;
    if (plugin->GetParam(tolIdx))
    {
      const int enumIdx = std::clamp(plugin->GetParam(tolIdx)->Int(), 0, 4);
      autotune.SetToleranceOctaves(enumIdx + 1);
    }
  }
  
  mChunker.Reset();

  if (mTransformer)
    mTransformer->OnReset(sampleRate, config.chunkSize, config.bufferWindowSize, nChans);

  if (mMorph)
    mMorph->OnReset(sampleRate, config.chunkSize, nChans);
    
  mChunker.SetMorph(mMorph);

  // Apply parameter bindings
  paramManager->ApplyBindingsTo(plugin, mTransformer.get(), mMorph.get());
}

void DSPContext::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames, 
                              iplug::Plugin* plugin, DSPConfig& config, ParameterManager* paramManager)
{
  // Thread-safe transformer swap
  if (mPendingTransformer)
  {
    mTransformer = std::move(mPendingTransformer);
    mPendingTransformer.reset();
    
    int extraLatency = mTransformer ? mTransformer->GetAdditionalLatencySamples(config.chunkSize, config.bufferWindowSize) : 0;
    plugin->SetLatency(config.chunkSize + extraLatency);

    paramManager->ApplyBindingsTo(plugin, mTransformer.get(), mMorph.get());
  }

  // Thread-safe morph swap
  if (mPendingMorph)
  {
    mMorph = std::move(mPendingMorph);
    mPendingMorph.reset();
    mChunker.SetMorph(mMorph);

    paramManager->ApplyBindingsTo(plugin, mTransformer.get(), mMorph.get());
  }

  const double inGain = plugin->GetParam(kInGain)->DBToAmp();
  const double outGain = plugin->GetParam(kOutGain)->DBToAmp();
  const bool agcEnabled = plugin->GetParam(kAGC)->Bool();

  const int inChans = plugin->NInChansConnected();
  const int outChans = plugin->NOutChansConnected();
  
  if (inChans <= 0 || outChans <= 0 || !inputs || !outputs)
  {
    for (int ch = 0; ch < outChans; ++ch)
      if (outputs[ch])
        memset(outputs[ch], 0, sizeof(iplug::sample) * nFrames);
    return;
  }

  // Apply input gain
  for (int s = 0; s < nFrames; s++)
  {
    const double smoothedInGain = mInGainSmoother.Process(inGain);
    for (int ch = 0; ch < inChans; ch++)
    {
      inputs[ch][s] *= smoothedInGain;
    }
  }

  // Feed chunker
  mChunker.PushAudio(inputs, nFrames);

  // Transform
  if (mTransformer)
  {
    const int required = mTransformer->GetRequiredLookaheadChunks();
    if (mChunker.GetWindowCount() >= required)
      mTransformer->Process(mChunker);
  }

  // Render
  mChunker.RenderOutput(outputs, nFrames, outChans, agcEnabled);

  // Apply output gain
  for (int s = 0; s < nFrames; s++)
  {
    const double smoothedOutGain = mOutGainSmoother.Process(outGain);
    for (int ch = 0; ch < outChans; ++ch)
      outputs[ch][s] *= smoothedOutGain;
  }
}

} // namespace synaptic
