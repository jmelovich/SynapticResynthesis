/**
 * @file ProgressOverlay.cpp
 * @brief Implementation of modal progress overlay
 */

#include "ProgressOverlay.h"
#include "../styles/UIStyles.h"
#include "plugin_src/ui_bridge/MessageTags.h"

using namespace iplug;
using namespace igraphics;

namespace synaptic {
namespace ui {

ProgressOverlay::ProgressOverlay(const IRECT& bounds)
: IControl(bounds)
, mIsVisible(false)
, mIndeterminate(false)
, mShowCancelButton(false)
, mProgress(0.0f)
{
  SetIgnoreMouse(true); // Ignore mouse events when hidden
  IControl::Hide(true); // Start hidden
  SetDisabled(true); // Start disabled
}

void ProgressOverlay::Draw(IGraphics& g)
{
  if (!mIsVisible)
    return;

  // Always use current graphics bounds to ensure correct positioning after window resizes
  const IRECT currentBounds = g.GetBounds();

  // Draw semi-transparent dark overlay covering entire UI
  IColor overlayBG = IColor(200, 0, 0, 0); // 78% opacity black
  g.FillRect(overlayBG, currentBounds);

  // Calculate centered modal card bounds using current window size
  const float cardWidth = 400.f;
  const float cardHeight = mShowCancelButton ? 200.f : 160.f;  // Taller if showing cancel button
  const float centerX = currentBounds.MW();
  const float centerY = currentBounds.MH();
  IRECT cardRect(centerX - cardWidth/2.f, centerY - cardHeight/2.f,
                 centerX + cardWidth/2.f, centerY + cardHeight/2.f);

  // Draw modal card background
  g.FillRoundRect(kPanelDark, cardRect, 8.f);
  g.DrawRoundRect(kPanelBorder, cardRect, 8.f, nullptr, 2.f);

  // Draw title
  IRECT titleRect = cardRect.GetPadded(-20.f).GetFromTop(30.f);
  IText titleStyle = IText(18.f, kTextPrimary, "Roboto-Regular", EAlign::Center, EVAlign::Middle, 0);
  g.DrawText(titleStyle, mTitle.c_str(), titleRect);

  // Draw message
  IRECT messageRect = cardRect.GetPadded(-20.f);
  messageRect.T = titleRect.B + 10.f;
  messageRect.B = messageRect.T + 24.f;
  IText messageStyle = IText(13.f, kTextSecond, "Roboto-Regular", EAlign::Center, EVAlign::Middle, 0);
  g.DrawText(messageStyle, mMessage.c_str(), messageRect);

  // Draw progress bar
  IRECT progressBarRect = cardRect.GetPadded(-20.f);
  progressBarRect.T = messageRect.B + 20.f;
  progressBarRect.B = progressBarRect.T + 16.f;

  // Background
  g.FillRoundRect(kControlBG, progressBarRect, 8.f);
  g.DrawRoundRect(kControlBorder, progressBarRect, 8.f, nullptr, 1.f);

  // Filled portion
  float progressPercent = mIndeterminate ? 50.0f : mProgress;
  progressPercent = std::min(100.0f, std::max(0.0f, progressPercent));

  if (progressPercent > 0.0f)
  {
    IRECT filledRect = progressBarRect;
    filledRect.R = filledRect.L + (progressBarRect.W() * progressPercent / 100.0f);

    // Ensure we have a valid rectangle for drawing
    if (filledRect.W() > 0.1f)
    {
      g.FillRoundRect(kAccentBlue, filledRect, 8.f);
    }
  }

  // Draw cancel button if requested
  if (mShowCancelButton)
  {
    IRECT cancelButtonRect = GetCancelButtonRect(cardRect);
    
    // Button background
    g.FillRoundRect(IColor(255, 220, 38, 38), cancelButtonRect, 6.f);  // Red button
    g.DrawRoundRect(IColor(255, 185, 28, 28), cancelButtonRect, 6.f, nullptr, 1.f);
    
    // Button text
    IText buttonStyle = IText(14.f, IColor(255, 255, 255, 255), "Roboto-Regular", EAlign::Center, EVAlign::Middle, 0);
    g.DrawText(buttonStyle, "Cancel", cancelButtonRect);
  }
}

void ProgressOverlay::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  // Block all mouse events when visible
  if (mIsVisible)
  {
    // Check if cancel button was clicked
    if (mShowCancelButton)
    {
      const IRECT currentBounds = GetUI()->GetBounds();
      const float cardWidth = 400.f;
      const float cardHeight = 200.f;
      const float centerX = currentBounds.MW();
      const float centerY = currentBounds.MH();
      IRECT cardRect(centerX - cardWidth/2.f, centerY - cardHeight/2.f,
                     centerX + cardWidth/2.f, centerY + cardHeight/2.f);
      
      IRECT cancelButtonRect = GetCancelButtonRect(cardRect);
      
      if (cancelButtonRect.Contains(x, y))
      {
        // Send cancellation message to plugin
        auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(GetUI()->GetDelegate());
        if (pDelegate)
        {
          pDelegate->SendArbitraryMsgFromUI(kMsgTagCancelOperation, kNoTag, 0, nullptr);
        }
      }
    }
    
    // Don't propagate the event
    return;
  }
}

void ProgressOverlay::Show(const std::string& title, const std::string& message, float progress, bool showCancelButton)
{
  mIsVisible = true;
  mTitle = title;
  mMessage = message;
  mProgress = progress;
  mShowCancelButton = showCancelButton;
  SetDisabled(false); // Enable the control
  SetIgnoreMouse(false); // Capture mouse events to block interaction
  IControl::Hide(false); // Make control visible
  SetDirty(true);
}

void ProgressOverlay::UpdateProgress(const std::string& message, float progress)
{
  if (mIsVisible)
  {
    mMessage = message;
    mProgress = progress;
    SetDirty(true);
  }
}

void ProgressOverlay::Hide()
{
  if (mIsVisible)
  {
    mIsVisible = false;
    SetIgnoreMouse(true); // Stop capturing mouse events
    IControl::Hide(true); // Hide control
    SetDisabled(true); // Disable the control
    SetDirty(true);
  }
}

void ProgressOverlay::SetIndeterminate(bool indeterminate)
{
  mIndeterminate = indeterminate;
  if (mIsVisible)
  {
    SetDirty(true);
  }
}

IRECT ProgressOverlay::GetCancelButtonRect(const IRECT& cardRect) const
{
  IRECT cancelButtonRect = cardRect.GetPadded(-20.f);
  cancelButtonRect.T = cardRect.B - 50.f;  // 50 pixels from bottom
  cancelButtonRect.B = cardRect.B - 20.f;  // 20 pixels from bottom
  return cancelButtonRect;
}

} // namespace ui
} // namespace synaptic

