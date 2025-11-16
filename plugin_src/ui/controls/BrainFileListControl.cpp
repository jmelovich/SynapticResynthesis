/**
 * @file BrainFileListControl.cpp
 * @brief Implementation of scrollable brain file list with interactive remove buttons
 *
 * Implements:
 * - Row-based layout with background, text, and remove button per file
 * - Hover effects for rows and buttons
 * - Mouse wheel scrolling with bounds checking
 * - Row hit testing for click detection
 * - Message sending for file removal
 * - Empty state rendering
 */

#include "BrainFileListControl.h"
#include "BrainFileHelpers.h"
#include "SynapticResynthesis.h"
#include <algorithm>


using namespace iplug;
using namespace igraphics;

namespace synaptic {
namespace ui {

BrainFileListControl::BrainFileListControl(const IRECT& bounds)
: IControl(bounds)
, mHoveredRow(-1)
, mHoveringRemoveButton(false)
, mScrollOffset(0.f)
{
}

void BrainFileListControl::Draw(IGraphics& g)
{
  // Draw background - dark theme to match plugin
  g.FillRect(kPanelDark, mRECT);
  g.DrawRect(kControlBorder, mRECT);

  if (mFiles.empty())
  {
    // Draw empty state message - different message when no external brain
    const char* message = (!mHasExternalBrain)
      ? "You must create or load a Brain before importing files"
      : "No files in Brain";

    // Use centered text style for empty state message
    IText centeredText = kSmallText;
    centeredText.mAlign = EAlign::Center;
    centeredText.mVAlign = EVAlign::Middle;
    g.DrawText(centeredText, message, mRECT);
    return;
  }

  // Draw file rows
  for (int i = 0; i < static_cast<int>(mFiles.size()); ++i)
  {
    IRECT rowRect = GetRowRect(i);

    // Skip if row is outside visible area
    if (rowRect.B < mRECT.T || rowRect.T > mRECT.B)
      continue;

    // Clip to bounds
    rowRect = rowRect.Intersect(mRECT);
    if (rowRect.H() <= 0) continue;

    const auto& file = mFiles[i];
    bool isHovered = (i == mHoveredRow);

    // Draw row background - slightly lighter on hover
    IColor bgColor = isHovered ? kControlBG : kPanelDark;
    g.FillRect(bgColor, rowRect);

    // Draw row border
    g.DrawLine(kPanelBorder, rowRect.L, rowRect.B, rowRect.R, rowRect.B);

    // Draw filename and chunk count with light text for dark background
    char label[256];
    snprintf(label, sizeof(label), "%s (%d chunks)", file.name.c_str(), file.chunkCount);

    IRECT textRect = rowRect.GetPadded(-mPadding);
    textRect.R -= 70.f; // Leave space for remove button

    IText text = kSmallText;
    text.mAlign = EAlign::Near;
    text.mVAlign = EVAlign::Middle;
    text.mSize = 13.f;
    g.DrawText(text, label, textRect);

    // Draw remove button
    IRECT btnRect = GetRemoveButtonRect(rowRect);
    bool btnHovered = isHovered && mHoveringRemoveButton;

    IColor btnColor = btnHovered ? IColor(255, 220, 53, 69) : IColor(255, 239, 68, 68);
    g.FillRoundRect(btnColor, btnRect, 4.f);

    IText btnText = kSmallText;
    btnText.mAlign = EAlign::Center;
    btnText.mVAlign = EVAlign::Middle;
    btnText.mSize = 11.f;
    btnText.mFGColor = COLOR_WHITE;
    g.DrawText(btnText, "X", btnRect);
  }
}

void BrainFileListControl::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  int row = FindRowAtY(y);
  if (row >= 0 && row < static_cast<int>(mFiles.size()))
  {
    if (IsInRemoveButton(x, y, row))
    {
      SendRemoveFileMessage(mFiles[row].id);
    }
  }
}

void BrainFileListControl::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  int row = FindRowAtY(y);
  bool inRemoveBtn = (row >= 0) && IsInRemoveButton(x, y, row);

