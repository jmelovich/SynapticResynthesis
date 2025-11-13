#include "ParameterManager.h"
#include "plugin_src/transformers/TransformerFactory.h"
#include "plugin_src/morph/MorphFactory.h"
#include "plugin_src/params/DynamicParamSchema.h"
#include "SynapticResynthesis.h"  // For EParams enum

namespace synaptic
{
  namespace
  {
    // Build a union of dynamic parameter descs across all known transformers and morph modes
    void BuildTransformerUnion(std::vector<ExposedParamDesc>& out)
    {
      out.clear();
      std::vector<ExposedParamDesc> tmp;
      auto consider = [&](std::shared_ptr<IChunkBufferTransformer> t){
        tmp.clear();
        t->GetParamDescs(tmp);
        for (const auto& d : tmp)
        {
          auto it = std::find_if(out.begin(), out.end(), [&](const auto& e){ return e.id == d.id; });
          if (it == out.end()) out.push_back(d);
        }
      };
      // Iterate all known transformers from the factory
      for (const auto& info : TransformerFactory::GetAll())
        consider(info.create());

      // Also consider morph modes
      auto considerMorph = [&](std::shared_ptr<IMorph> m){
        tmp.clear();
        m->GetParamDescs(tmp);
        for (const auto& d : tmp)
        {
          auto it = std::find_if(out.begin(), out.end(), [&](const auto& e){ return e.id == d.id; });
          if (it == out.end()) out.push_back(d);
        }
      };
      for (const auto& info : MorphFactory::GetAll())
        considerMorph(info.create());
    }
  }

  ParameterManager::ParameterManager()
  {
  }

  void ParameterManager::InitializeCoreParameters(iplug::Plugin* plugin, const DSPConfig& config)
  {
    // Use EParams enum directly (single source of truth)
    mParamIdxChunkSize = ::kChunkSize;
    plugin->GetParam(mParamIdxChunkSize)->InitInt("Chunk Size", config.chunkSize, 1, 262144, "samples", iplug::IParam::kFlagCannotAutomate);

    mParamIdxBufferWindow = ::kBufferWindow;
    plugin->GetParam(mParamIdxBufferWindow)->InitInt("Buffer Window", config.bufferWindowSize, 1, 1024, "chunks", iplug::IParam::kFlagCannotAutomate);

    // Hidden dirty flag param solely for host-dirty nudges (non-automatable)
    mParamIdxDirtyFlag = ::kDirtyFlag;
    plugin->GetParam(mParamIdxDirtyFlag)->InitBool("Dirty Flag", false, "", iplug::IParam::kFlagCannotAutomate);

    // Build algorithm enum from factory UI list (deterministic)
    mParamIdxAlgorithm = ::kAlgorithm;
    {
      const int count = TransformerFactory::GetUiCount();
      plugin->GetParam(mParamIdxAlgorithm)->InitEnum("Algorithm", config.algorithmId, count, "");
      const auto labels = TransformerFactory::GetUiLabels();
      for (int i = 0; i < (int)labels.size(); ++i)
        plugin->GetParam(mParamIdxAlgorithm)->SetDisplayText(i, labels[i].c_str());
    }

    // Output window function (global)
    mParamIdxOutputWindow = ::kOutputWindow;
    plugin->GetParam(mParamIdxOutputWindow)->InitEnum("Output Window", config.outputWindowMode - 1, 4, "");
    plugin->GetParam(mParamIdxOutputWindow)->SetDisplayText(0, "Hann");
    plugin->GetParam(mParamIdxOutputWindow)->SetDisplayText(1, "Hamming");
    plugin->GetParam(mParamIdxOutputWindow)->SetDisplayText(2, "Blackman");
    plugin->GetParam(mParamIdxOutputWindow)->SetDisplayText(3, "Rectangular");

    // Analysis window function (for brain analysis)
    mParamIdxAnalysisWindow = ::kAnalysisWindow;
    plugin->GetParam(mParamIdxAnalysisWindow)->InitEnum("Chunk Analysis Window", config.analysisWindowMode - 1, 4, "", iplug::IParam::kFlagCannotAutomate);
    plugin->GetParam(mParamIdxAnalysisWindow)->SetDisplayText(0, "Hann");
    plugin->GetParam(mParamIdxAnalysisWindow)->SetDisplayText(1, "Hamming");
    plugin->GetParam(mParamIdxAnalysisWindow)->SetDisplayText(2, "Blackman");
    plugin->GetParam(mParamIdxAnalysisWindow)->SetDisplayText(3, "Rectangular");

    // Enable overlap-add processing
    mParamIdxEnableOverlap = ::kEnableOverlap;
    plugin->GetParam(mParamIdxEnableOverlap)->InitBool("Enable Overlap-Add", config.enableOverlapAdd);

    // Autotune blend (percentage)
    mParamIdxAutotuneBlend = ::kAutotuneBlend;
    plugin->GetParam(mParamIdxAutotuneBlend)->InitDouble("Autotune Blend", 0.0, 0.0, 100.0, 0.1, "%");

    // Autotune mode (FFT peak or HPS)
    mParamIdxAutotuneMode = ::kAutotuneMode;
    plugin->GetParam(mParamIdxAutotuneMode)->InitEnum("Autotune Mode", 1, 2, "");
    plugin->GetParam(mParamIdxAutotuneMode)->SetDisplayText(0, "FFT Peak");
    plugin->GetParam(mParamIdxAutotuneMode)->SetDisplayText(1, "HPS");

    // Autotune tolerance (octaves)
    mParamIdxAutotuneToleranceOctaves = ::kAutotuneToleranceOctaves;
    plugin->GetParam(mParamIdxAutotuneToleranceOctaves)->InitInt("Autotune Range (Octaves)", 3, 1, 5, "oct");

    // Morph mode parameters (factory-driven)
    mParamIdxMorphMode = ::kMorphMode;
    {
      const int count = synaptic::MorphFactory::GetUiCount();
      plugin->GetParam(mParamIdxMorphMode)->InitEnum("Morph Mode", 0, count, "");
      const auto labels = synaptic::MorphFactory::GetUiLabels();
      for (int i = 0; i < (int)labels.size(); ++i)
        plugin->GetParam(mParamIdxMorphMode)->SetDisplayText(i, labels[i].c_str());
    }
  }

