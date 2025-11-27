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
  namespace ui {
    class ProgressOverlayManager;
  }

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
   * @brief Context for parameter change coordination
   *
   * Bundles all dependencies needed to handle parameter changes.
   * Uses DSPContext reference instead of raw pointers to shared_ptrs.
   */
  struct ParameterChangeContext
  {
    // Core plugin reference
    iplug::Plugin* plugin = nullptr;

    // Configuration
    DSPConfig* config = nullptr;

    // DSP context (owns transformer/morph instances)
    DSPContext* dspContext = nullptr;

    // Individual DSP components
    AudioStreamChunker* chunker = nullptr;
    Brain* brain = nullptr;
    Window* analysisWindow = nullptr;

    // Coordinators/Managers
    WindowCoordinator* windowCoordinator = nullptr;
    BrainManager* brainManager = nullptr;
    ui::ProgressOverlayManager* progressOverlayMgr = nullptr;

    // Callbacks for plugin state management
    std::function<void(uint32_t)> setPendingUpdate;
    std::function<bool(uint32_t)> checkAndClearPendingUpdate;
    std::function<int()> computeLatency;
    std::function<void(int)> setLatency;
  };

  /**
   * @brief Manages all plugin parameters
   *
   * Handles initialization of core DSP parameters and dynamic transformer parameters,
   * maintains bindings between IParams and transformers, and routes parameter changes.
   */
  class ParameterManager
  {
  public:
    ParameterManager();

    /**
     * @brief Calculate total number of parameters including dynamic ones
     */
    static int GetTotalParams();

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
     * This unified method replaces the separate ApplyBindingsToTransformer and
     * ApplyBindingsToOwners methods.
     *
     * @param plugin Plugin instance to read parameter values from
     * @param transformer Transformer to apply values to (can be nullptr)
     * @param morph Morph to apply values to (can be nullptr)
     */
    void ApplyBindingsTo(iplug::Plugin* plugin, IChunkBufferTransformer* transformer, IMorph* morph);

    // Legacy wrappers for backward compatibility
    void ApplyBindingsToTransformer(iplug::Plugin* plugin, IChunkBufferTransformer* transformer)
    {
      ApplyBindingsTo(plugin, transformer, nullptr);
    }

    void ApplyBindingsToOwners(iplug::Plugin* plugin, IChunkBufferTransformer* transformer, IMorph* morph)
    {
      ApplyBindingsTo(plugin, transformer, morph);
    }

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

    // === Main Entry Point for Parameter Changes ===

    /**
     * @brief Handle ALL parameter changes with centralized coordination
     *
     * Routes to specific handlers based on parameter type and coordinates
     * all necessary side effects.
     */
    void OnParamChange(int paramIdx, ParameterChangeContext& ctx);

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

    void HandleChunkSizeParam(int paramIdx, ParameterChangeContext& ctx);
    void HandleBufferWindowParam(int paramIdx, ParameterChangeContext& ctx);
    void HandleAlgorithmParam(int paramIdx, ParameterChangeContext& ctx);
    void HandleOutputWindowParam(int paramIdx, ParameterChangeContext& ctx);
    void HandleAnalysisWindowParam(int paramIdx, ParameterChangeContext& ctx);
    void HandleEnableOverlapParam(int paramIdx, ParameterChangeContext& ctx);
    void HandleAutotuneBlendParam(int paramIdx, ParameterChangeContext& ctx);
    void HandleAutotuneModeParam(int paramIdx, ParameterChangeContext& ctx);
    void HandleAutotuneToleranceParam(int paramIdx, ParameterChangeContext& ctx);
    void HandleMorphModeParam(int paramIdx, ParameterChangeContext& ctx);
    void HandleWindowLockParam(int paramIdx, ParameterChangeContext& ctx);
    void HandleDynamicParam(int paramIdx, ParameterChangeContext& ctx);

    // === Internal Helpers ===

    void ApplyBindingValue(const TransformerParamBinding& binding, iplug::IParam* param,
                          IChunkBufferTransformer* transformer, IMorph* morph);

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

    int mTransformerParamBase = -1;
  };
}
