/**
 * @file ParameterManager.cpp
 * @brief Implementation of parameter management and coordination
 */

#include "ParameterManager.h"
#include "plugin_src/transformers/TransformerFactory.h"
#include "plugin_src/morph/MorphFactory.h"
#include "plugin_src/params/DynamicParamSchema.h"
#include "plugin_src/modules/AudioStreamChunker.h"
#include "plugin_src/modules/WindowCoordinator.h"
#include "plugin_src/audio/DSPContext.h"
#include "plugin_src/brain/Brain.h"
#include "plugin_src/brain/BrainManager.h"
#include "plugin_src/ui/core/ProgressOverlayManager.h"
#include "plugin_src/Structs.h"
#include "plugin_src/modules/UISyncManager.h"

#if IPLUG_EDITOR
  #include "plugin_src/ui/controls/UIControls.h"
#endif

namespace synaptic
{
  namespace
  {
    // Build a union of dynamic parameter descs across all known transformers and morph modes
    void BuildTransformerUnion(std::vector<ExposedParamDesc>& out)
    {
      out.clear();
      std::vector<ExposedParamDesc> tmp;

      auto consider = [&](std::shared_ptr<IChunkBufferTransformer> t) {
        tmp.clear();
        t->GetParamDescs(tmp, true);
        for (const auto& d : tmp)
        {
          auto it = std::find_if(out.begin(), out.end(), [&](const auto& e) { return e.id == d.id; });
          if (it == out.end()) out.push_back(d);
        }
      };

      for (const auto& info : TransformerFactory::GetAll())
        consider(info.create());

      auto considerMorph = [&](std::shared_ptr<IMorph> m) {
        tmp.clear();
        m->GetParamDescs(tmp, true);
        for (const auto& d : tmp)
        {
          auto it = std::find_if(out.begin(), out.end(), [&](const auto& e) { return e.id == d.id; });
          if (it == out.end()) out.push_back(d);
        }
      };

      for (const auto& info : MorphFactory::GetAll())
        considerMorph(info.create());
    }
  }

  // ============================================================================
  // Construction and Initialization
  // ============================================================================

  ParameterManager::ParameterManager() {}

  int ParameterManager::GetTotalParams()
  {
    std::vector<ExposedParamDesc> unionDescs;
    BuildTransformerUnion(unionDescs);
    return kNumParams + (int)unionDescs.size();
  }

