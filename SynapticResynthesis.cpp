#include "SynapticResynthesis.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"
#include "json.hpp"
#include "Extras/WebView/IPlugWebViewEditorDelegate.h"

SynapticResynthesis::SynapticResynthesis(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kGain)->InitGain("Gain", -70., -70, 0.);

#ifdef DEBUG
  SetEnableDevTools(true);
#endif

  mEditorInitFunc = [&]()
  {
    LoadIndexHtml(__FILE__, GetBundleID());
    EnableScroll(false);
  };

  MakePreset("One", -70.);
  MakePreset("Two", -30.);
  MakePreset("Three", 0.);

  // Default transformer = passthrough
  mTransformer = std::make_unique<synaptic::PassthroughTransformer>();
  // Note: OnReset will be called later with proper channel counts
}

void SynapticResynthesis::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const double gain = GetParam(kGain)->DBToAmp();

  // Safety check for valid inputs/outputs
  const int inChans = NInChansConnected();
  const int outChans = NOutChansConnected();
  if (inChans <= 0 || outChans <= 0 || !inputs || !outputs)
  {
    // Clear outputs and return
    for (int ch = 0; ch < outChans; ++ch)
      if (outputs[ch])
        std::memset(outputs[ch], 0, sizeof(sample) * nFrames);
    return;
  }

  // Feed the input into the chunker
  mChunker.PushAudio(inputs, nFrames);

  // Transform pending input chunks -> output queue (gate by lookahead)
  if (mTransformer)
  {
    const int required = mTransformer->GetRequiredLookaheadChunks();
    if (mChunker.GetWindowCount() >= required)
      mTransformer->Process(mChunker);
  }

  // Render queued output to the host buffers
  mChunker.RenderOutput(outputs, nFrames, outChans);

  // Apply gain
  for (int s = 0; s < nFrames; s++)
  {
    const double smoothedGain = mGainSmoother.Process(gain);
    for (int ch = 0; ch < outChans; ++ch)
      outputs[ch][s] *= smoothedGain;
  }
}

void SynapticResynthesis::OnReset()
{
  auto sr = GetSampleRate();
  mOscillator.SetSampleRate(sr);
  mGainSmoother.SetSmoothTime(20., sr);
  mChunker.SetChunkSize(mChunkSize);
  mChunker.SetBufferWindowSize(mBufferWindowSize);
  // Ensure chunker channel count matches current connection
  mChunker.SetNumChannels(NInChansConnected());
  mChunker.Reset();

  // Report algorithmic latency to the host (in samples)
  SetLatency(ComputeLatencySamples());

  if (mTransformer)
    mTransformer->OnReset(sr, mChunkSize, mBufferWindowSize, NInChansConnected());

  // When audio engine resets, leave brain state intact; just resend summary to UI
  SendBrainSummaryToUI();
}

