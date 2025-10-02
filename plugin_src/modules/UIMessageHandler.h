#pragma once

namespace synaptic
{
  /**
   * @brief Message router for UI-to-C++ messages
   *
   * Provides clean switch-based routing instead of massive if/else chain.
   * This is a lightweight header-only router that calls back to plugin handlers.
   */
  class UIMessageRouter
  {
  public:
    /**
     * @brief Route message using clean switch statement
     * @tparam PluginT Plugin type (SynapticResynthesis)
     * @param plugin Plugin instance that implements handle methods
     * @param msgTag Message tag from UI
     * @param ctrlTag Control tag (integer value or ID)
     * @param dataSize Size of binary data
     * @param pData Binary data pointer
     * @return true if message was handled
     */
    template<typename PluginT>
    static bool Route(PluginT* plugin, int msgTag, int ctrlTag, int dataSize, const void* pData)
    {
      switch (msgTag)
      {
        // UI Control Messages
        case ::kMsgTagUiReady:
          return plugin->HandleUiReadyMsg();

        // DSP Configuration Messages
        case ::kMsgTagSetChunkSize:
          return plugin->HandleSetChunkSizeMsg(ctrlTag);

        case ::kMsgTagSetBufferWindowSize:
          return plugin->HandleSetBufferWindowSizeMsg(ctrlTag);

        case ::kMsgTagSetOutputWindowMode:
          return plugin->HandleSetOutputWindowMsg(ctrlTag);

        case ::kMsgTagSetAnalysisWindowMode:
          return plugin->HandleSetAnalysisWindowMsg(ctrlTag);

        case ::kMsgTagSetAlgorithm:
          return plugin->HandleSetAlgorithmMsg(ctrlTag);

        // Transformer Parameter Messages
        case ::kMsgTagTransformerSetParam:
          return plugin->HandleTransformerSetParamMsg(pData, dataSize);

        // Brain Messages
        case ::kMsgTagBrainAddFile:
          return plugin->HandleBrainAddFileMsg(dataSize, pData);

        case ::kMsgTagBrainRemoveFile:
          return plugin->HandleBrainRemoveFileMsg(ctrlTag);

        case ::kMsgTagBrainExport:
          return plugin->HandleBrainExportMsg();

        case ::kMsgTagBrainImport:
          return plugin->HandleBrainImportMsg();

        case ::kMsgTagBrainReset:
          return plugin->HandleBrainResetMsg();

        case ::kMsgTagBrainDetach:
          return plugin->HandleBrainDetachMsg();

        case ::kMsgTagResizeToFit:
          return plugin->HandleResizeToFitMsg(dataSize, pData);

        default:
          return false;
      }
    }
  };
}


