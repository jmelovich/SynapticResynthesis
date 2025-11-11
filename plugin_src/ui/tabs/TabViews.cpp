/**
 * @file TabViews.cpp
 * @brief Implementation of tab content layout and control creation
 *
 * Contains the detailed layout logic for:
 * - DSP Tab: Chunk size, analysis window, transformer selection, morph selection,
 *   output window, overlap controls, AGC toggle, and gain knobs
 * - Brain Tab: File drop zone, file list, brain management buttons
 *   (import, export, reset, detach)
 */

#include "TabViews.h"
#include "../styles/UIStyles.h"
#include "../controls/UIControls.h"
#include "../controls/DeferredNumberBoxControl.h"
#include "../styles/UITheme.h"
#include "../layout/UILayout.h"
#include "../SynapticResynthesis.h"

using namespace iplug;
using namespace igraphics;
using namespace synaptic::ui;

namespace synaptic {
namespace ui {
namespace tabs {

void BuildDSPTab(SynapticUI& ui, const IRECT& bounds, const UILayout& layout, float startY)
{
  float yPos = startY;

  // BRAIN ANALYSIS CARD
  float analysisCardHeight = 165.f;
  IRECT analysisCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + analysisCardHeight);
  ui.attach(new CardPanel(analysisCard, "BRAIN ANALYSIS"), ControlGroup::DSP);

  IRECT warnRect = analysisCard.GetPadded(-layout.cardPadding).GetFromTop(34.f).GetTranslated(0.f, 28.f);
  ui.attach(new WarningBox(warnRect, "Changing these settings triggers Brain reanalysis"), ControlGroup::DSP);

  float rowY = warnRect.B + 14.f;
  float labelWidth = 180.f;
  float controlWidth = 200.f;

  // Chunk Size - Using deferred control to prevent triggering rechunking during drag
  IRECT chunkSizeRow = IRECT(analysisCard.L + layout.cardPadding, rowY, analysisCard.R - layout.cardPadding, rowY + layout.controlHeight);
  ui.attach(new ITextControl(chunkSizeRow.GetFromLeft(labelWidth), "Chunk Size", kLabelText), ControlGroup::DSP);
  ui.attach(new DeferredNumberBoxControl(
    chunkSizeRow.GetFromLeft(controlWidth).GetTranslated(labelWidth + 8.f, 0.f),
    kChunkSize,
    nullptr,
    "",
    kSynapticStyle
  ), ControlGroup::DSP);

  rowY += layout.controlHeight + 10.f;

  // Analysis Window
  IRECT analysisWindowRow = IRECT(analysisCard.L + layout.cardPadding, rowY, analysisCard.R - layout.cardPadding, rowY + layout.controlHeight);
  ui.attach(new ITextControl(analysisWindowRow.GetFromLeft(labelWidth), "Analysis Window", kLabelText), ControlGroup::DSP);
  float tabSwitchWidth = controlWidth + 80.f;
  ui.attach(new IVTabSwitchControl(
    analysisWindowRow.GetFromLeft(tabSwitchWidth).GetTranslated(labelWidth + 12.f, 0.f),
    kAnalysisWindow,
    {"Hann", "Hamming", "Blackman", "Rect"},
    "",
    kSynapticStyle,
    EVShape::Rectangle,
    EDirection::Horizontal
  ), ControlGroup::DSP);

  yPos = analysisCard.B + layout.sectionGap;

  // TRANSFORMER CARD (with space for dynamic params)
  // Base height for dropdown, will be expanded by dynamic params if needed
  float transformerCardBaseHeight = 120.f;
  float transformerCardMaxHeight = 450.f; // Max space for dynamic params
  IRECT transformerCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + transformerCardMaxHeight);
  auto* transformerCardPanel = new CardPanel(transformerCard, "TRANSFORMER");
  ui.attach(transformerCardPanel, ControlGroup::DSP);
  ui.mTransformerCardPanel = transformerCardPanel; // Store reference for resizing

  rowY = transformerCard.T + layout.cardPadding + 24.f;
  float dropdownHeight = 48.f;
  float dropdownWidth = transformerCard.W() * 0.5f;
  float dropdownStartX = transformerCard.L + (transformerCard.W() - dropdownWidth) / 2.f;
  IRECT transformerRow = IRECT(dropdownStartX, rowY, dropdownStartX + dropdownWidth, rowY + dropdownHeight);
  ui.attach(new IVMenuButtonControl(
    transformerRow,
    kAlgorithm,
    "Algorithm",
    kSynapticStyle
  ), ControlGroup::DSP);

  // Reserve space for dynamic transformer parameters below dropdown
  IRECT transformerParamBounds = IRECT(
    transformerCard.L + layout.cardPadding,
    transformerRow.B + 16.f,
    transformerCard.R - layout.cardPadding,
    transformerCard.B - layout.cardPadding
  );
  ui.setTransformerParamBounds(transformerParamBounds);