  void ParameterManager::InitializeCoreParameters(iplug::Plugin* plugin, const DSPConfig& config)
  {
    mParamIdxChunkSize = kChunkSize;
    plugin->GetParam(mParamIdxChunkSize)->InitInt("Chunk Size", config.chunkSize, 
      DSPDefaults::kMinChunkSize, DSPDefaults::kMaxChunkSize, "samples", iplug::IParam::kFlagCannotAutomate);

    mParamIdxBufferWindow = kBufferWindow;
    plugin->GetParam(mParamIdxBufferWindow)->InitInt("Buffer Window", config.bufferWindowSize, 
      DSPDefaults::kMinBufferWindow, DSPDefaults::kMaxBufferWindow, "chunks", iplug::IParam::kFlagCannotAutomate);

    mParamIdxDirtyFlag = kDirtyFlag;
    plugin->GetParam(mParamIdxDirtyFlag)->InitBool("Dirty Flag", false, "", iplug::IParam::kFlagCannotAutomate);

    mParamIdxAlgorithm = kAlgorithm;
    {
      const int count = TransformerFactory::GetUiCount();
      plugin->GetParam(mParamIdxAlgorithm)->InitEnum("Algorithm", config.algorithmId, count, "");
      const auto labels = TransformerFactory::GetUiLabels();
      for (int i = 0; i < (int)labels.size(); ++i)
        plugin->GetParam(mParamIdxAlgorithm)->SetDisplayText(i, labels[i].c_str());
    }

    mParamIdxOutputWindow = kOutputWindow;
    plugin->GetParam(mParamIdxOutputWindow)->InitEnum("Output Window", WindowMode::ConfigToParam(config.outputWindowMode), 4, "");
    plugin->GetParam(mParamIdxOutputWindow)->SetDisplayText(0, "Hann");
    plugin->GetParam(mParamIdxOutputWindow)->SetDisplayText(1, "Hamming");
    plugin->GetParam(mParamIdxOutputWindow)->SetDisplayText(2, "Blackman");
    plugin->GetParam(mParamIdxOutputWindow)->SetDisplayText(3, "Rectangular");

    mParamIdxAnalysisWindow = kAnalysisWindow;
    plugin->GetParam(mParamIdxAnalysisWindow)->InitEnum("Chunk Analysis Window", WindowMode::ConfigToParam(config.analysisWindowMode), 4, "", iplug::IParam::kFlagCannotAutomate);
    plugin->GetParam(mParamIdxAnalysisWindow)->SetDisplayText(0, "Hann");
    plugin->GetParam(mParamIdxAnalysisWindow)->SetDisplayText(1, "Hamming");
    plugin->GetParam(mParamIdxAnalysisWindow)->SetDisplayText(2, "Blackman");
    plugin->GetParam(mParamIdxAnalysisWindow)->SetDisplayText(3, "Rectangular");

    mParamIdxEnableOverlap = kEnableOverlap;
    plugin->GetParam(mParamIdxEnableOverlap)->InitBool("Enable Overlap-Add", config.enableOverlapAdd);

    mParamIdxAutotuneBlend = kAutotuneBlend;
    plugin->GetParam(mParamIdxAutotuneBlend)->InitDouble("Autotune Blend", 0.0, 0.0, 100.0, 0.1, "%");

    mParamIdxAutotuneMode = kAutotuneMode;
    plugin->GetParam(mParamIdxAutotuneMode)->InitEnum("Autotune Mode", 1, 2, "");
    plugin->GetParam(mParamIdxAutotuneMode)->SetDisplayText(0, "FFT Peak");
    plugin->GetParam(mParamIdxAutotuneMode)->SetDisplayText(1, "HPS");

    mParamIdxAutotuneToleranceOctaves = kAutotuneToleranceOctaves;
    plugin->GetParam(mParamIdxAutotuneToleranceOctaves)->InitEnum("Autotune Range (Octaves)", 2, 5, "");
    for (int i = 0; i < 5; ++i)
      plugin->GetParam(mParamIdxAutotuneToleranceOctaves)->SetDisplayText(i, std::to_string(i + 1).c_str());

    mParamIdxMorphMode = kMorphMode;
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
    std::vector<ExposedParamDesc> unionDescs;
    BuildTransformerUnion(unionDescs);
    int base = kNumParams;
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
          for (int k = 0; k < n; ++k)
            plugin->GetParam(idx)->SetDisplayText(k, d.options[k].label.c_str());

          TransformerParamBinding binding{d.id, d.type, idx, {}};
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

  // ============================================================================
  // Core Parameter Change Handling
  // ============================================================================

  bool ParameterManager::HandleCoreParameterChange(int paramIdx, iplug::IParam* param, DSPConfig& config)
  {
    if (paramIdx == mParamIdxChunkSize)
    {
      config.chunkSize = std::max(1, param->Int());
      return true;
    }
    if (paramIdx == mParamIdxBufferWindow)
    {
      config.bufferWindowSize = std::max(1, param->Int());
      return true;
    }
    if (paramIdx == mParamIdxAlgorithm)
    {
      config.algorithmId = param->Int();
      return true;
    }
    if (paramIdx == mParamIdxOutputWindow)
    {
      config.outputWindowMode = WindowMode::ParamToConfig(param->Int());
      return true;
    }
    if (paramIdx == mParamIdxAnalysisWindow)
    {
      config.analysisWindowMode = WindowMode::ParamToConfig(param->Int());
      return true;
    }
    if (paramIdx == mParamIdxEnableOverlap)
    {
      config.enableOverlapAdd = param->Bool();
      return true;
    }
    if (paramIdx == mParamIdxMorphMode)
    {
      return true;
    }

    return false;
  }

  // ============================================================================
  // Dynamic Parameter Handling
  // ============================================================================

  bool ParameterManager::HandleTransformerParameterChange(int paramIdx, iplug::IParam* param,
                                                          IChunkBufferTransformer* transformer)
  {
    return HandleDynamicParameterChange(paramIdx, param, transformer, nullptr);
  }

  bool ParameterManager::HandleDynamicParameterChange(int paramIdx, iplug::IParam* param,
                                                      IChunkBufferTransformer* transformer,
                                                      IMorph* morph,
                                                      bool* outNeedsTransformerRebuild,
                                                      bool* outNeedsMorphRebuild)
  {
    if (outNeedsTransformerRebuild) *outNeedsTransformerRebuild = false;
    if (outNeedsMorphRebuild) *outNeedsMorphRebuild = false;

    for (const auto& b : mBindings)
    {
      if (b.paramIdx == paramIdx)
      {
        if (transformer && transformer->ParamChangeRequiresUIRebuild(b.id) && outNeedsTransformerRebuild)
          *outNeedsTransformerRebuild = true;
        if (morph && morph->ParamChangeRequiresUIRebuild(b.id) && outNeedsMorphRebuild)
          *outNeedsMorphRebuild = true;

        ApplyBindingValue(b, param, transformer, morph);
        return true;
      }
    }
    return false;
  }

  // ============================================================================
  // Unified Binding Application
  // ============================================================================

  void ParameterManager::ApplyBindingValue(const TransformerParamBinding& binding, iplug::IParam* param,
                                           IChunkBufferTransformer* transformer, IMorph* morph)
  {
    switch (binding.type)
    {
      case ParamType::Number:
      {
        const double v = param->Value();
        if (transformer) transformer->SetParamFromNumber(binding.id, v);
        if (morph) morph->SetParamFromNumber(binding.id, v);
        break;
      }
      case ParamType::Boolean:
      {
        const bool v = param->Bool();
        if (transformer) transformer->SetParamFromBool(binding.id, v);
        if (morph) morph->SetParamFromBool(binding.id, v);
        break;
      }
      case ParamType::Enum:
      {
        int idx = param->Int();
        std::string val = (idx >= 0 && idx < (int)binding.enumValues.size())
          ? binding.enumValues[idx]
          : std::to_string(idx);
        if (transformer) transformer->SetParamFromString(binding.id, val);
        if (morph) morph->SetParamFromString(binding.id, val);
        break;
      }
      case ParamType::Text:
        break;
    }
  }

  void ParameterManager::ApplyBindingsTo(iplug::Plugin* plugin, IChunkBufferTransformer* transformer, IMorph* morph)
  {
    if (!transformer && !morph) return;

    for (const auto& b : mBindings)
    {
      if (b.paramIdx < 0) continue;
      auto* param = plugin->GetParam(b.paramIdx);
      if (!param) continue;

      ApplyBindingValue(b, param, transformer, morph);
    }
  }

  // ============================================================================
  // Query Methods
  // ============================================================================

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
    return mTransformerParamBase >= 0 && paramIdx >= mTransformerParamBase;
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

  // ============================================================================
  // Parameter Utility Methods
  // ============================================================================

  void ParameterManager::SetParameterFromUI(iplug::Plugin* plugin, int paramIdx, double value)
  {
    const double norm = plugin->GetParam(paramIdx)->ToNormalized(value);
    plugin->BeginInformHostOfParamChangeFromUI(paramIdx);
    plugin->SendParameterValueFromUI(paramIdx, norm);
    plugin->EndInformHostOfParamChangeFromUI(paramIdx);
  }

  void ParameterManager::RollbackParameter(iplug::Plugin* plugin, int paramIdx, double oldValue, const char* debugName)
  {
    if (paramIdx < 0) return;

    plugin->GetParam(paramIdx)->Set(oldValue);
    SetParameterFromUI(plugin, paramIdx, oldValue);
    SyncControlToParameter(plugin, paramIdx);

    if (debugName)
    {
      DBGMSG("%s CANCELLED - rolled back parameter to %g\n", debugName, oldValue);
    }
  }

  void ParameterManager::SyncControlToParameter(iplug::Plugin* plugin, int paramIdx)
  {
#if IPLUG_EDITOR
    if (!plugin->GetUI()) return;

    auto* graphics = plugin->GetUI();
    int numControls = graphics->NControls();

    for (int i = 0; i < numControls; ++i)
    {
      auto* ctrl = graphics->GetControl(i);
      if (ctrl && ctrl->GetParamIdx() == paramIdx)
      {
        if (const iplug::IParam* pParam = plugin->GetParam(paramIdx))
        {
          ctrl->SetValueFromDelegate(pParam->GetNormalized());
          ctrl->SetDirty(true);
        }
      }
    }

    graphics->SetAllControlsDirty();
#endif
  }

  // ============================================================================
  // Main Entry Point: OnParamChange
  // ============================================================================

  void ParameterManager::OnParamChange(int paramIdx, ParameterChangeContext& ctx)
  {
    if (!ctx.plugin || !ctx.config) return;

    // Route to specific handler based on parameter type
    if (paramIdx == mParamIdxChunkSize)
      HandleChunkSizeParam(paramIdx, ctx);
    else if (paramIdx == mParamIdxBufferWindow)
      HandleBufferWindowParam(paramIdx, ctx);
    else if (paramIdx == mParamIdxAlgorithm)
      HandleAlgorithmParam(paramIdx, ctx);
    else if (paramIdx == mParamIdxOutputWindow)
      HandleOutputWindowParam(paramIdx, ctx);
    else if (paramIdx == mParamIdxAnalysisWindow)
      HandleAnalysisWindowParam(paramIdx, ctx);
    else if (paramIdx == mParamIdxEnableOverlap)
      HandleEnableOverlapParam(paramIdx, ctx);
    else if (paramIdx == mParamIdxAutotuneBlend)
      HandleAutotuneBlendParam(paramIdx, ctx);
    else if (paramIdx == mParamIdxAutotuneMode)
      HandleAutotuneModeParam(paramIdx, ctx);
    else if (paramIdx == mParamIdxAutotuneToleranceOctaves)
      HandleAutotuneToleranceParam(paramIdx, ctx);
    else if (paramIdx == mParamIdxMorphMode)
      HandleMorphModeParam(paramIdx, ctx);
    else if (paramIdx == kWindowLock)
      HandleWindowLockParam(paramIdx, ctx);
    else
      HandleDynamicParam(paramIdx, ctx);
  }

  // ============================================================================
  // Per-Parameter Handlers
  // ============================================================================

  void ParameterManager::HandleChunkSizeParam(int paramIdx, ParameterChangeContext& ctx)
  {
    const int oldChunkSize = ctx.config->chunkSize;

    if (ctx.brainManager && ctx.brainManager->IsOperationInProgress())
    {
      HandleCoreParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config);
      if (ctx.windowCoordinator && ctx.dspContext)
        ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.dspContext->GetTransformerRaw());
      if (ctx.setLatency && ctx.computeLatency)
        ctx.setLatency(ctx.computeLatency());
      return;
    }

