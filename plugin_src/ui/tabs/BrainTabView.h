#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "../layout/UILayout.h"
#include "../core/SynapticUI.h"

namespace synaptic {
namespace ui {
namespace tabs {

namespace ig = iplug::igraphics; // alias for brevity in headers

void BuildBrainTab(SynapticUI& ui, const ig::IRECT& bounds, const UILayout& layout, float startY);

} // namespace tabs
} // namespace ui
} // namespace synaptic


