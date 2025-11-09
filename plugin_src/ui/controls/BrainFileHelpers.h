/**
 * @file BrainFileHelpers.h
 * @brief Shared utility functions for brain file operations
 * 
 * Responsibilities:
 * - File validation: Check if file extension is supported (.wav, .mp3, .flac)
 * - Path parsing: Extract filename from full path
 * - File loading: Read audio file and package for plugin message
 * - Message sending: Centralized helper to send messages to plugin
 * 
 * These helpers are used by both BrainFileDropControl and BrainFileListControl
 * to avoid code duplication.
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include <string>

namespace synaptic {
namespace ui {
// Note: namespace ig alias is defined in UITheme.h
namespace ig = iplug::igraphics;

/**
 * @brief Shared helpers for Brain file operations
 *
 * These functions are used by both BrainFileDropControl and BrainFileListControl
 * to avoid code duplication.
 */
namespace BrainFileHelpers {

  /**
   * @brief Check if file has a supported audio extension
   * @param path File path to check
   * @return true if extension is .wav, .wave, .mp3, or .flac (case-insensitive)
   */
  bool IsSupportedAudioFile(const std::string& path);

  /**
   * @brief Extract filename from full path
   * @param path Full file path
   * @return Filename without directory path
   */
  std::string ExtractFilename(const std::string& path);

  /**
   * @brief Load audio file and send to plugin via message
   * @param path File path to load
   * @param pGraphics IGraphics instance to send message through
   * @return true if file was loaded and message sent successfully
   */
  bool LoadAndSendFile(const char* path, ig::IGraphics* pGraphics);

  /**
   * @brief Send an arbitrary message to the plugin
   * @param pGraphics IGraphics instance to send message through
   * @param msgTag Message tag
   * @param ctrlTag Control tag (use kNoTag if not applicable)
   * @param dataSize Size of data in bytes
   * @param pData Pointer to data (can be nullptr if dataSize is 0)
   * @return true if message was sent successfully
   */
  bool SendMessageToPlugin(ig::IGraphics* pGraphics, int msgTag, int ctrlTag, int dataSize, const void* pData);

} // namespace BrainFileHelpers

} // namespace ui
} // namespace synaptic

