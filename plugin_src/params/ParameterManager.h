/**
 * @file ParameterManager.h
 * @brief Manages all plugin parameters including dynamic transformer/morph parameters
 *
 * Responsibilities:
 * - Initialize core DSP parameters and dynamic transformer parameters
 * - Maintain bindings between IParams and transformer/morph instances
 * - Route parameter changes to appropriate handlers
 * - Provide parameter utility methods (set from UI, rollback, sync controls)
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "plugin_src/params/DynamicParamSchema.h"
#include "plugin_src/params/ParameterIds.h"
#include "plugin_src/transformers/BaseTransformer.h"
#include "plugin_src/transformers/types/ExpandedSimpleSampleBrainTransformer.h"
#include "plugin_src/transformers/TransformerFactory.h"
#include "plugin_src/morph/IMorph.h"
#include "plugin_src/morph/MorphFactory.h"
#include "plugin_src/modules/DSPConfig.h"
#include "plugin_src/modules/WindowModeHelpers.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>

namespace synaptic
{
  // Forward declarations
  class AudioStreamChunker;
  class Brain;
  class Window;
  class BrainManager;
  class WindowCoordinator;
  class DSPContext;
  class UISyncManager;

  /**
   * @brief Binding between IParam and transformer/morph parameter
   */
  struct TransformerParamBinding
  {
    std::string id;
    synaptic::ParamType type;
    int paramIdx = -1;
    std::vector<std::string> enumValues; // For enums: index -> string value mapping
  };

  /**
   * @brief Manages all plugin parameters
   *
   * Stores references to all plugin components it needs, set once via SetContext().
   * This eliminates the need to pass a large context struct on every parameter change.
   */
  class ParameterManager
  {
  public:
    ParameterManager();

    /**
     * @brief Calculate total number of parameters including dynamic ones
     */
    static int GetTotalParams();

    // === Context Setup (call once after construction) ===

    /**
     * @brief Set all component references needed for parameter handling
     *
     * Call this once after all components are constructed.
     * Must be called before any parameter changes occur.
     */
    void SetContext(
      iplug::Plugin* plugin,
      DSPConfig* config,
      DSPContext* dspContext,
      Brain* brain,
      Window* analysisWindow,
      WindowCoordinator* windowCoordinator,
      BrainManager* brainManager,
      UISyncManager* uiSyncManager
    );

    // === Initialization ===

    void InitializeCoreParameters(iplug::Plugin* plugin, const DSPConfig& config);
    void InitializeTransformerParameters(iplug::Plugin* plugin);

    // === Core Parameter Handlers ===

    bool HandleCoreParameterChange(int paramIdx, iplug::IParam* param, DSPConfig& config);

    // === Coordinated Parameter Change Handlers ===

    template<typename PluginT>
    bool HandleChunkSizeChange(int paramIdx, iplug::IParam* param, DSPConfig& config,
                               PluginT* plugin, synaptic::AudioStreamChunker& chunker,
                               synaptic::Window& analysisWindow)
    {
      int oldChunkSize = config.chunkSize;
      HandleCoreParameterChange(paramIdx, param, config);
      chunker.SetChunkSize(config.chunkSize);
      analysisWindow.Set(synaptic::Window::IntToType(config.analysisWindowMode), config.chunkSize);
      return config.chunkSize != oldChunkSize;
    }

    template<typename PluginT>
    std::shared_ptr<IChunkBufferTransformer> HandleAlgorithmChange(
      int paramIdx, iplug::IParam* param, DSPConfig& config,
      PluginT* plugin, synaptic::Brain& brain, double sampleRate, int channels)
    {
      HandleCoreParameterChange(paramIdx, param, config);

      auto newTransformer = synaptic::TransformerFactory::CreateByUiIndex(config.algorithmId);
      if (!newTransformer)
      {
        config.algorithmId = 0;
        newTransformer = synaptic::TransformerFactory::CreateByUiIndex(config.algorithmId);
      }

      if (auto sb = dynamic_cast<synaptic::BaseSampleBrainTransformer*>(newTransformer.get()))
        sb->SetBrain(&brain);

      if (newTransformer)
        newTransformer->OnReset(sampleRate, config.chunkSize, config.bufferWindowSize, channels);

      ApplyBindingsTo(plugin, newTransformer.get(), nullptr);

      return newTransformer;
    }

    template<typename PluginT>
    std::shared_ptr<IMorph> HandleMorphModeChange(
      int paramIdx, iplug::IParam* param,
      PluginT* plugin, double sampleRate, int fftSize, int channels)
    {
      (void)paramIdx;
      const int modeIdx = param->Int();
      auto newMorph = synaptic::MorphFactory::CreateByUiIndex(modeIdx);
      if (newMorph)
        newMorph->OnReset(sampleRate, fftSize, channels);
      ApplyBindingsTo(plugin, nullptr, newMorph.get());
      return newMorph;
    }

    bool HandleAnalysisWindowChange(int paramIdx, iplug::IParam* param, DSPConfig& config,
                                    synaptic::Window& analysisWindow, synaptic::Brain& brain)
    {
      int oldAnalysisWindowMode = config.analysisWindowMode;
      HandleCoreParameterChange(paramIdx, param, config);
      analysisWindow.Set(synaptic::Window::IntToType(config.analysisWindowMode), config.chunkSize);
      brain.SetWindow(&analysisWindow);
      return config.analysisWindowMode != oldAnalysisWindowMode;
    }

    // === Dynamic Parameter Handlers ===

    bool HandleTransformerParameterChange(int paramIdx, iplug::IParam* param,
                                          IChunkBufferTransformer* transformer);

    bool HandleDynamicParameterChange(int paramIdx, iplug::IParam* param,
                                      IChunkBufferTransformer* transformer,
                                      IMorph* morph,
                                      bool* outNeedsTransformerRebuild = nullptr,
                                      bool* outNeedsMorphRebuild = nullptr);

    // === Unified Binding Application ===

    /**
     * @brief Apply all current parameter bindings to transformer and/or morph
     *
     * @param plugin Plugin instance to read parameter values from
     * @param transformer Transformer to apply values to (can be nullptr)
     * @param morph Morph to apply values to (can be nullptr)
     */
    void ApplyBindingsTo(iplug::Plugin* plugin, IChunkBufferTransformer* transformer, IMorph* morph);

    // === Query Methods ===

    bool IsCoreParameter(int paramIdx) const;
    bool IsTransformerParameter(int paramIdx) const;
    const TransformerParamBinding* GetBindingForParam(int paramIdx) const;
    const std::vector<TransformerParamBinding>& GetBindings() const { return mBindings; }

    // === Parameter Index Accessors ===

    int GetChunkSizeParamIdx() const { return mParamIdxChunkSize; }
    int GetBufferWindowParamIdx() const { return mParamIdxBufferWindow; }
    int GetOutputWindowParamIdx() const { return mParamIdxOutputWindow; }
    int GetAnalysisWindowParamIdx() const { return mParamIdxAnalysisWindow; }
    int GetAlgorithmParamIdx() const { return mParamIdxAlgorithm; }
    int GetDirtyFlagParamIdx() const { return mParamIdxDirtyFlag; }
    int GetEnableOverlapParamIdx() const { return mParamIdxEnableOverlap; }
    int GetAutotuneBlendParamIdx() const { return mParamIdxAutotuneBlend; }
    int GetAutotuneModeParamIdx() const { return mParamIdxAutotuneMode; }
    int GetAutotuneToleranceOctavesParamIdx() const { return mParamIdxAutotuneToleranceOctaves; }
    int GetMorphModeParamIdx() const { return mParamIdxMorphMode; }
    int GetWindowLockParamIdx() const { return mParamIdxWindowLock; }

    // === Main Entry Point for Parameter Changes ===

    /**
     * @brief Handle ALL parameter changes with centralized coordination
     *
     * Routes to specific handlers based on parameter type and coordinates
     * all necessary side effects. Uses stored context references.
     *
     * @param paramIdx Parameter index that changed
     */
    void OnParamChange(int paramIdx);

    // === Parameter Utility Methods ===

    /**
     * @brief Set parameter from UI and inform host
     * @param plugin Plugin instance
     * @param paramIdx Parameter index to update
     * @param value New parameter value (unnormalized)
     */
    static void SetParameterFromUI(iplug::Plugin* plugin, int paramIdx, double value);

    /**
     * @brief Rollback parameter to old value after cancelled operation
     * @param plugin Plugin instance
     * @param paramIdx Parameter index to rollback
     * @param oldValue Old parameter value to restore
     * @param debugName Operation name for debug logging (can be nullptr)
     */
    static void RollbackParameter(iplug::Plugin* plugin, int paramIdx, double oldValue, const char* debugName);

    /**
     * @brief Sync UI control to match parameter value
     * @param plugin Plugin instance
     * @param paramIdx Parameter index to sync
     */
    static void SyncControlToParameter(iplug::Plugin* plugin, int paramIdx);

  private:
    // === Per-Parameter Handlers ===
    // These are called by OnParamChange to handle specific parameter types

    void HandleChunkSizeParam(int paramIdx);
    void HandleBufferWindowParam(int paramIdx);
    void HandleAlgorithmParam(int paramIdx);
    void HandleOutputWindowParam(int paramIdx);
    void HandleAnalysisWindowParam(int paramIdx);
    void HandleEnableOverlapParam(int paramIdx);
    void HandleAutotuneBlendParam(int paramIdx);
    void HandleAutotuneModeParam(int paramIdx);
    void HandleAutotuneToleranceParam(int paramIdx);
    void HandleMorphModeParam(int paramIdx);
    void HandleWindowLockParam(int paramIdx);
    void HandleDynamicParam(int paramIdx);

    // === Internal Helpers ===

    void ApplyBindingValue(const TransformerParamBinding& binding, iplug::IParam* param,
                          IChunkBufferTransformer* transformer, IMorph* morph);
    
    AudioStreamChunker* GetChunker() const;
    int ComputeLatency() const;
    void SetLatency(int latency);
    void SetPendingUpdate(uint32_t flag);
    bool CheckAndClearPendingUpdate(uint32_t flag);

    // === Stored Context (set once via SetContext) ===
    iplug::Plugin* mPlugin = nullptr;
    DSPConfig* mConfig = nullptr;
    DSPContext* mDSPContext = nullptr;
    Brain* mBrain = nullptr;
    Window* mAnalysisWindow = nullptr;
    WindowCoordinator* mWindowCoordinator = nullptr;
    BrainManager* mBrainManager = nullptr;
    UISyncManager* mUISyncManager = nullptr;

    // Bindings
    std::vector<TransformerParamBinding> mBindings;

    // Core parameter indices
    int mParamIdxChunkSize = -1;
    int mParamIdxBufferWindow = -1;
    int mParamIdxOutputWindow = -1;
    int mParamIdxAnalysisWindow = -1;
    int mParamIdxAlgorithm = -1;
    int mParamIdxDirtyFlag = -1;
    int mParamIdxEnableOverlap = -1;
    int mParamIdxAutotuneBlend = -1;
    int mParamIdxAutotuneMode = -1;
    int mParamIdxAutotuneToleranceOctaves = -1;
    int mParamIdxMorphMode = -1;
    int mParamIdxWindowLock = -1;

    int mTransformerParamBase = -1;
  };
}
