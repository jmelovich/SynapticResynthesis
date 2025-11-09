#include "BrainFileListControl.h"
#include "../../SynapticResynthesis.h"
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
  // Draw background
  g.FillRect(IColor(255, 250, 250, 250), mRECT);
  g.DrawRect(kControlBorder, mRECT);

  if (mFiles.empty())
  {
    // Draw empty state message
    IText text = IText(13.f, kTextSecond, "Roboto-Regular", EAlign::Center, EVAlign::Middle, 0);
    g.DrawText(text, "No files in Brain", mRECT);
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

    const auto& file = mFiles[i];
    bool isHovered = (i == mHoveredRow);

    // Draw row background
    IColor bgColor = isHovered ? IColor(255, 245, 245, 245) : IColor(255, 255, 255, 255);
    g.FillRect(bgColor, rowRect);

    // Draw row border
    g.DrawLine(kPanelBorder, rowRect.L, rowRect.B, rowRect.R, rowRect.B);

    // Draw filename and chunk count
    char label[256];
    snprintf(label, sizeof(label), "%s (%d chunks)", file.name.c_str(), file.chunkCount);
    
    IRECT textRect = rowRect.GetPadded(-mPadding);
    textRect.R -= 60.f; // Leave space for remove button
    
    IText text = IText(13.f, kTextPrimary, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
    g.DrawText(text, label, textRect);

    // Draw remove button
    IRECT btnRect = GetRemoveButtonRect(rowRect);
    bool btnHovered = isHovered && mHoveringRemoveButton;
    
    IColor btnColor = btnHovered ? IColor(255, 220, 53, 69) : IColor(255, 239, 68, 68);
    IColor btnTextColor = COLOR_WHITE;
    
    g.FillRoundRect(btnColor, btnRect, 4.f);
    
    IText btnText = IText(12.f, btnTextColor, "Roboto-Regular", EAlign::Center, EVAlign::Middle, 0);
    g.DrawText(btnText, "Remove", btnRect);
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
    SetDirty(false);
  }
}

void BrainFileListControl::OnMouseOut()
{
  if (mHoveredRow >= 0 || mHoveringRemoveButton)
  {
    mHoveredRow = -1;
    mHoveringRemoveButton = false;
    SetDirty(false);
  }
}

void BrainFileListControl::UpdateList(const std::vector<BrainFileEntry>& files)
{
  mFiles = files;
  SetDirty(false);
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
  auto* pGraphics = GetUI();
  if (!pGraphics)
    return;

  auto* pDelegate = dynamic_cast<IEditorDelegate*>(pGraphics->GetDelegate());
  if (!pDelegate)
    return;

  pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainRemoveFile, fileId, 0, nullptr);
}

} // namespace ui
} // namespace synaptic

