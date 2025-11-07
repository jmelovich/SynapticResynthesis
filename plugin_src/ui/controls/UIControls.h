#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include <functional>
#include "../styles/UITheme.h"

namespace synaptic {
namespace ui {
// Local alias to shorten iplug::igraphics qualifiers in headers
namespace ig = iplug::igraphics;

class CardPanel : public ig::IControl
{
public:
  CardPanel(const ig::IRECT& bounds, const char* title = nullptr);
  void Draw(ig::IGraphics& g) override;
private:
  const char* mTitle;
};

class WarningBox : public ig::IControl
{
public:
  WarningBox(const ig::IRECT& bounds, const char* text);
  void Draw(ig::IGraphics& g) override;
private:
  const char* mText;
};

class TabButton : public ig::IControl
{
public:
  TabButton(const ig::IRECT& bounds, const char* label, std::function<void()> onClick);
  void Draw(ig::IGraphics& g) override;
  void OnMouseDown(float x, float y, const ig::IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const ig::IMouseMod& mod) override;
  void OnMouseOut() override;
  void SetActive(bool active);
private:
  const char* mLabel;
  std::function<void()> mOnClick;
  bool mIsActive;
  bool mIsHovered;
};

class SectionLabel : public ig::IControl
{
public:
  SectionLabel(const ig::IRECT& bounds, const char* text);
  void Draw(ig::IGraphics& g) override;
private:
  const char* mText;
};

} // namespace ui
} // namespace synaptic


