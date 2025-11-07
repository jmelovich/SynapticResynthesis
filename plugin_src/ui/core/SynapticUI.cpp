#include "SynapticUI.h"
#include "../tabs/DSPTabView.h"
#include "../tabs/BrainTabView.h"
#include "../styles/UITheme.h"

#include "../../SynapticResynthesis.h"

using namespace iplug;
using namespace igraphics;

namespace synaptic {
namespace ui {

SynapticUI::SynapticUI(IGraphics* graphics)
: mGraphics(graphics)
{}

void SynapticUI::build()
{
#if IPLUG_EDITOR
  if (!mGraphics) return;

  const IRECT bounds = mGraphics->GetBounds();
  mLayout = UILayout::Calculate(bounds);

  // One-time feature setup
  mGraphics->SetLayoutOnResize(true);
  mGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
  mGraphics->LoadFont("ForkAwesome", FORK_AWESOME_FN);
  mGraphics->EnableMouseOver(true);
  mGraphics->EnableTooltips(true);
  mGraphics->AttachTextEntryControl();
  mGraphics->AttachPanelBackground(kBGDark);

  float yPos = mLayout.padding;

  // Header
  buildHeader(bounds);

  IRECT headerRow = GetHeaderRowBounds(bounds, mLayout);
  yPos = headerRow.B + mLayout.sectionGap;

  // Tabs
  buildDSP(bounds, yPos);
  buildBrain(bounds, yPos);

  setActiveTab(Tab::DSP);
#endif
}

void SynapticUI::rebuild()
{
#if IPLUG_EDITOR
  if (!mGraphics) return;

  const IRECT bounds = mGraphics->GetBounds();
  mLayout = UILayout::Calculate(bounds);

  Tab previousTab = mCurrentTab;

  // Clear state and controls
  mDSPControls.clear();
  mBrainControls.clear();
  mDSPTabButton = nullptr;
  mBrainTabButton = nullptr;
  mGraphics->RemoveAllControls();

  // Background
  mGraphics->AttachPanelBackground(kBGDark);

  float yPos = mLayout.padding;

  // Header
  buildHeader(bounds);
  IRECT headerRow = GetHeaderRowBounds(bounds, mLayout);
  yPos = headerRow.B + mLayout.sectionGap;

  // Tabs
  buildDSP(bounds, yPos);
  buildBrain(bounds, yPos);

  setActiveTab(previousTab);
#endif
}

void SynapticUI::setActiveTab(Tab tab)
{
  mCurrentTab = tab;

  for (size_t i = 0; i < mDSPControls.size(); ++i)
  {
    if (mDSPControls[i]) {
      mDSPControls[i]->Hide(tab != Tab::DSP);
      mDSPControls[i]->SetDisabled(tab != Tab::DSP);
    }
  }

  for (size_t i = 0; i < mBrainControls.size(); ++i)
  {
    if (mBrainControls[i]) {
      mBrainControls[i]->Hide(tab != Tab::Brain);
      mBrainControls[i]->SetDisabled(tab != Tab::Brain);
    }
  }

  if (mDSPTabButton) mDSPTabButton->SetActive(tab == Tab::DSP);
  if (mBrainTabButton) mBrainTabButton->SetActive(tab == Tab::Brain);
}

void SynapticUI::onAlgorithmChanged(int /*algoId*/)
{
  // Placeholder for dynamic param panel refresh
}

IControl* SynapticUI::attachGlobal(IControl* ctrl)
{
  return mGraphics->AttachControl(ctrl);
}

IControl* SynapticUI::attachDSP(IControl* ctrl)
{
  IControl* added = mGraphics->AttachControl(ctrl);
  if (added)
  {
    mDSPControls.push_back(added);
    if (mCurrentTab != Tab::DSP) {
      added->Hide(true);
      added->SetDisabled(true);
    }
  }
  return added;
}

IControl* SynapticUI::attachBrain(IControl* ctrl)
{
  IControl* added = mGraphics->AttachControl(ctrl);
  if (added)
  {
    mBrainControls.push_back(added);
    if (mCurrentTab != Tab::Brain) {
      added->Hide(true);
      added->SetDisabled(true);
    }
  }
  return added;
}

void SynapticUI::clearControls()
{
  mDSPControls.clear();
  mBrainControls.clear();
  mDSPTabButton = nullptr;
  mBrainTabButton = nullptr;
}

void SynapticUI::buildHeader(const IRECT& bounds)
{
  IRECT headerRow = GetHeaderRowBounds(bounds, mLayout);

  IRECT titleRect = GetTitleBounds(headerRow);
  attachGlobal(new ITextControl(titleRect, "Synaptic Resynthesis", kTitleText));

  IRECT dspTabRect = GetDSPTabBounds(headerRow);
  IRECT brainTabRect = GetBrainTabBounds(headerRow);

  mDSPTabButton = new TabButton(dspTabRect, "DSP", [this]() { setActiveTab(Tab::DSP); });
  attachGlobal(mDSPTabButton);

  mBrainTabButton = new TabButton(brainTabRect, "Brain", [this]() { setActiveTab(Tab::Brain); });
  attachGlobal(mBrainTabButton);
}

void SynapticUI::buildDSP(const IRECT& bounds, float startY)
{
  tabs::BuildDSPTab(*this, bounds, mLayout, startY);
}

void SynapticUI::buildBrain(const IRECT& bounds, float startY)
{
  tabs::BuildBrainTab(*this, bounds, mLayout, startY);
}

} // namespace ui
} // namespace synaptic


