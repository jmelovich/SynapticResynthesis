#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "UITheme.h"

namespace synaptic {
namespace ui {
// Alias to shorten iplug::igraphics in this header
namespace ig = iplug::igraphics;

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

inline const ig::IVStyle kTabSwitchStyle = ig::IVStyle{
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
  2.f,
  4.f,
  0.70f,
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


