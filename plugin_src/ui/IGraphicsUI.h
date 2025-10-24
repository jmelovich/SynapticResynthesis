#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "IconsForkAwesome.h"

// Minimal IGraphics UI for SynapticResynthesis
// Provides a basic title label and a button to bootstrap the native UI.
//
// NOTE: This must be called from within the plugin's constructor where
// mMakeGraphicsFunc and mLayoutFunc are accessible as protected members.

using namespace iplug;
using namespace igraphics;

namespace synaptic {

// Layout function to be used by mLayoutFunc
inline void BuildIGraphicsLayout(IGraphics* pGraphics)
{
#if IPLUG_EDITOR
  if (!pGraphics) return;

  // Load fonts
  pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
  pGraphics->LoadFont("ForkAwesome", FORK_AWESOME_FN);

  pGraphics->EnableMouseOver(true);
  pGraphics->EnableMultiTouch(true);
  pGraphics->AttachCornerResizer(EUIResizerMode::Scale, true);
  pGraphics->AttachTextEntryControl();

  const IRECT b = pGraphics->GetBounds();
  const IVStyle style;

  // Title label - centered at top
  const IText titleText { 24.f, COLOR_WHITE };
  pGraphics->AttachControl(new ITextControl(b.GetFromTop(40.f), "SynapticResynthesis (IGraphics)", titleText));

  // AGC Checkbox Control
  // (this is just to show how easily a control can change a param)
  // kAGC is an integer defined in enum that points to param ID, so this control automatically updates the param when toggled
  const IRECT agcRow = b.GetGridCell(0, 1, 1, 6).GetCentredInside(300.f, 40.f);
  const IText labelText { 18.f, COLOR_WHITE, "Roboto-Regular" };
  const IText checkboxIconText { 24.f, COLOR_WHITE, "ForkAwesome" };

  // Label for AGC
  pGraphics->AttachControl(new ITextControl(agcRow.GetFromLeft(200.f), "AGC (Auto Gain Control)", labelText));

  // Checkbox icon control (opaque BG to ensure redraw when toggling off)
  pGraphics->AttachControl(new ITextToggleControl(agcRow.GetFromRight(40.f), kAGC, ICON_FK_SQUARE_O, ICON_FK_CHECK_SQUARE, checkboxIconText, COLOR_BLACK));

  // Simple button (no-op action for now)
  pGraphics->AttachControl(new IVButtonControl(b.GetFromTRHC(150.f, 40.f).GetPadded(-5.f), [](IControl* pCaller){ SplashClickActionFunc(pCaller); }, "Test Button", style));
#endif
}

}


