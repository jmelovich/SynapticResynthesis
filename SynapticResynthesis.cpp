#include "SynapticResynthesis.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"
#include "json.hpp"
#include "Extras/WebView/IPlugWebViewEditorDelegate.h"
#include "plugin_src/TransformerFactory.h"
#include "plugin_src/PlatformFileDialogs.h"
#include <thread>
#include <mutex>
#ifdef AAX_API
#include "IPlugAAX.h"
#endif

namespace {
  // Build a union of transformer parameter descs across all known transformers (by id)
  static void BuildTransformerUnion(std::vector<synaptic::IChunkBufferTransformer::ExposedParamDesc>& out)
  {
    out.clear();
    std::vector<synaptic::IChunkBufferTransformer::ExposedParamDesc> tmp;
    auto consider = [&](std::unique_ptr<synaptic::IChunkBufferTransformer> t){
      tmp.clear(); t->GetParamDescs(tmp);
      for (const auto& d : tmp)
      {
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& e){ return e.id == d.id; });
        if (it == out.end()) out.push_back(d);
      }
    };
    // Iterate all known transformers from the factory
    for (const auto& info : synaptic::TransformerFactory::GetAll())
      consider(info.create());
  }

  static int ComputeTotalParams()
  {
    // base params (kNumParams) + ChunkSize + BufferWindow + Algorithm + OutputWindow + DirtyFlag + AnalysisWindow + EnableOverlap + union of transformer params
    std::vector<synaptic::IChunkBufferTransformer::ExposedParamDesc> unionDescs;
    BuildTransformerUnion(unionDescs);
    return kNumParams + 7 + (int) unionDescs.size();
  }
}

SynapticResynthesis::SynapticResynthesis(const InstanceInfo& info)
: Plugin(info, MakeConfig(ComputeTotalParams(), kNumPresets))
{
  GetParam(kInGain)->InitGain("Input Gain", 0.0, -70, 0.);
  GetParam(kOutGain)->InitGain("Output Gain", 0.0, -70, 0.);

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

  // Default transformer = first UI-visible entry
  mAlgorithmId = 0;
  mTransformer = synaptic::TransformerFactory::CreateByUiIndex(mAlgorithmId);
  if (auto sb = dynamic_cast<synaptic::SimpleSampleBrainTransformer*>(mTransformer.get()))
    sb->SetBrain(&mBrain);

  // Initialize window with default Hann window
  mWindow.Set(synaptic::Window::Type::Hann, mChunkSize); // Default size, will be updated as needed

  // Set the window reference in the Brain
  mBrain.SetWindow(&mWindow);

  // Note: OnReset will be called later with proper channel counts

  // Create core DSP params into the pre-allocated slots
  mParamIdxChunkSize = kNumParams + 0; GetParam(mParamIdxChunkSize)->InitInt("Chunk Size", mChunkSize, 1, 262144, "samples", IParam::kFlagCannotAutomate);
  // Buffer window size is no longer user-exposed (internal). Reserve slot but don't publish.
  mParamIdxBufferWindow = kNumParams + 1; GetParam(mParamIdxBufferWindow)->InitInt("Buffer Window", mBufferWindowSize, 1, 1024, "chunks", IParam::kFlagCannotAutomate);
  // Hidden dirty flag param solely for host-dirty nudges (non-automatable)
  mParamIdxDirtyFlag = kNumParams + 4; // allocate dedicated slot
  GetParam(mParamIdxDirtyFlag)->InitBool("Dirty Flag", false, "", IParam::kFlagCannotAutomate);
  // Build algorithm enum from factory UI list (deterministic)
  mParamIdxAlgorithm = kNumParams + 2;
  {
    const int count = synaptic::TransformerFactory::GetUiCount();
    GetParam(mParamIdxAlgorithm)->InitEnum("Algorithm", mAlgorithmId, count, "");
    const auto labels = synaptic::TransformerFactory::GetUiLabels();
    for (int i = 0; i < (int) labels.size(); ++i)
      GetParam(mParamIdxAlgorithm)->SetDisplayText(i, labels[i].c_str());
  }
  // Output window function (global)
  mParamIdxOutputWindow = kNumParams + 3;
  GetParam(mParamIdxOutputWindow)->InitEnum("Output Window", mOutputWindowMode - 1, 4, "");
  GetParam(mParamIdxOutputWindow)->SetDisplayText(0, "Hann");
  GetParam(mParamIdxOutputWindow)->SetDisplayText(1, "Hamming");
  GetParam(mParamIdxOutputWindow)->SetDisplayText(2, "Blackman");
  GetParam(mParamIdxOutputWindow)->SetDisplayText(3, "Rectangular");

  // Analysis window function (for brain analysis)
  mParamIdxAnalysisWindow = kNumParams + 5;
  GetParam(mParamIdxAnalysisWindow)->InitEnum("Chunk Analysis Window", mAnalysisWindowMode - 1, 4, "", IParam::kFlagCannotAutomate);
  GetParam(mParamIdxAnalysisWindow)->SetDisplayText(0, "Hann");
  GetParam(mParamIdxAnalysisWindow)->SetDisplayText(1, "Hamming");
  GetParam(mParamIdxAnalysisWindow)->SetDisplayText(2, "Blackman");
  GetParam(mParamIdxAnalysisWindow)->SetDisplayText(3, "Rectangular");

  // Enable overlap-add processing
  mParamIdxEnableOverlap = kNumParams + 6;
  GetParam(mParamIdxEnableOverlap)->InitBool("Enable Overlap-Add", mEnableOverlapAdd);

  // Build union descs and initialize the remaining pre-allocated params
  std::vector<synaptic::IChunkBufferTransformer::ExposedParamDesc> unionDescs;
  BuildTransformerUnion(unionDescs);
  int base = kNumParams + 7;
  mTransformerBindings.clear();
  for (size_t i = 0; i < unionDescs.size(); ++i)
  {
    const auto& d = unionDescs[i];
    const int idx = base + (int) i;
    switch (d.type)
    {
      case synaptic::IChunkBufferTransformer::ParamType::Number:
        GetParam(idx)->InitDouble(d.label.c_str(), d.defaultNumber, d.minValue, d.maxValue, d.step);
        break;
      case synaptic::IChunkBufferTransformer::ParamType::Boolean:
        GetParam(idx)->InitBool(d.label.c_str(), d.defaultBool);
        break;
      case synaptic::IChunkBufferTransformer::ParamType::Enum:
      {
        const int n = (int) d.options.size();
        GetParam(idx)->InitEnum(d.label.c_str(), 0, n, "");
        // Apply display texts for enum items
        for (int k = 0; k < n; ++k)
        {
          const char* lab = (k < n) ? d.options[k].label.c_str() : "";
          GetParam(idx)->SetDisplayText(k, lab);
        }
        TransformerParamBinding binding{d.id, d.type, idx};
        for (const auto& opt : d.options) binding.enumValues.push_back(opt.value);
        mTransformerBindings.push_back(std::move(binding));
        continue;
      }
      case synaptic::IChunkBufferTransformer::ParamType::Text:
        GetParam(idx)->InitDouble(d.label.c_str(), 0.0, 0.0, 1.0, 0.01, "", IParam::kFlagCannotAutomate);
        break;
    }
    mTransformerBindings.push_back({d.id, d.type, idx, {}});
  }
}
void SynapticResynthesis::EnqueueUiPayload(const std::string& payload)
{
  std::lock_guard<std::mutex> lock(mUiQueueMutex);
  mUiQueue.push_back(payload);
}

