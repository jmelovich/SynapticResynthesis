// Message Handler Implementations for SynapticResynthesis
// This file contains all the HandleXxxMsg methods to keep the main file cleaner

#include "SynapticResynthesis.h"
#include "json.hpp"
#include "plugin_src/TransformerFactory.h"

// NOTE: Do NOT include "IPlug_include_in_plug_src.h" here - it defines global symbols
// that should only be included in the main SynapticResynthesis.cpp file

// === UI Control Message Handlers ===

bool SynapticResynthesis::HandleUiReadyMsg()
{
  // UI is ready to receive state; resend current values to repopulate panels
  mUIBridge.SendTransformerParams(mTransformer);
  mUIBridge.SendMorphParams(mMorph);

  SyncAndSendDSPConfig();

  mUIBridge.SendBrainSummary(mBrain);
  mUIBridge.SendExternalRefInfo(mBrainManager.UseExternal(), mBrainManager.ExternalPath());
  return true;
}

// === DSP Configuration Message Handlers ===

bool SynapticResynthesis::HandleSetChunkSizeMsg(int newSize)
{
  // Clamp to valid range
  newSize = std::max(1, newSize);
  const int paramIdx = mParamManager.GetChunkSizeParamIdx();
  if (paramIdx >= 0)
  {
    SetParameterFromUI(paramIdx, (double)newSize);
  }
  mDSPConfig.chunkSize = newSize;
  DBGMSG("Set Chunk Size: %i\n", mDSPConfig.chunkSize);
  mChunker.SetChunkSize(mDSPConfig.chunkSize);

  // Update analysis window size to match new chunk size
  UpdateBrainAnalysisWindow();

  UpdateChunkerWindowing();

  {
    nlohmann::json j; j["id"] = "brainChunkSize"; j["size"] = mDSPConfig.chunkSize;
    const std::string payload = j.dump();
    SendArbitraryMsgFromDelegate(-1, (int)payload.size(), payload.c_str());
  }
  // Update latency and DSP config immediately on UI thread
  SetLatency(ComputeLatencySamples());

  // Update DSPConfig with current external brain state and send to UI
  SyncAndSendDSPConfig();

  // Trigger background rechunk using BrainManager
  mBrainManager.RechunkAllFilesAsync(mDSPConfig.chunkSize, (int)GetSampleRate(), [this]()
  {
    SetPendingUpdate(PendingUpdate::BrainSummary);
    SetPendingUpdate(PendingUpdate::MarkDirty);
  });
  return true;
}

bool SynapticResynthesis::HandleSetOutputWindowMsg(int mode)
{
  // mode carries an integer enum: 1=Hann,2=Hamming,3=Blackman,4=Rectangular
  mDSPConfig.outputWindowMode = std::clamp(mode, 1, 4);
  const int paramIdx = mParamManager.GetOutputWindowParamIdx();
  if (paramIdx >= 0)
  {
    const int idx = mDSPConfig.outputWindowMode - 1;
    SetParameterFromUI(paramIdx, (double)idx);
  }

  UpdateChunkerWindowing();

  // Update and send DSP config to UI
  SyncAndSendDSPConfig();
  return true;
}

bool SynapticResynthesis::HandleSetAnalysisWindowMsg(int mode)
{
  // mode carries an integer enum: 1=Hann,2=Hamming,3=Blackman,4=Rectangular
  mDSPConfig.analysisWindowMode = std::clamp(mode, 1, 4);
  const int paramIdx = mParamManager.GetAnalysisWindowParamIdx();
  if (paramIdx >= 0)
  {
    const int idx = mDSPConfig.analysisWindowMode - 1;
    SetParameterFromUI(paramIdx, (double)idx);
  }
  // Update analysis window used by Brain
  UpdateBrainAnalysisWindow();

  // Trigger background reanalysis using BrainManager
  mBrainManager.ReanalyzeAllChunksAsync((int)GetSampleRate(), [this]()
  {
    SetPendingUpdate(PendingUpdate::BrainSummary);
    SetPendingUpdate(PendingUpdate::MarkDirty);
  });

  // Update and send DSP config to UI
  SyncAndSendDSPConfig();
  return true;
}

