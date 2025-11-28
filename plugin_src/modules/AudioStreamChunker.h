/**
 * @file AudioStreamChunker.h
 * @brief Real-time audio chunking, transformation, and overlap-add synthesis
 *
 * Coordinates the flow of audio through:
 * 1. Input accumulation and chunking
 * 2. FFT analysis for transformer/morph consumption
 * 3. Lookahead window for algorithms requiring future context
 * 4. Output synthesis via overlap-add or sequential playback
 *
 * Uses ChunkPool for memory management and OverlapAddSynthesizer for OLA.
 */

#pragma once

#include <vector>
#include <memory>
#include <cstdint>

#include "IPlug_include_in_plug_hdr.h"
#include "../audio/Window.h"
#include "../audio/FFT.h"
#include "../audio/AutotuneProcessor.h"
#include "../audio/ChunkPool.h"
#include "../audio/OverlapAddSynthesizer.h"
#include "../Structs.h"
#include "../morph/IMorph.h"

namespace synaptic
{

/**
 * @brief Manages real-time audio chunking, transformation, and output synthesis
 */
class AudioStreamChunker
{
public:
  explicit AudioStreamChunker(int numChannels);

  // === Configuration ===

  void Configure(int numChannels, int chunkSize, int windowSize);
  void SetChunkSize(int chunkSize);
  void SetBufferWindowSize(int windowSize);
  void SetNumChannels(int numChannels);
  void EnableOverlap(bool enable);
  void SetOutputWindow(const Window& w);
  void SetInputAnalysisWindow(const Window& w);
  void ResetOverlapBuffer();
  void Reset();

  // === Accessors ===

  int GetChunkSize() const { return mChunkSize; }
  int GetFFTSize() const { return mFFTSize; }
  int GetNumChannels() const { return mNumChannels; }

  void SetMorph(std::shared_ptr<IMorph> morph);

  AutotuneProcessor& GetAutotuneProcessor() { return mAutotuneProcessor; }
  const AutotuneProcessor& GetAutotuneProcessor() const { return mAutotuneProcessor; }

  // === Audio Input ===

  void PushAudio(iplug::sample** inputs, int nFrames);

  // === Transformer API ===

  bool PopPendingInputChunkIndex(int& outIdx);
  const AudioChunk* GetInputChunk(int idx) const;
  AudioChunk* GetOutputChunk(int idx);
  void CommitOutputChunk(int idx, int numFrames);
  void ClearOutputChunk(int idx, iplug::sample value = 0.0);

  // === Audio Output ===

  void RenderOutput(iplug::sample** outputs, int nFrames, int outChans, bool agcEnabled = false);

  // === Lookahead Window Access ===

  int GetWindowCapacity() const { return mBufferWindowSize; }
  int GetWindowCount() const { return mPool.Window().count; }
  int GetWindowIndexFromOldest(int ordinal) const;
  int GetWindowIndexFromNewest(int ordinal) const;

  // === Output Queue Access ===

  int GetOutputCount() const { return mPool.Output().count; }
  int GetOutputIndexFromOldest(int ordinal) const;
  bool PeekCurrentOutput(int& outPoolIdx, int& outFrameIndex) const;
  const AudioChunk* GetSourceChunkForOutput(int outputPoolIdx) const;

  // === Spectral Processing ===

  void SpectralProcessing(int poolIdx);

private:
  static constexpr int kExtraPoolCapacity = 8;

  // === Private Helper Methods ===

  void ResetState();
  void UpdateSpectralRescale();
  bool IsSpectralProcessingActive() const;
  bool ShouldUseOverlapAdd(bool spectralActive) const;
  int ComputeInputHopSize() const;
  bool ProcessAccumulatedChunk(int hopSize);
  void AddToWindow(int poolIdx);
  void AddToPending(int poolIdx);
  void ShiftAccumulationBuffer(int hopSize);
  double ComputeChunkRMS(const AudioChunk& chunk, int numFrames) const;
  void EnsureChunkSpectrum(AudioChunk& chunk);
  float ComputeAGC(int outputIdx, bool agcEnabled) const;
  void RenderWithOverlapAdd(iplug::sample** outputs, int nFrames, int chansToWrite,
                            int outChans, bool spectralActive, bool agcEnabled);
  void RenderSequential(iplug::sample** outputs, int nFrames, int chansToWrite,
                        int outChans, bool spectralActive, bool agcEnabled);

  // === Member Variables ===

  // Configuration
  int mNumChannels = 2;
  int mChunkSize = 3000;
  int mBufferWindowSize = 1;
  bool mEnableOverlap = true;

  // Pool and synthesis
  ChunkPool mPool;
  OverlapAddSynthesizer mOLASynthesizer;

  // Accumulation buffer
  std::vector<std::vector<iplug::sample>> mAccumulation;
  int mAccumulatedFrames = 0;

  // FFT and spectral processing
  int mFFTSize = 0;
  FFTProcessor mFFT;
  Window mOutputWindow;
  Window mInputAnalysisWindow;
  float mSpectralOLARescale = 1.0f;

  // Morph and autotune
  std::shared_ptr<IMorph> mMorph;
  AutotuneProcessor mAutotuneProcessor;

  // Latency tracking
  int64_t mTotalInputSamplesPushed = 0;
  int64_t mTotalOutputSamplesRendered = 0;
  int mOutputFrontFrameIndex = 0;
};

} // namespace synaptic
