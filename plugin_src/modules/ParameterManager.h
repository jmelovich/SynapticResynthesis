#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "plugin_src/ChunkBufferTransformer.h"
#include "plugin_src/transformers/ExpandedSimpleSampleBrainTransformer.h"
#include "DSPConfig.h"
#include <vector>
#include <string>

namespace synaptic
{
  /**
   * @brief Binding between IParam and transformer parameter
   */
  struct TransformerParamBinding
  {
    std::string id;
    IChunkBufferTransformer::ParamType type;
    int paramIdx = -1;
    // For enums, map index<->string value
    std::vector<std::string> enumValues; // order corresponds to indices 0..N-1
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
     */
    template<typename PluginT>
    void HandleChunkSizeChange(int paramIdx, iplug::IParam* param, DSPConfig& config,
                              PluginT* plugin, synaptic::AudioStreamChunker& chunker,
                              synaptic::Window& analysisWindow)
    {
      HandleCoreParameterChange(paramIdx, param, config);
      chunker.SetChunkSize(config.chunkSize);
      analysisWindow.Set(synaptic::Window::IntToType(config.analysisWindowMode), config.chunkSize);
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

    /**
     * @brief Handle analysis window parameter change with side effects
     * Returns true if reanalysis should be triggered
     */
    bool HandleAnalysisWindowChange(int paramIdx, iplug::IParam* param, DSPConfig& config,
                                   synaptic::Window& analysisWindow, synaptic::Brain& brain)
    {
      HandleCoreParameterChange(paramIdx, param, config);
      analysisWindow.Set(synaptic::Window::IntToType(config.analysisWindowMode), config.chunkSize);
      brain.SetWindow(&analysisWindow);
      return true; // Signal caller to trigger reanalysis
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

    // === Transformer Binding Management ===

    /**
     * @brief Apply all current parameter values to transformer
     * @param plugin Plugin instance to read parameter values from
     * @param transformer Transformer to apply values to
     */
    void ApplyBindingsToTransformer(iplug::Plugin* plugin, IChunkBufferTransformer* transformer);

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

    // First transformer parameter index (base for dynamic params)
    int mTransformerParamBase = -1;
  };
}

