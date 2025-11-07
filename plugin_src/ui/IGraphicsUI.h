#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include <memory>

#include "core/SynapticUI.h"

namespace synaptic {

inline void BuildIGraphicsLayout(iplug::igraphics::IGraphics* pGraphics)
{
#if IPLUG_EDITOR
  using namespace ui;
  static std::unique_ptr<SynapticUI> sUI;
  if (!pGraphics) return;
  if (!sUI) sUI = std::make_unique<SynapticUI>(pGraphics);
  if (pGraphics->NControls() > 0) sUI->rebuild();
  else sUI->build();
#endif
}

} // namespace synaptic