void SynapticResynthesis::DrainUiQueueOnMainThread()
{
  // Coalesce structured resend flags first
  if (mPendingSendBrainSummary.exchange(false))
    SendBrainSummaryToUI();
  if (mPendingSendDSPConfig.exchange(false))
    SendDSPConfigToUI();
  if (mPendingMarkDirty.exchange(false))
    MarkHostStateDirty();

  // Drain queued JSON payloads
  std::vector<std::string> local;
  {
    std::lock_guard<std::mutex> lock(mUiQueueMutex);
    if (!mUiQueue.empty())
    {
      local.swap(mUiQueue);
    }
  }
  // Apply any pending imported settings (chunk size + analysis window) on main thread
  {
    const int impCS = mPendingImportedChunkSize.exchange(-1);
    const int impAW = mPendingImportedAnalysisWindow.exchange(-1);
    if (impCS > 0 || impAW > 0)
    {
      if (impCS > 0 && mParamIdxChunkSize >= 0)
      {
        const double norm = GetParam(mParamIdxChunkSize)->ToNormalized((double) impCS);
        BeginInformHostOfParamChangeFromUI(mParamIdxChunkSize);
        SendParameterValueFromUI(mParamIdxChunkSize, norm);
        EndInformHostOfParamChangeFromUI(mParamIdxChunkSize);
        mChunkSize = impCS;
        mChunker.SetChunkSize(mChunkSize);
      }
      if (impAW > 0 && mParamIdxAnalysisWindow >= 0)
      {
        const int idx = std::clamp(impAW - 1, 0, 3);
        const double norm = GetParam(mParamIdxAnalysisWindow)->ToNormalized((double) idx);
        mSuppressNextAnalysisReanalyze = true;
        BeginInformHostOfParamChangeFromUI(mParamIdxAnalysisWindow);
        SendParameterValueFromUI(mParamIdxAnalysisWindow, norm);
        EndInformHostOfParamChangeFromUI(mParamIdxAnalysisWindow);
        mAnalysisWindowMode = impAW;
      }
      // Update analysis window instance and Brain pointer, but suppress auto-reanalysis because data already analyzed in file
      mWindow.Set(IntToWindowType(mAnalysisWindowMode), mChunkSize);
      mBrain.SetWindow(&mWindow);
      SendDSPConfigToUI();
    }
  }
  for (const auto& s : local)
    SendArbitraryMsgFromDelegate(-1, (int) s.size(), s.c_str());
}


void SynapticResynthesis::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const double inGain = GetParam(kInGain)->DBToAmp();
  const double outGain = GetParam(kOutGain)->DBToAmp();

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
    const double smoothedGain = mGainSmoother.Process(outGain);
    for (int ch = 0; ch < outChans; ++ch)
      outputs[ch][s] *= smoothedGain;
  }
}

