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
  IText textStyle = IText(12.f, IColor(255, 255, 230, 140), "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
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

SectionLabel::SectionLabel(const IRECT& bounds, const char* text)
: IControl(bounds)
, mText(text)
{
  SetIgnoreMouse(true);
}

void SectionLabel::Draw(IGraphics& g)
{
  g.DrawText(kSectionHeaderText, mText, mRECT);
  IRECT lineRect = mRECT.GetFromBottom(1.f).GetTranslated(0.f, -8.f);
  lineRect.R = lineRect.L + 40.f;
  g.FillRect(kPanelBorder, lineRect);
}

} // namespace ui
} // namespace synaptic


