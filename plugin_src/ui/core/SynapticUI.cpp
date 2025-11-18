/**
 * @file SynapticUI.cpp
 * @brief Implementation of the main UI coordinator for Synaptic Resynthesis
 *
 * Implements the core UI management logic including:
 * - Initial UI construction and rebuild operations
 * - Dynamic parameter control creation and positioning
 * - Card panel resizing and layout adjustment
 * - Control visibility management for tab switching
 * - Window resize-to-fit calculations
 */

#include "SynapticUI.h"
#include "../tabs/TabViews.h"
#include "../styles/UITheme.h"
#include "../controls/BrainFileListControl.h"

#include "SynapticResynthesis.h"
#include "plugin_src/transformers/BaseTransformer.h"
#include "plugin_src/morph/IMorph.h"
#include "plugin_src/params/ParameterManager.h"
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

  // Configure number of columns from compile-time config
  mNumColumns = UI_NUM_COLUMNS > 0 ? UI_NUM_COLUMNS : 1;

  IRECT bounds = mGraphics->GetBounds();
  mLayout = UILayout::Calculate(bounds);

  // If using multiple columns, ensure the window is wide enough
  if (mNumColumns > 1)
  {
    const int targetWidth = (UI_BASE_COLUMN_WIDTH * mNumColumns) + (UI_COLUMN_GAP * (mNumColumns - 1));
    if (mGraphics->Width() != targetWidth)
    {
      mGraphics->Resize(targetWidth, mGraphics->Height(), mGraphics->GetDrawScale(), true);
      bounds = mGraphics->GetBounds();
      mLayout = UILayout::Calculate(bounds);
    }
  }

  // One-time feature setup
  mGraphics->SetLayoutOnResize(false); // Disable auto-resize to prevent parameter reset
  mGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
  mGraphics->LoadFont("ForkAwesome", FORK_AWESOME_FN);
  mGraphics->EnableMouseOver(true);
  mGraphics->EnableTooltips(true);
  mGraphics->AttachTextEntryControl();
  
  // Attach background panel and keep reference for resizing
  mBackgroundPanel = new IPanelControl(bounds, kBGDark);
  mGraphics->AttachControl(mBackgroundPanel);
  mGraphics->GetControl(0)->SetDelegate(*mGraphics->GetDelegate()); // Ensure it's at the bottom

  float yPos = mLayout.padding;

  // Header
  buildHeader(bounds);

  IRECT headerRow = GetHeaderRowBounds(bounds, mLayout);
  yPos = headerRow.B + mLayout.sectionGap;

  // Tabs
  tabs::BuildDSPTab(*this, bounds, mLayout, yPos);
  tabs::BuildBrainTab(*this, bounds, mLayout, yPos);

  setActiveTab(Tab::DSP);

  // Progress overlay (attached last to be on top, initially hidden)
  // Don't use attach() helper - manage this control completely independently
  mProgressOverlay = new ProgressOverlay(bounds);
  mGraphics->AttachControl(mProgressOverlay);
#endif
}

