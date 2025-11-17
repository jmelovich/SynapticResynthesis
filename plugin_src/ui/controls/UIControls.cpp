/**
 * @file UIControls.cpp
 * @brief Implementation of custom UI control drawing and interaction logic
 *
 * Implements the rendering and behavior for:
 * - CardPanel: Draws rounded card backgrounds with borders and titles
 * - WarningBox: Draws warning boxes with icons and styled text
 * - TabButton: Handles drawing, mouse interaction, and active state visualization
 * - BrainStatusControl: Renders file count and storage mode status text
 */

#include "UIControls.h"
#include "../styles/UIStyles.h"
#include "../IconsForkAwesome.h"
#include "config.h"

using namespace iplug;
using namespace igraphics;

namespace synaptic {
namespace ui {

CardPanel::CardPanel(const IRECT& bounds, const char* title)
: IControl(bounds)
, mTitle(title)
{
  SetIgnoreMouse(true);
}

void CardPanel::Draw(IGraphics& g)
{
  g.FillRoundRect(kPanelDark, mRECT, 6.f);
  g.DrawRoundRect(kPanelBorder, mRECT, 6.f, nullptr, 1.5f);
  if (mTitle)
  {
    IRECT titleRect = mRECT.GetPadded(-12.f).GetFromTop(20.f);
    g.DrawText(kSectionHeaderText, mTitle, titleRect);
  }
}

WarningBox::WarningBox(const IRECT& bounds, const char* text)
: IControl(bounds)
, mText(text)
{
  SetIgnoreMouse(true);
}

void WarningBox::Draw(IGraphics& g)
{
  g.FillRoundRect(kWarnBG, mRECT, 4.f);
  g.DrawRoundRect(kWarnText, mRECT, 4.f, nullptr, 1.f);

  IRECT iconRect = mRECT.GetFromLeft(30.f);
  IText iconStyle = IText(14.f, kWarnText, "ForkAwesome", EAlign::Center, EVAlign::Middle, 0);
  g.DrawText(iconStyle, ICON_FK_EXCLAMATION_TRIANGLE, iconRect);

  IRECT textRect = mRECT;
  textRect.L += 32.f;
  textRect.R -= 8.f;
  IText textStyle = kWarnTextStyle;
  textStyle.mAlign = EAlign::Near;
  g.DrawText(textStyle, mText, textRect);
}

TabButton::TabButton(const IRECT& bounds, const char* label, std::function<void()> onClick)
: IControl(bounds)
, mLabel(label)
, mOnClick(onClick)
, mIsActive(false)
, mIsHovered(false)
{}

void TabButton::Draw(IGraphics& g)
{
  IColor bgColor = mIsActive ? kTabActive : (mIsHovered ? kTabHover : kTabInactive);
  g.FillRoundRect(bgColor, mRECT, 4.f);
  if (mIsActive)
  {
    g.DrawRoundRect(kAccentBlue, mRECT, 4.f, nullptr, 2.f);
  }
  g.DrawText(kButtonTextStyle, mLabel, mRECT);
}

void TabButton::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  if (mOnClick) mOnClick();
  SetDirty(false);
}

void TabButton::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  mIsHovered = true;
  SetDirty(false);
}

void TabButton::OnMouseOut()
{
  mIsHovered = false;
  SetDirty(false);
}

void TabButton::SetActive(bool active)
{
  if (mIsActive != active)
  {
    mIsActive = active;
    SetDirty(false);
  }
}

BrainStatusControl::BrainStatusControl(const IRECT& bounds)
: IControl(bounds)
{
  SetIgnoreMouse(true);
}

void BrainStatusControl::Draw(IGraphics& g)
{
  char statusText[256];
  snprintf(statusText, sizeof(statusText), "Files: %d | Storage: %s", mFileCount, mStorageMode.c_str());

  g.DrawText(kSmallText, statusText, mRECT);
}

