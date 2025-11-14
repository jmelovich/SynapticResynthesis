/**
 * @file BrainFileHelpers.cpp
 * @brief Implementation of brain file utility functions
 *
 * Implements:
 * - Case-insensitive file extension checking
 * - Filename extraction from paths
 * - Binary file reading with message packaging
 * - Generic message sending to plugin
 */

#include "BrainFileHelpers.h"
#include "../../SynapticResynthesis.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>
#include <cstring>

using namespace iplug;
using namespace igraphics;

namespace synaptic {
namespace ui {
namespace BrainFileHelpers {

bool IsSupportedAudioFile(const std::string& path)
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

std::string ExtractFilename(const std::string& path)
{
  size_t lastSlash = path.find_last_of("/\\");
  return (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
}

bool LoadAndSendFile(const char* path, IGraphics* pGraphics)
{
  if (!path || !pGraphics)
    return false;

  // Open file in binary mode
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open())
    return false;

  // Get file size
  std::streamsize fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  // Extract filename from path
  std::string filename = ExtractFilename(std::string(path));

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
  auto* pDelegate = dynamic_cast<IEditorDelegate*>(pGraphics->GetDelegate());
  if (!pDelegate)
    return false;

  pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainAddFile, kNoTag,
                                    static_cast<int>(totalSize), buffer.data());

  return true;
}

bool SendMessageToPlugin(IGraphics* pGraphics, int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  if (!pGraphics)
    return false;

  auto* pDelegate = dynamic_cast<IEditorDelegate*>(pGraphics->GetDelegate());
  if (!pDelegate)
    return false;

  pDelegate->SendArbitraryMsgFromUI(msgTag, ctrlTag, dataSize, pData);
  return true;
}

} // namespace BrainFileHelpers
} // namespace ui
} // namespace synaptic