void SynapticUI::rebuild()
{
#if IPLUG_EDITOR
  if (!mGraphics) return;

  // Re-apply column configuration (compile-time for now)
  mNumColumns = UI_NUM_COLUMNS > 0 ? UI_NUM_COLUMNS : 1;

  IRECT bounds = mGraphics->GetBounds();
  mLayout = UILayout::Calculate(bounds);

  // Ensure width for multi-column mode
  if (mNumColumns > 1)
  {
    const int targetWidth = (UI_BASE_COLUMN_WIDTH * mNumColumns) + (UI_COLUMN_GAP * (mNumColumns - 1));
    if (mGraphics->Width() != targetWidth)
    {
      mGraphics->Resize(targetWidth, mGraphics->Height(), mGraphics->GetDrawScale(), true);
      bounds = mGraphics->GetBounds();
      mLayout = UILayout::Calculate(bounds);
    }
  }

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
  mBrainDropControl = nullptr;
  mCreateNewBrainButton = nullptr;
  mProgressOverlay = nullptr;
  mTransformerCardPanel = nullptr;
  mMorphCardPanel = nullptr;
  mAudioProcessingCardPanel = nullptr;
  mBackgroundPanel = nullptr;
  mGraphics->RemoveAllControls();

  // Attach background panel and keep reference for resizing
  mBackgroundPanel = new IPanelControl(bounds, kBGDark);
  mGraphics->AttachControl(mBackgroundPanel);
  mGraphics->GetControl(0)->SetDelegate(*mGraphics->GetDelegate()); // Ensure it's at the bottom

  float yPos = mLayout.padding;

  // Header
  buildHeader(bounds);
  IRECT headerRow = GetHeaderRowBounds(bounds, mLayout);
  yPos = headerRow.B + mLayout.sectionGap;

  // Tabs
  tabs::BuildDSPTab(*this, bounds, mLayout, yPos);
  tabs::BuildBrainTab(*this, bounds, mLayout, yPos);

  setActiveTab(previousTab);

  // Sync all static controls with their current parameter values
  if (mRebuildContext.plugin)
  {
    for (auto* ctrl : mDSPControls)
    {
      SyncControlWithParam(ctrl, mRebuildContext.plugin);
    }
  }

  // Rebuild dynamic params using cached context if available
  if (mRebuildContext.transformer && mRebuildContext.paramManager && mRebuildContext.plugin)
  {
    rebuildDynamicParams(DynamicParamType::Transformer, mRebuildContext.transformer.get(), *mRebuildContext.paramManager, mRebuildContext.plugin);
  }
  if (mRebuildContext.morph && mRebuildContext.paramManager && mRebuildContext.plugin)
  {
    rebuildDynamicParams(DynamicParamType::Morph, mRebuildContext.morph.get(), *mRebuildContext.paramManager, mRebuildContext.plugin);
  }

  // Progress overlay (attached ABSOLUTELY LAST to be on top of everything, initially hidden)
  // Don't use attach() helper - manage this control completely independently
  mProgressOverlay = new ProgressOverlay(bounds);
  mGraphics->AttachControl(mProgressOverlay);

  // Resize window to fit content
  resizeWindowToFitContent();
#endif
}

void SynapticUI::setActiveTab(Tab tab)
{
  if (!mGraphics) return; // Safety check

  mCurrentTab = tab;

  // Set visibility for each control group
  SetControlGroupVisibility(mDSPControls, tab == Tab::DSP);
  SetControlGroupVisibility(mBrainControls, tab == Tab::Brain);

  // Update tab buttons
  if (mDSPTabButton) mDSPTabButton->SetActive(tab == Tab::DSP);
  if (mBrainTabButton) mBrainTabButton->SetActive(tab == Tab::Brain);

  // Update Create New Brain button visibility based on current tab and brain state
  // Button should only be visible when: (1) no brain is loaded AND (2) Brain tab is active
  if (mCreateNewBrainButton)
  {
    bool shouldHide = mHasBrainLoaded || (tab != Tab::Brain);
    mCreateNewBrainButton->Hide(shouldHide);
    mCreateNewBrainButton->SetDisabled(shouldHide);
  }

  // Auto-resize window to fit content when switching tabs
  resizeWindowToFitContent();
}

void SynapticUI::SetControlGroupVisibility(std::vector<IControl*>& controls, bool visible)
{
  for (auto* ctrl : controls)
  {
    if (ctrl)
    {
      ctrl->Hide(!visible);
      ctrl->SetDisabled(!visible);
    }
  }
}