void SynapticResynthesis::OnReset()
{
  auto sr = GetSampleRate();
  mOscillator.SetSampleRate(sr);
  mGainSmoother.SetSmoothTime(20., sr);
  // Pull current values from IParams
  if (mParamIdxChunkSize >= 0) mChunkSize = std::max(1, GetParam(mParamIdxChunkSize)->Int());
  if (mParamIdxBufferWindow >= 0) mBufferWindowSize = std::max(1, GetParam(mParamIdxBufferWindow)->Int());
  if (mParamIdxAlgorithm >= 0) mAlgorithmId = GetParam(mParamIdxAlgorithm)->Int();
  if (mParamIdxOutputWindow >= 0) mOutputWindowMode = 1 + std::clamp(GetParam(mParamIdxOutputWindow)->Int(), 0, 3);
  if (mParamIdxAnalysisWindow >= 0) mAnalysisWindowMode = 1 + std::clamp(GetParam(mParamIdxAnalysisWindow)->Int(), 0, 3);
  if (mParamIdxEnableOverlap >= 0) mEnableOverlapAdd = GetParam(mParamIdxEnableOverlap)->Bool();

  mWindow.Set(IntToWindowType(mAnalysisWindowMode), mChunkSize);
  mBrain.SetWindow(&mWindow);

  mChunker.SetChunkSize(mChunkSize);
  mChunker.SetBufferWindowSize(mBufferWindowSize);
  // Ensure chunker channel count matches current connection
  mChunker.SetNumChannels(NInChansConnected());
  mChunker.Reset();

  UpdateChunkerWindowing();

  // Report algorithmic latency to the host (in samples)
  SetLatency(ComputeLatencySamples());

  if (mTransformer)
    mTransformer->OnReset(sr, mChunkSize, mBufferWindowSize, NInChansConnected());
  // After reset, apply IParam values to transformer implementation
  {
    // Reuse the binding list to push current values into transformer
    for (const auto& b : mTransformerBindings)
    {
      if (b.paramIdx < 0) continue;
      switch (b.type)
      {
        case synaptic::IChunkBufferTransformer::ParamType::Number:
          if (GetParam(b.paramIdx)) mTransformer->SetParamFromNumber(b.id, GetParam(b.paramIdx)->Value());
          break;
        case synaptic::IChunkBufferTransformer::ParamType::Boolean:
          if (GetParam(b.paramIdx)) mTransformer->SetParamFromBool(b.id, GetParam(b.paramIdx)->Bool());
          break;
        case synaptic::IChunkBufferTransformer::ParamType::Enum:
        {
          if (GetParam(b.paramIdx))
          {
            int idx = GetParam(b.paramIdx)->Int();
            std::string val = (idx >= 0 && idx < (int) b.enumValues.size()) ? b.enumValues[idx] : std::to_string(idx);
            mTransformer->SetParamFromString(b.id, val);
          }
          break;
        }
        case synaptic::IChunkBufferTransformer::ParamType::Text:
          break;
      }
    }
  }

  // When audio engine resets, leave brain state intact; just resend summary to UI
  SendBrainSummaryToUI();
  // Send current transformer parameters schema/values
  SendTransformerParamsToUI();
  // Send DSP config (algorithm + sizes) to UI
  SendDSPConfigToUI();
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
    const int newSize = std::max(1, ctrlTag);
    if (mParamIdxChunkSize >= 0)
    {
      const double norm = GetParam(mParamIdxChunkSize)->ToNormalized((double) newSize);
      BeginInformHostOfParamChangeFromUI(mParamIdxChunkSize);
      SendParameterValueFromUI(mParamIdxChunkSize, norm);
      EndInformHostOfParamChangeFromUI(mParamIdxChunkSize);
    }
    mChunkSize = newSize;
    DBGMSG("Set Chunk Size: %i\n", mChunkSize);
    mChunker.SetChunkSize(mChunkSize);

    // Update analysis window size to match new chunk size
    mWindow.Set(IntToWindowType(mAnalysisWindowMode), mChunkSize);

    UpdateChunkerWindowing();

    {
      nlohmann::json j; j["id"] = "brainChunkSize"; j["size"] = mChunkSize;
      const std::string payload = j.dump();
      SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
    }
    { nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Rechunking..."); const std::string payload = j.dump(); SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str()); }
    if (!mRechunking.exchange(true))
    {
      // Show overlay once before starting background job
      { nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Rechunking..."); const std::string payload = j.dump(); SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str()); }

      // Update latency and DSP config immediately on UI thread; brain summary will be updated after background completes
      SetLatency(ComputeLatencySamples());
      SendDSPConfigToUI();
      MarkHostStateDirty();

      std::thread([this]()
      {
        auto stats = mBrain.RechunkAllFiles(mChunkSize, (int) GetSampleRate());
        DBGMSG("Brain Rechunk: processed=%d, rechunked=%d, totalChunks=%d\n", stats.filesProcessed, stats.filesRechunked, stats.newTotalChunks);
        mBrainDirty = true;
        // Defer UI updates to main thread: summary + overlay hide
        mPendingSendBrainSummary = true;
        { nlohmann::json j; j["id"] = "overlay"; j["visible"] = false; EnqueueUiPayload(j.dump()); }
        mRechunking = false;
      }).detach();
    }
    else
    {
      DBGMSG("Rechunk request ignored: already running.\n");
    }
    // Early return; background task will finalize UI updates
    return true;
  }
  else if (msgTag == kMsgTagSetBufferWindowSize)
  {
    // Deprecated from UI; ignore but keep for compatibility
    return true;
  }
  else if (msgTag == kMsgTagSetOutputWindowMode)
  {
    // ctrlTag carries an integer enum: 1=Hann,2=Hamming,3=Blackman,4=Rectangular
    mOutputWindowMode = std::clamp(ctrlTag, 1, 4);
    if (mParamIdxOutputWindow >= 0)
    {
      const int idx = mOutputWindowMode - 1;
      const double norm = GetParam(mParamIdxOutputWindow)->ToNormalized((double) idx);
      BeginInformHostOfParamChangeFromUI(mParamIdxOutputWindow);
      SendParameterValueFromUI(mParamIdxOutputWindow, norm);
      EndInformHostOfParamChangeFromUI(mParamIdxOutputWindow);
    }

    UpdateChunkerWindowing();

    SendDSPConfigToUI();
    return true;
  }
  else if (msgTag == kMsgTagSetAnalysisWindowMode)
  {
    // ctrlTag carries an integer enum: 1=Hann,2=Hamming,3=Blackman,4=Rectangular
    auto toWin = [](int v){
      using WT = synaptic::Window::Type;
      switch (v) { case 1: return WT::Hann; case 2: return WT::Hamming; case 3: return WT::Blackman; case 4: return WT::Rectangular; default: return WT::Hann; }
    };
    mAnalysisWindowMode = std::clamp(ctrlTag, 1, 4);
    if (mParamIdxAnalysisWindow >= 0)
    {
      const int idx = mAnalysisWindowMode - 1;
      const double norm = GetParam(mParamIdxAnalysisWindow)->ToNormalized((double) idx);
      BeginInformHostOfParamChangeFromUI(mParamIdxAnalysisWindow);
      SendParameterValueFromUI(mParamIdxAnalysisWindow, norm);
      EndInformHostOfParamChangeFromUI(mParamIdxAnalysisWindow);
    }
    // Update analysis window used by Brain
    mWindow.Set(IntToWindowType(mAnalysisWindowMode), mChunkSize);
    mBrain.SetWindow(&mWindow);
    // Trigger background reanalysis of all existing chunks
    { nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Reanalyzing..."); const std::string payload = j.dump(); SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str()); }
    if (!mRechunking.exchange(true))
    {
      std::thread([this]()
      {
        auto stats = mBrain.ReanalyzeAllChunks((int) GetSampleRate());
        DBGMSG("Brain Reanalyze: files=%d chunks=%d\n", stats.filesProcessed, stats.chunksProcessed);
        mBrainDirty = true;
        mPendingSendBrainSummary = true;
        { nlohmann::json j; j["id"] = "overlay"; j["visible"] = false; EnqueueUiPayload(j.dump()); }
        mPendingMarkDirty = true;
        mRechunking = false;
      }).detach();
    }
    else
    {
      DBGMSG("Reanalyze request ignored: job already running.\n");
    }
    SendDSPConfigToUI();
    return true;
  }
  else if (msgTag == kMsgTagSetAlgorithm)
  {
    // ctrlTag selects algorithm by UI index
    mAlgorithmId = ctrlTag;
    if (mParamIdxAlgorithm >= 0)
    {
      const double norm = GetParam(mParamIdxAlgorithm)->ToNormalized((double) mAlgorithmId);
      BeginInformHostOfParamChangeFromUI(mParamIdxAlgorithm);
      SendParameterValueFromUI(mParamIdxAlgorithm, norm);
      EndInformHostOfParamChangeFromUI(mParamIdxAlgorithm);
    }
    mTransformer = synaptic::TransformerFactory::CreateByUiIndex(mAlgorithmId);
    if (!mTransformer)
    {
      // Fallback to first available
      mAlgorithmId = 0;
      mTransformer = synaptic::TransformerFactory::CreateByUiIndex(mAlgorithmId);
    }
    if (auto sb = dynamic_cast<synaptic::SimpleSampleBrainTransformer*>(mTransformer.get()))
      sb->SetBrain(&mBrain);

    if (mTransformer)
      mTransformer->OnReset(GetSampleRate(), mChunkSize, mBufferWindowSize, NInChansConnected());

    UpdateChunkerWindowing();

    // Reapply persisted IParam values to the new transformer instance
    for (const auto& b : mTransformerBindings)
    {
      if (b.paramIdx < 0) continue;
      switch (b.type)
      {
        case synaptic::IChunkBufferTransformer::ParamType::Number:
          if (GetParam(b.paramIdx)) mTransformer->SetParamFromNumber(b.id, GetParam(b.paramIdx)->Value());
          break;
        case synaptic::IChunkBufferTransformer::ParamType::Boolean:
          if (GetParam(b.paramIdx)) mTransformer->SetParamFromBool(b.id, GetParam(b.paramIdx)->Bool());
          break;
        case synaptic::IChunkBufferTransformer::ParamType::Enum:
        {
          if (GetParam(b.paramIdx))
          {
            int idx = GetParam(b.paramIdx)->Int();
            std::string val = (idx >= 0 && idx < (int) b.enumValues.size()) ? b.enumValues[idx] : std::to_string(idx);
            mTransformer->SetParamFromString(b.id, val);
          }
          break;
        }
        case synaptic::IChunkBufferTransformer::ParamType::Text:
          break;
      }
    }

    SetLatency(ComputeLatencySamples());
    // Send params schema/values for selected transformer
    SendTransformerParamsToUI();
    SendDSPConfigToUI();
    return true;
  }
  else if (msgTag == kMsgTagTransformerSetParam)
  {
    // pData is expected to be raw JSON bytes: {"id":"...","type":"number|boolean|string","value":...}
    if (!pData || dataSize <= 0 || !mTransformer)
      return false;
    const char* bytes = reinterpret_cast<const char*>(pData);
    std::string s(bytes, bytes + dataSize);
    try
    {
      auto j = nlohmann::json::parse(s);
      std::string id = j.value("id", std::string());
      std::string type = j.value("type", std::string());
      bool ok = false;
      if (type == "number" && j.contains("value"))
      {
        double v = j["value"].get<double>();
        ok = mTransformer->SetParamFromNumber(id, v);
      }
      else if (type == "boolean" && j.contains("value"))
      {
        bool v = j["value"].get<bool>();
        ok = mTransformer->SetParamFromBool(id, v);
      }
      else if (type == "text" || type == "string")
      {
        std::string v = j.value("value", std::string());
        ok = mTransformer->SetParamFromString(id, v);
      }
      else if (type == "enum")
      {
        std::string v = j.value("value", std::string());
        // forward as string; transformer can validate against options
        ok = mTransformer->SetParamFromString(id, v);
      }

      if (ok)
      {
        // Mirror to corresponding IParam and inform host as a UI gesture (for last-touched)
        auto it = std::find_if(mTransformerBindings.begin(), mTransformerBindings.end(), [&](const auto& b){ return b.id == id; });
        if (it != mTransformerBindings.end() && it->paramIdx >= 0)
        {
          double normalized = 0.0;
          if (type == "number")
          {
            const double v = j["value"].get<double>();
            normalized = GetParam(it->paramIdx)->ToNormalized(v);
          }
          else if (type == "boolean")
          {
            normalized = j["value"].get<bool>() ? 1.0 : 0.0;
          }
          else if (type == "enum")
          {
            const std::string v = j.value("value", std::string());
            int idx = 0;
            for (int k = 0; k < (int) it->enumValues.size(); ++k)
            {
              if (it->enumValues[k] == v) { idx = k; break; }
            }
            normalized = GetParam(it->paramIdx)->ToNormalized((double) idx);
          }
          BeginInformHostOfParamChangeFromUI(it->paramIdx);
          SendParameterValueFromUI(it->paramIdx, normalized);
          EndInformHostOfParamChangeFromUI(it->paramIdx);
        }
        // echo updated params back to UI
        SendTransformerParamsToUI();
      }
      return ok;
    }
    catch (...)
    {
      return false;
    }
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
    // Show overlay text during import
    { nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Importing ") + name; const std::string payload = j.dump(); SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str()); }
    int newId = mBrain.AddAudioFileFromMemory(fileData, fileSize, name, (int) GetSampleRate(), NInChansConnected(), mChunkSize);
    if (newId >= 0)
    {
      mBrainDirty = true;
      SendBrainSummaryToUI(); // UI will refresh list
      MarkHostStateDirty();
    }
    { nlohmann::json j; j["id"] = "overlay"; j["visible"] = false; const std::string payload = j.dump(); SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str()); }
    return newId >= 0;
  }
  else if (msgTag == kMsgTagBrainExport)
  {
    // Move to background thread to avoid WebView2 re-entrancy when showing native dialogs
    std::thread([this]() {
      // Show overlay while we open dialog and write file
      { nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Exporting Brain..."); EnqueueUiPayload(j.dump()); }

      std::string savePath;
      const bool chose = platform::GetSaveFilePath(savePath, L"Synaptic Brain (*.sbrain)\0*.sbrain\0All Files (*.*)\0*.*\0\0", L"SynapticResynthesis-Brain.sbrain");
      if (!chose)
      {
        // Hide overlay and bail on cancel
        { nlohmann::json j; j["id"] = "overlay"; j["visible"] = false; EnqueueUiPayload(j.dump()); }
        return;
      }

      iplug::IByteChunk blob; mBrain.SerializeSnapshotToChunk(blob);
      FILE* fp = fopen(savePath.c_str(), "wb");
      if (fp)
      {
        fwrite(blob.GetData(), 1, (size_t) blob.Size(), fp);
        fclose(fp);
        mExternalBrainPath = savePath;
        mUseExternalBrain = true;
        mBrainDirty = false;
        // Notify UI about new external ref immediately
        { nlohmann::json j; j["id"] = "brainExternalRef"; j["info"] = { {"path", mExternalBrainPath} }; EnqueueUiPayload(j.dump()); }
        // Also refresh DSP config so storage indicator updates consistently
        mPendingSendDSPConfig = true;
        // Export implies state saved externally; still treat as modification so hosts with chunks notice external ref change
        mPendingMarkDirty = true;
      }

      // Hide overlay at end
      { nlohmann::json j; j["id"] = "overlay"; j["visible"] = false; EnqueueUiPayload(j.dump()); }
    }).detach();
    return true;
  }
  else if (msgTag == kMsgTagBrainImport)
  {
    // Native Open dialog; C++ reads file directly
    std::thread([this]() {
      // Show overlay
      { nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Importing Brain..."); EnqueueUiPayload(j.dump()); }
      std::string openPath;
      if (!platform::GetOpenFilePath(openPath, L"Synaptic Brain (*.sbrain)\0*.sbrain\0All Files (*.*)\0*.*\0\0"))
      {
        { nlohmann::json j; j["id"] = "overlay"; j["visible"] = false; EnqueueUiPayload(j.dump()); }
        return;
      }
      // Read file
      FILE* fp = fopen(openPath.c_str(), "rb");
      if (!fp)
      {
        nlohmann::json j; j["id"] = "overlay"; j["visible"] = false; const std::string payload = j.dump();
        SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
        return;
      }
      fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
      std::vector<char> data; data.resize((size_t) sz);
      fread(data.data(), 1, (size_t) sz, fp); fclose(fp);
      // Deserialize
      iplug::IByteChunk in; in.PutBytes(data.data(), (int) data.size());
      mBrain.DeserializeSnapshotFromChunk(in, 0);
      mBrain.SetWindow(&mWindow);
      mExternalBrainPath = openPath; mUseExternalBrain = true; mBrainDirty = false;
      // After import, align UI params to imported brain's settings
      const int importedChunkSize = mBrain.GetChunkSize();
      int importedWindowMode = 1;
      switch (mBrain.GetSavedAnalysisWindowType())
      {
        case synaptic::Brain::SavedWindowType::Hann: importedWindowMode = 1; break;
        case synaptic::Brain::SavedWindowType::Hamming: importedWindowMode = 2; break;
        case synaptic::Brain::SavedWindowType::Blackman: importedWindowMode = 3; break;
        case synaptic::Brain::SavedWindowType::Rectangular: importedWindowMode = 4; break;
      }
      mPendingImportedChunkSize = importedChunkSize;
      mPendingImportedAnalysisWindow = importedWindowMode;
      // Defer UI updates
      mPendingSendBrainSummary = true;
      { nlohmann::json j; j["id"] = "brainExternalRef"; j["info"] = { {"path", mExternalBrainPath} }; EnqueueUiPayload(j.dump()); }
      { nlohmann::json j; j["id"] = "overlay"; j["visible"] = false; EnqueueUiPayload(j.dump()); }
      mPendingMarkDirty = true;
    }).detach();
    return true;
  }
  else if (msgTag == kMsgTagBrainReset)
  {
    // Clear brain and detach external reference
    mBrain.Reset();
    mBrain.SetWindow(&mWindow);
    mUseExternalBrain = false;
    mExternalBrainPath.clear();
    mBrainDirty = false;
    SendBrainSummaryToUI();
    // Update storage indicator in UI
    { nlohmann::json j; j["id"] = "brainExternalRef"; j["info"] = { {"path", std::string()} }; SendArbitraryMsgFromDelegate(-1, (int) j.dump().size(), j.dump().c_str()); }
    MarkHostStateDirty();
    return true;
  }
  else if (msgTag == kMsgTagBrainDetach)
  {
    // Stop referencing the external file; keep current in-memory brain and save inline next
    mUseExternalBrain = false;
    mExternalBrainPath.clear();
    mBrainDirty = true;
    { nlohmann::json j; j["id"] = "brainExternalRef"; j["info"] = { {"path", std::string()} }; SendArbitraryMsgFromDelegate(-1, (int) j.dump().size(), j.dump().c_str()); }
    MarkHostStateDirty();
    return true;
  }
  else if (msgTag == kMsgTagBrainRemoveFile)
  {
    // ctrlTag carries fileId
    DBGMSG("BrainRemoveFile: id=%d\n", ctrlTag);
    mBrain.RemoveFile(ctrlTag);
    mBrainDirty = true;
    SendBrainSummaryToUI();
    MarkHostStateDirty();
    return true;
  }

  else if (msgTag == kMsgTagUiReady)
  {
    // UI is ready to receive state; resend current values to repopulate panels
    SendTransformerParamsToUI();
    SendDSPConfigToUI();
    SendBrainSummaryToUI();
    // Also let UI know storage mode/path
    {
      nlohmann::json j; j["id"] = "brainExternalRef"; j["info"] = { {"path", mUseExternalBrain ? mExternalBrainPath : std::string()} };
      const std::string payload = j.dump();
      SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
    }
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

void SynapticResynthesis::SendTransformerParamsToUI()
{
  if (!mTransformer)
  {
    // Send empty list
    nlohmann::json j;
    j["id"] = "transformerParams";
    j["params"] = nlohmann::json::array();
    const std::string payload = j.dump();
    SendArbitraryMsgFromDelegate(-1, static_cast<int>(payload.size()), payload.c_str());
    return;
  }

  std::vector<synaptic::IChunkBufferTransformer::ExposedParamDesc> descs;
  mTransformer->GetParamDescs(descs);

  nlohmann::json j;
  j["id"] = "transformerParams";
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& d : descs)
  {
    nlohmann::json o;
    o["id"] = d.id;
    o["label"] = d.label;
    // type
    switch (d.type)
    {
      case synaptic::IChunkBufferTransformer::ParamType::Number: o["type"] = "number"; break;
      case synaptic::IChunkBufferTransformer::ParamType::Boolean: o["type"] = "boolean"; break;
      case synaptic::IChunkBufferTransformer::ParamType::Enum: o["type"] = "enum"; break;
      case synaptic::IChunkBufferTransformer::ParamType::Text: o["type"] = "text"; break;
    }
    // control
    switch (d.control)
    {
      case synaptic::IChunkBufferTransformer::ControlType::Slider: o["control"] = "slider"; break;
      case synaptic::IChunkBufferTransformer::ControlType::NumberBox: o["control"] = "numberbox"; break;
      case synaptic::IChunkBufferTransformer::ControlType::Select: o["control"] = "select"; break;
      case synaptic::IChunkBufferTransformer::ControlType::Checkbox: o["control"] = "checkbox"; break;
      case synaptic::IChunkBufferTransformer::ControlType::TextBox: o["control"] = "textbox"; break;
    }
    // numeric bounds
    o["min"] = d.minValue;
    o["max"] = d.maxValue;
    o["step"] = d.step;
    // options
    if (!d.options.empty())
    {
      nlohmann::json opts = nlohmann::json::array();
      for (const auto& opt : d.options)
      {
        nlohmann::json jo;
        jo["value"] = opt.value;
        jo["label"] = opt.label;
        opts.push_back(jo);
      }
      o["options"] = opts;
    }
    // current value
    double num;
    bool b;
    std::string str;
    if (mTransformer->GetParamAsNumber(d.id, num))
      o["value"] = num;
    else if (mTransformer->GetParamAsBool(d.id, b))
      o["value"] = b;
    else if (mTransformer->GetParamAsString(d.id, str))
      o["value"] = str;
    else
    {
      // fall back to defaults
      if (d.type == synaptic::IChunkBufferTransformer::ParamType::Number) o["value"] = d.defaultNumber;
      else if (d.type == synaptic::IChunkBufferTransformer::ParamType::Boolean) o["value"] = d.defaultBool;
      else o["value"] = d.defaultString;
    }

    arr.push_back(o);
  }
  j["params"] = arr;
  const std::string payload = j.dump();
  SendArbitraryMsgFromDelegate(-1, static_cast<int>(payload.size()), payload.c_str());
}


void SynapticResynthesis::OnUIOpen()
{
  // Ensure UI gets current values when window opens
  Plugin::OnUIOpen();
  SendTransformerParamsToUI();
  SendDSPConfigToUI();
  SendBrainSummaryToUI();
}

void SynapticResynthesis::OnIdle()
{
  // Drain any UI messages queued by background threads
  DrainUiQueueOnMainThread();
}

void SynapticResynthesis::OnRestoreState()
{
  Plugin::OnRestoreState();
  SendTransformerParamsToUI();
  SendDSPConfigToUI();
  SendBrainSummaryToUI();
}

void SynapticResynthesis::OnParamChange(int paramIdx)
{
  if (paramIdx == kInGain)
  {
    DBGMSG("input gain %f\n", GetParam(paramIdx)->Value());
    return;
  }

  if (paramIdx == kOutGain)
  {
    DBGMSG("output gain %f\n", GetParam(paramIdx)->Value());
    return;
  }

  if (paramIdx == mParamIdxChunkSize && mParamIdxChunkSize >= 0)
  {
    mChunkSize = std::max(1, GetParam(mParamIdxChunkSize)->Int());
    mChunker.SetChunkSize(mChunkSize);

    // Update analysis window size to match new chunk size
    mWindow.Set(IntToWindowType(mAnalysisWindowMode), mChunkSize);

    UpdateChunkerWindowing();

    // Do NOT rechunk synchronously on the audio thread. The UI button handler launches a background rechunk when needed.
    SetLatency(ComputeLatencySamples());
    return;
  }

  if (paramIdx == mParamIdxBufferWindow && mParamIdxBufferWindow >= 0)
  {
    mBufferWindowSize = std::max(1, GetParam(mParamIdxBufferWindow)->Int());
    mChunker.SetBufferWindowSize(mBufferWindowSize);
    return;
  }

  if (paramIdx == mParamIdxAlgorithm && mParamIdxAlgorithm >= 0)
  {
    mAlgorithmId = GetParam(mParamIdxAlgorithm)->Int();
    mTransformer = synaptic::TransformerFactory::CreateByUiIndex(mAlgorithmId);
    if (!mTransformer)
    {
      mAlgorithmId = 0;
      mTransformer = synaptic::TransformerFactory::CreateByUiIndex(mAlgorithmId);
    }
    if (auto sb = dynamic_cast<synaptic::SimpleSampleBrainTransformer*>(mTransformer.get()))
      sb->SetBrain(&mBrain);
    if (mTransformer)
      mTransformer->OnReset(GetSampleRate(), mChunkSize, mBufferWindowSize, NInChansConnected());

    UpdateChunkerWindowing();

    // Reapply persisted IParam values to the new instance
    for (const auto& b : mTransformerBindings)
    {
      if (b.paramIdx < 0 || !mTransformer) continue;
      switch (b.type)
      {
        case synaptic::IChunkBufferTransformer::ParamType::Number:
          if (GetParam(b.paramIdx)) mTransformer->SetParamFromNumber(b.id, GetParam(b.paramIdx)->Value());
          break;
        case synaptic::IChunkBufferTransformer::ParamType::Boolean:
          if (GetParam(b.paramIdx)) mTransformer->SetParamFromBool(b.id, GetParam(b.paramIdx)->Bool());
          break;
        case synaptic::IChunkBufferTransformer::ParamType::Enum:
        {
          if (GetParam(b.paramIdx))
          {
            int idx = GetParam(b.paramIdx)->Int();
            std::string val = (idx >= 0 && idx < (int) b.enumValues.size()) ? b.enumValues[idx] : std::to_string(idx);
            mTransformer->SetParamFromString(b.id, val);
          }
          break;
        }
        case synaptic::IChunkBufferTransformer::ParamType::Text:
          break;
      }
    }
    SetLatency(ComputeLatencySamples());
    return;
  }

  if (paramIdx == mParamIdxOutputWindow && mParamIdxOutputWindow >= 0)
  {
    mOutputWindowMode = 1 + std::clamp(GetParam(mParamIdxOutputWindow)->Int(), 0, 3);
    UpdateChunkerWindowing();
    return;
  }

  if (paramIdx == mParamIdxAnalysisWindow && mParamIdxAnalysisWindow >= 0)
  {
    mAnalysisWindowMode = 1 + std::clamp(GetParam(mParamIdxAnalysisWindow)->Int(), 0, 3);
    mWindow.Set(IntToWindowType(mAnalysisWindowMode), mChunkSize);
    mBrain.SetWindow(&mWindow);
    // Kick background reanalysis when analysis window changes via host/restore, unless suppressed (e.g. import sync)
    if (!mSuppressNextAnalysisReanalyze.exchange(false))
    {
      { nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Reanalyzing..."); const std::string payload = j.dump(); SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str()); }
      if (!mRechunking.exchange(true))
      {
        std::thread([this]()
        {
          auto stats = mBrain.ReanalyzeAllChunks((int) GetSampleRate());
          DBGMSG("Brain Reanalyze: files=%d chunks=%d\n", stats.filesProcessed, stats.chunksProcessed);
          mBrainDirty = true;
          mPendingSendBrainSummary = true;
          { nlohmann::json j; j["id"] = "overlay"; j["visible"] = false; EnqueueUiPayload(j.dump()); }
          mPendingMarkDirty = true;
          mRechunking = false;
        }).detach();
      }
      else
      {
        DBGMSG("Reanalyze request ignored: job already running.\n");
      }
    }
    mPendingSendDSPConfig = true;
    return;
  }

  if (paramIdx == mParamIdxEnableOverlap && mParamIdxEnableOverlap >= 0)
  {
    mEnableOverlapAdd = GetParam(mParamIdxEnableOverlap)->Bool();
    UpdateChunkerWindowing();
    return;
  }

  // Check transformer dynamic param bindings
  for (const auto& b : mTransformerBindings)
  {
    if (b.paramIdx == paramIdx && mTransformer)
    {
      switch (b.type)
      {
        case synaptic::IChunkBufferTransformer::ParamType::Number:
          mTransformer->SetParamFromNumber(b.id, GetParam(paramIdx)->Value());
          break;
        case synaptic::IChunkBufferTransformer::ParamType::Boolean:
          mTransformer->SetParamFromBool(b.id, GetParam(paramIdx)->Bool());
          break;
        case synaptic::IChunkBufferTransformer::ParamType::Enum:
        {
          int idx = GetParam(paramIdx)->Int();
          std::string val = (idx >= 0 && idx < (int) b.enumValues.size()) ? b.enumValues[idx] : std::to_string(idx);
          mTransformer->SetParamFromString(b.id, val);
          break;
        }
        case synaptic::IChunkBufferTransformer::ParamType::Text:
          // Not supported as text; ignore
          break;
      }
      break;
    }
  }
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

void SynapticResynthesis::SendDSPConfigToUI()
{
  nlohmann::json j;
  j["id"] = "dspConfig";
  j["chunkSize"] = mChunkSize;
  j["bufferWindowSize"] = mBufferWindowSize;
  j["outputWindowMode"] = mOutputWindowMode; // 1=Hann,2=Hamming,3=Blackman,4=Rectangular
  j["analysisWindowMode"] = mAnalysisWindowMode; // 1=Hann,2=Hamming,3=Blackman,4=Rectangular
  j["algorithmId"] = mAlgorithmId;
  // storage info
  j["useExternalBrain"] = mUseExternalBrain;
  j["externalPath"] = mUseExternalBrain ? mExternalBrainPath : std::string();
  // Also send transformer options derived from factory for dynamic UI population
  {
    nlohmann::json opts = nlohmann::json::array();
    const auto ids = synaptic::TransformerFactory::GetUiIds();
    const auto labels = synaptic::TransformerFactory::GetUiLabels();
    const int n = (int) std::min(ids.size(), labels.size());
    for (int i = 0; i < n; ++i)
    {
      nlohmann::json o;
      o["id"] = ids[i];
      o["label"] = labels[i];
      o["index"] = i;
      opts.push_back(o);
    }
    j["algorithms"] = opts;
  }
  const std::string payload = j.dump();
  SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
}

synaptic::Window::Type SynapticResynthesis::IntToWindowType(int mode)
{
  using WT = synaptic::Window::Type;
  switch (mode)
  {
    case 1: return WT::Hann;
    case 2: return WT::Hamming;
    case 3: return WT::Blackman;
    case 4: return WT::Rectangular;
    default: return WT::Hann;
  }
}

void SynapticResynthesis::UpdateChunkerWindowing()
{
  // Validate chunk size
  if (mChunkSize <= 0)
  {
    DBGMSG("Warning: Invalid chunk size %d, using default\n", mChunkSize);
    mChunkSize = 3000;
  }

  // Set up output window first
  mOutputWindow.Set(IntToWindowType(mOutputWindowMode), mChunkSize);

  // Configure overlap behavior based on user setting, window type, and transformer capabilities
  const bool isRectangular = (mOutputWindowMode == 4); // Rectangular
  const bool transformerWantsOverlap = mTransformer ? mTransformer->WantsOverlapAdd() : true;
  const bool shouldUseOverlap = mEnableOverlapAdd && !isRectangular && transformerWantsOverlap;

  mChunker.EnableOverlap(shouldUseOverlap);
  mChunker.SetOutputWindow(mOutputWindow);

  DBGMSG("Window config: type=%d, userEnabled=%s, shouldUseOverlap=%s, chunkSize=%d\n",
         mOutputWindowMode, mEnableOverlapAdd ? "true" : "false", shouldUseOverlap ? "true" : "false", mChunkSize);
}

void SynapticResynthesis::MarkHostStateDirty()
{
  // Cross-API lightweight dirty notification.
  // AAX: bump compare state so the compare light turns on.
#ifdef AAX_API
  if (auto* aax = dynamic_cast<IPlugAAX*>(this))
  {
    aax->DirtyPTCompareState();
  }
#endif
  // For VST3/others, ping a single parameter (use hidden non-automatable dirty flag)
  int idx = (mParamIdxDirtyFlag >= 0) ? mParamIdxDirtyFlag : ((mParamIdxBufferWindow >= 0) ? mParamIdxBufferWindow : 0);
  if (idx >= 0 && GetParam(idx))
  {
    // Toggle value quickly to ensure a host-visible delta without semantic changes
    const bool cur = GetParam(idx)->Bool();
    const double norm = GetParam(idx)->ToNormalized(cur ? 0.0 : 1.0);
    BeginInformHostOfParamChangeFromUI(idx);
    SendParameterValueFromUI(idx, norm);
    EndInformHostOfParamChangeFromUI(idx);
  }
}

bool SynapticResynthesis::SerializeState(IByteChunk& chunk) const
{
  if (!Plugin::SerializeState(chunk)) return false;
  // Append small manifest: external flag + path or inline snapshot
  const uint32_t tag = 0x42524E53; // 'BRNS'
  chunk.Put(&tag);
  int32_t sectionSize = 0; int sizePos = chunk.Size(); chunk.Put(&sectionSize);
  int start = chunk.Size();
  uint8_t mode = mUseExternalBrain ? 1 : 0; chunk.Put(&mode);
  if (mUseExternalBrain && !mExternalBrainPath.empty())
  {
    // Store external path
    chunk.PutStr(mExternalBrainPath.c_str());
    // If brain has changed, sync it to file now to persist on project save
    if (mBrainDirty)
    {
      iplug::IByteChunk blob; mBrain.SerializeSnapshotToChunk(blob);
      FILE* fp = fopen(mExternalBrainPath.c_str(), "wb");
      if (fp)
      {
        fwrite(blob.GetData(), 1, (size_t) blob.Size(), fp);
        fclose(fp);
        mBrainDirty = false;
      }
    }
  }
  else
  {
    // Inline snapshot for small brains
    iplug::IByteChunk brainChunk; mBrain.SerializeSnapshotToChunk(brainChunk);
    int32_t sz = brainChunk.Size();
    chunk.Put(&sz);
    if (sz > 0) chunk.PutBytes(brainChunk.GetData(), sz);
  }
  int end = chunk.Size();
  sectionSize = end - start;
  memcpy(chunk.GetData() + sizePos, &sectionSize, sizeof(sectionSize));
  return true;
}

int SynapticResynthesis::UnserializeState(const IByteChunk& chunk, int startPos)
{
  int pos = Plugin::UnserializeState(chunk, startPos);
  if (pos < 0) return pos;

  // Look for our optional section
  uint32_t tag = 0; int next = chunk.Get(&tag, pos);
  if (next < 0) return pos; // no extra data
  if (tag != 0x42524E53)
  {
    // Not our tag; leave pos unchanged (backwards compatibility)
    return pos;
  }
  pos = next;
  int32_t sectionSize = 0; pos = chunk.Get(&sectionSize, pos);
  if (pos < 0 || sectionSize < 0) return pos;
  int start = pos;
  uint8_t mode = 0; pos = chunk.Get(&mode, pos); if (pos < 0) return start + sectionSize;
  if (mode == 1)
  {
    // external
    WDL_String p; pos = chunk.GetStr(p, pos); if (pos < 0) return start + sectionSize;
    mExternalBrainPath = p.Get();
    mUseExternalBrain = !mExternalBrainPath.empty();
    // Try to load from path if readable
    if (mUseExternalBrain)
    {
      FILE* fp = fopen(mExternalBrainPath.c_str(), "rb");
      if (fp)
      {
        fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
        std::string data; data.resize((size_t) sz);
        fread(data.data(), 1, (size_t) sz, fp); fclose(fp);
        iplug::IByteChunk in; in.PutBytes(data.data(), (int) data.size());
        mBrain.DeserializeSnapshotFromChunk(in, 0);
      }
    }
  }
  else
  {
    // inline
    int32_t sz = 0; pos = chunk.Get(&sz, pos); if (pos < 0 || sz < 0) return start + sectionSize;
    int consumed = mBrain.DeserializeSnapshotFromChunk(chunk, pos);
    if (consumed >= 0) pos = consumed; else pos = start + sectionSize;
  }

  // Re-link window pointer and notify UI
  mBrain.SetWindow(&mWindow);
  SendBrainSummaryToUI();
  SendDSPConfigToUI();
  SendTransformerParamsToUI();
  // Send storage mode/path so UI shows external correctly after project load
  {
    nlohmann::json j; j["id"] = "brainExternalRef"; j["info"] = { {"path", mUseExternalBrain ? mExternalBrainPath : std::string()} };
    const std::string payload = j.dump();
    SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
  }
  return pos;
}
