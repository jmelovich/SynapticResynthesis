/**
 * @file UIStyles.h
 * @brief IVStyle definitions for IPlug control styling
 *
 * Responsibilities:
 * - Defines kSynapticStyle: Standard style for most controls (knobs, sliders, switches, menus)
 * - Defines kButtonStyle: Style specifically for button controls
 *
 * These IVStyle objects configure the appearance of IPlug's built-in vector controls,
 * including colors, borders, text properties, and animation settings.
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "UITheme.h"

namespace synaptic {
namespace ui {
// Note: namespace ig alias is defined in UITheme.h

inline const ig::IVStyle kSynapticStyle = ig::IVStyle{
  true,
  true,
  {
	ig::IColor(0, 0, 0, 0),
    kControlBG,
    kControlBorder,
    kAccentBlue,
    kAccentBlue,
	  iplug::igraphics::IColor(0, 0, 0, 0),
    kTextPrimary,
    kControlBorder,
    kAccentBlue
  },
  {
    14.f,
    kTextPrimary,
    "Roboto-Regular",
	  ig::EAlign::Center,
	  ig::EVAlign::Bottom,
    0
  },
  {
    13.f,
    kTextPrimary,
    "Roboto-Regular",
	  ig::EAlign::Center,
	  ig::EVAlign::Middle,
    0
  },
  false,
  true,
  false,
  0,
  0.2f,
  3.f,
  4.f,
  1.f,
  0.f
};

inline const ig::IVStyle kButtonStyle = ig::IVStyle{
  true,
  true,
  {
	iplug::igraphics::IColor(0, 0, 0, 0),
    kControlBG,
    kControlBorder,
    kAccentBlue,
    kAccentBlue,
	  ig::IColor(0, 0, 0, 0),
    kTextPrimary,
    kControlBorder,
    kAccentBlue
  },
  {
    14.f,
    kTextPrimary,
    "Roboto-Regular",
	  ig::EAlign::Center,
	  ig::EVAlign::Middle,
    0
  },
  {
    13.f,
    kTextPrimary,
    "Roboto-Regular",
	  ig::EAlign::Center,
	  ig::EVAlign::Middle,
    0
  },
  false,
  true,
  false,
  0,
  0.2f,
  3.f,
  4.f,
  1.f,
  0.f
};

} // namespace ui
} // namespace synaptic


