#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "Oscillator.h"
#include "Smoothers.h"
#include "plugin_src/AudioStreamChunker.h"
#include "plugin_src/ChunkBufferTransformer.h"
#include "plugin_src/samplebrain/Brain.h"

using namespace iplug;

const int kNumPresets = 3;

enum EParams
{
  kGain = 0,
  kNumParams
};

enum EMsgTags
{
  kMsgTagButton1 = 0,
  kMsgTagButton2 = 1,
  kMsgTagButton3 = 2,
  kMsgTagBinaryTest = 3,
  kMsgTagSetChunkSize = 4,
  kMsgTagSetBufferWindowSize = 5,
  kMsgTagSetAlgorithm = 6,
  // Brain UI -> C++ messages
  kMsgTagBrainAddFile = 100,
  kMsgTagBrainRemoveFile = 101,
  // C++ -> UI JSON updates use msgTag = -1, with id fields "brainSummary"
};

class SynapticResynthesis final : public Plugin
{
public:
  SynapticResynthesis(const InstanceInfo& info);

  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
  void OnParamChange(int paramIdx) override;
  bool CanNavigateToURL(const char* url);
  bool OnCanDownloadMIMEType(const char* mimeType) override;
  void OnFailedToDownloadFile(const char* path) override;
  void OnDownloadedFile(const char* path) override;
  void OnGetLocalDownloadPathForFile(const char* fileName, WDL_String& localPath) override;

private:
  float mLastPeak = 0.;
  FastSinOscillator<sample> mOscillator {0., 440.};
  LogParamSmooth<sample, 1> mGainSmoother;
  int mChunkSize = 4096;
  int mBufferWindowSize = 4;
  synaptic::AudioStreamChunker mChunker {2};
  std::unique_ptr<synaptic::IChunkBufferTransformer> mTransformer;
  int ComputeLatencySamples() const { return mChunkSize + (mTransformer ? mTransformer->GetAdditionalLatencySamples(mChunkSize, mBufferWindowSize) : 0); }

  // Samplebrain in-memory state
  synaptic::Brain mBrain;
  void SendBrainSummaryToUI();
};