bool SynapticResynthesis::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  if (msgTag == kMsgTagButton1)
    Resize(512, 335);
  else if(msgTag == kMsgTagButton2)
    Resize(1024, 335);
  else if(msgTag == kMsgTagButton3)
    Resize(1024, 768);
  else if (msgTag == kMsgTagBinaryTest)
  {
    auto uint8Data = reinterpret_cast<const uint8_t*>(pData);
    DBGMSG("Data Size %i bytes\n",  dataSize);
    DBGMSG("Byte values: %i, %i, %i, %i\n", uint8Data[0], uint8Data[1], uint8Data[2], uint8Data[3]);
  }
  else if (msgTag == kMsgTagSetChunkSize)
  {
    // ctrlTag carries the integer value from UI
    mChunkSize = std::max(1, ctrlTag);
    DBGMSG("Set Chunk Size: %i\n", mChunkSize);
    mChunker.SetChunkSize(mChunkSize);
    // Notify UI brain chunk size and trigger rechunk placeholder
    nlohmann::json j; j["id"] = "brainChunkSize"; j["size"] = mChunkSize;
    const std::string payload = j.dump();
    SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
    // Rechunk brain (clears for now)
    mBrain.RechunkAllFiles(mChunkSize, (int) GetSampleRate());
    SendBrainSummaryToUI();
    SetLatency(ComputeLatencySamples());
    return true;
  }
  else if (msgTag == kMsgTagSetBufferWindowSize)
  {
    mBufferWindowSize = std::max(1, ctrlTag);
    DBGMSG("Set Buffer Window Size: %i\n", mBufferWindowSize);
    mChunker.SetBufferWindowSize(mBufferWindowSize);
    // For passthrough, latency does not depend on window size; no change here
    return true;
  }
  else if (msgTag == kMsgTagSetAlgorithm)
  {
    // ctrlTag selects algorithm ID; 0 = passthrough, 1 = sine match
    if (ctrlTag == 0)
      mTransformer = std::make_unique<synaptic::PassthroughTransformer>();
    else if (ctrlTag == 1)
      mTransformer = std::make_unique<synaptic::SineMatchTransformer>();
    else if (ctrlTag == 2)
    {
      auto t = std::make_unique<synaptic::SimpleSampleBrainTransformer>();
      t->SetBrain(&mBrain);
      mTransformer = std::move(t);
    }

    if (mTransformer)
      mTransformer->OnReset(GetSampleRate(), mChunkSize, mBufferWindowSize, NInChansConnected());

    SetLatency(ComputeLatencySamples());
    return true;
  }
  else if (msgTag == kMsgTagBrainAddFile)
  {
    // pData holds raw bytes of a small header + file data. Header format:
    // [uint16_t nameLenLE][name bytes UTF-8][file bytes]
    if (!pData || dataSize <= 2)
      return false;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(pData);
    uint16_t nameLen = (uint16_t) (bytes[0] | (bytes[1] << 8));
    if (2 + nameLen > dataSize)
      return false;
    std::string name(reinterpret_cast<const char*>(bytes + 2), reinterpret_cast<const char*>(bytes + 2 + nameLen));
    const void* fileData = bytes + 2 + nameLen;
    size_t fileSize = static_cast<size_t>(dataSize - (2 + nameLen));

    DBGMSG("BrainAddFile: name=%s size=%zu SR=%d CH=%d chunk=%d\n", name.c_str(), fileSize, (int) GetSampleRate(), NInChansConnected(), mChunkSize);
    int newId = mBrain.AddAudioFileFromMemory(fileData, fileSize, name, (int) GetSampleRate(), NInChansConnected(), mChunkSize);
    if (newId >= 0)
      SendBrainSummaryToUI(); // UI will hide overlay when it receives brainSummary
    return newId >= 0;
  }
  else if (msgTag == kMsgTagBrainRemoveFile)
  {
    // ctrlTag carries fileId
    DBGMSG("BrainRemoveFile: id=%d\n", ctrlTag);
    mBrain.RemoveFile(ctrlTag);
    SendBrainSummaryToUI();
    return true;
  }

  return false;
}

void SynapticResynthesis::SendBrainSummaryToUI()
{
  nlohmann::json j;
  j["id"] = "brainSummary";
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& s : mBrain.GetSummary())
  {
    nlohmann::json o;
    o["id"] = s.id;
    o["name"] = s.name;
    o["chunks"] = s.chunkCount;
    arr.push_back(o);
  }
  j["files"] = arr;
  const std::string payload = j.dump();
  SendArbitraryMsgFromDelegate(-1, static_cast<int>(payload.size()), payload.c_str());
}

void SynapticResynthesis::OnParamChange(int paramIdx)
{
  DBGMSG("gain %f\n", GetParam(paramIdx)->Value());
}

void SynapticResynthesis::ProcessMidiMsg(const IMidiMsg& msg)
{
  TRACE;

  msg.PrintMsg();
  SendMidiMsg(msg);
}

bool SynapticResynthesis::CanNavigateToURL(const char* url)
{
  DBGMSG("Navigating to URL %s\n", url);

  return true;
}

bool SynapticResynthesis::OnCanDownloadMIMEType(const char* mimeType)
{
  return std::string_view(mimeType) != "text/html";
}

void SynapticResynthesis::OnDownloadedFile(const char* path)
{
  WDL_String str;
  str.SetFormatted(64, "Downloaded file to %s\n", path);
  LoadHTML(str.Get());
}

void SynapticResynthesis::OnFailedToDownloadFile(const char* path)
{
  WDL_String str;
  str.SetFormatted(64, "Faild to download file to %s\n", path);
  LoadHTML(str.Get());
}

void SynapticResynthesis::OnGetLocalDownloadPathForFile(const char* fileName, WDL_String& localPath)
{
  DesktopPath(localPath);
  localPath.AppendFormatted(MAX_WIN32_PATH_LEN, "/%s", fileName);
}