    bool chunkSizeChanged = HandleChunkSizeChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config,
                                                  ctx.plugin, *ctx.chunker, *ctx.analysisWindow);
    if (ctx.windowCoordinator && ctx.dspContext)
      ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.dspContext->GetTransformerRaw());
    if (ctx.setLatency && ctx.computeLatency)
      ctx.setLatency(ctx.computeLatency());

    if (chunkSizeChanged && ctx.brainManager && ctx.progressOverlayMgr)
    {
      ctx.progressOverlayMgr->Show("Rechunking", "Starting...", 0.0f, true);

      auto* plugin = ctx.plugin;
      auto* config = ctx.config;
      auto* chunker = ctx.chunker;
      auto* windowCoordinator = ctx.windowCoordinator;
      auto* dspContext = ctx.dspContext;
      auto* progressOverlayMgr = ctx.progressOverlayMgr;
      auto setPendingUpdate = ctx.setPendingUpdate;
      auto computeLatency = ctx.computeLatency;
      auto setLatency = ctx.setLatency;

      ctx.brainManager->RechunkAllFilesAsync(
        ctx.config->chunkSize, (int)ctx.plugin->GetSampleRate(),
        [progressOverlayMgr](const std::string& fileName, int current, int total) {
          const float p = (total > 0) ? ((float)current / (float)total * 100.0f) : 50.0f;
          char buf[256];
          snprintf(buf, sizeof(buf), "%s (chunk %d/%d)", fileName.c_str(), current, total);
          progressOverlayMgr->Update(buf, p);
        },
        [plugin, config, chunker, windowCoordinator, dspContext, progressOverlayMgr,
         setPendingUpdate, computeLatency, setLatency, oldChunkSize, paramIdx](bool wasCancelled) {
          progressOverlayMgr->Hide();
          if (!wasCancelled)
          {
            if (setPendingUpdate)
            {
              setPendingUpdate((uint32_t)PendingUpdate::BrainSummary);
              setPendingUpdate((uint32_t)PendingUpdate::MarkDirty);
            }
          }
          else
          {
            config->chunkSize = oldChunkSize;
            chunker->SetChunkSize(oldChunkSize);
            if (windowCoordinator)
            {
              windowCoordinator->UpdateBrainAnalysisWindow(*config);
              windowCoordinator->UpdateChunkerWindowing(*config, dspContext->GetTransformerRaw());
            }
            if (setLatency && computeLatency)
              setLatency(computeLatency());

            RollbackParameter(plugin, paramIdx, (double)oldChunkSize, "Rechunking");
          }
        }
      );
    }
  }

  void ParameterManager::HandleBufferWindowParam(int paramIdx, ParameterChangeContext& ctx)
  {
    HandleCoreParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config);
    if (ctx.chunker)
      ctx.chunker->SetBufferWindowSize(ctx.config->bufferWindowSize);
  }

  void ParameterManager::HandleAlgorithmParam(int paramIdx, ParameterChangeContext& ctx)
  {
    if (ctx.dspContext && ctx.brain)
    {
      auto newTransformer = HandleAlgorithmChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config,
                                                   ctx.plugin, *ctx.brain, ctx.plugin->GetSampleRate(),
                                                   ctx.plugin->NInChansConnected());
      ctx.dspContext->SetPendingTransformer(newTransformer);

      if (ctx.windowCoordinator)
        ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, newTransformer.get());
    }