  void ParameterManager::InitializeTransformerParameters(iplug::Plugin* plugin)
  {
    // Build union descs and initialize the remaining pre-allocated params
    std::vector<ExposedParamDesc> unionDescs;
    BuildTransformerUnion(unionDescs);
    int base = ::kNumParams;  // Use EParams enum value directly
    mTransformerParamBase = base;
    mBindings.clear();

    for (size_t i = 0; i < unionDescs.size(); ++i)
    {
      const auto& d = unionDescs[i];
      const int idx = base + (int)i;

      switch (d.type)
      {
        case ParamType::Number:
          plugin->GetParam(idx)->InitDouble(d.label.c_str(), d.defaultNumber, d.minValue, d.maxValue, d.step);
          break;

        case ParamType::Boolean:
          plugin->GetParam(idx)->InitBool(d.label.c_str(), d.defaultBool);
          break;

        case ParamType::Enum:
        {
          const int n = (int)d.options.size();
          plugin->GetParam(idx)->InitEnum(d.label.c_str(), 0, n, "");
          // Apply display texts for enum items
          for (int k = 0; k < n; ++k)
          {
            const char* lab = (k < n) ? d.options[k].label.c_str() : "";
            plugin->GetParam(idx)->SetDisplayText(k, lab);
          }
          TransformerParamBinding binding{d.id, d.type, idx};
          for (const auto& opt : d.options)
            binding.enumValues.push_back(opt.value);
          mBindings.push_back(std::move(binding));
          continue;
        }

        case ParamType::Text:
          plugin->GetParam(idx)->InitDouble(d.label.c_str(), 0.0, 0.0, 1.0, 0.01, "", iplug::IParam::kFlagCannotAutomate);
          break;
      }
      mBindings.push_back({d.id, d.type, idx, {}});
    }
  }

  bool ParameterManager::HandleCoreParameterChange(int paramIdx, iplug::IParam* param, DSPConfig& config)
  {
    if (paramIdx == mParamIdxChunkSize)
    {
      config.chunkSize = std::max(1, param->Int());
      return true;
    }
    else if (paramIdx == mParamIdxBufferWindow)
    {
      config.bufferWindowSize = std::max(1, param->Int());
      return true;
    }
    else if (paramIdx == mParamIdxAlgorithm)
    {
      config.algorithmId = param->Int();
      return true;
    }
    else if (paramIdx == mParamIdxOutputWindow)
    {
      config.outputWindowMode = 1 + std::clamp(param->Int(), 0, 3);
      return true;
    }
    else if (paramIdx == mParamIdxAnalysisWindow)
    {
      config.analysisWindowMode = 1 + std::clamp(param->Int(), 0, 3);
      return true;
    }
    else if (paramIdx == mParamIdxEnableOverlap)
    {
      config.enableOverlapAdd = param->Bool();
      return true;
    }
    else if (paramIdx == mParamIdxMorphMode)
    {
      // Morph mode is handled by the plugin directly, not stored in DSPConfig
      return true;
    }

    return false;
  }

  bool ParameterManager::HandleTransformerParameterChange(int paramIdx, iplug::IParam* param,
                                                          IChunkBufferTransformer* transformer)
  {
    return HandleDynamicParameterChange(paramIdx, param, transformer, nullptr);
  }

