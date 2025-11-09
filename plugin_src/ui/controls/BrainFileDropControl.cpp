/**
 * @file BrainFileDropControl.cpp
 * @brief Implementation of file drop zone with browser dialog integration
 *
 * Implements:
 * - Visual rendering with hover feedback
 * - Native file browser dialog with audio file filters
 * - Drag-and-drop event handling
 * - File validation before sending to plugin
 * - Support for single and multiple file drops
 */

#include "BrainFileDropControl.h"
#include "BrainFileHelpers.h"
#include "../../SynapticResynthesis.h"
#include "../../PlatformFileDialogs.h"

using namespace iplug;
using namespace igraphics;

namespace synaptic {
namespace ui {

BrainFileDropControl::BrainFileDropControl(const IRECT& bounds)
: IControl(bounds)
, mIsHovered(false)
{
}

void BrainFileDropControl::Draw(IGraphics& g)
{
  // Determine colors based on hover state
  IColor borderColor = mIsHovered ? kPanelBorder : kControlBorder;
  IColor bgColor = kPanelDark;
  IColor textColor = kTextSecond;

  // Draw background
  g.FillRect(bgColor, mRECT);

  // Draw border
  g.DrawRect(borderColor, mRECT, nullptr, 2.f);

  // Draw text
  IText text = IText(14.f, textColor, "Roboto-Regular", EAlign::Center, EVAlign::Middle, 0);
  g.DrawText(text, "Drag and drop audio files or click to browse", mRECT);
}

void BrainFileDropControl::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  // Open file browser dialog
  std::string selectedPath;
  const wchar_t* filter = L"Audio Files\0*.wav;*.wave;*.mp3;*.flac\0"
                          L"WAV Files (*.wav)\0*.wav;*.wave\0"
                          L"MP3 Files (*.mp3)\0*.mp3\0"
                          L"FLAC Files (*.flac)\0*.flac\0"
                          L"All Files (*.*)\0*.*\0\0";

  if (platform::GetOpenFilePath(selectedPath, filter))
  {
    if (BrainFileHelpers::IsSupportedAudioFile(selectedPath))
    {
      BrainFileHelpers::LoadAndSendFile(selectedPath.c_str(), GetUI());
    }
  }
}

void BrainFileDropControl::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  mIsHovered = true;
  SetDirty(true); // Trigger redraw for hover effect
}

void BrainFileDropControl::OnMouseOut()
{
  mIsHovered = false;
  SetDirty(true); // Trigger redraw to clear hover effect
}

void BrainFileDropControl::OnDrop(const char* str)
{
  if (!str)
    return;

  std::string path(str);
  if (BrainFileHelpers::IsSupportedAudioFile(path))
  {
    BrainFileHelpers::LoadAndSendFile(str, GetUI());
  }
}

void BrainFileDropControl::OnDropMultiple(const std::vector<const char*>& paths)
{
  for (const char* path : paths)
  {
    if (path && BrainFileHelpers::IsSupportedAudioFile(std::string(path)))
    {
      BrainFileHelpers::LoadAndSendFile(path, GetUI());
    }
  }
}


} // namespace ui
} // namespace synaptic

