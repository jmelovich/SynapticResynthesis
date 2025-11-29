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
#include "plugin_src/ui/core/UIConstants.h"
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
  // Construction and Context Setup
  // ============================================================================

  ParameterManager::ParameterManager() {}

  void ParameterManager::SetContext(
    iplug::Plugin* plugin,
    DSPConfig* config,
    DSPContext* dspContext,
    Brain* brain,
    Window* analysisWindow,
    WindowCoordinator* windowCoordinator,
    BrainManager* brainManager,
    UISyncManager* uiSyncManager)
  {
    mPlugin = plugin;
    mConfig = config;
    mDSPContext = dspContext;
    mBrain = brain;
    mAnalysisWindow = analysisWindow;
    mWindowCoordinator = windowCoordinator;
    mBrainManager = brainManager;
    mUISyncManager = uiSyncManager;
  }

  int ParameterManager::GetTotalParams()
  {
    std::vector<ExposedParamDesc> unionDescs;
    BuildTransformerUnion(unionDescs);
    return kNumParams + (int)unionDescs.size();
  }

  // ============================================================================
  // Context Helper Methods
  // ============================================================================

  AudioStreamChunker* ParameterManager::GetChunker() const
  {
    return mDSPContext ? &mDSPContext->GetChunker() : nullptr;
  }

  int ParameterManager::ComputeLatency() const
  {
    if (!mDSPContext || !mConfig) return 0;
    return mDSPContext->ComputeLatencySamples(mConfig->chunkSize, mConfig->bufferWindowSize);
  }

  void ParameterManager::SetLatency(int latency)
  {
    if (mPlugin) mPlugin->SetLatency(latency);
  }

  void ParameterManager::SetPendingUpdate(uint32_t flag)
  {
    if (mUISyncManager)
      mUISyncManager->SetPendingUpdate(static_cast<PendingUpdate>(flag));
  }

  bool ParameterManager::CheckAndClearPendingUpdate(uint32_t flag)
  {
    if (mUISyncManager)
      return mUISyncManager->CheckAndClearPendingUpdate(static_cast<PendingUpdate>(flag));
    return false;
  }

  // ============================================================================
  // Initialization
  // ============================================================================

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

    // WindowLock is initialized in the main plugin constructor, but we track the index here
    mParamIdxWindowLock = kWindowLock;
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

  void ParameterManager::OnParamChange(int paramIdx)
  {
    if (!mPlugin || !mConfig) return;

    // Route to specific handler based on parameter type
    if (paramIdx == mParamIdxChunkSize)
      HandleChunkSizeParam(paramIdx);
    else if (paramIdx == mParamIdxBufferWindow)
      HandleBufferWindowParam(paramIdx);
    else if (paramIdx == mParamIdxAlgorithm)
      HandleAlgorithmParam(paramIdx);
    else if (paramIdx == mParamIdxOutputWindow)
      HandleOutputWindowParam(paramIdx);
    else if (paramIdx == mParamIdxAnalysisWindow)
      HandleAnalysisWindowParam(paramIdx);
    else if (paramIdx == mParamIdxEnableOverlap)
      HandleEnableOverlapParam(paramIdx);
    else if (paramIdx == mParamIdxAutotuneBlend)
      HandleAutotuneBlendParam(paramIdx);
    else if (paramIdx == mParamIdxAutotuneMode)
      HandleAutotuneModeParam(paramIdx);
    else if (paramIdx == mParamIdxAutotuneToleranceOctaves)
      HandleAutotuneToleranceParam(paramIdx);
    else if (paramIdx == mParamIdxMorphMode)
      HandleMorphModeParam(paramIdx);
    else if (paramIdx == mParamIdxWindowLock)
      HandleWindowLockParam(paramIdx);
    else
      HandleDynamicParam(paramIdx);
  }

  // ============================================================================
  // Per-Parameter Handlers
  // ============================================================================

  void ParameterManager::HandleChunkSizeParam(int paramIdx)
  {
    const int oldChunkSize = mConfig->chunkSize;
    auto* chunker = GetChunker();

    if (mBrainManager && mBrainManager->IsOperationInProgress())
    {
      HandleCoreParameterChange(paramIdx, mPlugin->GetParam(paramIdx), *mConfig);
      if (mWindowCoordinator && mDSPContext)
        mWindowCoordinator->UpdateChunkerWindowing(*mConfig, mDSPContext->GetTransformerRaw());
      SetLatency(ComputeLatency());
      return;
    }

    bool chunkSizeChanged = chunker && mAnalysisWindow
      ? HandleChunkSizeChange(paramIdx, mPlugin->GetParam(paramIdx), *mConfig, mPlugin, *chunker, *mAnalysisWindow)
      : false;

    if (mWindowCoordinator && mDSPContext)
      mWindowCoordinator->UpdateChunkerWindowing(*mConfig, mDSPContext->GetTransformerRaw());
    SetLatency(ComputeLatency());

    if (chunkSizeChanged && mBrainManager)
    {
      // Capture overlay manager at operation start for multi-instance safety
      auto* overlayMgr = ui::ProgressOverlayManager::Get();
      if (overlayMgr)
        overlayMgr->Show("Rechunking", "Starting...", 0.0f, true);

      // Capture what we need for the async callback
      auto* plugin = mPlugin;
      auto* config = mConfig;
      auto* windowCoordinator = mWindowCoordinator;
      auto* dspContext = mDSPContext;
      ParameterManager* self = this;

      mBrainManager->RechunkAllFilesAsync(
        mConfig->chunkSize, (int)mPlugin->GetSampleRate(),
        [overlayMgr](const std::string& fileName, int current, int total) {
          if (overlayMgr)
          {
            const float p = (total > 0)
              ? ((float)current / (float)total * ui::Progress::kMaxProgress)
              : ui::Progress::kDefaultProgress;
            char buf[256];
            snprintf(buf, sizeof(buf), "%s (chunk %d/%d)", fileName.c_str(), current, total);
            overlayMgr->Update(buf, p);
          }
        },
        [self, plugin, config, chunker, windowCoordinator, dspContext,
         oldChunkSize, paramIdx, overlayMgr](bool wasCancelled) {
          if (overlayMgr)
            overlayMgr->Hide();
          if (!wasCancelled)
          {
            self->SetPendingUpdate((uint32_t)PendingUpdate::BrainSummary);
            self->SetPendingUpdate((uint32_t)PendingUpdate::MarkDirty);
          }
          else
          {
            config->chunkSize = oldChunkSize;
            if (chunker) chunker->SetChunkSize(oldChunkSize);
            if (windowCoordinator)
            {
              windowCoordinator->UpdateBrainAnalysisWindow(*config);
              if (dspContext)
                windowCoordinator->UpdateChunkerWindowing(*config, dspContext->GetTransformerRaw());
            }
            self->SetLatency(self->ComputeLatency());
            RollbackParameter(plugin, paramIdx, (double)oldChunkSize, "Rechunking");
          }
        }
      );
    }
  }

  void ParameterManager::HandleBufferWindowParam(int paramIdx)
  {
    HandleCoreParameterChange(paramIdx, mPlugin->GetParam(paramIdx), *mConfig);
    if (auto* chunker = GetChunker())
      chunker->SetBufferWindowSize(mConfig->bufferWindowSize);
  }

  void ParameterManager::HandleAlgorithmParam(int paramIdx)
  {
    if (mDSPContext && mBrain)
    {
      auto newTransformer = HandleAlgorithmChange(paramIdx, mPlugin->GetParam(paramIdx), *mConfig,
                                                   mPlugin, *mBrain, mPlugin->GetSampleRate(),
                                                   mPlugin->NInChansConnected());
      mDSPContext->SetPendingTransformer(newTransformer);

      if (mWindowCoordinator)
        mWindowCoordinator->UpdateChunkerWindowing(*mConfig, newTransformer.get());
    }

#if IPLUG_EDITOR
    SetPendingUpdate((uint32_t)PendingUpdate::RebuildTransformer);
#endif
  }

  void ParameterManager::HandleOutputWindowParam(int paramIdx)
  {
    const int oldWindowMode = mConfig->outputWindowMode;

    HandleCoreParameterChange(paramIdx, mPlugin->GetParam(paramIdx), *mConfig);
    if (mWindowCoordinator && mDSPContext)
      mWindowCoordinator->UpdateChunkerWindowing(*mConfig, mDSPContext->GetTransformerRaw());

    const bool windowsAreLocked = mPlugin->GetParam(mParamIdxWindowLock)->Bool();
    if (windowsAreLocked)
    {
      const int outputWindowIdx = mPlugin->GetParam(kOutputWindow)->Int();
      const int analysisWindowIdx = mPlugin->GetParam(kAnalysisWindow)->Int();

      if (outputWindowIdx != analysisWindowIdx && mWindowCoordinator)
      {
        mWindowCoordinator->SyncAnalysisToOutputWindow(mPlugin, *mConfig, false);

        auto* config = mConfig;
        auto* windowCoordinator = mWindowCoordinator;
        auto* dspContext = mDSPContext;
        auto* plugin = mPlugin;
        ParameterManager* self = this;

        mWindowCoordinator->TriggerBrainReanalysisAsync(
          (int)mPlugin->GetSampleRate(),
          [self, plugin, config, windowCoordinator, dspContext,
           oldWindowMode, paramIdx](bool wasCancelled) {
            if (!wasCancelled)
            {
              self->SetPendingUpdate((uint32_t)PendingUpdate::BrainSummary);
              self->SetPendingUpdate((uint32_t)PendingUpdate::MarkDirty);
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

              self->SetPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze);

              const int oldIdx = WindowMode::ConfigToParam(oldWindowMode);
              RollbackParameter(plugin, kAnalysisWindow, (double)oldIdx, nullptr);
              RollbackParameter(plugin, paramIdx, (double)oldIdx, "Reanalysis (Output Window)");

              DBGMSG("Reanalysis CANCELLED - rolled back both locked windows to mode %d\n", oldWindowMode);
            }
          });

        SetPendingUpdate((uint32_t)PendingUpdate::DSPConfig);
      }
    }
  }

  void ParameterManager::HandleAnalysisWindowParam(int paramIdx)
  {
    const int oldWindowMode = mConfig->analysisWindowMode;

    if (mBrainManager && mBrainManager->IsOperationInProgress())
    {
      CheckAndClearPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze);
      HandleCoreParameterChange(paramIdx, mPlugin->GetParam(paramIdx), *mConfig);
      if (mWindowCoordinator)
        mWindowCoordinator->UpdateBrainAnalysisWindow(*mConfig);
      return;
    }

    bool analysisWindowChanged = (mAnalysisWindow && mBrain)
      ? HandleAnalysisWindowChange(paramIdx, mPlugin->GetParam(paramIdx), *mConfig, *mAnalysisWindow, *mBrain)
      : false;

    const bool windowsAreLocked = mPlugin->GetParam(mParamIdxWindowLock)->Bool();
    if (windowsAreLocked)
    {
      const int analysisWindowIdx = mPlugin->GetParam(kAnalysisWindow)->Int();
      const int outputWindowIdx = mPlugin->GetParam(kOutputWindow)->Int();

      if (analysisWindowIdx != outputWindowIdx && mWindowCoordinator)
      {
        mWindowCoordinator->SyncOutputToAnalysisWindow(mPlugin, *mConfig);
        if (mDSPContext)
          mWindowCoordinator->UpdateChunkerWindowing(*mConfig, mDSPContext->GetTransformerRaw());
      }
    }

    if (analysisWindowChanged && !CheckAndClearPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze))
    {
      if (mWindowCoordinator)
      {
        auto* config = mConfig;
        auto* windowCoordinator = mWindowCoordinator;
        auto* dspContext = mDSPContext;
        auto* plugin = mPlugin;
        ParameterManager* self = this;

        mWindowCoordinator->TriggerBrainReanalysisAsync(
          (int)mPlugin->GetSampleRate(),
          [self, plugin, config, windowCoordinator, dspContext,
           oldWindowMode, windowsAreLocked, paramIdx](bool wasCancelled) {
            if (!wasCancelled)
            {
              self->SetPendingUpdate((uint32_t)PendingUpdate::BrainSummary);
              self->SetPendingUpdate((uint32_t)PendingUpdate::MarkDirty);
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

              self->SetPendingUpdate((uint32_t)PendingUpdate::SuppressAnalysisReanalyze);

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

    SetPendingUpdate((uint32_t)PendingUpdate::DSPConfig);
  }

  void ParameterManager::HandleEnableOverlapParam(int paramIdx)
  {
    HandleCoreParameterChange(paramIdx, mPlugin->GetParam(paramIdx), *mConfig);
    if (mWindowCoordinator && mDSPContext)
      mWindowCoordinator->UpdateChunkerWindowing(*mConfig, mDSPContext->GetTransformerRaw());
  }

  void ParameterManager::HandleAutotuneBlendParam(int paramIdx)
  {
    const double blendPercent = mPlugin->GetParam(paramIdx)->Value();
    if (auto* chunker = GetChunker())
      chunker->GetAutotuneProcessor().SetBlend((float)(blendPercent / 100.0));
  }

  void ParameterManager::HandleAutotuneModeParam(int paramIdx)
  {
    const int mode = mPlugin->GetParam(paramIdx)->Int();
    if (auto* chunker = GetChunker())
      chunker->GetAutotuneProcessor().SetMode(mode == 1);
  }

  void ParameterManager::HandleAutotuneToleranceParam(int paramIdx)
  {
    const int enumIdx = std::clamp(mPlugin->GetParam(paramIdx)->Int(), 0, 4);
    if (auto* chunker = GetChunker())
      chunker->GetAutotuneProcessor().SetToleranceOctaves(enumIdx + 1);
  }

  void ParameterManager::HandleMorphModeParam(int paramIdx)
  {
    if (mDSPContext)
    {
      auto newMorph = HandleMorphModeChange(paramIdx, mPlugin->GetParam(paramIdx), mPlugin,
                                             mPlugin->GetSampleRate(), mConfig->chunkSize,
                                             mPlugin->NInChansConnected());
      mDSPContext->SetPendingMorph(newMorph);
    }

#if IPLUG_EDITOR
    SetPendingUpdate((uint32_t)PendingUpdate::RebuildMorph);
#endif
  }

  void ParameterManager::HandleWindowLockParam(int paramIdx)
  {
    if (mPlugin->GetParam(mParamIdxWindowLock)->Bool())
    {
#if IPLUG_EDITOR
      int clickedWindowParam = synaptic::ui::LockButtonControl::GetLastClickedWindowParam();
#else
      int clickedWindowParam = kOutputWindow;
#endif

      if (mWindowCoordinator)
      {
        mWindowCoordinator->HandleWindowLockToggle(true, clickedWindowParam, mPlugin, *mConfig);
        if (mDSPContext)
          mWindowCoordinator->UpdateChunkerWindowing(*mConfig, mDSPContext->GetTransformerRaw());
      }
      SetPendingUpdate((uint32_t)PendingUpdate::DSPConfig);
    }
  }

  void ParameterManager::HandleDynamicParam(int paramIdx)
  {
    bool needsTransformerRebuild = false;
    bool needsMorphRebuild = false;

    IChunkBufferTransformer* transformer = mDSPContext ? mDSPContext->GetTransformerRaw() : nullptr;
    IMorph* morph = mDSPContext ? mDSPContext->GetMorphRaw() : nullptr;

    if (HandleDynamicParameterChange(paramIdx, mPlugin->GetParam(paramIdx), transformer, morph,
                                      &needsTransformerRebuild, &needsMorphRebuild))
    {
#if IPLUG_EDITOR
      if (needsTransformerRebuild)
        SetPendingUpdate((uint32_t)PendingUpdate::RebuildTransformer);
      if (needsMorphRebuild)
        SetPendingUpdate((uint32_t)PendingUpdate::RebuildMorph);
#endif
    }
  }

}
