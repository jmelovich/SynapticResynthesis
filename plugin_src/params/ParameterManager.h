#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "plugin_src/params/DynamicParamSchema.h"
#include "plugin_src/transformers/BaseTransformer.h"
#include "plugin_src/transformers/types/ExpandedSimpleSampleBrainTransformer.h"
#include "plugin_src/transformers/TransformerFactory.h"
#include "plugin_src/morph/IMorph.h"
#include "plugin_src/morph/MorphFactory.h"
#include "plugin_src/modules/DSPConfig.h"
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
  namespace ui {
    class ProgressOverlayManager;
  }
  /**
   * @brief Binding between IParam and transformer parameter
   */
  struct TransformerParamBinding
  {
    std::string id;
    synaptic::ParamType type;
    int paramIdx = -1;
    // For enums, map index<->string value
    std::vector<std::string> enumValues; // order corresponds to indices 0..N-1
  };

  /**
   * @brief Context for parameter change coordination
   * 
   * Bundles all dependencies needed to handle parameter changes.
   * Reduces parameter passing and makes the API cleaner.
   */
  struct ParameterChangeContext
  {
    // Core plugin reference
    iplug::Plugin* plugin = nullptr;
    
    // Configuration
    DSPConfig* config = nullptr;
    
    // DSP components
    AudioStreamChunker* chunker = nullptr;
    Brain* brain = nullptr;
    Window* analysisWindow = nullptr;
    
    // Transformers/Morphs (use shared_ptr to keep alive during async operations)
    std::shared_ptr<IChunkBufferTransformer>* currentTransformer = nullptr;
    std::shared_ptr<IChunkBufferTransformer>* pendingTransformer = nullptr;
    std::shared_ptr<IMorph>* currentMorph = nullptr;
    std::shared_ptr<IMorph>* pendingMorph = nullptr;
    
    // Coordinators/Managers
    WindowCoordinator* windowCoordinator = nullptr;
    BrainManager* brainManager = nullptr;
    ui::ProgressOverlayManager* progressOverlayMgr = nullptr;
    
    // Callbacks for plugin state management
    std::function<void(uint32_t)> setPendingUpdate;  // Set pending update flags
    std::function<bool(uint32_t)> checkAndClearPendingUpdate;  // Check and clear flags
    std::function<int()> computeLatency;  // Compute current latency
    std::function<void(int)> setLatency;  // Update plugin latency
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

    // === Initialization (called from plugin constructor) ===

    /**
     * @brief Initialize core DSP parameters (chunk size, algorithm, windows, etc.)
     * @param plugin Plugin instance to add parameters to
     * @param config DSPConfig with default values
     */
    void InitializeCoreParameters(iplug::Plugin* plugin, const DSPConfig& config);

    /**
     * @brief Initialize transformer parameters (dynamic union across all transformers)
     * @param plugin Plugin instance to add parameters to
     */
    void InitializeTransformerParameters(iplug::Plugin* plugin);

    // === Parameter Change Handlers ===

    /**
     * @brief Handle core parameter change
     * @param paramIdx Parameter index that changed
     * @param param The parameter object
     * @param config DSPConfig to update
     * @return true if this was a core parameter
     */
    bool HandleCoreParameterChange(int paramIdx, iplug::IParam* param, DSPConfig& config);

    // === Coordinated Parameter Change Handlers (with side effects) ===
    // These methods handle parameter changes AND coordinate updates to DSP components

    /**
     * @brief Handle chunk size parameter change with side effects
     * Coordinates: config update, chunker resize, window resize, latency update
     * @return true if chunk size changed (triggers rechunking)
     */
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

    /**
     * @brief Handle algorithm parameter change with side effects
     * Coordinates: config update, transformer creation, brain wiring, reset, binding application, latency
     */
    template<typename PluginT>
    std::shared_ptr<IChunkBufferTransformer> HandleAlgorithmChange(
      int paramIdx, iplug::IParam* param, DSPConfig& config,
      PluginT* plugin, synaptic::Brain& brain, double sampleRate, int channels)
    {
      HandleCoreParameterChange(paramIdx, param, config);

      // Create new transformer
      auto newTransformer = synaptic::TransformerFactory::CreateByUiIndex(config.algorithmId);
      if (!newTransformer)
      {
        config.algorithmId = 0;
        newTransformer = synaptic::TransformerFactory::CreateByUiIndex(config.algorithmId);
      }

      // Wire brain if needed
      if (auto sb = dynamic_cast<synaptic::BaseSampleBrainTransformer*>(newTransformer.get()))
        sb->SetBrain(&brain);

      // Reset transformer
      if (newTransformer)
        newTransformer->OnReset(sampleRate, config.chunkSize, config.bufferWindowSize, channels);

      // Apply bindings
      ApplyBindingsToTransformer(plugin, newTransformer.get());

      return newTransformer;
    }

    // Create/reset morph on morph mode change; apply bindings
    template<typename PluginT>
    std::shared_ptr<IMorph> HandleMorphModeChange(
      int paramIdx, iplug::IParam* param,
      PluginT* plugin, double sampleRate, int fftSize, int channels)
    {
      (void) paramIdx;
      (void) param;
      // Create new morph by UI index stored in param
      const int modeIdx = param->Int();
      auto newMorph = synaptic::MorphFactory::CreateByUiIndex(modeIdx);
      if (newMorph)
        newMorph->OnReset(sampleRate, fftSize, channels);
      // Apply current bindings to morph (and transformer if caller passes it)
      ApplyBindingsToOwners(plugin, nullptr, newMorph.get());
      return newMorph;
    }

    /**
     * @brief Handle analysis window parameter change with side effects
     * Returns true if reanalysis should be triggered
     */
    bool HandleAnalysisWindowChange(int paramIdx, iplug::IParam* param, DSPConfig& config,
                                   synaptic::Window& analysisWindow, synaptic::Brain& brain)
    {
      int oldAnalysisWindowMode = config.analysisWindowMode;
      HandleCoreParameterChange(paramIdx, param, config);
      analysisWindow.Set(synaptic::Window::IntToType(config.analysisWindowMode), config.chunkSize);
      brain.SetWindow(&analysisWindow);
      return config.analysisWindowMode != oldAnalysisWindowMode; // Only trigger reanalysis if mode changed
    }

    /**
     * @brief Handle transformer parameter change
     * @param paramIdx Parameter index that changed
     * @param param The parameter object
     * @param transformer Transformer to update
     * @return true if this was a transformer parameter
     */
    bool HandleTransformerParameterChange(int paramIdx, iplug::IParam* param,
                                          IChunkBufferTransformer* transformer);

    // Unified handler: routes to transformer and/or morph
    // Optional output parameters indicate if UI rebuild is needed
    bool HandleDynamicParameterChange(int paramIdx, iplug::IParam* param,
                                      IChunkBufferTransformer* transformer,
                                      IMorph* morph,
                                      bool* outNeedsTransformerRebuild = nullptr,
                                      bool* outNeedsMorphRebuild = nullptr);

    // === Transformer Binding Management ===

    /**
     * @brief Apply all current parameter values to transformer
     * @param plugin Plugin instance to read parameter values from
     * @param transformer Transformer to apply values to
     */
    void ApplyBindingsToTransformer(iplug::Plugin* plugin, IChunkBufferTransformer* transformer);
    void ApplyBindingsToOwners(iplug::Plugin* plugin, IChunkBufferTransformer* transformer, IMorph* morph);

    // === Query Methods ===

    /**
     * @brief Check if parameter index is a core parameter
     */
    bool IsCoreParameter(int paramIdx) const;

    /**
     * @brief Check if parameter index is a transformer parameter
     */
    bool IsTransformerParameter(int paramIdx) const;

    /**
     * @brief Get binding for a parameter index
     * @return Binding pointer or nullptr if not found
     */
    const TransformerParamBinding* GetBindingForParam(int paramIdx) const;

    /**
     * @brief Get all transformer bindings (for state serialization)
     */
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

    // === Phase 3: Centralized Parameter Coordination ===

    /**
     * @brief Handle ALL parameter changes with centralized coordination
     * 
     * This is the main entry point for parameter changes. It routes to specific
     * handlers based on parameter type and coordinates all necessary side effects.
     * 
     * @param paramIdx Parameter index that changed
     * @param ctx Context with all dependencies needed for coordination
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
     * @brief Sync UI control to match parameter value (C++ UI only)
     * @param plugin Plugin instance
     * @param paramIdx Parameter index to sync
     */
    static void SyncControlToParameter(iplug::Plugin* plugin, int paramIdx);

  private:
    // Transformer parameter bindings (union across all transformers)
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

    // First transformer parameter index (base for dynamic params)
    int mTransformerParamBase = -1;
  };
}

