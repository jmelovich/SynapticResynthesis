#pragma once

#include "MessageTags.h"

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
        // Brain Messages
        case ::kMsgTagBrainAddFile:
          return plugin->HandleBrainAddFileMsg(dataSize, pData);

        case ::kMsgTagBrainRemoveFile:
          return plugin->HandleBrainRemoveFileMsg(ctrlTag);

        case ::kMsgTagBrainExport:
          return plugin->HandleBrainExportMsg();

        case ::kMsgTagBrainImport:
          return plugin->HandleBrainImportMsg();

        case ::kMsgTagBrainEject:
          return plugin->HandleBrainEjectMsg();

        case ::kMsgTagBrainDetach:
          return plugin->HandleBrainDetachMsg();

        case ::kMsgTagBrainCreateNew:
          return plugin->HandleBrainCreateNewMsg();

        case ::kMsgTagBrainSetCompactMode:
          return plugin->HandleBrainSetCompactModeMsg(ctrlTag);

        case ::kMsgTagCancelOperation:
          return plugin->HandleCancelOperationMsg();

        default:
          return false;
      }
    }
  };
}


