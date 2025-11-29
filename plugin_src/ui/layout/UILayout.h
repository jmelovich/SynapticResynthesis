/**
 * @file UILayout.h
 * @brief Layout constants and geometry helper functions
 *
 * Defines UILayout struct with padding, spacing, and size constants.
 * Provides Calculate() function to compute layout from window bounds.
 */

#pragma once

#include "plugin_src/ui/core/UIConstants.h"

namespace synaptic {
namespace ui {

struct UILayout
{
  float width;
  float height;
  float padding;
  float cardPadding;
  float lineHeight;
  float sectionGap;
  float controlHeight;

  static UILayout Calculate(const ig::IRECT& bounds)
  {
    UILayout layout;
    layout.width = bounds.W();
    layout.height = bounds.H();
    layout.padding = 18.f;
    layout.cardPadding = LayoutConstants::kCardPadding;
    layout.lineHeight = 36.f;
    layout.sectionGap = 24.f;
    layout.controlHeight = 32.f;
    return layout;
  }

  ig::IRECT GetContentArea(const ig::IRECT& bounds) const
  {
    return bounds.GetPadded(-padding);
  }
};

// Header geometry helpers
inline ig::IRECT GetHeaderRowBounds(const ig::IRECT& bounds, const UILayout& layout)
{
  return ig::IRECT(layout.padding, layout.padding, bounds.W() - layout.padding, layout.padding + 40.f);
}

inline ig::IRECT GetTitleBounds(const ig::IRECT& headerRow)
{
  return headerRow.GetFromLeft(300.f);
}

inline ig::IRECT GetDSPTabBounds(const ig::IRECT& headerRow)
{
  return headerRow.GetFromRight(180.f).GetFromLeft(85.f);
}

inline ig::IRECT GetBrainTabBounds(const ig::IRECT& headerRow)
{
  return headerRow.GetFromRight(85.f);
}

inline ig::IRECT CenteredBox(const ig::IRECT& parent, float w, float h)
{
  float x = parent.L + (parent.W() - w) / 2.f;
  float y = parent.T + (parent.H() - h) / 2.f;
  return ig::IRECT(x, y, x + w, y + h);
}

} // namespace ui
} // namespace synaptic