  yPos = transformerCard.B + layout.sectionGap;

  // MORPH CARD (with space for dynamic params)
  // Base height for dropdown, will be expanded by dynamic params if needed
  float morphCardBaseHeight = 120.f;
  float morphCardMaxHeight = 450.f; // Max space for dynamic params
  IRECT morphCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + morphCardMaxHeight);
  auto* morphCardPanel = new CardPanel(morphCard, "MORPH");
  ui.attach(morphCardPanel, ControlGroup::DSP);
  ui.mMorphCardPanel = morphCardPanel; // Store reference for resizing

  rowY = morphCard.T + layout.cardPadding + 24.f;
  float morphDropdownHeight = 48.f;
  float morphDropdownWidth = morphCard.W() * 0.5f;
  float morphDropdownStartX = morphCard.L + (morphCard.W() - morphDropdownWidth) / 2.f;
  IRECT morphRow = IRECT(morphDropdownStartX, rowY, morphDropdownStartX + morphDropdownWidth, rowY + morphDropdownHeight);
  ui.attach(new IVMenuButtonControl(
    morphRow,
    kMorphMode,
    "Morph Mode",
    kSynapticStyle
  ), ControlGroup::DSP);

  // Reserve space for dynamic morph parameters below dropdown
  IRECT morphParamBounds = IRECT(
    morphCard.L + layout.cardPadding,
    morphRow.B + 16.f,
    morphCard.R - layout.cardPadding,
    morphCard.B - layout.cardPadding
  );
  ui.setMorphParamBounds(morphParamBounds);

  yPos = morphCard.B + layout.sectionGap;

  // AUDIO PROCESSING CARD
  float audioCardHeight = 225.f;
  IRECT audioCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + audioCardHeight);
  auto* audioCardPanel = new CardPanel(audioCard, "AUDIO PROCESSING");
  ui.attach(audioCardPanel, ControlGroup::DSP);
  ui.mAudioProcessingCardPanel = audioCardPanel; // Store reference for repositioning

  rowY = audioCard.T + layout.cardPadding + 28.f;

  // Output Window
  IRECT outputWindowRow = IRECT(audioCard.L + layout.cardPadding, rowY, audioCard.R - layout.cardPadding, rowY + layout.controlHeight);
  ui.attach(new ITextControl(outputWindowRow.GetFromLeft(180.f), "Output Window", kLabelText), ControlGroup::DSP);
  float outputTabSwitchWidth = 200.f + 80.f;
  ui.attach(new IVTabSwitchControl(
    outputWindowRow.GetFromLeft(outputTabSwitchWidth).GetTranslated(180.f + 12.f, 0.f),
    kOutputWindow,
    {"Hann", "Hamming", "Blackman", "Rect"},
    "",
    kSynapticStyle,
    EVShape::Rectangle,
    EDirection::Horizontal
  ), ControlGroup::DSP);

  rowY += layout.controlHeight + 12.f;

  // Toggles
  float toggleHeight = 48.f;
  float toggleWidth = 180.f;
  float toggleGap = 24.f;
  float toggleGroupWidth = (toggleWidth * 2) + toggleGap;
  float toggleStartX = audioCard.L + (audioCard.W() - toggleGroupWidth) / 2.f;

  IRECT overlapRect = IRECT(toggleStartX, rowY, toggleStartX + toggleWidth, rowY + toggleHeight);
  ui.attach(new IVToggleControl(
    overlapRect,
    kEnableOverlap,
    "Overlap-Add",
    kSynapticStyle,
    "OFF",
    "ON"
  ), ControlGroup::DSP);

  IRECT agcRect = IRECT(toggleStartX + toggleWidth + toggleGap, rowY, toggleStartX + toggleWidth + toggleGap + toggleWidth, rowY + toggleHeight);
  ui.attach(new IVToggleControl(
    agcRect,
    kAGC,
    "AGC",
    kSynapticStyle,
    "OFF",
    "ON"
  ), ControlGroup::DSP);

  rowY += layout.controlHeight + 22.f;

  // Gain knobs
  float knobSize = 75.f;
  float knobSpacing = 160.f;
  float knobAreaWidth = (knobSize * 2) + knobSpacing;
  float knobStartX = (bounds.W() - knobAreaWidth) / 2.f;
  float knobY = rowY;

  IRECT inGainRect = IRECT(knobStartX, knobY, knobStartX + knobSize, knobY + knobSize);
  ui.attach(new IVKnobControl(inGainRect, kInGain, "Input Gain", kSynapticStyle), ControlGroup::DSP);

  IRECT outGainRect = IRECT(knobStartX + knobSize + knobSpacing, knobY, knobStartX + knobSize + knobSpacing + knobSize, knobY + knobSize);
  ui.attach(new IVKnobControl(outGainRect, kOutGain, "Output Gain", kSynapticStyle), ControlGroup::DSP);
}

