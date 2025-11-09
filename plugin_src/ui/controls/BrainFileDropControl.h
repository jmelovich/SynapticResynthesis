/**
 * @file BrainFileDropControl.h
 * @brief Drag-and-drop target for importing audio files into the Brain
 *
 * Responsibilities:
 * - Provides a visual drop zone for audio file import
 * - Opens file browser dialog when clicked
 * - Accepts drag-and-drop of single or multiple audio files
 * - Validates file types (WAV, MP3, FLAC)
 * - Sends file data to plugin for brain analysis
 * - Shows hover feedback to indicate interactivity
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "../styles/UITheme.h"
#include <vector>
#include <string>
#include <fstream>

namespace synaptic {
namespace ui {
// Note: namespace ig alias is defined in UITheme.h

/**
 * @brief Control that accepts drag-and-drop audio files for Brain import
 */
class BrainFileDropControl : public ig::IControl
{
public:
  BrainFileDropControl(const ig::IRECT& bounds);

  void Draw(ig::IGraphics& g) override;
  void OnMouseDown(float x, float y, const ig::IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const ig::IMouseMod& mod) override;
  void OnMouseOut() override;
  void OnDrop(const char* str) override;
  void OnDropMultiple(const std::vector<const char*>& paths) override;

private:
  bool mIsHovered;
};

} // namespace ui
} // namespace synaptic

