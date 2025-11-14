/**
 * @file TabViews.h
 * @brief Tab content builders for DSP and Brain views
 *
 * Responsibilities:
 * - BuildDSPTab(): Creates all controls for the DSP processing tab
 *   (Brain Analysis, Transformer, Morph, Audio Processing cards)
 * - BuildBrainTab(): Creates all controls for the Brain management tab
 *   (Sample Library, Brain Management cards)
 *
 * These functions are called by SynapticUI during initial build and rebuild operations.
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "../layout/UILayout.h"
#include "../core/SynapticUI.h"

namespace synaptic {
namespace ui {
namespace tabs {
// Note: namespace ig alias is defined in UITheme.h

void BuildDSPTab(SynapticUI& ui, const iplug::igraphics::IRECT& bounds, const UILayout& layout, float startY);
void BuildBrainTab(SynapticUI& ui, const iplug::igraphics::IRECT& bounds, const UILayout& layout, float startY);

} // namespace tabs
} // namespace ui
} // namespace synaptic

