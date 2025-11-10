/**
 * @file BrainFileListControl.h
 * @brief Scrollable list control for brain file management
 *
 * Responsibilities:
 * - Displays a list of audio files currently loaded in the Brain
 * - Shows file names and chunk counts for each entry
 * - Provides remove buttons (X) for each file
 * - Handles mouse wheel scrolling for long lists
 * - Accepts drag-and-drop of additional audio files
 * - Sends messages to plugin for add/remove operations
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "plugin_src/brain/Brain.h"
#include "../styles/UITheme.h"
#include <vector>
#include <string>

namespace synaptic {
namespace ui {
// Note: namespace ig alias is defined in UITheme.h

/**
 * @brief File entry in the brain file list
 */
struct BrainFileEntry
{
  int id;
  std::string name;
  int chunkCount;
};

/**
 * @brief Control that displays list of Brain files with remove buttons
 */
class BrainFileListControl : public ig::IControl
{
public:
  BrainFileListControl(const ig::IRECT& bounds);

  void Draw(ig::IGraphics& g) override;
  void OnMouseDown(float x, float y, const ig::IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const ig::IMouseMod& mod) override;
  void OnMouseOut() override;
  void OnMouseWheel(float x, float y, const ig::IMouseMod& mod, float d) override;
  void OnDrop(const char* str) override;
  void OnDropMultiple(const std::vector<const char*>& paths) override;

  /**
   * @brief Update the file list from Brain summary
   */
  void UpdateList(const std::vector<BrainFileEntry>& files);

  /**
   * @brief Get current file count
   */
  int GetFileCount() const { return static_cast<int>(mFiles.size()); }

private:
  /**
   * @brief Get row rect for a file entry
   */
  ig::IRECT GetRowRect(int index) const;

  /**
   * @brief Get remove button rect for a row
   */
  ig::IRECT GetRemoveButtonRect(const ig::IRECT& rowRect) const;

  /**
   * @brief Find which row index is at y coordinate (or -1 if none)
   */
  int FindRowAtY(float y) const;

  /**
   * @brief Check if point is in remove button for given row
   */
  bool IsInRemoveButton(float x, float y, int rowIndex) const;

  /**
   * @brief Send remove file message to plugin
   */
  void SendRemoveFileMessage(int fileId);

  /**
   * @brief Send add file message to plugin
   */
  void SendAddFileMessage(const char* path);

private:
  std::vector<BrainFileEntry> mFiles;
  int mHoveredRow;
  bool mHoveringRemoveButton;
  float mScrollOffset;
  const float mRowHeight = 32.f;
  const float mPadding = 8.f;
};

} // namespace ui
} // namespace synaptic

