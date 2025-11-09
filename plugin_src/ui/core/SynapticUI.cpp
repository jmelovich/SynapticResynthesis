#include "SynapticUI.h"
#include "../tabs/DSPTabView.h"
#include "../tabs/BrainTabView.h"
#include "../styles/UITheme.h"
#include "../controls/BrainFileListControl.h"

#include "../../SynapticResynthesis.h"
#include "plugin_src/ChunkBufferTransformer.h"
#include "plugin_src/morph/IMorph.h"
#include "plugin_src/modules/ParameterManager.h"
#include "config.h"

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
  mGraphics->SetLayoutOnResize(false); // Disable auto-resize to prevent parameter reset
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
  mTransformerParamControls.clear(); // Clear before RemoveAllControls to avoid double-removal
  mMorphParamControls.clear(); // Clear before RemoveAllControls to avoid double-removal
  mDSPTabButton = nullptr;
  mBrainTabButton = nullptr;
  mBrainFileListControl = nullptr;
  mBrainStatusControl = nullptr;
  mTransformerCardPanel = nullptr;
  mMorphCardPanel = nullptr;
  mAudioProcessingCardPanel = nullptr;
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

  // Sync all static controls with their current parameter values
  if (mRebuildContext.plugin)
  {
    for (auto* ctrl : mDSPControls)
    {
      if (ctrl)
      {
        const int paramIdx = ctrl->GetParamIdx();
        if (paramIdx > kNoParameter)
        {
          if (const IParam* pParam = mRebuildContext.plugin->GetParam(paramIdx))
          {
            ctrl->SetValueFromDelegate(pParam->GetNormalized());
          }
        }
      }
    }
  }

  // Rebuild dynamic params using cached context if available
  if (mRebuildContext.transformer && mRebuildContext.paramManager && mRebuildContext.plugin)
  {
    rebuildTransformerParams(mRebuildContext.transformer, *mRebuildContext.paramManager, mRebuildContext.plugin);
  }
  if (mRebuildContext.morph && mRebuildContext.paramManager && mRebuildContext.plugin)
  {
    rebuildMorphParams(mRebuildContext.morph, *mRebuildContext.paramManager, mRebuildContext.plugin);
  }

  // Resize window to fit content
  resizeWindowToFitContent();
#endif
}

void SynapticUI::setActiveTab(Tab tab)
{
  if (!mGraphics) return; // Safety check

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

  // Auto-resize window to fit content when switching tabs
  resizeWindowToFitContent();
}

void SynapticUI::rebuildTransformerParams(
  const synaptic::IChunkBufferTransformer* transformer,
  const synaptic::ParameterManager& paramManager,
  Plugin* plugin)
{
#if IPLUG_EDITOR
  if (!mGraphics || !transformer || !plugin)
    return;

  // Cache context for future rebuilds
  mRebuildContext.transformer = transformer;
  mRebuildContext.paramManager = &paramManager;
  mRebuildContext.plugin = plugin;

  // Remove old controls
  RemoveAndClearControls(mTransformerParamControls, mDSPControls);

  // Build new controls
  auto newControls = mDynamicParamMgr.BuildTransformerParams(
    mGraphics, mTransformerParamBounds, mLayout, transformer, paramManager, plugin);

  // Attach and sync new controls
  AttachAndSyncControls(newControls, mTransformerParamControls, mDSPControls, plugin);

  // Resize card and reposition subsequent cards
  float heightDelta = ResizeCardToFitContent(mTransformerCardPanel, mTransformerParamBounds,
                                              mTransformerParamControls, 120.f);
  if (heightDelta != 0.f)
  {
    repositionCardsAfterTransformer(heightDelta);
  }

  // Force full UI redraw to clear old card outline
  mGraphics->SetAllControlsDirty();
#endif
}

void SynapticUI::RemoveAndClearControls(std::vector<IControl*>& paramControls, std::vector<IControl*>& dspControls)
{
#if IPLUG_EDITOR
  if (!paramControls.empty())
  {
    for (auto* ctrl : paramControls)
    {
      if (ctrl)
      {
        mGraphics->RemoveControl(ctrl);
        // Also remove from dspControls
        auto it = std::find(dspControls.begin(), dspControls.end(), ctrl);
        if (it != dspControls.end())
          dspControls.erase(it);
      }
    }
    paramControls.clear();
  }
#endif
}

