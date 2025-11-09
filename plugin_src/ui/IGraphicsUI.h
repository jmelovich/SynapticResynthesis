#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include <memory>

#include "core/SynapticUI.h"

namespace synaptic {

// Forward declarations
class IChunkBufferTransformer;
struct IMorph;
class ParameterManager;

namespace {
  // Static UI instance
  static std::unique_ptr<ui::SynapticUI> g_SynapticUI;
}

inline void BuildIGraphicsLayout(iplug::igraphics::IGraphics* pGraphics)
{
#if IPLUG_EDITOR
  using namespace ui;
  if (!pGraphics) return;

  // Always recreate UI instance on layout call (happens on UI open)
  // This ensures we don't have stale control pointers
  g_SynapticUI.reset();
  g_SynapticUI = std::make_unique<SynapticUI>(pGraphics);
  g_SynapticUI->build();
#endif
}

inline ui::SynapticUI* GetSynapticUI()
{
#if IPLUG_EDITOR
  return g_SynapticUI.get();
#else
  return nullptr;
#endif
}

inline void ResetSynapticUI()
{
#if IPLUG_EDITOR
  g_SynapticUI.reset();
#endif
}

} // namespace synaptic