bool SynapticResynthesis::HandleSetAlgorithmMsg(int algorithmId)
{
  // algorithmId selects algorithm by UI index
  mDSPConfig.algorithmId = algorithmId;
  const int paramIdx = mParamManager.GetAlgorithmParamIdx();
  if (paramIdx >= 0)
  {
    SetParameterFromUI(paramIdx, (double)mDSPConfig.algorithmId);
  }

  // Create new transformer in pending slot for thread-safe swap
  auto newTransformer = synaptic::TransformerFactory::CreateByUiIndex(mDSPConfig.algorithmId);
  if (!newTransformer)
  {
    // Fallback to first available
    mDSPConfig.algorithmId = 0;
    newTransformer = synaptic::TransformerFactory::CreateByUiIndex(mDSPConfig.algorithmId);
  }
  if (auto sb = dynamic_cast<synaptic::BaseSampleBrainTransformer*>(newTransformer.get()))
    sb->SetBrain(&mBrain);

  if (newTransformer)
    newTransformer->OnReset(GetSampleRate(), mDSPConfig.chunkSize, mDSPConfig.bufferWindowSize, NInChansConnected());

  // Reapply persisted IParam values to the new transformer instance using ParameterManager
  mParamManager.ApplyBindingsToTransformer(this, newTransformer.get());

  // Store for thread-safe swap in ProcessBlock
  mPendingTransformer = std::move(newTransformer);

  UpdateChunkerWindowing();

  // Send transformer params and DSP config to UI (use pending transformer since swap hasn't happened yet)
#if SR_USE_WEB_UI
  mUIBridge.SendTransformerParams(mPendingTransformer);
#else
  // For C++ UI, trigger rebuild on UI thread
  SetPendingUpdate(PendingUpdate::RebuildTransformer);
#endif
  SyncAndSendDSPConfig();
  // Note: SetLatency will be called in ProcessBlock after swap
  return true;
}

// === Transformer Parameter Message Handler ===

bool SynapticResynthesis::HandleTransformerSetParamMsg(const void* jsonData, int dataSize)
{
  // jsonData is expected to be raw JSON bytes: {"id":"...","type":"number|boolean|string","value":...}
  if (!jsonData || dataSize <= 0)
    return false;

  const char* bytes = reinterpret_cast<const char*>(jsonData);
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
      if (mTransformer) ok |= mTransformer->SetParamFromNumber(id, v);
      if (mMorph) ok |= mMorph->SetParamFromNumber(id, v);
    }
    else if (type == "boolean" && j.contains("value"))
    {
      bool v = j["value"].get<bool>();
      if (mTransformer) ok |= mTransformer->SetParamFromBool(id, v);
      if (mMorph) ok |= mMorph->SetParamFromBool(id, v);
    }
    else if (type == "text" || type == "string")
    {
      std::string v = j.value("value", std::string());
      if (mTransformer) ok |= mTransformer->SetParamFromString(id, v);
      if (mMorph) ok |= mMorph->SetParamFromString(id, v);
    }
    else if (type == "enum")
    {
      std::string v = j.value("value", std::string());
      if (mTransformer) ok |= mTransformer->SetParamFromString(id, v);
      if (mMorph) ok |= mMorph->SetParamFromString(id, v);
    }

    if (ok)
    {
      // Mirror to corresponding IParam and inform host as a UI gesture
      const auto& bindings = mParamManager.GetBindings();
      auto it = std::find_if(bindings.begin(), bindings.end(), [&](const auto& b){ return b.id == id; });
      if (it != bindings.end() && it->paramIdx >= 0)
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
          for (int k = 0; k < (int)it->enumValues.size(); ++k)
          {
            if (it->enumValues[k] == v) { idx = k; break; }
          }
          normalized = GetParam(it->paramIdx)->ToNormalized((double)idx);
        }
        SetParameterFromUI(it->paramIdx, GetParam(it->paramIdx)->FromNormalized(normalized));
      }
      mUIBridge.SendTransformerParams(mTransformer);
      mUIBridge.SendMorphParams(mMorph);
    }
    return ok;
  }
  catch (...)
  {
    return false;
  }
}

// === Brain Message Handlers ===