#if IPLUG_EDITOR
    if (ctx.setPendingUpdate)
      ctx.setPendingUpdate((uint32_t)PendingUpdate::RebuildTransformer);
#endif
  }

  void ParameterManager::HandleOutputWindowParam(int paramIdx, ParameterChangeContext& ctx)
  {
    const int oldWindowMode = ctx.config->outputWindowMode;

    HandleCoreParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config);
    if (ctx.windowCoordinator && ctx.dspContext)
      ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.dspContext->GetTransformerRaw());

    const bool windowsAreLocked = ctx.plugin->GetParam(kWindowLock)->Bool();
    if (windowsAreLocked)
    {
      const int outputWindowIdx = ctx.plugin->GetParam(kOutputWindow)->Int();
      const int analysisWindowIdx = ctx.plugin->GetParam(kAnalysisWindow)->Int();

      if (outputWindowIdx != analysisWindowIdx && ctx.windowCoordinator)
      {
        ctx.windowCoordinator->SyncAnalysisToOutputWindow(ctx.plugin, *ctx.config, false);

        auto* plugin = ctx.plugin;
        auto* config = ctx.config;
        auto* windowCoordinator = ctx.windowCoordinator;
        auto* dspContext = ctx.dspContext;
        auto setPendingUpdate = ctx.setPendingUpdate;

        ctx.windowCoordinator->TriggerBrainReanalysisAsync(
          (int)ctx.plugin->GetSampleRate(),
          [this, plugin, config, windowCoordinator, dspContext, setPendingUpdate,
           oldWindowMode, paramIdx](bool wasCancelled) {
            if (!wasCancelled)
            {
              if (setPendingUpdate)
              {
                setPendingUpdate((uint32_t)PendingUpdate::BrainSummary);
                setPendingUpdate((uint32_t)PendingUpdate::MarkDirty);
              }
            }
            else
            {
              config->outputWindowMode = oldWindowMode;
              config->analysisWindowMode = oldWindowMode;

              if (windowCoordinator)
              {
                windowCoordinator->UpdateBrainAnalysisWindow(*config);
                if (dspContext)
                  windowCoordinator->UpdateChunkerWindowing(*config, dspContext->GetTransformerRaw());
              }

              setPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze);

              const int oldIdx = WindowMode::ConfigToParam(oldWindowMode);
              RollbackParameter(plugin, kAnalysisWindow, (double)oldIdx, nullptr);
              RollbackParameter(plugin, paramIdx, (double)oldIdx, "Reanalysis (Output Window)");

              DBGMSG("Reanalysis CANCELLED - rolled back both locked windows to mode %d\n", oldWindowMode);
            }
          });

        if (ctx.setPendingUpdate)
          ctx.setPendingUpdate((uint32_t)PendingUpdate::DSPConfig);
      }
    }
  }

  void ParameterManager::HandleAnalysisWindowParam(int paramIdx, ParameterChangeContext& ctx)
  {
    const int oldWindowMode = ctx.config->analysisWindowMode;

    if (ctx.brainManager && ctx.brainManager->IsOperationInProgress())
    {
      if (ctx.checkAndClearPendingUpdate)
        ctx.checkAndClearPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze);

      HandleCoreParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config);
      if (ctx.windowCoordinator)
        ctx.windowCoordinator->UpdateBrainAnalysisWindow(*ctx.config);
      return;
    }

    bool analysisWindowChanged = HandleAnalysisWindowChange(paramIdx, ctx.plugin->GetParam(paramIdx),
                                                             *ctx.config, *ctx.analysisWindow, *ctx.brain);

    const bool windowsAreLocked = ctx.plugin->GetParam(kWindowLock)->Bool();
    if (windowsAreLocked)
    {
      const int analysisWindowIdx = ctx.plugin->GetParam(kAnalysisWindow)->Int();
      const int outputWindowIdx = ctx.plugin->GetParam(kOutputWindow)->Int();

      if (analysisWindowIdx != outputWindowIdx && ctx.windowCoordinator)
      {
        ctx.windowCoordinator->SyncOutputToAnalysisWindow(ctx.plugin, *ctx.config);
        if (ctx.dspContext)
          ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.dspContext->GetTransformerRaw());
      }
    }

    if (analysisWindowChanged && ctx.checkAndClearPendingUpdate &&
        !ctx.checkAndClearPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze))
    {
      if (ctx.windowCoordinator)
      {
        auto* plugin = ctx.plugin;
        auto* config = ctx.config;
        auto* windowCoordinator = ctx.windowCoordinator;
        auto* dspContext = ctx.dspContext;
        auto setPendingUpdate = ctx.setPendingUpdate;

        ctx.windowCoordinator->TriggerBrainReanalysisAsync(
          (int)ctx.plugin->GetSampleRate(),
          [this, plugin, config, windowCoordinator, dspContext, setPendingUpdate,
           oldWindowMode, windowsAreLocked, paramIdx](bool wasCancelled) {
            if (!wasCancelled)
            {
              if (setPendingUpdate)
              {
                setPendingUpdate((uint32_t)PendingUpdate::BrainSummary);
                setPendingUpdate((uint32_t)PendingUpdate::MarkDirty);
              }
            }
            else
            {
              config->analysisWindowMode = oldWindowMode;
              if (windowsAreLocked)
                config->outputWindowMode = oldWindowMode;

              if (windowCoordinator)
              {
                windowCoordinator->UpdateBrainAnalysisWindow(*config);
                if (dspContext)
                  windowCoordinator->UpdateChunkerWindowing(*config, dspContext->GetTransformerRaw());
              }

              setPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze);

              const int oldIdx = WindowMode::ConfigToParam(oldWindowMode);
              RollbackParameter(plugin, paramIdx, (double)oldIdx, "Reanalysis (Analysis Window)");

              if (windowsAreLocked)
                RollbackParameter(plugin, kOutputWindow, (double)oldIdx, nullptr);

              DBGMSG("Reanalysis CANCELLED - rolled back %s to mode %d\n",
                     windowsAreLocked ? "both locked windows" : "analysis window", oldWindowMode);
            }
          });
      }
    }

    if (ctx.setPendingUpdate)
      ctx.setPendingUpdate((uint32_t)PendingUpdate::DSPConfig);
  }

  void ParameterManager::HandleEnableOverlapParam(int paramIdx, ParameterChangeContext& ctx)
  {
    HandleCoreParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config);
    if (ctx.windowCoordinator && ctx.dspContext)
      ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.dspContext->GetTransformerRaw());
  }

  void ParameterManager::HandleAutotuneBlendParam(int paramIdx, ParameterChangeContext& ctx)
  {
    const double blendPercent = ctx.plugin->GetParam(paramIdx)->Value();
    if (ctx.chunker)
      ctx.chunker->GetAutotuneProcessor().SetBlend((float)(blendPercent / 100.0));
  }

  void ParameterManager::HandleAutotuneModeParam(int paramIdx, ParameterChangeContext& ctx)
  {
    const int mode = ctx.plugin->GetParam(paramIdx)->Int();
    if (ctx.chunker)
      ctx.chunker->GetAutotuneProcessor().SetMode(mode == 1);
  }

  void ParameterManager::HandleAutotuneToleranceParam(int paramIdx, ParameterChangeContext& ctx)
  {
    const int enumIdx = std::clamp(ctx.plugin->GetParam(paramIdx)->Int(), 0, 4);
    if (ctx.chunker)
      ctx.chunker->GetAutotuneProcessor().SetToleranceOctaves(enumIdx + 1);
  }

  void ParameterManager::HandleMorphModeParam(int paramIdx, ParameterChangeContext& ctx)
  {
    if (ctx.dspContext)
    {
      auto newMorph = HandleMorphModeChange(paramIdx, ctx.plugin->GetParam(paramIdx), ctx.plugin,
                                             ctx.plugin->GetSampleRate(), ctx.config->chunkSize,
                                             ctx.plugin->NInChansConnected());
      ctx.dspContext->SetPendingMorph(newMorph);
    }

#if IPLUG_EDITOR
    if (ctx.setPendingUpdate)
      ctx.setPendingUpdate((uint32_t)PendingUpdate::RebuildMorph);
#endif
  }

  void ParameterManager::HandleWindowLockParam(int paramIdx, ParameterChangeContext& ctx)
  {
    if (ctx.plugin->GetParam(kWindowLock)->Bool())
    {
#if IPLUG_EDITOR
      int clickedWindowParam = synaptic::ui::LockButtonControl::GetLastClickedWindowParam();
#else
      int clickedWindowParam = kOutputWindow;
#endif

      if (ctx.windowCoordinator)
      {
        ctx.windowCoordinator->HandleWindowLockToggle(true, clickedWindowParam, ctx.plugin, *ctx.config);
        if (ctx.dspContext)
          ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.dspContext->GetTransformerRaw());
      }
      if (ctx.setPendingUpdate)
        ctx.setPendingUpdate((uint32_t)PendingUpdate::DSPConfig);
    }
  }

  void ParameterManager::HandleDynamicParam(int paramIdx, ParameterChangeContext& ctx)
  {
    bool needsTransformerRebuild = false;
    bool needsMorphRebuild = false;

    IChunkBufferTransformer* transformer = ctx.dspContext ? ctx.dspContext->GetTransformerRaw() : nullptr;
    IMorph* morph = ctx.dspContext ? ctx.dspContext->GetMorphRaw() : nullptr;

    if (HandleDynamicParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), transformer, morph,
                                      &needsTransformerRebuild, &needsMorphRebuild))
    {
#if IPLUG_EDITOR
      if (ctx.setPendingUpdate)
      {
        if (needsTransformerRebuild)
          ctx.setPendingUpdate((uint32_t)PendingUpdate::RebuildTransformer);
        if (needsMorphRebuild)
          ctx.setPendingUpdate((uint32_t)PendingUpdate::RebuildMorph);
      }
#endif
    }
  }

}
