#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "../styles/UITheme.h"
#include <vector>
#include <string>
#include <fstream>

namespace synaptic {
namespace ui {

namespace ig = iplug::igraphics;

/**
 * @brief Control that accepts drag-and-drop audio files for Brain import
 */
class BrainFileDropControl : public ig::IControl
{
public:
  BrainFileDropControl(const ig::IRECT& bounds);

  void Draw(ig::IGraphics& g) override;
  void OnMouseOver(float x, float y, const ig::IMouseMod& mod) override;
  void OnMouseOut() override;
  void OnDrop(const char* str) override;
  void OnDropMultiple(const std::vector<const char*>& paths) override;

private:
  /**
   * @brief Check if file extension is a supported audio format
   */
  bool IsSupportedAudioFile(const std::string& path) const;

  /**
   * @brief Send file to plugin for processing
   */
  void SendFileToPlugin(const char* path);

  /**
   * @brief Load file into memory and send to plugin via message
   */
  bool LoadAndSendFile(const char* path);

private:
  bool mIsHovered;
  bool mIsDragOver;
};

} // namespace ui
} // namespace synaptic