bool SynapticResynthesis::HandleBrainAddFileMsg(int dataSize, const void* pData)
{
  // pData holds raw bytes: [uint16_t nameLenLE][name bytes UTF-8][file bytes]
  if (!pData || dataSize <= 2)
    return false;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(pData);
  uint16_t nameLen = (uint16_t)(bytes[0] | (bytes[1] << 8));
  if (2 + nameLen > dataSize)
    return false;

  std::string name(reinterpret_cast<const char*>(bytes + 2), reinterpret_cast<const char*>(bytes + 2 + nameLen));
  const void* fileData = bytes + 2 + nameLen;
  size_t fileSize = static_cast<size_t>(dataSize - (2 + nameLen));

  DBGMSG("BrainAddFile: name=%s size=%zu SR=%d CH=%d chunk=%d\n", name.c_str(), fileSize, (int)GetSampleRate(), NInChansConnected(), mDSPConfig.chunkSize);

  // Use BrainManager to add file
  int newId = mBrainManager.AddFileFromMemory(fileData, fileSize, name, (int)GetSampleRate(), NInChansConnected(), mDSPConfig.chunkSize);
  if (newId >= 0)
  {
#if SR_USE_WEB_UI
    mUIBridge.SendBrainSummary(mBrain);
#else
    // For C++ UI, set flag to update in OnIdle
    SetPendingUpdate(PendingUpdate::BrainSummary);
#endif
    MarkHostStateDirty();
  }
  return newId >= 0;
}

bool SynapticResynthesis::HandleBrainRemoveFileMsg(int fileId)
{
  DBGMSG("BrainRemoveFile: id=%d\n", fileId);
  mBrainManager.RemoveFile(fileId);
#if SR_USE_WEB_UI
  mUIBridge.SendBrainSummary(mBrain);
#else
  // For C++ UI, set flag to update in OnIdle
  SetPendingUpdate(PendingUpdate::BrainSummary);
#endif
  MarkHostStateDirty();
  return true;
}

bool SynapticResynthesis::HandleBrainExportMsg()
{
  mBrainManager.ExportToFileAsync([this]()
  {
    SetPendingUpdate(PendingUpdate::BrainSummary);  // Update brain UI state (includes storage label)
    SetPendingUpdate(PendingUpdate::DSPConfig);
    SetPendingUpdate(PendingUpdate::MarkDirty);
  });
  return true;
}

bool SynapticResynthesis::HandleBrainImportMsg()
{
  mBrainManager.ImportFromFileAsync([this]()
  {
    SetPendingUpdate(PendingUpdate::BrainSummary);
    SetPendingUpdate(PendingUpdate::MarkDirty);
  });
  return true;
}

bool SynapticResynthesis::HandleBrainResetMsg()
{
  mBrainManager.Reset();
#if SR_USE_WEB_UI
  mUIBridge.SendBrainSummary(mBrain);
#else
  // For C++ UI, set flag to update in OnIdle
  SetPendingUpdate(PendingUpdate::BrainSummary);
#endif
  MarkHostStateDirty();
  return true;
}

bool SynapticResynthesis::HandleBrainDetachMsg()
{
  mBrainManager.Detach();
#if SR_USE_WEB_UI
  mUIBridge.SendBrainSummary(mBrain);
#else
  // For C++ UI, set flag to update in OnIdle (updates storage label back to inline)
  SetPendingUpdate(PendingUpdate::BrainSummary);
#endif
  MarkHostStateDirty();
  return true;
}

bool SynapticResynthesis::HandleResizeToFitMsg(int dataSize, const void* pData)
{
#if SR_USE_WEB_UI
  if (!pData || dataSize <= 0)
    return false;

  const char* bytes = reinterpret_cast<const char*>(pData);
  std::string s(bytes, bytes + dataSize);
  try
  {
    auto j = nlohmann::json::parse(s);
    int width = j.value("width", 1024);
    int height = j.value("height", 600);

    // Clamp to reasonable bounds
    width = std::clamp(width, 400, 2560);
    height = std::clamp(height, 300, 1440);

    Resize(width, height);
    return true;
  }
  catch (...)
  {
    return false;
  }
#else
  (void)dataSize;
  (void)pData;
  return false;
#endif
}