void SynapticUI::AttachAndSyncControls(std::vector<IControl*>& newControls, std::vector<IControl*>& paramControls,
                                       std::vector<IControl*>& dspControls, Plugin* plugin)
{
#if IPLUG_EDITOR
  for (auto* ctrl : newControls)
  {
    if (ctrl)
    {
      auto* attached = attachDSP(ctrl);
      paramControls.push_back(attached);

      // Sync control with current parameter value
      const int paramIdx = attached->GetParamIdx();
      if (paramIdx > kNoParameter && plugin)
      {
        if (const IParam* pParam = plugin->GetParam(paramIdx))
        {
          attached->SetValueFromDelegate(pParam->GetNormalized());
        }
      }
    }
  }
#endif
}

float SynapticUI::ResizeCardToFitContent(IControl* cardPanel, const IRECT& paramBounds,
                                         const std::vector<IControl*>& controls, float minHeight)
{
#if IPLUG_EDITOR
  if (!cardPanel)
    return 0.f;

  // Calculate required height
  float paramHeight = 0.f;
  if (!controls.empty())
  {
    for (auto* ctrl : controls)
    {
      if (ctrl)
      {
        float ctrlBottom = ctrl->GetRECT().B;
        if (ctrlBottom > paramBounds.T)
          paramHeight = std::max(paramHeight, ctrlBottom - paramBounds.T);
      }
    }
  }

  float cardPadding = 16.f;
  float totalHeight = (paramBounds.T - cardPanel->GetRECT().T) + paramHeight + cardPadding;
  totalHeight = std::max(totalHeight, minHeight);

  IRECT oldBounds = cardPanel->GetRECT();
  IRECT newBounds = oldBounds;
  newBounds.B = newBounds.T + totalHeight;

  if (newBounds != oldBounds)
  {
    cardPanel->SetTargetAndDrawRECTs(newBounds);
    return newBounds.H() - oldBounds.H();
  }
#endif
  return 0.f;
}

