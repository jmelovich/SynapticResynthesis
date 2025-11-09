#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "plugin_src/samplebrain/Brain.h"
#include "../styles/UITheme.h"
#include <vector>
#include <string>

namespace synaptic {
namespace ui {

namespace ig = iplug::igraphics;

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

