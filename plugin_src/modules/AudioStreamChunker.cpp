/**
 * @file AudioStreamChunker.cpp
 * @brief Implementation of real-time audio chunking, transformation, and overlap-add synthesis
 */

#include "AudioStreamChunker.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace synaptic
{

// ============================================================================
// Construction and Configuration
// ============================================================================

AudioStreamChunker::AudioStreamChunker(int numChannels)
  : mNumChannels(numChannels)
{
  Configure(mNumChannels, mChunkSize, mBufferWindowSize);
}

void AudioStreamChunker::Configure(int numChannels, int chunkSize, int windowSize)
{
  const int newNumChannels = std::max(1, numChannels);
  const int newChunkSize = std::max(1, chunkSize);
  const int newBufferWindowSize = std::max(1, windowSize);

  const bool needsReallocation = (newNumChannels != mNumChannels ||
                                  newChunkSize != mChunkSize);

  mNumChannels = newNumChannels;
  mChunkSize = newChunkSize;
  mBufferWindowSize = newBufferWindowSize;

  // Configure chunk pool
  mPool.Configure(mNumChannels, mChunkSize, mBufferWindowSize, kExtraPoolCapacity);

  if (needsReallocation)
  {
    // Pre-size accumulation scratch
    mAccumulation.assign(mNumChannels, std::vector<iplug::sample>(mChunkSize, 0.0));
  }

  // Configure OLA synthesizer
  mOLASynthesizer.Configure(mNumChannels, mChunkSize);

  // Reset state
  ResetState();

  // Configure FFT
  mFFTSize = Window::NextValidFFTSize(mChunkSize);
  mFFT.Configure(mFFTSize);

  // Keep analysis window in sync
  mInputAnalysisWindow.Set(mInputAnalysisWindow.GetType(), mChunkSize);
  UpdateSpectralRescale();

  // Initialize autotune processor
  mAutotuneProcessor.OnReset(mAutotuneProcessor.GetSampleRate(), mFFTSize, mNumChannels);
}

void AudioStreamChunker::SetChunkSize(int chunkSize)
{
  Configure(mNumChannels, chunkSize, mBufferWindowSize);
}

void AudioStreamChunker::SetBufferWindowSize(int windowSize)
{
  Configure(mNumChannels, mChunkSize, windowSize);
}

void AudioStreamChunker::SetNumChannels(int numChannels)
{
  Configure(numChannels, mChunkSize, mBufferWindowSize);
}

void AudioStreamChunker::EnableOverlap(bool enable)
{
  if (mEnableOverlap != enable)
  {
    mEnableOverlap = enable;
    Reset();
  }
}

void AudioStreamChunker::SetOutputWindow(const Window& w)
{
  if (mOutputWindow.GetType() != w.GetType())
    mOLASynthesizer.Reset();
  mOutputWindow = w;
}

void AudioStreamChunker::SetInputAnalysisWindow(const Window& w)
{
  if (mInputAnalysisWindow.GetType() != w.GetType() || mInputAnalysisWindow.Size() != w.Size())
  {
    mInputAnalysisWindow = w;
    UpdateSpectralRescale();
  }
}

void AudioStreamChunker::ResetOverlapBuffer()
{
  mOLASynthesizer.Reset();
}

void AudioStreamChunker::Reset()
{
  Configure(mNumChannels, mChunkSize, mBufferWindowSize);
}

void AudioStreamChunker::SetMorph(std::shared_ptr<IMorph> morph)
{
  mMorph = std::move(morph);
}

// ============================================================================
// Audio Input
// ============================================================================

void AudioStreamChunker::PushAudio(iplug::sample** inputs, int nFrames)
{
  if (!inputs || nFrames <= 0 || mNumChannels <= 0) return;

  mTotalInputSamplesPushed += nFrames;

  int frameIndex = 0;
  while (frameIndex < nFrames)
  {
    const int framesToCopy = std::min(mChunkSize - mAccumulatedFrames, nFrames - frameIndex);

    // Copy input to accumulation buffer
    for (int ch = 0; ch < mNumChannels; ++ch)
    {
      if (ch >= static_cast<int>(mAccumulation.size()) || !inputs[ch]) continue;
      if (mAccumulation[ch].size() < static_cast<size_t>(mAccumulatedFrames + framesToCopy)) continue;

      iplug::sample* dst = mAccumulation[ch].data() + mAccumulatedFrames;
      const iplug::sample* src = inputs[ch] + frameIndex;
      std::memcpy(dst, src, sizeof(iplug::sample) * framesToCopy);
    }
    mAccumulatedFrames += framesToCopy;
    frameIndex += framesToCopy;

    // Determine hop size
    const int inputHopSize = ComputeInputHopSize();

    // Process complete chunks
    while (mAccumulatedFrames >= mChunkSize)
    {
      if (!ProcessAccumulatedChunk(inputHopSize))
      {
        // Pool full - shift buffer and try again
        ShiftAccumulationBuffer(inputHopSize);
        continue;
      }
      ShiftAccumulationBuffer(inputHopSize);
    }
  }
}

// ============================================================================
// Transformer API
// ============================================================================

bool AudioStreamChunker::PopPendingInputChunkIndex(int& outIdx)
{
  if (!mPool.Pending().Pop(outIdx))
    return false;
  mPool.DecRefAndMaybeFree(outIdx);
  return true;
}

const AudioChunk* AudioStreamChunker::GetInputChunk(int idx) const
{
  return mPool.GetInputChunk(idx);
}

AudioChunk* AudioStreamChunker::GetOutputChunk(int idx)
{
  return mPool.GetOutputChunk(idx);
}

void AudioStreamChunker::CommitOutputChunk(int idx, int numFrames)
{
  if (idx < 0 || idx >= mPool.GetPoolCapacity()) return;

  auto* entry = mPool.GetEntry(idx);
  if (!entry) return;

  numFrames = std::clamp(numFrames, 0, mChunkSize);
  entry->outputChunk.numFrames = numFrames;

  // Calculate output RMS
  entry->outputChunk.rms = ComputeChunkRMS(entry->outputChunk, numFrames);

  // Add output reference and enqueue
  mPool.IncRef(idx);
  mPool.Output().Push(idx);
}

void AudioStreamChunker::ClearOutputChunk(int idx, iplug::sample value)
{
  auto* chunk = GetOutputChunk(idx);
  if (!chunk) return;

  for (auto& ch : chunk->channelSamples)
    std::fill(ch.begin(), ch.end(), value);
}

// ============================================================================
// Audio Output
// ============================================================================

void AudioStreamChunker::RenderOutput(iplug::sample** outputs, int nFrames, int outChans, bool agcEnabled)
{
  if (!outputs || nFrames <= 0 || outChans <= 0) return;

  const int chansToWrite = std::min(outChans, mNumChannels);
  const bool spectralActive = IsSpectralProcessingActive();
  const bool useOverlapAdd = ShouldUseOverlapAdd(spectralActive);

  if (useOverlapAdd)
  {
    RenderWithOverlapAdd(outputs, nFrames, chansToWrite, outChans, spectralActive, agcEnabled);
  }
  else
  {
    RenderSequential(outputs, nFrames, chansToWrite, outChans, spectralActive, agcEnabled);
  }
}

// ============================================================================
// Lookahead Window Access
// ============================================================================

int AudioStreamChunker::GetWindowIndexFromOldest(int ordinal) const
{
  const auto& window = mPool.Window();
  if (ordinal < 0 || ordinal >= window.count) return -1;
  return window.GetAt(ordinal);
}

int AudioStreamChunker::GetWindowIndexFromNewest(int ordinal) const
{
  const auto& window = mPool.Window();
  if (ordinal < 0 || ordinal >= window.count) return -1;
  const int cap = window.Capacity();
  int newestPos = (window.head + window.count - 1) % cap;
  int pos = (newestPos - ordinal);
  while (pos < 0) pos += cap;
  pos %= cap;
  return window.data[pos];
}

// ============================================================================
// Output Queue Access
// ============================================================================

int AudioStreamChunker::GetOutputIndexFromOldest(int ordinal) const
{
  const auto& output = mPool.Output();
  if (ordinal < 0 || ordinal >= output.count) return -1;
  return output.GetAt(ordinal);
}

bool AudioStreamChunker::PeekCurrentOutput(int& outPoolIdx, int& outFrameIndex) const
{
  if (mPool.Output().Empty()) return false;
  outPoolIdx = mPool.Output().PeekOldest();
  outFrameIndex = mOutputFrontFrameIndex;
  return true;
}

const AudioChunk* AudioStreamChunker::GetSourceChunkForOutput(int outputPoolIdx) const
{
  return mPool.GetInputChunk(outputPoolIdx);
}

// ============================================================================
// Spectral Processing
// ============================================================================

void AudioStreamChunker::SpectralProcessing(int poolIdx)
{
  if (poolIdx < 0 || poolIdx >= mPool.GetPoolCapacity()) return;
  if (mFFTSize <= 0) return;

  auto* entry = mPool.GetEntry(poolIdx);
  if (!entry) return;

  const bool autotuneActive = mAutotuneProcessor.IsActive();
  const bool morphActive = mMorph && mMorph->IsActive();
  const bool spectralActive = morphActive || autotuneActive;

  if (spectralActive)
  {
    // Ensure spectra are computed
    EnsureChunkSpectrum(entry->inputChunk);
    EnsureChunkSpectrum(entry->outputChunk);

    if (autotuneActive)
      mAutotuneProcessor.Process(entry->inputChunk, entry->outputChunk, mFFT);

    if (morphActive)
      mMorph->Process(entry->inputChunk, entry->outputChunk, mFFT);

    // Synthesize back to time domain
    mFFT.ComputeChunkIFFT(entry->outputChunk);

    // Polish edges to avoid artifacts
    for (int ch = 0; ch < mNumChannels; ++ch)
      mOutputWindow.Polish(entry->outputChunk.channelSamples[ch].data());
  }
}

// ============================================================================
// Private Helper Methods
// ============================================================================

void AudioStreamChunker::ResetState()
{
  mAccumulatedFrames = 0;
  mOutputFrontFrameIndex = 0;
  mTotalInputSamplesPushed = 0;
  mTotalOutputSamplesRendered = 0;
  mOLASynthesizer.Reset();
}

void AudioStreamChunker::UpdateSpectralRescale()
{
  const float ovl = mInputAnalysisWindow.GetOverlap();
  const int hop = std::max(1, static_cast<int>(std::lround(mChunkSize * (1.0 - ovl))));
  mSpectralOLARescale = ComputeOLARescale(mInputAnalysisWindow, mChunkSize, hop);
}

bool AudioStreamChunker::IsSpectralProcessingActive() const
{
  return (mMorph && mMorph->IsActive()) || mAutotuneProcessor.IsActive();
}

bool AudioStreamChunker::ShouldUseOverlapAdd(bool spectralActive) const
{
  if (!mEnableOverlap) return false;
  return spectralActive
    ? (mInputAnalysisWindow.GetOverlap() > 0.0f)
    : (mOutputWindow.GetOverlap() > 0.0f);
}

int AudioStreamChunker::ComputeInputHopSize() const
{
  const bool spectralActive = IsSpectralProcessingActive();
  const bool overlapActive = mEnableOverlap && (spectralActive
    ? (mInputAnalysisWindow.GetOverlap() > 0.0f)
    : (mOutputWindow.GetOverlap() > 0.0f));

  if (!overlapActive) return mChunkSize;

  const float ovl = spectralActive ? mInputAnalysisWindow.GetOverlap() : mOutputWindow.GetOverlap();
  return std::max(1, static_cast<int>(std::lround(mChunkSize * (1.0 - ovl))));
}

bool AudioStreamChunker::ProcessAccumulatedChunk(int hopSize)
{
  int poolIdx;
  if (!mPool.Free().Pop(poolIdx))
    return false;

  auto* entry = mPool.GetEntry(poolIdx);

  // Copy accumulation to pool entry
  for (int ch = 0; ch < mNumChannels; ++ch)
  {
    std::memcpy(entry->inputChunk.channelSamples[ch].data(),
                mAccumulation[ch].data(),
                sizeof(iplug::sample) * mChunkSize);
  }
  entry->inputChunk.numFrames = mChunkSize;
  entry->inputChunk.startSample = mTotalInputSamplesPushed - mAccumulatedFrames;
  entry->inputChunk.rms = ComputeChunkRMS(entry->inputChunk, mChunkSize);

  // Add to lookahead window
  AddToWindow(poolIdx);

  // Add to pending queue
  AddToPending(poolIdx);

  // Compute input spectrum
  if (mFFTSize > 0)
    mFFT.ComputeChunkSpectrum(entry->inputChunk, mInputAnalysisWindow);

  return true;
}

void AudioStreamChunker::AddToWindow(int poolIdx)
{
  auto& window = mPool.Window();
  if (window.Full())
  {
    int oldIdx;
    window.Pop(oldIdx);
    mPool.DecRefAndMaybeFree(oldIdx);
  }
  window.Push(poolIdx);
  mPool.IncRef(poolIdx);
}

void AudioStreamChunker::AddToPending(int poolIdx)
{
  auto& pending = mPool.Pending();
  if (!pending.Push(poolIdx))
  {
    int dropped;
    if (pending.Pop(dropped))
      mPool.DecRefAndMaybeFree(dropped);
    pending.Push(poolIdx);
  }
  mPool.IncRef(poolIdx);
}

void AudioStreamChunker::ShiftAccumulationBuffer(int hopSize)
{
  mAccumulatedFrames -= hopSize;
  if (mAccumulatedFrames > 0)
  {
    for (int ch = 0; ch < mNumChannels; ++ch)
    {
      std::memmove(mAccumulation[ch].data(),
                   mAccumulation[ch].data() + hopSize,
                   sizeof(iplug::sample) * mAccumulatedFrames);
    }
  }
  else
  {
    mAccumulatedFrames = 0;
  }
}

double AudioStreamChunker::ComputeChunkRMS(const AudioChunk& chunk, int numFrames) const
{
  double sumSquares = 0.0;
  int totalSamples = 0;

  for (int ch = 0; ch < mNumChannels && ch < static_cast<int>(chunk.channelSamples.size()); ++ch)
  {
    const auto& data = chunk.channelSamples[ch];
    for (int i = 0; i < numFrames && i < static_cast<int>(data.size()); ++i)
    {
      sumSquares += data[i] * data[i];
      ++totalSamples;
    }
  }

  return totalSamples > 0 ? std::sqrt(sumSquares / totalSamples) : 0.0;
}

void AudioStreamChunker::EnsureChunkSpectrum(AudioChunk& chunk)
{
  if (mFFTSize <= 0) return;
  if (chunk.fftSize != mFFTSize ||
      static_cast<int>(chunk.complexSpectrum.size()) != static_cast<int>(chunk.channelSamples.size()))
  {
    chunk.fftSize = 0;  // Signal to recompute
  }
  mFFT.ComputeChunkSpectrum(chunk, mInputAnalysisWindow);
}

float AudioStreamChunker::ComputeAGC(int outputIdx, bool agcEnabled) const
{
  if (!agcEnabled) return 1.0f;
  if (outputIdx < 0 || outputIdx >= mPool.GetPoolCapacity()) return 1.0f;

  const AudioChunk* sourceChunk = GetSourceChunkForOutput(outputIdx);
  const auto* entry = mPool.GetEntry(outputIdx);
  if (!entry) return 1.0f;

  const bool spectralActive = IsSpectralProcessingActive();
  const bool overlapActive = ShouldUseOverlapAdd(spectralActive);

  double num = sourceChunk ? sourceChunk->rms : 0.0;
  double denom = entry->outputChunk.rms;

  if (spectralActive && sourceChunk)
  {
    const double Ein = FFTProcessor::ComputeChunkSpectralEnergy(*sourceChunk);
    const double Eout = FFTProcessor::ComputeChunkSpectralEnergy(entry->outputChunk);
    num = std::sqrt(std::max(0.0, Ein));
    denom = std::sqrt(std::max(0.0, Eout));
  }

  if (overlapActive)
  {
    const float finalRescale = spectralActive ? mSpectralOLARescale : mOutputWindow.GetOverlapRescale();
    const float olaGain = spectralActive
      ? ((finalRescale > 1e-9f) ? (1.0f / finalRescale) : 1.0f)
      : 1.0f;
    denom *= static_cast<double>(olaGain * finalRescale);
  }

  return (denom > 1e-9) ? static_cast<float>(num / denom) : 1.0f;
}

void AudioStreamChunker::RenderWithOverlapAdd(iplug::sample** outputs, int nFrames, int chansToWrite,
                                              int outChans, bool spectralActive, bool agcEnabled)
{
  const float ovl = spectralActive ? mInputAnalysisWindow.GetOverlap() : mOutputWindow.GetOverlap();
  const int hopSize = std::max(1, static_cast<int>(std::lround(mChunkSize * (1.0 - ovl))));
  const float rescale = spectralActive ? mSpectralOLARescale : mOutputWindow.GetOverlapRescale();

  // Process queued output chunks
  auto& output = mPool.Output();
  while (!output.Empty())
  {
    int idx;
    output.Pop(idx);
    auto* entry = mPool.GetEntry(idx);

    if (entry && entry->outputChunk.numFrames > 0)
    {
      SpectralProcessing(idx);
      const float agc = ComputeAGC(idx, agcEnabled);

      // Get window coefficients for non-spectral path
      const std::vector<float>* windowCoeffs = nullptr;
      if (!spectralActive)
      {
        if (mOutputWindow.Size() != entry->outputChunk.numFrames)
          mOutputWindow.Set(mOutputWindow.GetType(), entry->outputChunk.numFrames);
        windowCoeffs = &mOutputWindow.Coeffs();
      }

      // Add to OLA buffer
      mOLASynthesizer.AddChunk(entry->outputChunk, windowCoeffs, agc, hopSize);
    }

    mPool.DecRefAndMaybeFree(idx);
  }

  // Render output with latency control
  const int64_t samplesAvailableToRender = mTotalInputSamplesPushed - mChunkSize - mTotalOutputSamplesRendered;
  const int64_t maxToRender = std::max(static_cast<int64_t>(0), samplesAvailableToRender);

  int rendered = mOLASynthesizer.RenderOutput(outputs, nFrames, chansToWrite, rescale, maxToRender);
  mTotalOutputSamplesRendered += rendered;

  // Zero remainder if needed
  if (rendered < nFrames)
  {
    for (int ch = 0; ch < outChans; ++ch)
      std::memset(outputs[ch] + rendered, 0, sizeof(iplug::sample) * (nFrames - rendered));
  }
}

void AudioStreamChunker::RenderSequential(iplug::sample** outputs, int nFrames, int chansToWrite,
                                          int outChans, bool spectralActive, bool agcEnabled)
{
  auto& output = mPool.Output();

  for (int s = 0; s < nFrames; ++s)
  {
    const bool canOutput = (mTotalOutputSamplesRendered < mTotalInputSamplesPushed - mChunkSize);

    // Zero output first
    for (int ch = 0; ch < outChans; ++ch)
      if (outputs[ch]) outputs[ch][s] = 0.0;

    if (canOutput && !output.Empty())
    {
      int idx = output.PeekOldest();
      if (idx >= 0 && idx < mPool.GetPoolCapacity())
      {
        auto* entry = mPool.GetEntry(idx);

        // Spectral processing at start of each chunk
        if (mOutputFrontFrameIndex == 0 && entry->outputChunk.numFrames > 0)
          SpectralProcessing(idx);

        if (mOutputFrontFrameIndex < entry->outputChunk.numFrames)
        {
          const float agc = ComputeAGC(idx, agcEnabled);

          // Apply windowing for non-spectral path
          float windowCoeff = 1.0f;
          if (!spectralActive && mOutputWindow.GetOverlap() > 0.0f &&
              mOutputFrontFrameIndex < mOutputWindow.Size())
          {
            windowCoeff = mOutputWindow.Coeffs()[mOutputFrontFrameIndex];
          }

          for (int ch = 0; ch < chansToWrite; ++ch)
          {
            if (outputs[ch] && ch < static_cast<int>(entry->outputChunk.channelSamples.size()) &&
                mOutputFrontFrameIndex < static_cast<int>(entry->outputChunk.channelSamples[ch].size()))
            {
              outputs[ch][s] = entry->outputChunk.channelSamples[ch][mOutputFrontFrameIndex] * windowCoeff * agc;
            }
          }
        }

        ++mOutputFrontFrameIndex;
        ++mTotalOutputSamplesRendered;

        if (mOutputFrontFrameIndex >= entry->outputChunk.numFrames)
        {
          int finished;
          output.Pop(finished);
          mPool.DecRefAndMaybeFree(finished);
          mOutputFrontFrameIndex = 0;
        }
      }
    }
  }
}

} // namespace synaptic