  bool ParameterManager::HandleDynamicParameterChange(int paramIdx, iplug::IParam* param,
                                                      IChunkBufferTransformer* transformer,
                                                      IMorph* morph)
  {
    for (const auto& b : mBindings)
    {
      if (b.paramIdx == paramIdx)
      {
        switch (b.type)
        {
          case ParamType::Number:
          {
            const double v = param->Value();
            bool handled = false;
            if (transformer) handled |= transformer->SetParamFromNumber(b.id, v);
            if (morph) handled |= morph->SetParamFromNumber(b.id, v);
            return handled;
          }
          case ParamType::Boolean:
          {
            const bool v = param->Bool();
            bool handled = false;
            if (transformer) handled |= transformer->SetParamFromBool(b.id, v);
            if (morph) handled |= morph->SetParamFromBool(b.id, v);
            return handled;
          }
          case ParamType::Enum:
          {
            int idx = param->Int();
            std::string val = (idx >= 0 && idx < (int)b.enumValues.size()) ? b.enumValues[idx] : std::to_string(idx);
            bool handled = false;
            if (transformer) handled |= transformer->SetParamFromString(b.id, val);
            if (morph) handled |= morph->SetParamFromString(b.id, val);
            return handled;
          }
          case ParamType::Text:
            // Not supported as text; ignore
            return false;
        }
        return false;
      }
    }
    return false;
  }

  void ParameterManager::ApplyBindingsToTransformer(iplug::Plugin* plugin, IChunkBufferTransformer* transformer)
  {
    if (!transformer) return;

    // Reuse the binding list to push current values into transformer
    for (const auto& b : mBindings)
    {
      if (b.paramIdx < 0) continue;
      auto* param = plugin->GetParam(b.paramIdx);
      if (!param) continue;

      switch (b.type)
      {
        case ParamType::Number:
          transformer->SetParamFromNumber(b.id, param->Value());
          break;

        case ParamType::Boolean:
          transformer->SetParamFromBool(b.id, param->Bool());
          break;

        case ParamType::Enum:
        {
          int idx = param->Int();
          std::string val = (idx >= 0 && idx < (int)b.enumValues.size()) ? b.enumValues[idx] : std::to_string(idx);
          transformer->SetParamFromString(b.id, val);
          break;
        }

        case ParamType::Text:
          // Not supported
          break;
      }
    }
  }

  void ParameterManager::ApplyBindingsToOwners(iplug::Plugin* plugin, IChunkBufferTransformer* transformer, IMorph* morph)
  {
    for (const auto& b : mBindings)
    {
      if (b.paramIdx < 0) continue;
      auto* param = plugin->GetParam(b.paramIdx);
      if (!param) continue;

      switch (b.type)
      {
        case ParamType::Number:
        {
          const double v = param->Value();
          if (transformer) transformer->SetParamFromNumber(b.id, v);
          if (morph) morph->SetParamFromNumber(b.id, v);
          break;
        }
        case ParamType::Boolean:
        {
          const bool v = param->Bool();
          if (transformer) transformer->SetParamFromBool(b.id, v);
          if (morph) morph->SetParamFromBool(b.id, v);
          break;
        }
        case ParamType::Enum:
        {
          int idx = param->Int();
          std::string val = (idx >= 0 && idx < (int)b.enumValues.size()) ? b.enumValues[idx] : std::to_string(idx);
          if (transformer) transformer->SetParamFromString(b.id, val);
          if (morph) morph->SetParamFromString(b.id, val);
          break;
        }
        case ParamType::Text:
          // Not supported
          break;
      }
    }
  }

  bool ParameterManager::IsCoreParameter(int paramIdx) const
  {
    return (paramIdx == mParamIdxChunkSize ||
            paramIdx == mParamIdxBufferWindow ||
            paramIdx == mParamIdxAlgorithm ||
            paramIdx == mParamIdxOutputWindow ||
            paramIdx == mParamIdxAnalysisWindow ||
            paramIdx == mParamIdxDirtyFlag ||
            paramIdx == mParamIdxEnableOverlap ||
            paramIdx == mParamIdxAutotuneBlend ||
            paramIdx == mParamIdxAutotuneMode ||
            paramIdx == mParamIdxAutotuneToleranceOctaves ||
            paramIdx == mParamIdxMorphMode);
  }

  bool ParameterManager::IsTransformerParameter(int paramIdx) const
  {
    if (mTransformerParamBase < 0) return false;
    return paramIdx >= mTransformerParamBase;
  }

  const TransformerParamBinding* ParameterManager::GetBindingForParam(int paramIdx) const
  {
    for (const auto& b : mBindings)
    {
      if (b.paramIdx == paramIdx)
        return &b;
    }
    return nullptr;
  }
}

