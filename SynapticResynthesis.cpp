#include "SynapticResynthesis.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"
#include "json.hpp"
#include "Extras/WebView/IPlugWebViewEditorDelegate.h"
#include "plugin_src/TransformerFactory.h"

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
    // base params (kNumParams) + ChunkSize + BufferWindow + Algorithm + union of transformer params
    std::vector<synaptic::IChunkBufferTransformer::ExposedParamDesc> unionDescs;
    BuildTransformerUnion(unionDescs);
    return kNumParams + 3 + (int) unionDescs.size();
  }
}

SynapticResynthesis::SynapticResynthesis(const InstanceInfo& info)
: Plugin(info, MakeConfig(ComputeTotalParams(), kNumPresets))
{
  GetParam(kGain)->InitGain("Gain", 0.0, -70, 0.);

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
  if (mTransformer)
  {
    using OW = synaptic::IChunkBufferTransformer::OutputWindowMode;
    OW mode = OW::Hann;
    switch (mOutputWindowMode) { case 1: mode = OW::Hann; break; case 2: mode = OW::Hamming; break; case 3: mode = OW::Blackman; break; case 4: mode = OW::Rectangular; break; default: break; }
    mTransformer->SetOutputWindowMode(mode);
  }

  // Initialize window with default Hann window
  mWindow.Set(synaptic::Window::Type::Hann, mChunkSize); // Default size, will be updated as needed

  // Set the window reference in the Brain
  mBrain.SetWindow(&mWindow);

  // Note: OnReset will be called later with proper channel counts

  // Create core DSP params into the pre-allocated slots
  mParamIdxChunkSize = kNumParams + 0; GetParam(mParamIdxChunkSize)->InitInt("Chunk Size", mChunkSize, 1, 262144, "samples", IParam::kFlagCannotAutomate);
  // Buffer window size is no longer user-exposed (internal). Reserve slot but don't publish.
  mParamIdxBufferWindow = kNumParams + 1; GetParam(mParamIdxBufferWindow)->InitInt("Buffer Window", mBufferWindowSize, 1, 1024, "chunks", IParam::kFlagCannotAutomate);
  // Build algorithm enum from factory UI list (deterministic)
  mParamIdxAlgorithm = kNumParams + 2;
  {
    const int count = synaptic::TransformerFactory::GetUiCount();
    GetParam(mParamIdxAlgorithm)->InitEnum("Algorithm", mAlgorithmId, count, "");
    const auto labels = synaptic::TransformerFactory::GetUiLabels();
    for (int i = 0; i < (int) labels.size(); ++i)
      GetParam(mParamIdxAlgorithm)->SetDisplayText(i, labels[i].c_str());
  }

  // Build union descs and initialize the remaining pre-allocated params
  std::vector<synaptic::IChunkBufferTransformer::ExposedParamDesc> unionDescs;
  BuildTransformerUnion(unionDescs);
  int base = kNumParams + 3;
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
  // Pull current values from IParams
  if (mParamIdxChunkSize >= 0) mChunkSize = std::max(1, GetParam(mParamIdxChunkSize)->Int());
  if (mParamIdxBufferWindow >= 0) mBufferWindowSize = std::max(1, GetParam(mParamIdxBufferWindow)->Int());
  if (mParamIdxAlgorithm >= 0) mAlgorithmId = GetParam(mParamIdxAlgorithm)->Int();

  mWindow.Set(synaptic::Window::Type::Hann, mChunkSize);
  mBrain.SetWindow(&mWindow);

  mChunker.SetChunkSize(mChunkSize);
  mChunker.SetBufferWindowSize(mBufferWindowSize);
  // Ensure chunker channel count matches current connection
  mChunker.SetNumChannels(NInChansConnected());
  mChunker.Reset();

  // Apply output window mode to transformer
  if (mTransformer)
  {
    using OW = synaptic::IChunkBufferTransformer::OutputWindowMode;
    OW mode = OW::Hann;
    switch (mOutputWindowMode) { case 1: mode = OW::Hann; break; case 2: mode = OW::Hamming; break; case 3: mode = OW::Blackman; break; case 4: mode = OW::Rectangular; break; default: break; }
    mTransformer->SetOutputWindowMode(mode);
  }

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

    // Update window size to match new chunk size
    mWindow.Set(synaptic::Window::Type::Hann, mChunkSize);

    {
      nlohmann::json j; j["id"] = "brainChunkSize"; j["size"] = mChunkSize;
      const std::string payload = j.dump();
      SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
    }
    {
      nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Rechunking...");
      const std::string payload = j.dump();
      SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
    }
    {
      auto stats = mBrain.RechunkAllFiles(mChunkSize, (int) GetSampleRate(), [&](const std::string& name){
        nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Rechunking ") + name;
        const std::string payload = j.dump();
        SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
      });
      DBGMSG("Brain Rechunk: processed=%d, rechunked=%d, totalChunks=%d\n", stats.filesProcessed, stats.filesRechunked, stats.newTotalChunks);
    }
    SendBrainSummaryToUI();
    {
      nlohmann::json j; j["id"] = "overlay"; j["visible"] = false;
      const std::string payload = j.dump();
      SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
    }
    SetLatency(ComputeLatencySamples());
    SendDSPConfigToUI();
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
    auto toMode = [](int v){
      using OW = synaptic::IChunkBufferTransformer::OutputWindowMode;
      switch (v) { case 1: return OW::Hann; case 2: return OW::Hamming; case 3: return OW::Blackman; case 4: return OW::Rectangular; default: return OW::Hann; }
    };
    mOutputWindowMode = std::clamp(ctrlTag, 1, 4);
    if (mTransformer)
    {
      mTransformer->SetOutputWindowMode(toMode(mOutputWindowMode));
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

    // Apply global output window mode to the new transformer
    if (mTransformer)
    {
      using OW = synaptic::IChunkBufferTransformer::OutputWindowMode;
      OW mode = OW::Hann;
      switch (mOutputWindowMode) { case 1: mode = OW::Hann; break; case 2: mode = OW::Hamming; break; case 3: mode = OW::Blackman; break; case 4: mode = OW::Rectangular; break; default: break; }
      mTransformer->SetOutputWindowMode(mode);
    }

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
    {
      nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Importing ") + name;
      const std::string payload = j.dump();
      SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
    }
    int newId = mBrain.AddAudioFileFromMemory(fileData, fileSize, name, (int) GetSampleRate(), NInChansConnected(), mChunkSize);
    if (newId >= 0)
      SendBrainSummaryToUI(); // UI will refresh list
    // Hide overlay explicitly after attempt
    {
      nlohmann::json j; j["id"] = "overlay"; j["visible"] = false;
      const std::string payload = j.dump();
      SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
    }
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

  else if (msgTag == kMsgTagUiReady)
  {
    // UI is ready to receive state; resend current values to repopulate panels
    SendTransformerParamsToUI();
    SendDSPConfigToUI();
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

void SynapticResynthesis::OnRestoreState()
{
  Plugin::OnRestoreState();
  SendTransformerParamsToUI();
  SendDSPConfigToUI();
  SendBrainSummaryToUI();
}

void SynapticResynthesis::OnParamChange(int paramIdx)
{
  if (paramIdx == kGain)
  {
    DBGMSG("gain %f\n", GetParam(paramIdx)->Value());
    return;
  }

  if (paramIdx == mParamIdxChunkSize && mParamIdxChunkSize >= 0)
  {
    mChunkSize = std::max(1, GetParam(mParamIdxChunkSize)->Int());
    mChunker.SetChunkSize(mChunkSize);

    // Update window size to match new chunk size
    mWindow.Set(synaptic::Window::Type::Hann, mChunkSize);

    // Rechunk brain to new size
    {
      nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Rechunking...");
      const std::string payload = j.dump();
      SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
    }
    auto stats = mBrain.RechunkAllFiles(mChunkSize, (int) GetSampleRate(), [&](const std::string& name){
      nlohmann::json j; j["id"] = "overlay"; j["visible"] = true; j["text"] = std::string("Rechunking ") + name;
      const std::string payload = j.dump();
      SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
    });
    DBGMSG("Brain Rechunk: processed=%d, rechunked=%d, totalChunks=%d\n", stats.filesProcessed, stats.filesRechunked, stats.newTotalChunks);
    SendBrainSummaryToUI();
    {
      nlohmann::json j; j["id"] = "overlay"; j["visible"] = false;
      const std::string payload = j.dump();
      SendArbitraryMsgFromDelegate(-1, (int) payload.size(), payload.c_str());
    }
    SetLatency(ComputeLatencySamples());
    SendDSPConfigToUI();
    return;
  }

  if (paramIdx == mParamIdxBufferWindow && mParamIdxBufferWindow >= 0)
  {
    mBufferWindowSize = std::max(1, GetParam(mParamIdxBufferWindow)->Int());
    mChunker.SetBufferWindowSize(mBufferWindowSize);
    SendDSPConfigToUI();
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
    SetLatency(ComputeLatencySamples());
    SendTransformerParamsToUI();
    SendDSPConfigToUI();
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
      // Reflect param change back to UI params panel
      SendTransformerParamsToUI();
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
  j["algorithmId"] = mAlgorithmId;
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
