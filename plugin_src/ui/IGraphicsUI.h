/**
 * @file IGraphicsUI.h
 * @brief Entry point for IGraphics-based UI initialization
 *
 * Responsibilities:
 * - Provides BuildIGraphicsLayout() function called by the plugin to create the UI
 * - Manages the global SynapticUI instance lifecycle
 * - Provides accessor functions for other plugin components to access the UI
 *
 * This is the bridge between the plugin's IGraphics system and our custom UI implementation.
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"

#if IPLUG_EDITOR
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
  using namespace ui;
  if (!pGraphics) return;

  // Always recreate UI instance on layout call (happens on UI open)
  // This ensures we don't have stale control pointers
  g_SynapticUI.reset();
  g_SynapticUI = std::make_unique<SynapticUI>(pGraphics);
  g_SynapticUI->build();
}

inline ui::SynapticUI* GetSynapticUI()
{
  return g_SynapticUI.get();
}

inline void ResetSynapticUI()
{
  g_SynapticUI.reset();
}

} // namespace synaptic

#endif // IPLUG_EDITOR


