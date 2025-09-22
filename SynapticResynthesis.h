#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "Oscillator.h"
#include "Smoothers.h"
#include "plugin_src/AudioStreamChunker.h"
#include "plugin_src/ChunkBufferTransformer.h"
#include "plugin_src/samplebrain/Brain.h"
#include "plugin_src/Window.h"
#include <atomic>

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
  kMsgTagSetOutputWindowMode = 7,
  kMsgTagSetAlgorithm = 6,
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
  kMsgTagBrainReset = 106,
  kMsgTagBrainDetach = 107,
  // C++ -> UI JSON updates use msgTag = -1, with id fields "brainSummary"

};

class SynapticResynthesis final : public Plugin
{
public:
  SynapticResynthesis(const InstanceInfo& info);

  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
  void OnUIOpen() override;
  void OnRestoreState() override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
  void OnParamChange(int paramIdx) override;
  // state serialization
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;
  bool CanNavigateToURL(const char* url);
  bool OnCanDownloadMIMEType(const char* mimeType) override;
  void OnFailedToDownloadFile(const char* path) override;
  void OnDownloadedFile(const char* path) override;
  void OnGetLocalDownloadPathForFile(const char* fileName, WDL_String& localPath) override;

private:
  float mLastPeak = 0.;
  FastSinOscillator<sample> mOscillator {0., 440.};
  LogParamSmooth<sample, 1> mGainSmoother;
  int mChunkSize = 3000;
  int mBufferWindowSize = 1;
  int mOutputWindowMode = 1; // 1=Hann,2=Hamming,3=Blackman,4=Rectangular
  synaptic::AudioStreamChunker mChunker {2};
  std::unique_ptr<synaptic::IChunkBufferTransformer> mTransformer;
  int mAlgorithmId = 0; // 0=passthrough, 1=sine, 2=samplebrain
  // Indices of core params created at runtime
  int mParamIdxChunkSize = -1;
  int mParamIdxBufferWindow = -1;
  int mParamIdxOutputWindow = -1;
  int mParamIdxAlgorithm = -1;
  int mParamIdxDirtyFlag = -1; // hidden internal param used to nudge host dirty state
  struct TransformerParamBinding {
    std::string id;
    synaptic::IChunkBufferTransformer::ParamType type;
    int paramIdx = -1;
    // For enums, map index<->string value
    std::vector<std::string> enumValues; // order corresponds to indices 0..N-1
  };
  std::vector<TransformerParamBinding> mTransformerBindings; // union across all transformers
  int ComputeLatencySamples() const { return mChunkSize + (mTransformer ? mTransformer->GetAdditionalLatencySamples(mChunkSize, mBufferWindowSize) : 0); }

  // Samplebrain in-memory state
  synaptic::Brain mBrain;
  synaptic::Window mWindow;
  // External snapshot reference (Proposal 2 step)
  std::string mExternalBrainPath;
  bool mUseExternalBrain = false;
  mutable bool mBrainDirty = false;
  std::atomic<bool> mRechunking { false };
  void SendBrainSummaryToUI();
  void SendTransformerParamsToUI();
  void SendDSPConfigToUI();
  void ApplyTransformerParamsFromIParams();
  // Notify host that state changed (e.g., brain edited) so host marks project as modified
  void MarkHostStateDirty();
};
