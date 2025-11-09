/**
 * @file UIControls.h
 * @brief Custom UI control classes for the Synaptic Resynthesis interface
 *
 * Defines custom control types:
 * - CardPanel: Rounded rectangle container with optional title
 * - WarningBox: Styled warning message box with icon
 * - TabButton: Clickable tab selector with hover and active states
 * - BrainStatusControl: Display-only status line showing file count and storage mode
 *
 * Also includes brain-specific controls via their own headers:
 * - BrainFileDropControl: Drag-and-drop zone for audio files
 * - BrainFileListControl: Scrollable list of brain files with remove buttons
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include <functional>
#include "../styles/UITheme.h"

namespace synaptic {
namespace ui {
// Note: namespace ig alias is defined in UITheme.h

class CardPanel : public ig::IControl
{
public:
  CardPanel(const ig::IRECT& bounds, const char* title = nullptr);
  void Draw(ig::IGraphics& g) override;
private:
  const char* mTitle;
};

class WarningBox : public ig::IControl
{
public:
  WarningBox(const ig::IRECT& bounds, const char* text);
  void Draw(ig::IGraphics& g) override;
private:
  const char* mText;
};

class TabButton : public ig::IControl
{
public:
  TabButton(const ig::IRECT& bounds, const char* label, std::function<void()> onClick);
  void Draw(ig::IGraphics& g) override;
  void OnMouseDown(float x, float y, const ig::IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const ig::IMouseMod& mod) override;
  void OnMouseOut() override;
  void SetActive(bool active);
private:
  const char* mLabel;
  std::function<void()> mOnClick;
  bool mIsActive;
  bool mIsHovered;
};

class BrainStatusControl : public ig::IControl
{
public:
  BrainStatusControl(const ig::IRECT& bounds);
  void Draw(ig::IGraphics& g) override;
  void SetFileCount(int count) { mFileCount = count; SetDirty(true); }
  void SetStorageMode(const std::string& mode) { mStorageMode = mode; SetDirty(true); }
private:
  int mFileCount = 0;
  std::string mStorageMode = "(inline)";
};

} // namespace ui
} // namespace synaptic

// Include Brain-specific controls
#include "BrainFileDropControl.h"
#include "BrainFileListControl.h"


