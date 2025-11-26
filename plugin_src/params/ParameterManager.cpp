#include "ParameterManager.h"
#include "plugin_src/transformers/TransformerFactory.h"
#include "plugin_src/morph/MorphFactory.h"
#include "plugin_src/params/DynamicParamSchema.h"
#include "plugin_src/modules/AudioStreamChunker.h"
#include "plugin_src/modules/WindowCoordinator.h"
#include "plugin_src/brain/Brain.h"
#include "plugin_src/brain/BrainManager.h"
#include "plugin_src/ui/core/ProgressOverlayManager.h"
#include "plugin_src/Structs.h" // For EParams enum
#include "plugin_src/modules/UISyncManager.h" // For PendingUpdate enum

using namespace synaptic; // Ensure namespace visibility if needed, or fully qualify

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
      auto consider = [&](std::shared_ptr<IChunkBufferTransformer> t){
        tmp.clear();
        t->GetParamDescs(tmp, true);  // includeAll=true to get ALL params for binding
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
        m->GetParamDescs(tmp, true);  // includeAll=true to get ALL params for binding
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

  int ParameterManager::GetTotalParams()
  {
    std::vector<ExposedParamDesc> unionDescs;
    BuildTransformerUnion(unionDescs);
    return kNumParams + (int)unionDescs.size();
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

    // Autotune tolerance (octaves) - enum with values 1-5
    mParamIdxAutotuneToleranceOctaves = ::kAutotuneToleranceOctaves;
    plugin->GetParam(mParamIdxAutotuneToleranceOctaves)->InitEnum("Autotune Range (Octaves)", 2, 5, "");
    plugin->GetParam(mParamIdxAutotuneToleranceOctaves)->SetDisplayText(0, "1");
    plugin->GetParam(mParamIdxAutotuneToleranceOctaves)->SetDisplayText(1, "2");
    plugin->GetParam(mParamIdxAutotuneToleranceOctaves)->SetDisplayText(2, "3");
    plugin->GetParam(mParamIdxAutotuneToleranceOctaves)->SetDisplayText(3, "4");
    plugin->GetParam(mParamIdxAutotuneToleranceOctaves)->SetDisplayText(4, "5");

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
                                                      IMorph* morph,
                                                      bool* outNeedsTransformerRebuild,
                                                      bool* outNeedsMorphRebuild)
  {
    // Initialize output parameters
    if (outNeedsTransformerRebuild) *outNeedsTransformerRebuild = false;
    if (outNeedsMorphRebuild) *outNeedsMorphRebuild = false;

    for (const auto& b : mBindings)
    {
      if (b.paramIdx == paramIdx)
      {
        // Check if UI rebuild is needed
        if (transformer && transformer->ParamChangeRequiresUIRebuild(b.id) && outNeedsTransformerRebuild)
          *outNeedsTransformerRebuild = true;
        if (morph && morph->ParamChangeRequiresUIRebuild(b.id) && outNeedsMorphRebuild)
          *outNeedsMorphRebuild = true;
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

  // === Phase 3: Parameter Utility Methods ===

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

    // Find and sync any control bound to this parameter
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

    // Force redraw
    graphics->SetAllControlsDirty();
#endif
  }

  // === Phase 3: Centralized OnParamChange Coordinator ===

  void ParameterManager::OnParamChange(int paramIdx, ParameterChangeContext& ctx)
  {
    // Validate context
    if (!ctx.plugin || !ctx.config) return;

    // Handle chunk size parameter
    if (paramIdx == mParamIdxChunkSize)
    {
      // Remember old chunk size so we can roll back if user cancels rechunk
      const int oldChunkSize = ctx.config->chunkSize;

      // Skip triggering rechunk if operation is already in progress (e.g., during rollback)
      if (ctx.brainManager && ctx.brainManager->IsOperationInProgress())
      {
        // Just sync config without triggering operation
        HandleCoreParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config);
        if (ctx.windowCoordinator && ctx.currentTransformer)
          ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.currentTransformer->get());
        if (ctx.setLatency && ctx.computeLatency)
          ctx.setLatency(ctx.computeLatency());
        return;
      }

      bool chunkSizeChanged = HandleChunkSizeChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config,
                                                     ctx.plugin, *ctx.chunker, *ctx.analysisWindow);
      if (ctx.windowCoordinator && ctx.currentTransformer)
        ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.currentTransformer->get());
      if (ctx.setLatency && ctx.computeLatency)
        ctx.setLatency(ctx.computeLatency());

      // Only trigger background rechunk if chunk size actually changed (not just parameter editing)
      if (chunkSizeChanged && ctx.brainManager && ctx.progressOverlayMgr)
      {
        ctx.progressOverlayMgr->Show("Rechunking", "Starting...", 0.0f, true);

        // Capture context for async callback
        auto* plugin = ctx.plugin;
        auto* config = ctx.config;
        auto* chunker = ctx.chunker;
        auto* windowCoordinator = ctx.windowCoordinator;
        auto* currentTransformer = ctx.currentTransformer;
        auto* progressOverlayMgr = ctx.progressOverlayMgr;
        auto setPendingUpdate = ctx.setPendingUpdate;
        auto computeLatency = ctx.computeLatency;
        auto setLatency = ctx.setLatency;

        ctx.brainManager->RechunkAllFilesAsync(
          ctx.config->chunkSize, (int)ctx.plugin->GetSampleRate(),
          [progressOverlayMgr](const std::string& fileName, int current, int total)
          {
            const float p = (total > 0) ? ((float)current / (float)total * 100.0f) : 50.0f;
            char buf[256];
            snprintf(buf, sizeof(buf), "%s (chunk %d/%d)", fileName.c_str(), current, total);
            progressOverlayMgr->Update(buf, p);
          },
          [plugin, config, chunker, windowCoordinator, currentTransformer, progressOverlayMgr,
           setPendingUpdate, computeLatency, setLatency, oldChunkSize, paramIdx](bool wasCancelled)
          {
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
              // Rollback DSP config and dependent state
              config->chunkSize = oldChunkSize;
              chunker->SetChunkSize(oldChunkSize);
              if (windowCoordinator)
              {
                windowCoordinator->UpdateBrainAnalysisWindow(*config);
                windowCoordinator->UpdateChunkerWindowing(*config, currentTransformer->get());
              }
              if (setLatency && computeLatency)
                setLatency(computeLatency());

              // Rollback parameter and UI
              RollbackParameter(plugin, paramIdx, (double)oldChunkSize, "Rechunking");
            }
          }
        );
      }
    }
    // Handle buffer window parameter
    else if (paramIdx == mParamIdxBufferWindow)
    {
      HandleCoreParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config);
      if (ctx.chunker)
        ctx.chunker->SetBufferWindowSize(ctx.config->bufferWindowSize);
    }
    // Handle algorithm change
    else if (paramIdx == mParamIdxAlgorithm)
    {
      // Store new transformer in pending slot for thread-safe swap in ProcessBlock
      if (ctx.pendingTransformer && ctx.brain)
      {
        *ctx.pendingTransformer = HandleAlgorithmChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config,
                                                         ctx.plugin, *ctx.brain, ctx.plugin->GetSampleRate(),
                                                         ctx.plugin->NInChansConnected());
        if (ctx.windowCoordinator)
          ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.pendingTransformer->get());
      }
      // Note: SetLatency will be called in ProcessBlock after swap

      // Schedule transformer parameter UI rebuild (must happen on UI thread)