void SynapticUI::SyncControlWithParam(IControl* ctrl, Plugin* plugin)
{
  if (!ctrl || !plugin) return;

  const int paramIdx = ctrl->GetParamIdx();
  if (paramIdx > kNoParameter)
  {
    if (const IParam* pParam = plugin->GetParam(paramIdx))
    {
      ctrl->SetValueFromDelegate(pParam->GetNormalized());
    }
  }
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
      auto* attached = attach(ctrl, ControlGroup::DSP);
      paramControls.push_back(attached);

      // Sync control with current parameter value
      SyncControlWithParam(attached, plugin);
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


void SynapticUI::rebuildDynamicParams(
  DynamicParamType type,
  const void* owner,
  const synaptic::ParameterManager& paramManager,
  Plugin* plugin)
{
#if IPLUG_EDITOR
  if (!mGraphics || !owner || !plugin)
    return;

  // Note: mRebuildContext is set via setDynamicParamContext with shared_ptr copies
  // to prevent race conditions. We don't update it here with raw pointers.

  // Select appropriate control list and bounds
  std::vector<IControl*>& paramControls = (type == DynamicParamType::Transformer)
    ? mTransformerParamControls : mMorphParamControls;
  const IRECT& bounds = (type == DynamicParamType::Transformer)
    ? mTransformerParamBounds : mMorphParamBounds;
  IControl* cardPanel = (type == DynamicParamType::Transformer)
    ? mTransformerCardPanel : mMorphCardPanel;

  // Remove old controls
  RemoveAndClearControls(paramControls, mDSPControls);

  // Build new controls
  std::vector<IControl*> newControls;
  if (type == DynamicParamType::Transformer)
  {
    newControls = mDynamicParamMgr.BuildTransformerParams(
      mGraphics, bounds, mLayout,
      static_cast<const synaptic::IChunkBufferTransformer*>(owner),
      paramManager, plugin);
  }
  else // Morph
  {
    newControls = mDynamicParamMgr.BuildMorphParams(
      mGraphics, bounds, mLayout,
      static_cast<const synaptic::IMorph*>(owner),
      paramManager, plugin);
  }

  // Attach and sync new controls
  AttachAndSyncControls(newControls, paramControls, mDSPControls, plugin);

  // Ensure progress overlay stays on top after adding dynamic params
  EnsureOverlayOnTop();

  // Resize card and reposition subsequent cards
  float heightDelta = ResizeCardToFitContent(cardPanel, bounds, paramControls, 120.f);
  if (heightDelta != 0.f)
  {
    repositionSubsequentCards(cardPanel, heightDelta);
    if (type == DynamicParamType::Transformer || type == DynamicParamType::Morph)
    {
      anchorMorphLayoutToCard(); // Re-anchor morph layout
    }
  }

  // Force full UI redraw to clear old card outline
  mGraphics->SetAllControlsDirty();
#endif
}

IControl* SynapticUI::attach(IControl* ctrl, ControlGroup group)
{
  IControl* added = mGraphics->AttachControl(ctrl);
  if (!added) return nullptr;

  // Add to appropriate control group and set visibility
  switch (group)
  {
    case ControlGroup::DSP:
      mDSPControls.push_back(added);
      if (mCurrentTab != Tab::DSP) {
        added->Hide(true);
        added->SetDisabled(true);
      }
      break;

    case ControlGroup::Brain:
      mBrainControls.push_back(added);
      if (mCurrentTab != Tab::Brain) {
        added->Hide(true);
        added->SetDisabled(true);
      }
      break;

    case ControlGroup::Global:
      // Global controls are always visible
      break;
  }

  return added;
}

void SynapticUI::repositionSubsequentCards(IControl* startCard, float heightDelta)
{
#if IPLUG_EDITOR
  if (!mGraphics || !startCard || heightDelta == 0.f) return;

  const IRECT startRect = startCard->GetRECT();
  const float startY = startRect.B;

  // Reposition all cards and controls below the start card
  for (auto* ctrl : mDSPControls)
  {
    if (ctrl && ctrl != startCard)
    {
      IRECT ctrlBounds = ctrl->GetRECT();
      // Move only controls in the same column (horizontal overlap within the start card's bounds),
      // and that are positioned below the start card.
      const bool sameColumn =
        (ctrlBounds.L >= startRect.L - 1.f) &&
        (ctrlBounds.R <= startRect.R + 1.f);
      if (sameColumn && ctrlBounds.T >= startY - 10.f) // 10px tolerance for positioning
      {
        ctrlBounds.T += heightDelta;
        ctrlBounds.B += heightDelta;
        ctrl->SetTargetAndDrawRECTs(ctrlBounds);
      }
    }
  }
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

  // Always update background panel and progress overlay bounds to ensure they cover the entire window
  // (regardless of whether we resized or not, in case previous resizes were missed)
  const IRECT currentBounds = mGraphics->GetBounds();
  
  if (mBackgroundPanel)
  {
    mBackgroundPanel->SetTargetAndDrawRECTs(currentBounds);
  }
  
  if (mProgressOverlay)
  {
    mProgressOverlay->UpdateBounds(currentBounds);
  }
#endif
}

void SynapticUI::buildHeader(const IRECT& bounds)
{
  IRECT headerRow = GetHeaderRowBounds(bounds, mLayout);

  IRECT titleRect = GetTitleBounds(headerRow);
  attach(new ITextControl(titleRect, "Synaptic Resynthesis", kTitleText), ControlGroup::Global);

  IRECT dspTabRect = GetDSPTabBounds(headerRow);
  IRECT brainTabRect = GetBrainTabBounds(headerRow);

  mDSPTabButton = new TabButton(dspTabRect, "DSP", [this]() { setActiveTab(Tab::DSP); });
  attach(mDSPTabButton, ControlGroup::Global);

  mBrainTabButton = new TabButton(brainTabRect, "Brain", [this]() { setActiveTab(Tab::Brain); });
  attach(mBrainTabButton, ControlGroup::Global);
}

void SynapticUI::setBrainFileListControl(BrainFileListControl* ctrl)
{
  mBrainFileListControl = ctrl;
}

void SynapticUI::setBrainStatusControl(BrainStatusControl* ctrl)
{
  mBrainStatusControl = ctrl;
}

void SynapticUI::setBrainDropControl(BrainFileDropControl* ctrl)
{
  mBrainDropControl = ctrl;
}

void SynapticUI::setCreateNewBrainButton(IControl* ctrl)
{
  mCreateNewBrainButton = ctrl;
}

void SynapticUI::setCompactModeToggle(IVToggleControl* ctrl)
{
  mCompactModeToggle = ctrl;
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

void SynapticUI::updateBrainState(bool useExternal, const std::string& externalPath)
{
#if IPLUG_EDITOR
  // Store state for tab switching and button visibility (single source of truth)
  mHasBrainLoaded = useExternal;

  // Update brain storage status display
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

  // Update file list control
  if (mBrainFileListControl)
  {
    mBrainFileListControl->SetHasExternalBrain(useExternal);
    mBrainFileListControl->SetDisabled(!useExternal);
    mBrainFileListControl->SetBlend(IBlend(EBlend::Default, useExternal ? 1.0f : 0.3f));
    mBrainFileListControl->SetDirty(true);
  }

  // Update drop control
  if (mBrainDropControl)
  {
    mBrainDropControl->SetHasExternalBrain(useExternal);
    mBrainDropControl->SetDisabled(!useExternal);
    mBrainDropControl->SetBlend(IBlend(EBlend::Default, useExternal ? 1.0f : 0.3f));
    mBrainDropControl->SetDirty(true);
  }

  // Show/hide "Create New Brain" button
  // Button should only be visible when: (1) no brain is loaded AND (2) Brain tab is active
  if (mCreateNewBrainButton)
  {
    bool shouldHide = useExternal || (mCurrentTab != Tab::Brain);
    mCreateNewBrainButton->Hide(shouldHide);
    mCreateNewBrainButton->SetDisabled(shouldHide);
  }
#endif
}

void SynapticUI::ShowProgressOverlay(const std::string& title, const std::string& message, float progress, bool showCancelButton)
{
#if IPLUG_EDITOR
  if (mProgressOverlay)
  {
    mProgressOverlay->Show(title, message, progress, showCancelButton);
  }
#endif
}

void SynapticUI::UpdateProgressOverlay(const std::string& message, float progress)
{
#if IPLUG_EDITOR
  if (mProgressOverlay)
  {
    mProgressOverlay->UpdateProgress(message, progress);
  }
#endif
}

void SynapticUI::HideProgressOverlay()
{
#if IPLUG_EDITOR
  if (mProgressOverlay)
  {
    mProgressOverlay->Hide();
  }
#endif
}

void SynapticUI::EnsureOverlayOnTop()
{
#if IPLUG_EDITOR
  if (!mGraphics || !mProgressOverlay) return;

  // Store current state
  bool wasVisible = mProgressOverlay->IsVisible();
  std::string title = mProgressOverlay->GetTitle();
  std::string message = mProgressOverlay->GetMessage();
  float progress = mProgressOverlay->GetProgress();

  // Remove and re-attach overlay to ensure it's last (on top)
  mGraphics->RemoveControl(mProgressOverlay);
  mProgressOverlay = new ProgressOverlay(mGraphics->GetBounds());
  mGraphics->AttachControl(mProgressOverlay);

  // Restore state if it was visible
  if (wasVisible)
  {
    mProgressOverlay->Show(title, message, progress);
  }
#endif
}

} // namespace ui
} // namespace synaptic



