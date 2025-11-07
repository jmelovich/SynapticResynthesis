#pragma once

#include <vector>
#include <memory>
#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"

#include "../styles/UITheme.h"
#include "../styles/UIStyles.h"
#include "../layout/UILayout.h"
#include "../controls/UIControls.h"

namespace synaptic {
namespace ui {

enum class Tab { DSP, Brain };

// Alias to shorten igraphics qualifiers in headers
namespace ig = iplug::igraphics;

class SynapticUI {
public:
  explicit SynapticUI(ig::IGraphics* graphics);

  void build();
  void rebuild();
  void setActiveTab(Tab tab);

  // Message hooks for future dynamic params
  void onAlgorithmChanged(int algoId);

  // Attachment helpers
  ig::IControl* attachGlobal(ig::IControl* ctrl);
  ig::IControl* attachDSP(ig::IControl* ctrl);
  ig::IControl* attachBrain(ig::IControl* ctrl);

  // Accessors
  ig::IGraphics* graphics() const { return mGraphics; }
  const UILayout& layout() const { return mLayout; }

private:
  void clearControls();
  void buildHeader(const ig::IRECT& bounds);
  void buildDSP(const ig::IRECT& bounds, float startY);
  void buildBrain(const ig::IRECT& bounds, float startY);

private:
  ig::IGraphics* mGraphics;
  UILayout mLayout;
  Tab mCurrentTab { Tab::DSP };
  std::vector<ig::IControl*> mDSPControls;
  std::vector<ig::IControl*> mBrainControls;
  TabButton* mDSPTabButton { nullptr };
  TabButton* mBrainTabButton { nullptr };
};

} // namespace ui
} // namespace synaptic