#if IPLUG_EDITOR
      if (ctx.setPendingUpdate)
        ctx.setPendingUpdate((uint32_t)PendingUpdate::RebuildTransformer);
#endif
    }
    // Handle output window
    else if (paramIdx == mParamIdxOutputWindow)
    {
      // Remember old window mode (if locked, both windows have the same mode)
      const int oldWindowMode = ctx.config->outputWindowMode;

      HandleCoreParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config);
      if (ctx.windowCoordinator && ctx.currentTransformer)
        ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.currentTransformer->get());

      // If window lock is enabled, sync analysis window to match output window
      const bool windowsAreLocked = ctx.plugin->GetParam(::kWindowLock)->Bool();
      if (windowsAreLocked)
      {
        const int outputWindowIdx = ctx.plugin->GetParam(::kOutputWindow)->Int();
        const int analysisWindowIdx = ctx.plugin->GetParam(::kAnalysisWindow)->Int();

        if (outputWindowIdx != analysisWindowIdx)
        {
          if (ctx.windowCoordinator)
          {
            // Sync analysis to output WITHOUT triggering reanalysis yet
            ctx.windowCoordinator->SyncAnalysisToOutputWindow(ctx.plugin, *ctx.config, false);

            // Now trigger reanalysis with proper rollback handling for BOTH windows
            auto* plugin = ctx.plugin;
            auto* config = ctx.config;
            auto* windowCoordinator = ctx.windowCoordinator;
            auto* currentTransformer = ctx.currentTransformer;
            auto setPendingUpdate = ctx.setPendingUpdate;

            ctx.windowCoordinator->TriggerBrainReanalysisAsync(
              (int)ctx.plugin->GetSampleRate(),
              [this, plugin, config, windowCoordinator, currentTransformer, setPendingUpdate,
               oldWindowMode, paramIdx](bool wasCancelled)
              {
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
                  // Rollback BOTH windows since they were locked together
                  config->outputWindowMode = oldWindowMode;
                  config->analysisWindowMode = oldWindowMode;

                  if (windowCoordinator)
                  {
                    windowCoordinator->UpdateBrainAnalysisWindow(*config);
                    if (currentTransformer)
                      windowCoordinator->UpdateChunkerWindowing(*config, currentTransformer->get());
                  }

                  // Set suppress flag to prevent rollback from triggering new reanalysis
                  setPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze);

                  // Rollback analysis window parameter FIRST (so output window sees them as matched)
                  const int oldIdx = oldWindowMode - 1;
                  RollbackParameter(plugin, ::kAnalysisWindow, (double)oldIdx, nullptr);

                  // Then rollback output window parameter (now they're already synced, won't trigger another sync)
                  RollbackParameter(plugin, paramIdx, (double)oldIdx, "Reanalysis (Output Window)");

                  DBGMSG("Reanalysis CANCELLED - rolled back both locked windows to mode %d\n", oldWindowMode);
                }
              });
          }
          if (ctx.setPendingUpdate)
            ctx.setPendingUpdate((uint32_t)PendingUpdate::DSPConfig);
        }
      }
    }
    // Handle analysis window (with background reanalysis)
    else if (paramIdx == mParamIdxAnalysisWindow)
    {
      // Remember old window mode (if locked, both windows have the same mode)
      const int oldWindowMode = ctx.config->analysisWindowMode;

      // Skip triggering reanalysis if operation is already in progress (e.g., during rollback)
      if (ctx.brainManager && ctx.brainManager->IsOperationInProgress())
      {
        // Clear suppress flag even if we're not triggering operation
        if (ctx.checkAndClearPendingUpdate)
          ctx.checkAndClearPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze);

        // Just sync config without triggering operation
        HandleCoreParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config);
        if (ctx.windowCoordinator)
          ctx.windowCoordinator->UpdateBrainAnalysisWindow(*ctx.config);
        return;
      }

      bool analysisWindowChanged = HandleAnalysisWindowChange(paramIdx, ctx.plugin->GetParam(paramIdx),
                                                               *ctx.config, *ctx.analysisWindow, *ctx.brain);

      // If window lock is enabled, sync output window to match analysis window
      const bool windowsAreLocked = ctx.plugin->GetParam(::kWindowLock)->Bool();
      if (windowsAreLocked)
      {
        const int analysisWindowIdx = ctx.plugin->GetParam(::kAnalysisWindow)->Int();
        const int outputWindowIdx = ctx.plugin->GetParam(::kOutputWindow)->Int();

        if (analysisWindowIdx != outputWindowIdx)
        {
          if (ctx.windowCoordinator)
          {
            ctx.windowCoordinator->SyncOutputToAnalysisWindow(ctx.plugin, *ctx.config);
            if (ctx.currentTransformer)
              ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.currentTransformer->get());
          }
        }
      }

      // Only trigger reanalysis if window mode actually changed AND not suppressed
      if (analysisWindowChanged && ctx.checkAndClearPendingUpdate &&
          !ctx.checkAndClearPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze))
      {
        if (ctx.windowCoordinator)
        {
          // Capture for async callback
          auto* plugin = ctx.plugin;
          auto* config = ctx.config;
          auto* windowCoordinator = ctx.windowCoordinator;
          auto* currentTransformer = ctx.currentTransformer;
          auto setPendingUpdate = ctx.setPendingUpdate;

          ctx.windowCoordinator->TriggerBrainReanalysisAsync(
            (int)ctx.plugin->GetSampleRate(),
            [this, plugin, config, windowCoordinator, currentTransformer, setPendingUpdate,
             oldWindowMode, windowsAreLocked, paramIdx](bool wasCancelled)
            {
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
                // Rollback DSP config and dependent state
                config->analysisWindowMode = oldWindowMode;

                // If windows were locked, rollback BOTH windows (they should have same mode)
                if (windowsAreLocked)
                {
                  config->outputWindowMode = oldWindowMode;
                }

                if (windowCoordinator)
                {
                  windowCoordinator->UpdateBrainAnalysisWindow(*config);
                  if (currentTransformer)
                    windowCoordinator->UpdateChunkerWindowing(*config, currentTransformer->get());
                }

                // Set suppress flag to prevent rollback from triggering new reanalysis
                setPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze);

                // Rollback parameter(s) and UI (convert mode 1-4 to enum index 0-3)
                const int oldIdx = oldWindowMode - 1;
                RollbackParameter(plugin, paramIdx, (double)oldIdx, "Reanalysis (Analysis Window)");

                // If windows were locked, also rollback output window
                if (windowsAreLocked)
                {
                  RollbackParameter(plugin, ::kOutputWindow, (double)oldIdx, nullptr);
                }

                DBGMSG("Reanalysis CANCELLED - rolled back %s to mode %d\n",
                       windowsAreLocked ? "both locked windows" : "analysis window", oldWindowMode);
              }
            });
        }
      }
      if (ctx.setPendingUpdate)
        ctx.setPendingUpdate((uint32_t)PendingUpdate::DSPConfig);
    }
    // Handle overlap enable
    else if (paramIdx == mParamIdxEnableOverlap)
    {
      HandleCoreParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), *ctx.config);
      if (ctx.windowCoordinator && ctx.currentTransformer)
        ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.currentTransformer->get());
    }
    // Handle autotune blend
    else if (paramIdx == mParamIdxAutotuneBlend)
    {
      const double blendPercent = ctx.plugin->GetParam(paramIdx)->Value();
      if (ctx.chunker)
        ctx.chunker->GetAutotuneProcessor().SetBlend((float)(blendPercent / 100.0));
    }
    // Handle autotune mode
    else if (paramIdx == mParamIdxAutotuneMode)
    {
      const int mode = ctx.plugin->GetParam(paramIdx)->Int(); // 0 = FFT Peak, 1 = HPS
      if (ctx.chunker)
        ctx.chunker->GetAutotuneProcessor().SetMode(mode == 1);
    }
    // Handle autotune tolerance (octaves)
    else if (paramIdx == mParamIdxAutotuneToleranceOctaves)
    {
      // Convert enum index (0-4) to octave value (1-5)
      const int enumIdx = std::clamp(ctx.plugin->GetParam(paramIdx)->Int(), 0, 4);
      if (ctx.chunker)
        ctx.chunker->GetAutotuneProcessor().SetToleranceOctaves(enumIdx + 1);
    }
    // Handle morph mode change
    else if (paramIdx == mParamIdxMorphMode)
    {
      // Create/reset new IMorph instance and apply bindings
      if (ctx.pendingMorph)
      {
        *ctx.pendingMorph = HandleMorphModeChange(paramIdx, ctx.plugin->GetParam(paramIdx), ctx.plugin,
                                                   ctx.plugin->GetSampleRate(), ctx.config->chunkSize,
                                                   ctx.plugin->NInChansConnected());
      }
      // Note: mChunker.SetMorph will be called in ProcessBlock after swap
      // Schedule morph parameter UI rebuild (must happen on UI thread)
#if IPLUG_EDITOR
      if (ctx.setPendingUpdate)
        ctx.setPendingUpdate((uint32_t)PendingUpdate::RebuildMorph);
#endif
    }
    // Handle window lock toggle
    else if (paramIdx == ::kWindowLock)
    {
      // When lock is toggled ON, sync the clicked control's window to match the other window
      if (ctx.plugin->GetParam(::kWindowLock)->Bool())
      {
        // Determine which window control's lock was clicked
        int clickedWindowParam = synaptic::ui::LockButtonControl::GetLastClickedWindowParam();

        if (ctx.windowCoordinator)
        {
          ctx.windowCoordinator->HandleWindowLockToggle(true, clickedWindowParam, ctx.plugin, *ctx.config);
          if (ctx.currentTransformer)
            ctx.windowCoordinator->UpdateChunkerWindowing(*ctx.config, ctx.currentTransformer->get());
        }
        if (ctx.setPendingUpdate)
          ctx.setPendingUpdate((uint32_t)PendingUpdate::DSPConfig);
      }
      // When unlocked, no automatic syncing happens - windows can diverge
    }
    // Handle dynamic parameters
    else
    {
      bool needsTransformerRebuild = false;
      bool needsMorphRebuild = false;
      IChunkBufferTransformer* transformer = ctx.currentTransformer ? ctx.currentTransformer->get() : nullptr;
      IMorph* morph = ctx.currentMorph ? ctx.currentMorph->get() : nullptr;

      if (HandleDynamicParameterChange(paramIdx, ctx.plugin->GetParam(paramIdx), transformer, morph,
                                        &needsTransformerRebuild, &needsMorphRebuild))
      {
        // Parameter was handled by ParameterManager
        // Check if UI rebuild is needed
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

    // For all parameters (including kAGC, kInGain, kOutGain, and any others),
    // the base Plugin class will notify parameter-bound controls automatically.
    // By not returning early, we let the default notification mechanism work.
  }
}