  if (row != mHoveredRow || inRemoveBtn != mHoveringRemoveButton)
  {
    mHoveredRow = row;
    mHoveringRemoveButton = inRemoveBtn;
    SetDirty(true); // Trigger redraw for hover effect
  }
}

void BrainFileListControl::OnMouseOut()
{
  if (mHoveredRow >= 0 || mHoveringRemoveButton)
  {
    mHoveredRow = -1;
    mHoveringRemoveButton = false;
    SetDirty(true); // Trigger redraw to clear hover effect
  }
}

void BrainFileListControl::OnMouseWheel(float x, float y, const IMouseMod& mod, float d)
{
  // Calculate total content height and visible height
  float contentHeight = mFiles.size() * mRowHeight;
  float visibleHeight = mRECT.H() - (2.f * mPadding);

  // Only scroll if content is larger than visible area
  if (contentHeight > visibleHeight)
  {
    // Scroll speed: each wheel "click" scrolls by one row
    float scrollAmount = d * mRowHeight;
    mScrollOffset -= scrollAmount;

    // Clamp scroll offset to valid range
    float maxScroll = contentHeight - visibleHeight;
    mScrollOffset = std::max(0.f, std::min(mScrollOffset, maxScroll));

    SetDirty(true);
  }
}

void BrainFileListControl::UpdateList(const std::vector<BrainFileEntry>& files)
{
  mFiles = files;

  // Reset scroll when list changes
  mScrollOffset = 0.f;

  SetDirty(true);
}

IRECT BrainFileListControl::GetRowRect(int index) const
{
  float y = mRECT.T + mPadding + index * mRowHeight - mScrollOffset;
  return IRECT(mRECT.L + mPadding, y, mRECT.R - mPadding, y + mRowHeight - 2.f);
}

IRECT BrainFileListControl::GetRemoveButtonRect(const IRECT& rowRect) const
{
  float btnWidth = 60.f;
  float btnHeight = 24.f;
  float x = rowRect.R - btnWidth - mPadding;
  float y = rowRect.MH() - btnHeight / 2.f;
  return IRECT(x, y, x + btnWidth, y + btnHeight);
}

int BrainFileListControl::FindRowAtY(float y) const
{
  if (y < mRECT.T || y > mRECT.B)
    return -1;

  for (int i = 0; i < static_cast<int>(mFiles.size()); ++i)
  {
    IRECT rowRect = GetRowRect(i);
    if (y >= rowRect.T && y < rowRect.B)
      return i;
  }
  return -1;
}

bool BrainFileListControl::IsInRemoveButton(float x, float y, int rowIndex) const
{
  if (rowIndex < 0 || rowIndex >= static_cast<int>(mFiles.size()))
    return false;

  IRECT rowRect = GetRowRect(rowIndex);
  IRECT btnRect = GetRemoveButtonRect(rowRect);
  return btnRect.Contains(x, y);
}

void BrainFileListControl::SendRemoveFileMessage(int fileId)
{
  BrainFileHelpers::SendMessageToPlugin(GetUI(), kMsgTagBrainRemoveFile, fileId, 0, nullptr);
}

void BrainFileListControl::SendAddFileMessage(const char* path)
{
  BrainFileHelpers::LoadAndSendFile(path, GetUI());
}

void BrainFileListControl::OnDrop(const char* str)
{
  // Don't accept drops if no external brain loaded
  if (!mHasExternalBrain) return;

  if (str)
    SendAddFileMessage(str);
}

void BrainFileListControl::OnDropMultiple(const std::vector<const char*>& paths)
{
  // Don't accept drops if no external brain loaded
  if (!mHasExternalBrain) return;

  for (const char* path : paths)
  {
    if (path)
      SendAddFileMessage(path);
  }
}

} // namespace ui
} // namespace synaptic