LockButtonControl::LockButtonControl(const IRECT& bounds, int paramIdx, int associatedWindowParamIdx)
: IControl(bounds, paramIdx)
, mAssociatedWindowParamIdx(associatedWindowParamIdx)
{
}

// Static member initialization
int LockButtonControl::sLastClickedWindowParam = -1;

void LockButtonControl::OnInit()
{
  // Load bitmaps on initialization (when IGraphics is available)
  mLockedBitmap = GetUI()->LoadBitmap(LOCK_LOCKED_FN, 1, false);
  mUnlockedBitmap = GetUI()->LoadBitmap(LOCK_UNLOCKED_FN, 1, false);
}

void LockButtonControl::Draw(IGraphics& g)
{
  // Draw the appropriate bitmap based on parameter value
  bool isLocked = GetValue() > 0.5;
  const IBitmap& bitmap = isLocked ? mLockedBitmap : mUnlockedBitmap;

  if (bitmap.IsValid())
  {
    g.DrawFittedBitmap(bitmap, mRECT);
  }
}

void LockButtonControl::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  // Store which window control's lock was clicked
  sLastClickedWindowParam = mAssociatedWindowParamIdx;

  // Toggle the parameter value
  SetValue(GetValue() > 0.5 ? 0.0 : 1.0);
  SetDirty(true);
}

WindowSelectorWithLock::WindowSelectorWithLock(const IRECT& bounds,
                                               int windowParamIdx,
                                               int lockParamIdx,
                                               const char* label,
                                               const std::vector<const char*>& options,
                                               const IVStyle& style)
: IContainerBase(bounds)
, mTabSwitch(nullptr)
, mLockButton(nullptr)
, mWindowParamIdx(windowParamIdx)
, mLockParamIdx(lockParamIdx)
, mLabel(label)
, mOptions(options)
, mStyle(style)
{
  // Child controls will be created in OnAttached() when IGraphics is available
}

void WindowSelectorWithLock::OnAttached()
{
  // Now we have access to IGraphics, so we can create child controls
  const float lockButtonSize = 24.f;
  const float gap = 6.f;

  // Lock button on the left, tab switch takes remaining space
  IRECT lockButtonRect = IRECT(
    mRECT.L,
    mRECT.T + (mRECT.H() - lockButtonSize) * 0.5f,
    mRECT.L + lockButtonSize,
    mRECT.T + (mRECT.H() - lockButtonSize) * 0.5f + lockButtonSize
  );

  IRECT tabSwitchRect = mRECT;
  tabSwitchRect.L += (lockButtonSize + gap);

  // Create lock button first (on the left)
  mLockButton = new LockButtonControl(lockButtonRect, mLockParamIdx, mWindowParamIdx);
  AddChildControl(mLockButton);

  // Create tab switch control for window selection
  mTabSwitch = new IVTabSwitchControl(
    tabSwitchRect,
    mWindowParamIdx,
    mOptions,
    mLabel,
    mStyle,
    EVShape::Rectangle,
    EDirection::Horizontal
  );
  AddChildControl(mTabSwitch);
}

void WindowSelectorWithLock::OnResize()
{
  // Recalculate layout if container is resized
  const float lockButtonSize = 24.f;
  const float gap = 6.f;

  IRECT lockButtonRect = IRECT(
    mRECT.L,
    mRECT.T + (mRECT.H() - lockButtonSize) * 0.5f,
    mRECT.L + lockButtonSize,
    mRECT.T + (mRECT.H() - lockButtonSize) * 0.5f + lockButtonSize
  );

  IRECT tabSwitchRect = mRECT;
  tabSwitchRect.L += (lockButtonSize + gap);

  if (mLockButton)
    mLockButton->SetTargetAndDrawRECTs(lockButtonRect);
  if (mTabSwitch)
    mTabSwitch->SetTargetAndDrawRECTs(tabSwitchRect);
}

} // namespace ui
} // namespace synaptic


