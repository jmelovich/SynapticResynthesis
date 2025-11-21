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

// Lock button control - toggles between locked/unlocked bitmaps
class LockButtonControl : public ig::IControl
{
public:
  LockButtonControl(const ig::IRECT& bounds, int paramIdx, int associatedWindowParamIdx);
  void Draw(ig::IGraphics& g) override;
  void OnMouseDown(float x, float y, const ig::IMouseMod& mod) override;
  void OnInit() override;
  static int GetLastClickedWindowParam() { return sLastClickedWindowParam; }
private:
  ig::IBitmap mLockedBitmap;
  ig::IBitmap mUnlockedBitmap;
  int mAssociatedWindowParamIdx;
  static int sLastClickedWindowParam;
};

// Window selector with lock button - combines a tab switch with a lock icon
class WindowSelectorWithLock : public ig::IContainerBase
{
public:
  WindowSelectorWithLock(const ig::IRECT& bounds,
                         int windowParamIdx,
                         int lockParamIdx,
                         const char* label,
                         const std::vector<const char*>& options,
                         const ig::IVStyle& style);
  void OnAttached() override;
  void OnResize() override;
private:
  ig::IVTabSwitchControl* mTabSwitch;
  LockButtonControl* mLockButton;
  int mWindowParamIdx;
  int mLockParamIdx;
  const char* mLabel;
  std::vector<const char*> mOptions;
  ig::IVStyle mStyle;
};

} // namespace ui
} // namespace synaptic

// Include Brain-specific controls
#include "BrainFileDropControl.h"
#include "BrainFileListControl.h"
#include "ProgressOverlay.h"


