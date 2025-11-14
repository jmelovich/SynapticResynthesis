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

} // namespace ui
} // namespace synaptic


