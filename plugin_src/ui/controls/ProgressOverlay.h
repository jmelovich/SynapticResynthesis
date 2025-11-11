/**
 * @file ProgressOverlay.h
 * @brief Modal progress overlay for long-running operations
 *
 * Displays a semi-transparent overlay that blocks UI interaction during
 * operations like file import, brain export/import, rechunking, etc.
 * Shows operation title, progress message, and progress bar.
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include <string>
#include "../styles/UITheme.h"

namespace synaptic {
namespace ui {

class ProgressOverlay : public ig::IControl
{
public:
  ProgressOverlay(const ig::IRECT& bounds);
  
  void Draw(ig::IGraphics& g) override;
  void OnMouseDown(float x, float y, const ig::IMouseMod& mod) override;
  
  /**
   * @brief Show overlay with operation details
   * @param title Operation title (e.g., "Importing Files")
   * @param message Current step message (e.g., "Processing file.wav")
   * @param progress Progress value (0-100 for determinate, or use indeterminate mode)
   */
  void Show(const std::string& title, const std::string& message, float progress = 0.0f);
  
  /**
   * @brief Update progress message and value
   * @param message Current step message
   * @param progress Progress value (0-100)
   */
  void UpdateProgress(const std::string& message, float progress);
  
  /**
   * @brief Hide the overlay
   */
  void Hide();
  
  /**
   * @brief Set indeterminate mode (shows 50% progress bar)
   * @param indeterminate True for indeterminate mode
   */
  void SetIndeterminate(bool indeterminate);
  
  /**
   * @brief Check if overlay is currently visible
   */
  bool IsVisible() const { return mIsVisible; }
  
  /**
   * @brief Get current title
   */
  const std::string& GetTitle() const { return mTitle; }
  
  /**
   * @brief Get current message
   */
  const std::string& GetMessage() const { return mMessage; }
  
  /**
   * @brief Get current progress value
   */
  float GetProgress() const { return mProgress; }
  
private:
  bool mIsVisible;
  bool mIndeterminate;
  std::string mTitle;
  std::string mMessage;
  float mProgress; // 0-100
};

} // namespace ui
} // namespace synaptic