void SynapticUI::rebuildMorphParams(
  const synaptic::IMorph* morph,
  const synaptic::ParameterManager& paramManager,
  Plugin* plugin)
{
#if IPLUG_EDITOR
  if (!mGraphics || !morph || !plugin)
    return;

  // Cache context for future rebuilds
  mRebuildContext.morph = morph;
  if (!mRebuildContext.paramManager) mRebuildContext.paramManager = &paramManager;
  if (!mRebuildContext.plugin) mRebuildContext.plugin = plugin;

  // Remove old controls
  RemoveAndClearControls(mMorphParamControls, mDSPControls);

  // Build new controls
  auto newControls = mDynamicParamMgr.BuildMorphParams(
    mGraphics, mMorphParamBounds, mLayout, morph, paramManager, plugin);

  // Attach and sync new controls
  AttachAndSyncControls(newControls, mMorphParamControls, mDSPControls, plugin);

  // Resize card and reposition subsequent cards
  float heightDelta = ResizeCardToFitContent(mMorphCardPanel, mMorphParamBounds,
                                              mMorphParamControls, 120.f);
  if (heightDelta != 0.f)
  {
    repositionCardsAfterMorph(heightDelta);
  }

  // Force full UI redraw to clear old card outline
  mGraphics->SetAllControlsDirty();
#endif
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

void SynapticUI::repositionSubsequentCards(IControl* startCard, float heightDelta)
{
#if IPLUG_EDITOR
  if (!mGraphics || !startCard || heightDelta == 0.f) return;

  const float startY = startCard->GetRECT().B;

  // Reposition all cards and controls below the start card
  for (auto* ctrl : mDSPControls)
  {
    if (ctrl && ctrl != startCard)
    {
      IRECT ctrlBounds = ctrl->GetRECT();
      if (ctrlBounds.T >= startY - 10.f) // 10px tolerance for positioning
      {
        ctrlBounds.T += heightDelta;
        ctrlBounds.B += heightDelta;
        ctrl->SetTargetAndDrawRECTs(ctrlBounds);
      }
    }
  }
#endif
}

void SynapticUI::repositionCardsAfterTransformer(float heightDelta)
{
#if IPLUG_EDITOR
  if (!mGraphics || !mTransformerCardPanel) return;

  repositionSubsequentCards(mTransformerCardPanel, heightDelta);
  anchorMorphLayoutToCard(); // Re-anchor morph dropdown and param bounds to morph card
#endif
}

void SynapticUI::repositionCardsAfterMorph(float heightDelta)
{
#if IPLUG_EDITOR
  if (!mGraphics || !mMorphCardPanel) return;

  repositionSubsequentCards(mMorphCardPanel, heightDelta);
  anchorMorphLayoutToCard(); // Re-anchor morph layout to card
#endif
}

void SynapticUI::anchorMorphLayoutToCard()
{
#if IPLUG_EDITOR
  if (!mGraphics || !mMorphCardPanel) return;

  const IRECT card = mMorphCardPanel->GetRECT();
  const float dropdownHeight = 48.f;
  const float dropdownWidth = card.W() * 0.5f;
  const float dropdownStartX = card.L + (card.W() - dropdownWidth) / 2.f;
  const float rowY = card.T + mLayout.cardPadding + 24.f;
  IRECT morphRow(dropdownStartX, rowY, dropdownStartX + dropdownWidth, rowY + dropdownHeight);

  // Position morph dropdown by param index
  for (auto* ctrl : mDSPControls)
  {
    if (ctrl && ctrl->GetParamIdx() == kMorphMode)
    {
      ctrl->SetTargetAndDrawRECTs(morphRow);
      break;
    }
  }

  // Set morph param area bounds directly from card and dropdown
  mMorphParamBounds = IRECT(
    card.L + mLayout.cardPadding,
    morphRow.B + 16.f,
    card.R - mLayout.cardPadding,
    card.B - mLayout.cardPadding
  );
#endif
}

void SynapticUI::resizeWindowToFitContent()
{
#if IPLUG_EDITOR
  if (!mGraphics) return;

  // Find the bottom-most control to determine required height
  // Only check controls for the ACTIVE tab
  float maxBottom = 0.f;

  if (mCurrentTab == Tab::DSP)
  {
    // Check DSP tab controls
    for (auto* ctrl : mDSPControls)
    {
      if (ctrl)
      {
        maxBottom = std::max(maxBottom, ctrl->GetRECT().B);
      }
    }
  }
  else if (mCurrentTab == Tab::Brain)
  {
    // Check Brain tab controls
    for (auto* ctrl : mBrainControls)
    {
      if (ctrl)
      {
        maxBottom = std::max(maxBottom, ctrl->GetRECT().B);
      }
    }
  }

  // Add bottom padding
  const float bottomPadding = mLayout.padding;
  const float requiredHeight = maxBottom + bottomPadding;

  // Get current size
  const int currentWidth = mGraphics->Width();
  const int currentHeight = mGraphics->Height();
  const float currentScale = mGraphics->GetDrawScale();

  // Only resize if height changed significantly (> 10 pixels)
  if (std::abs(requiredHeight - currentHeight) > 10.f)
  {
    mGraphics->Resize(currentWidth, static_cast<int>(requiredHeight), currentScale, true);
  }
#endif
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

void SynapticUI::setBrainFileListControl(BrainFileListControl* ctrl)
{
  mBrainFileListControl = ctrl;
}

void SynapticUI::setBrainStatusControl(BrainStatusControl* ctrl)
{
  mBrainStatusControl = ctrl;
}

void SynapticUI::updateBrainFileList(const std::vector<BrainFileEntry>& files)
{
#if IPLUG_EDITOR
  if (mBrainFileListControl)
  {
    mBrainFileListControl->UpdateList(files);
  }

  // Update file count in status line
  if (mBrainStatusControl)
  {
    mBrainStatusControl->SetFileCount((int)files.size());
  }
#endif
}

void SynapticUI::updateBrainStorage(bool useExternal, const std::string& externalPath)
{
#if IPLUG_EDITOR
  if (mBrainStatusControl)
  {
    if (useExternal && !externalPath.empty())
    {
      // Extract filename from full path for display
      size_t lastSlash = externalPath.find_last_of("/\\");
      std::string filename = (lastSlash != std::string::npos)
        ? externalPath.substr(lastSlash + 1)
        : externalPath;
      mBrainStatusControl->SetStorageMode(filename);
    }
    else
    {
      mBrainStatusControl->SetStorageMode("(inline)");
    }
  }
#endif
}

} // namespace ui
} // namespace synaptic



