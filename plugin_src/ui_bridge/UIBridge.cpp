#include "UIBridge.h"
#include "plugin_src/brain/Brain.h"
#include "plugin_src/transformers/BaseTransformer.h"
#include "plugin_src/transformers/TransformerFactory.h"
#include "plugin_src/morph/IMorph.h"
#include "plugin_src/morph/MorphFactory.h"
#include "plugin_src/params/DynamicParamSchema.h"

namespace synaptic
{
  UIBridge::UIBridge(iplug::IEditorDelegate* delegate)
    : mDelegate(delegate)
  {
  }

  void UIBridge::SendJSON(const nlohmann::json& j)
  {
    if (!mDelegate) return;
    const std::string payload = j.dump();
    mDelegate->SendArbitraryMsgFromDelegate(-1, static_cast<int>(payload.size()), payload.c_str());
  }

  void UIBridge::SendBrainSummary(const Brain& brain)
  {
    nlohmann::json j;
    j["id"] = "brainSummary";
    nlohmann::json arr = nlohmann::json::array();

    for (const auto& s : brain.GetSummary())
    {
      nlohmann::json o;
      o["id"] = s.id;
      o["name"] = s.name;
      o["chunks"] = s.chunkCount;
      arr.push_back(o);
    }

    j["files"] = arr;
    SendJSON(j);
  }

  void UIBridge::SendTransformerParams(std::shared_ptr<const IChunkBufferTransformer> transformer)
  {
    nlohmann::json j;
    j["id"] = "transformerParams";

    if (!transformer)
    {
      // Send empty list if no transformer
      j["params"] = nlohmann::json::array();
      SendJSON(j);
      return;
    }

    std::vector<ExposedParamDesc> descs;
    transformer->GetParamDescs(descs);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : descs)
    {
      nlohmann::json o;
      o["id"] = d.id;
      o["label"] = d.label;

      // Type
      switch (d.type)
      {
        case ParamType::Number:  o["type"] = "number";  break;
        case ParamType::Boolean: o["type"] = "boolean"; break;
        case ParamType::Enum:    o["type"] = "enum";    break;
        case ParamType::Text:    o["type"] = "text";    break;
      }

      // Control type
      switch (d.control)
      {
        case ControlType::Slider:    o["control"] = "slider";    break;
        case ControlType::NumberBox: o["control"] = "numberbox"; break;
        case ControlType::Select:    o["control"] = "select";    break;
        case ControlType::Checkbox:  o["control"] = "checkbox";  break;
        case ControlType::TextBox:   o["control"] = "textbox";   break;
      }

      // Numeric bounds
      o["min"] = d.minValue;
      o["max"] = d.maxValue;
      o["step"] = d.step;

      // Options (for enum/select)
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

      // Current value
      double num;
      bool b;
      std::string str;
      if (transformer->GetParamAsNumber(d.id, num))
        o["value"] = num;
      else if (transformer->GetParamAsBool(d.id, b))
        o["value"] = b;
      else if (transformer->GetParamAsString(d.id, str))
        o["value"] = str;
      else
      {
        // Fall back to defaults
        if (d.type == ParamType::Number)
          o["value"] = d.defaultNumber;
        else if (d.type == ParamType::Boolean)
          o["value"] = d.defaultBool;
        else
          o["value"] = d.defaultString;
      }

      arr.push_back(o);
    }

