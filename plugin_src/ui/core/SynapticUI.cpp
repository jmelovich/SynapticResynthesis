/**
 * @file SynapticUI.cpp
 * @brief Implementation of the main UI coordinator for Synaptic Resynthesis
 */

#include "SynapticUI.h"
#include "UIConstants.h"
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

  mNumColumns = UI_NUM_COLUMNS > 0 ? UI_NUM_COLUMNS : 1;

  IRECT bounds = mGraphics->GetBounds();
  mLayout = UILayout::Calculate(bounds);

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

  mGraphics->SetLayoutOnResize(false);
  mGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
  mGraphics->LoadFont("ForkAwesome", FORK_AWESOME_FN);
  mGraphics->EnableMouseOver(true);
  mGraphics->EnableTooltips(true);
  mGraphics->AttachTextEntryControl();

  mBackgroundPanel = new IPanelControl(bounds, kBGDark);
  mGraphics->AttachControl(mBackgroundPanel);
  mGraphics->GetControl(0)->SetDelegate(*mGraphics->GetDelegate());

  float yPos = mLayout.padding;

  buildHeader(bounds);

  IRECT headerRow = GetHeaderRowBounds(bounds, mLayout);
  yPos = headerRow.B + mLayout.sectionGap;

  tabs::BuildDSPTab(*this, bounds, mLayout, yPos);
  tabs::BuildBrainTab(*this, bounds, mLayout, yPos);

  setActiveTab(Tab::DSP);

  mProgressOverlay = new ProgressOverlay(bounds);
  mGraphics->AttachControl(mProgressOverlay);
#endif
}

void SynapticUI::rebuild()
{
#if IPLUG_EDITOR
  if (!mGraphics) return;

  mNumColumns = UI_NUM_COLUMNS > 0 ? UI_NUM_COLUMNS : 1;

  IRECT bounds = mGraphics->GetBounds();
  mLayout = UILayout::Calculate(bounds);

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

  mDSPControls.clear();
  mBrainControls.clear();
  mTransformerParamControls.clear();
  mMorphParamControls.clear();
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

  mBackgroundPanel = new IPanelControl(bounds, kBGDark);
  mGraphics->AttachControl(mBackgroundPanel);
  mGraphics->GetControl(0)->SetDelegate(*mGraphics->GetDelegate());

  float yPos = mLayout.padding;

  buildHeader(bounds);
  IRECT headerRow = GetHeaderRowBounds(bounds, mLayout);
  yPos = headerRow.B + mLayout.sectionGap;

  tabs::BuildDSPTab(*this, bounds, mLayout, yPos);
  tabs::BuildBrainTab(*this, bounds, mLayout, yPos);

  setActiveTab(previousTab);

  if (mRebuildContext.plugin)
  {
    for (auto* ctrl : mDSPControls)
    {
      SyncControlWithParam(ctrl, mRebuildContext.plugin);
    }
  }

  if (mRebuildContext.transformer && mRebuildContext.paramManager && mRebuildContext.plugin)
  {
    rebuildDynamicParams(DynamicParamType::Transformer, mRebuildContext.transformer.get(), *mRebuildContext.paramManager, mRebuildContext.plugin);
  }
  if (mRebuildContext.morph && mRebuildContext.paramManager && mRebuildContext.plugin)
  {
    rebuildDynamicParams(DynamicParamType::Morph, mRebuildContext.morph.get(), *mRebuildContext.paramManager, mRebuildContext.plugin);
  }

  mProgressOverlay = new ProgressOverlay(bounds);
  mGraphics->AttachControl(mProgressOverlay);

  resizeWindowToFitContent();
#endif
}

