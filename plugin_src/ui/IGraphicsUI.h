#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"

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

  // Load default font (Roboto-Regular is included with IPlug2)
  pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);

  pGraphics->EnableMouseOver(true);
  pGraphics->EnableMultiTouch(true);
  pGraphics->AttachCornerResizer(EUIResizerMode::Scale, true);
  pGraphics->AttachTextEntryControl();

  const IRECT b = pGraphics->GetBounds();

  // Title label
  const IText titleText { 24.f, COLOR_WHITE };
  pGraphics->AttachControl(new ITextControl(b.GetFromTop(40.f).GetMidHPadded(10.f), "SynapticResynthesis (IGraphics)", titleText));

  // Simple button (no-op action for now)
  const IVStyle style;
  pGraphics->AttachControl(new IVButtonControl(b.GetFromTRHC(150.f, 40.f).GetPadded(-5.f), [](IControl* pCaller){ SplashClickActionFunc(pCaller); }, "Test Button", style));
#endif
}

} // namespace synaptic