    j["params"] = arr;
    SendJSON(j);
  }

  void UIBridge::SendMorphParams(std::shared_ptr<const IMorph> morph)
  {
    nlohmann::json j;
    j["id"] = "morphParams";

    if (!morph)
    {
      j["params"] = nlohmann::json::array();
      SendJSON(j);
      return;
    }

    std::vector<ExposedParamDesc> descs;
    morph->GetParamDescs(descs);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : descs)
    {
      nlohmann::json o;
      o["id"] = d.id;
      o["label"] = d.label;
      switch (d.type)
      {
        case ParamType::Number:  o["type"] = "number";  break;
        case ParamType::Boolean: o["type"] = "boolean"; break;
        case ParamType::Enum:    o["type"] = "enum";    break;
        case ParamType::Text:    o["type"] = "text";    break;
      }
      switch (d.control)
      {
        case ControlType::Slider:    o["control"] = "slider";    break;
        case ControlType::NumberBox: o["control"] = "numberbox"; break;
        case ControlType::Select:    o["control"] = "select";    break;
        case ControlType::Checkbox:  o["control"] = "checkbox";  break;
        case ControlType::TextBox:   o["control"] = "textbox";   break;
      }
      o["min"] = d.minValue;
      o["max"] = d.maxValue;
      o["step"] = d.step;
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
      double num;
      bool b;
      std::string str;
      if (morph->GetParamAsNumber(d.id, num))
        o["value"] = num;
      else if (morph->GetParamAsBool(d.id, b))
        o["value"] = b;
      else if (morph->GetParamAsString(d.id, str))
        o["value"] = str;
      else
      {
        if (d.type == ParamType::Number)
          o["value"] = d.defaultNumber;
        else if (d.type == ParamType::Boolean)
          o["value"] = d.defaultBool;
        else
          o["value"] = d.defaultString;
      }
      arr.push_back(o);
    }

    j["params"] = arr;
    SendJSON(j);
  }

  void UIBridge::SendDSPConfig(const DSPConfig& config)
  {
    nlohmann::json j;
    j["id"] = "dspConfig";
    j["chunkSize"] = config.chunkSize;
    j["bufferWindowSize"] = config.bufferWindowSize;
    j["outputWindowMode"] = config.outputWindowMode;
    j["analysisWindowMode"] = config.analysisWindowMode;
    j["algorithmId"] = config.algorithmId;
    j["useExternalBrain"] = config.useExternalBrain;
    j["externalPath"] = config.externalPath;

    SendJSON(j);
  }

  void UIBridge::SendDSPConfigWithAlgorithms(const DSPConfig& config)
  {
    SendDSPConfigWithAlgorithms(config, 0);
  }

  void UIBridge::SendDSPConfigWithAlgorithms(const DSPConfig& config, int currentMorphIndex)
  {
    nlohmann::json j;
    j["id"] = "dspConfig";
    j["chunkSize"] = config.chunkSize;
    j["bufferWindowSize"] = config.bufferWindowSize;
    j["outputWindowMode"] = config.outputWindowMode;
    j["analysisWindowMode"] = config.analysisWindowMode;
    j["algorithmId"] = config.algorithmId;
    j["useExternalBrain"] = config.useExternalBrain;
    j["externalPath"] = config.externalPath;

    // Add transformer algorithm options from factory
    nlohmann::json opts = nlohmann::json::array();
    const auto ids = TransformerFactory::GetUiIds();
    const auto labels = TransformerFactory::GetUiLabels();
    const int n = (int)std::min(ids.size(), labels.size());

    for (int i = 0; i < n; ++i)
    {
      nlohmann::json o;
      o["id"] = ids[i];
      o["label"] = labels[i];
      o["index"] = i;
      opts.push_back(o);
    }

    j["algorithms"] = opts;

    // Add morph mode options from factory
    nlohmann::json mopts = nlohmann::json::array();
    const auto mids = MorphFactory::GetUiIds();
    const auto mlabels = MorphFactory::GetUiLabels();
    const int mn = (int)std::min(mids.size(), mlabels.size());
    for (int i = 0; i < mn; ++i)
    {
      nlohmann::json o;
      o["id"] = mids[i];
      o["label"] = mlabels[i];
      o["index"] = i;
      mopts.push_back(o);
    }
    j["morphModes"] = mopts;
    j["morphModeIndex"] = currentMorphIndex;

    SendJSON(j);
  }

  void UIBridge::SendExternalRefInfo(bool useExternal, const std::string& path)
  {
    nlohmann::json j;
    j["id"] = "brainExternalRef";
    nlohmann::json info;
    info["path"] = useExternal ? path : std::string();
    j["info"] = info;
    SendJSON(j);
  }

  void UIBridge::SendAllState(const Brain& brain,
                              std::shared_ptr<const IChunkBufferTransformer> transformer,
                              std::shared_ptr<const IMorph> morph,
                              const DSPConfig& config)
  {
    SendTransformerParams(transformer);
    SendMorphParams(morph);
    SendDSPConfigWithAlgorithms(config);
    SendBrainSummary(brain);
    SendExternalRefInfo(config.useExternalBrain, config.externalPath);
  }

  void UIBridge::ShowOverlay(const std::string& text)
  {
    nlohmann::json j;
    j["id"] = "overlay";
    j["visible"] = true;
    j["text"] = text;
    SendJSON(j);
  }

  void UIBridge::HideOverlay()
  {
    nlohmann::json j;
    j["id"] = "overlay";
    j["visible"] = false;
    SendJSON(j);
  }

  void UIBridge::ShowProgressOverlay(const std::string& title, const std::string& message, float progress)
  {
    // For WebUI, use the simple overlay message system
    nlohmann::json j;
    j["id"] = "overlay";
    j["visible"] = true;
    j["text"] = title + ": " + message;
    EnqueueJSON(j);
  }

  void UIBridge::UpdateProgressOverlay(const std::string& message, float progress)
  {
    // For WebUI, re-send the overlay with updated text
    nlohmann::json j;
    j["id"] = "overlay";
    j["visible"] = true;
    j["text"] = message;
    EnqueueJSON(j);
  }

  void UIBridge::EnqueuePayload(const std::string& jsonPayload)
  {
    std::lock_guard<std::mutex> lock(mQueueMutex);
    mQueue.push_back(jsonPayload);
  }

  void UIBridge::EnqueueJSON(const nlohmann::json& j)
  {
    EnqueuePayload(j.dump());
  }

  void UIBridge::DrainQueue()
  {
    // Process atomic flags first (coalescing)
    if (mPendingSendBrainSummary.exchange(false))
    {
      // Note: We can't send brain summary here without brain reference
      // The calling code should handle this by checking the flag
      // This is just for clearing the flag
    }

    if (mPendingDSPConfig.exchange(false))
    {
      // Same note as above - caller should handle
    }

    // Drain queued JSON payloads
    std::vector<std::string> local;
    {
      std::lock_guard<std::mutex> lock(mQueueMutex);
      if (!mQueue.empty())
      {
        local.swap(mQueue);
      }
    }

    // Send all queued messages
    for (const auto& s : local)
    {
      if (mDelegate)
        mDelegate->SendArbitraryMsgFromDelegate(-1, (int)s.size(), s.c_str());
    }
  }
}