void SynapticUI::setActiveTab(Tab tab)
{
  if (!mGraphics) return;

  mCurrentTab = tab;

  SetControlGroupVisibility(mDSPControls, tab == Tab::DSP);
  SetControlGroupVisibility(mBrainControls, tab == Tab::Brain);

  if (mDSPTabButton) mDSPTabButton->SetActive(tab == Tab::DSP);
  if (mBrainTabButton) mBrainTabButton->SetActive(tab == Tab::Brain);

  if (mCreateNewBrainButton)
  {
    bool shouldHide = mHasBrainLoaded || (tab != Tab::Brain);
    mCreateNewBrainButton->Hide(shouldHide);
    mCreateNewBrainButton->SetDisabled(shouldHide);
  }

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
  if (!mGraphics || paramControls.empty())
  {
    paramControls.clear();
    return;
  }

  for (auto* ctrl : paramControls)
  {
    if (ctrl && mGraphics->GetControlIdx(ctrl) >= 0)
    {
      mGraphics->RemoveControl(ctrl);
      auto it = std::find(dspControls.begin(), dspControls.end(), ctrl);
      if (it != dspControls.end())
        dspControls.erase(it);
    }
  }
  paramControls.clear();
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

  float totalHeight = (paramBounds.T - cardPanel->GetRECT().T) + paramHeight + LayoutConstants::kCardPadding;
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

  std::vector<IControl*>& paramControls = (type == DynamicParamType::Transformer)
    ? mTransformerParamControls : mMorphParamControls;
  const IRECT& bounds = (type == DynamicParamType::Transformer)
    ? mTransformerParamBounds : mMorphParamBounds;
  IControl* cardPanel = (type == DynamicParamType::Transformer)
    ? mTransformerCardPanel : mMorphCardPanel;

  RemoveAndClearControls(paramControls, mDSPControls);

  std::vector<IControl*> newControls;
  if (type == DynamicParamType::Transformer)
  {
    newControls = mDynamicParamMgr.BuildTransformerParams(
      mGraphics, bounds, mLayout,
      static_cast<const synaptic::IChunkBufferTransformer*>(owner),
      paramManager, plugin);
  }
  else
  {
    newControls = mDynamicParamMgr.BuildMorphParams(
      mGraphics, bounds, mLayout,
      static_cast<const synaptic::IMorph*>(owner),
      paramManager, plugin);
  }

  AttachAndSyncControls(newControls, paramControls, mDSPControls, plugin);

  EnsureOverlayOnTop();

  float heightDelta = ResizeCardToFitContent(cardPanel, bounds, paramControls, LayoutConstants::kMinCardHeight);
  if (heightDelta != 0.f)
  {
    repositionSubsequentCards(cardPanel, heightDelta);
    if (type == DynamicParamType::Transformer || type == DynamicParamType::Morph)
    {
      anchorMorphLayoutToCard();
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

  for (auto* ctrl : mDSPControls)
  {
    if (ctrl && ctrl != startCard)
    {
      IRECT ctrlBounds = ctrl->GetRECT();
      const bool sameColumn =
        (ctrlBounds.L >= startRect.L - LayoutConstants::kColumnBoundsEpsilon) &&
        (ctrlBounds.R <= startRect.R + LayoutConstants::kColumnBoundsEpsilon);
      if (sameColumn && ctrlBounds.T >= startY - LayoutConstants::kVerticalPositionTolerance)
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
  const float dropdownHeight = LayoutConstants::kDropdownHeight;
  const float dropdownWidth = card.W() * LayoutConstants::kMorphDropdownWidthRatio;
  const float dropdownStartX = card.L + (card.W() - dropdownWidth) / 2.f;
  const float rowY = card.T + mLayout.cardPadding + 24.f;
  IRECT morphRow(dropdownStartX, rowY, dropdownStartX + dropdownWidth, rowY + dropdownHeight);

  for (auto* ctrl : mDSPControls)
  {
    if (ctrl && ctrl->GetParamIdx() == kMorphMode)
    {
      ctrl->SetTargetAndDrawRECTs(morphRow);
      break;
    }
  }

  mMorphParamBounds = IRECT(
    card.L + mLayout.cardPadding,
    morphRow.B + LayoutConstants::kDynamicParamSpacing,
    card.R - mLayout.cardPadding,
    card.B - mLayout.cardPadding
  );
#endif
}

void SynapticUI::resizeWindowToFitContent()
{
#if IPLUG_EDITOR
  if (!mGraphics) return;

  float maxBottom = 0.f;

  if (mCurrentTab == Tab::DSP)
  {
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
    for (auto* ctrl : mBrainControls)
    {
      if (ctrl)
      {
        maxBottom = std::max(maxBottom, ctrl->GetRECT().B);
      }
    }
  }

  const float bottomPadding = mLayout.padding;
  const float requiredHeight = maxBottom + bottomPadding;

  const int currentWidth = mGraphics->Width();
  const int currentHeight = mGraphics->Height();
  const float currentScale = mGraphics->GetDrawScale();

  if (std::abs(requiredHeight - currentHeight) > LayoutConstants::kResizeThreshold)
  {
    mGraphics->Resize(currentWidth, static_cast<int>(requiredHeight), currentScale, true);
  }

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

  if (mBrainStatusControl)
  {
    mBrainStatusControl->SetFileCount((int)files.size());
  }
#endif
}

void SynapticUI::updateBrainState(bool useExternal, const std::string& externalPath)
{
#if IPLUG_EDITOR
  mHasBrainLoaded = useExternal;

  if (mBrainStatusControl)
  {
    if (useExternal && !externalPath.empty())
    {
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

  if (mBrainFileListControl)
  {
    mBrainFileListControl->SetHasExternalBrain(useExternal);
    mBrainFileListControl->SetDisabled(!useExternal);
    mBrainFileListControl->SetBlend(IBlend(EBlend::Default, useExternal ? 1.0f : 0.3f));
    mBrainFileListControl->SetDirty(true);
  }

  if (mBrainDropControl)
  {
    mBrainDropControl->SetHasExternalBrain(useExternal);
    mBrainDropControl->SetDisabled(!useExternal);
    mBrainDropControl->SetBlend(IBlend(EBlend::Default, useExternal ? 1.0f : 0.3f));
    mBrainDropControl->SetDirty(true);
  }

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

  bool wasVisible = mProgressOverlay->IsVisible();
  std::string title = mProgressOverlay->GetTitle();
  std::string message = mProgressOverlay->GetMessage();
  float progress = mProgressOverlay->GetProgress();

  mGraphics->RemoveControl(mProgressOverlay);
  mProgressOverlay = new ProgressOverlay(mGraphics->GetBounds());
  mGraphics->AttachControl(mProgressOverlay);

  if (wasVisible)
  {
    mProgressOverlay->Show(title, message, progress);
  }
#endif
}

} // namespace ui
} // namespace synaptic