void BuildBrainTab(SynapticUI& ui, const IRECT& bounds, const UILayout& layout, float startY)
{
  float yPos = startY;

  // SAMPLE LIBRARY CARD
  float libraryCardHeight = 510.f; // 50% taller (340 * 1.5 = 510)
  IRECT libraryCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + libraryCardHeight);
  ui.attach(new CardPanel(libraryCard, "SAMPLE LIBRARY"), ControlGroup::Brain);

  // File drop zone
  IRECT dropArea = libraryCard.GetPadded(-layout.cardPadding).GetFromTop(100.f).GetTranslated(0.f, 28.f);
  auto* dropControl = new BrainFileDropControl(dropArea);
  dropControl->SetDisabled(true); // Start disabled until brain is loaded
  dropControl->SetBlend(IBlend(EBlend::Default, 0.3f)); // Start grayed out
  ui.attach(dropControl, ControlGroup::Brain);
  ui.setBrainDropControl(dropControl); // Store reference for state updates

  // Status line
  IRECT statusRect = IRECT(libraryCard.L + layout.cardPadding, dropArea.B + 8.f, libraryCard.R - layout.cardPadding, dropArea.B + 24.f);
  auto* statusControl = new BrainStatusControl(statusRect);
  ui.attach(statusControl, ControlGroup::Brain);
  ui.setBrainStatusControl(statusControl); // Store reference for updates

  // File list
  IRECT fileListRect = IRECT(
    libraryCard.L + layout.cardPadding,
    statusRect.B + 8.f,
    libraryCard.R - layout.cardPadding,
    libraryCard.B - layout.cardPadding
  );
  auto* fileList = new BrainFileListControl(fileListRect);
  fileList->SetDisabled(true); // Start disabled until brain is loaded
  fileList->SetBlend(IBlend(EBlend::Default, 0.3f)); // Start grayed out
  ui.attach(fileList, ControlGroup::Brain);
  ui.setBrainFileListControl(fileList); // Store reference for updates

  // "Create New Brain" button - centered on drop area, shown only when no brain is loaded
  float createBtnWidth = 220.f;
  float createBtnHeight = 50.f;
  IRECT createBtnRect = IRECT(
    dropArea.MW() - createBtnWidth / 2.f,
    dropArea.MH() - createBtnHeight / 2.f,
    dropArea.MW() + createBtnWidth / 2.f,
    dropArea.MH() + createBtnHeight / 2.f
  );
  auto* createButton = new IVButtonControl(createBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainCreateNew, kNoTag, 0, nullptr);
    }
  }, "Create New Brain", kButtonStyle);
  ui.attach(createButton, ControlGroup::Brain);
  ui.setCreateNewBrainButton(createButton); // Store reference for state updates

  yPos = libraryCard.B + layout.sectionGap;

  // MANAGEMENT CARD
  float managementCardHeight = 160.f;
  IRECT managementCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + managementCardHeight);
  ui.attach(new CardPanel(managementCard, "BRAIN MANAGEMENT"), ControlGroup::Brain);

  float btnWidth = 200.f;
  float btnHeight = 45.f;
  float btnGapH = 20.f;
  float btnGapV = 16.f;
  float btnGridWidth = (btnWidth * 2) + btnGapH;
  float btnStartX = managementCard.L + (managementCard.W() - btnGridWidth) / 2.f;
  float btnY = managementCard.T + layout.cardPadding + 32.f;

  IRECT importBtnRect = IRECT(btnStartX, btnY, btnStartX + btnWidth, btnY + btnHeight);
  ui.attach(new IVButtonControl(importBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainImport, kNoTag, 0, nullptr);
    }
  }, "Import Brain", kButtonStyle), ControlGroup::Brain);

  IRECT exportBtnRect = IRECT(btnStartX + btnWidth + btnGapH, btnY, btnStartX + btnWidth + btnGapH + btnWidth, btnY + btnHeight);
  ui.attach(new IVButtonControl(exportBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainExport, kNoTag, 0, nullptr);
    }
  }, "Export Brain", kButtonStyle), ControlGroup::Brain);

  btnY += btnHeight + btnGapV;

  IRECT resetBtnRect = IRECT(btnStartX, btnY, btnStartX + btnWidth, btnY + btnHeight);
  ui.attach(new IVButtonControl(resetBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainReset, kNoTag, 0, nullptr);
    }
  }, "Reset Brain", kButtonStyle), ControlGroup::Brain);
}

} // namespace tabs
} // namespace ui
} // namespace synaptic

