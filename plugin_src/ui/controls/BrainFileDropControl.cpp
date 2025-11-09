#include "BrainFileDropControl.h"
#include "../../SynapticResynthesis.h"
#include <algorithm>
#include <cctype>

using namespace iplug;
using namespace igraphics;

namespace synaptic {
namespace ui {

BrainFileDropControl::BrainFileDropControl(const IRECT& bounds)
: IControl(bounds)
, mIsHovered(false)
, mIsDragOver(false)
{
}

void BrainFileDropControl::Draw(IGraphics& g)
{
  // Determine colors based on state
  IColor borderColor = kControlBorder;
  IColor bgColor = kPanelDark;
  IColor textColor = kTextSecond;

  if (mIsDragOver)
  {
    borderColor = kAccentBlue;
    bgColor = IColor(255, 239, 246, 255); // Light blue tint
    textColor = kAccentBlue;
  }
  else if (mIsHovered)
  {
    borderColor = kPanelBorder;
  }

  // Draw background
  g.FillRect(bgColor, mRECT);

  // Draw border (solid instead of dashed since DrawDashedRect doesn't exist)
  g.DrawRect(borderColor, mRECT, nullptr, 2.f);

  // Draw text
  IText text = IText(14.f, textColor, "Roboto-Regular", EAlign::Center, EVAlign::Middle, 0);
  const char* message = mIsDragOver
    ? "Drop audio files here"
    : "Drag and drop audio files (.wav, .mp3, .flac)";
  g.DrawText(text, message, mRECT);
}

void BrainFileDropControl::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  mIsHovered = true;
  SetDirty(false);
}

void BrainFileDropControl::OnMouseOut()
{
  mIsHovered = false;
  mIsDragOver = false;
  SetDirty(false);
}

void BrainFileDropControl::OnDrop(const char* str)
{
  mIsDragOver = false;
  SetDirty(false);

  if (!str)
    return;

  std::string path(str);
  if (IsSupportedAudioFile(path))
  {
    LoadAndSendFile(str);
  }
}

void BrainFileDropControl::OnDropMultiple(const std::vector<const char*>& paths)
{
  mIsDragOver = false;
  SetDirty(false);

  for (const char* path : paths)
  {
    if (path && IsSupportedAudioFile(std::string(path)))
    {
      LoadAndSendFile(path);
    }
  }
}

bool BrainFileDropControl::IsSupportedAudioFile(const std::string& path) const
{
  // Convert to lowercase for case-insensitive comparison
  std::string lowerPath = path;
  std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(),
    [](unsigned char c) { return std::tolower(c); });

  // Check file extension (C++17 compatible)
  auto hasExtension = [&lowerPath](const char* ext) -> bool {
    size_t extLen = std::strlen(ext);
    if (lowerPath.length() < extLen) return false;
    return lowerPath.compare(lowerPath.length() - extLen, extLen, ext) == 0;
  };

  return (hasExtension(".wav") ||
          hasExtension(".wave") ||
          hasExtension(".mp3") ||
          hasExtension(".flac"));
}

bool BrainFileDropControl::LoadAndSendFile(const char* path)
{
  // Open file in binary mode
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open())
    return false;

  // Get file size
  std::streamsize fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  // Extract filename from path
  std::string pathStr(path);
  size_t lastSlash = pathStr.find_last_of("/\\");
  std::string filename = (lastSlash != std::string::npos)
    ? pathStr.substr(lastSlash + 1)
    : pathStr;

  // Build message: [uint16 nameLen][name bytes][file data]
  uint16_t nameLen = static_cast<uint16_t>(filename.size());
  size_t totalSize = 2 + nameLen + fileSize;

  std::vector<uint8_t> buffer(totalSize);

  // Write nameLen (little-endian)
  buffer[0] = nameLen & 0xFF;
  buffer[1] = (nameLen >> 8) & 0xFF;

  // Write filename
  std::memcpy(buffer.data() + 2, filename.c_str(), nameLen);

  // Read and write file data
  if (!file.read(reinterpret_cast<char*>(buffer.data() + 2 + nameLen), fileSize))
  {
    file.close();
    return false;
  }

  file.close();

  // Send to plugin via message
  auto* pGraphics = GetUI();
  if (!pGraphics)
    return false;

  auto* pDelegate = dynamic_cast<IEditorDelegate*>(pGraphics->GetDelegate());
  if (!pDelegate)
    return false;

  pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainAddFile, kNoTag,
                                    static_cast<int>(totalSize), buffer.data());

  return true;
}

} // namespace ui
} // namespace synaptic

