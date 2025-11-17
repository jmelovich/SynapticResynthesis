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
#include "SynapticResynthesis.h"

using namespace iplug;
using namespace igraphics;
using namespace synaptic::ui;

namespace synaptic {
namespace ui {
namespace tabs {

void BuildDSPTab(SynapticUI& ui, const IRECT& bounds, const UILayout& layout, float startY)
{
  // Unified column layout system (works for both single and multi-column)
  const int numCols = std::max(1, ui.numColumns());
  const float contentLeft = layout.padding;
  const float contentRight = bounds.W() - layout.padding;
  const float contentWidth = contentRight - contentLeft;
  const float gap = layout.sectionGap;
  const float colWidth = (contentWidth - (numCols - 1) * gap) / numCols;
  auto columnRect = [&](int col, float top, float height) -> IRECT {
    const float left = contentLeft + col * (colWidth + gap);
    return IRECT(left, top, left + colWidth, top + height);
  };
  std::vector<float> colY((size_t) numCols, startY);
  auto nextCol = [&]() -> int {
    int best = 0;
    for (int i = 1; i < numCols; ++i)
      if (colY[i] < colY[best]) best = i;
    return best;
  };

  // TRANSFORMER CARD (with space for dynamic params)
  IRECT transformerCard;
  {
    const float transformerCardMaxHeight = 450.f; // Max space for dynamic params
    const int col = nextCol();
    transformerCard = columnRect(col, colY[col], transformerCardMaxHeight);
    auto* transformerCardPanel = new CardPanel(transformerCard, "TRANSFORMER");
    ui.attach(transformerCardPanel, ControlGroup::DSP);
    ui.mTransformerCardPanel = transformerCardPanel; // Store reference for resizing

    float rowY = transformerCard.T + layout.cardPadding + 24.f;
    const float dropdownHeight = 48.f;
    const float dropdownWidth = transformerCard.W() * 0.5f;
    const float dropdownStartX = transformerCard.L + (transformerCard.W() - dropdownWidth) / 2.f;
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

    colY[col] = transformerCard.B + layout.sectionGap;
  }

  // AUTOTUNE CARD
  {
    const float cardH = 210.f;
    const int col = nextCol();
    IRECT autotuneCard = columnRect(col, colY[col], cardH);
    auto* autotuneCardPanel = new CardPanel(autotuneCard, "AUTOTUNE");
    ui.attach(autotuneCardPanel, ControlGroup::DSP);

    float rowY = autotuneCard.T + layout.cardPadding + 24.f;

    // Autotune Blend slider
    const float sliderWidth = 280.f;
    const float sliderHeight = 40.f;
    const float sliderStartX = autotuneCard.L + (autotuneCard.W() - sliderWidth) / 2.f;
    IRECT autotuneRect = IRECT(sliderStartX, rowY, sliderStartX + sliderWidth, rowY + sliderHeight);
    ui.attach(new IVSliderControl(autotuneRect, kAutotuneBlend, "Autotune Blend", kSynapticStyle, true, EDirection::Horizontal), ControlGroup::DSP);

    rowY += sliderHeight + 18.f;

    // Autotune Mode (FFT Peak / HPS)
    IRECT modeRow = IRECT(autotuneCard.L + layout.cardPadding, rowY, autotuneCard.R - layout.cardPadding, rowY + layout.controlHeight);
    ui.attach(new ITextControl(modeRow.GetFromLeft(180.f), "Autotune Mode", kLabelText), ControlGroup::DSP);
    const float modeSwitchWidth = 220.f;
    ui.attach(new IVTabSwitchControl(
      modeRow.GetFromLeft(modeSwitchWidth).GetTranslated(180.f + 12.f, 0.f),
      kAutotuneMode,
      {"FFT Peak", "HPS"},
      "",
      kSynapticStyle,
      EVShape::Rectangle,
      EDirection::Horizontal
    ), ControlGroup::DSP);

    rowY += layout.controlHeight + 12.f;

    // Autotune Range (Octaves)
    IRECT tolRow = IRECT(autotuneCard.L + layout.cardPadding, rowY, autotuneCard.R - layout.cardPadding, rowY + layout.controlHeight);
    ui.attach(new ITextControl(tolRow.GetFromLeft(180.f), "Autotune Range (Octaves)", kLabelText), ControlGroup::DSP);
    const float tolSwitchWidth = 220.f;
    ui.attach(new IVTabSwitchControl(
      tolRow.GetFromLeft(tolSwitchWidth).GetTranslated(180.f + 12.f, 0.f),
      kAutotuneToleranceOctaves,
      {"1", "2", "3", "4", "5"},
      "",
      kSynapticStyle,
      EVShape::Rectangle,
      EDirection::Horizontal
    ), ControlGroup::DSP);

    colY[col] = autotuneCard.B + layout.sectionGap;
  }

  // MORPH CARD (with space for dynamic params)
  {
    const float morphCardMaxHeight = 450.f; // Max space for dynamic params
    const int col = nextCol();
    IRECT morphCard = columnRect(col, colY[col], morphCardMaxHeight);
    auto* morphCardPanel = new CardPanel(morphCard, "MORPH");
    ui.attach(morphCardPanel, ControlGroup::DSP);
    ui.mMorphCardPanel = morphCardPanel; // Store reference for resizing

    float rowY = morphCard.T + layout.cardPadding + 24.f;
    const float morphDropdownHeight = 48.f;
    const float morphDropdownWidth = morphCard.W() * 0.5f;
    const float morphDropdownStartX = morphCard.L + (morphCard.W() - morphDropdownWidth) / 2.f;
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

    colY[col] = morphCard.B + layout.sectionGap;
  }

  // AUDIO PROCESSING CARD
  {
    const float cardH = 225.f;
    const int col = nextCol();
    IRECT audioCard = columnRect(col, colY[col], cardH);
    auto* audioCardPanel = new CardPanel(audioCard, "AUDIO PROCESSING");
    ui.attach(audioCardPanel, ControlGroup::DSP);
    ui.mAudioProcessingCardPanel = audioCardPanel; // Store reference for repositioning

    float rowY = audioCard.T + layout.cardPadding + 28.f;

    // Output Window
    IRECT outputWindowRow = IRECT(audioCard.L + layout.cardPadding, rowY, audioCard.R - layout.cardPadding, rowY + layout.controlHeight);
    ui.attach(new ITextControl(outputWindowRow.GetFromLeft(180.f), "Output Window", kLabelText), ControlGroup::DSP);
    const float outputTabSwitchWidth = 200.f + 80.f;
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
    const float toggleHeight = 48.f;
    const float toggleWidth = 180.f;
    const float toggleGap = 24.f;
    const float toggleGroupWidth = (toggleWidth * 2) + toggleGap;
    const float toggleStartX = audioCard.L + (audioCard.W() - toggleGroupWidth) / 2.f;

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
    const float knobSize = 75.f;
    const float knobSpacing = 160.f;
    const float knobAreaWidth = (knobSize * 2) + knobSpacing;
    const float knobStartX = audioCard.L + (audioCard.W() - knobAreaWidth) / 2.f;
    const float knobY = rowY;

    IRECT inGainRect = IRECT(knobStartX, knobY, knobStartX + knobSize, knobY + knobSize);
    ui.attach(new IVKnobControl(inGainRect, kInGain, "Input Gain", kSynapticStyle), ControlGroup::DSP);

    IRECT outGainRect = IRECT(knobStartX + knobSize + knobSpacing, knobY, knobStartX + knobSize + knobSpacing + knobSize, knobY + knobSize);
    ui.attach(new IVKnobControl(outGainRect, kOutGain, "Output Gain", kSynapticStyle), ControlGroup::DSP);

    colY[col] = audioCard.B + layout.sectionGap;
  }
}

void BuildBrainTab(SynapticUI& ui, const IRECT& bounds, const UILayout& layout, float startY)
{
  // Unified column layout system (works for both single and multi-column)
  const int numCols = std::max(1, ui.numColumns());
  const float contentLeft = layout.padding;
  const float contentRight = bounds.W() - layout.padding;
  const float contentWidth = contentRight - contentLeft;
  const float gap = layout.sectionGap;
  const float colWidth = (contentWidth - (numCols - 1) * gap) / numCols;
  auto columnRect = [&](int col, float top, float height) -> IRECT {
    const float left = contentLeft + col * (colWidth + gap);
    return IRECT(left, top, left + colWidth, top + height);
  };
  std::vector<float> colY((size_t) numCols, startY);
  auto nextCol = [&]() -> int {
    int best = 0;
    for (int i = 1; i < numCols; ++i)
      if (colY[i] < colY[best]) best = i;
    return best;
  };

  // SAMPLE LIBRARY CARD
  IRECT libraryCard;
  {
    const float libraryCardHeight = 510.f; // 50% taller (340 * 1.5 = 510)
    const int col = nextCol();
    libraryCard = columnRect(col, colY[col], libraryCardHeight);
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
    const float createBtnWidth = 220.f;
    const float createBtnHeight = 50.f;
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

    colY[col] = libraryCard.B + layout.sectionGap;
  }

  // BRAIN ANALYSIS CARD
  {
    const float cardH = 165.f;
    const int col = nextCol();
    IRECT analysisCard = columnRect(col, colY[col], cardH);
    ui.attach(new CardPanel(analysisCard, "BRAIN ANALYSIS"), ControlGroup::Brain);

    IRECT warnRect = analysisCard.GetPadded(-layout.cardPadding).GetFromTop(34.f).GetTranslated(0.f, 28.f);
    ui.attach(new WarningBox(warnRect, "Changing these settings triggers Brain reanalysis"), ControlGroup::Brain);

    float rowY = warnRect.B + 14.f;
    float labelWidth = 180.f;
    float controlWidth = 200.f;

    // Chunk Size - Using deferred control to prevent triggering rechunking during drag
    IRECT chunkSizeRow = IRECT(analysisCard.L + layout.cardPadding, rowY, analysisCard.R - layout.cardPadding, rowY + layout.controlHeight);
    ui.attach(new ITextControl(chunkSizeRow.GetFromLeft(labelWidth), "Chunk Size", kLabelText), ControlGroup::Brain);
    ui.attach(new DeferredNumberBoxControl(
      chunkSizeRow.GetFromLeft(controlWidth).GetTranslated(labelWidth + 8.f, 0.f),
      kChunkSize,
      nullptr,
      "",
      kSynapticStyle
    ), ControlGroup::Brain);

    rowY += layout.controlHeight + 10.f;

    // Analysis Window
    IRECT analysisWindowRow = IRECT(analysisCard.L + layout.cardPadding, rowY, analysisCard.R - layout.cardPadding, rowY + layout.controlHeight);
    ui.attach(new ITextControl(analysisWindowRow.GetFromLeft(labelWidth), "Analysis Window", kLabelText), ControlGroup::Brain);
    float tabSwitchWidth = controlWidth + 80.f;
    ui.attach(new IVTabSwitchControl(
      analysisWindowRow.GetFromLeft(tabSwitchWidth).GetTranslated(labelWidth + 12.f, 0.f),
      kAnalysisWindow,
      {"Hann", "Hamming", "Blackman", "Rect"},
      "",
      kSynapticStyle,
      EVShape::Rectangle,
      EDirection::Horizontal
    ), ControlGroup::Brain);

    colY[col] = analysisCard.B + layout.sectionGap;
  }

  // MANAGEMENT CARD
  {
    const float managementCardHeight = 220.f;
    const int col = nextCol();
    IRECT managementCard = columnRect(col, colY[col], managementCardHeight);
    ui.attach(new CardPanel(managementCard, "BRAIN MANAGEMENT"), ControlGroup::Brain);

    const float btnWidth = 200.f;
    const float btnHeight = 45.f;
    const float btnGapH = 20.f;
    const float btnGapV = 16.f;
    const float btnGridWidth = (btnWidth * 2) + btnGapH;
    const float btnStartX = managementCard.L + (managementCard.W() - btnGridWidth) / 2.f;
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

    btnY += btnHeight + btnGapV + 4.f;

    // Compact Mode toggle
    const float toggleWidth = 200.f;
    const float toggleHeight = 40.f;
    const float toggleStartX = managementCard.L + (managementCard.W() - toggleWidth) / 2.f;
    IRECT compactToggleRect = IRECT(toggleStartX, btnY, toggleStartX + toggleWidth, btnY + toggleHeight);
    auto* compactToggle = new IVToggleControl(
      compactToggleRect,
      [](IControl* pCaller) {
        auto* pToggle = dynamic_cast<IVToggleControl*>(pCaller);
        if (pToggle) {
          bool isCompact = pToggle->GetValue() > 0.5;
          int value = isCompact ? 1 : 0;
          auto* pGraphics = pCaller->GetUI();
          auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
          if (pDelegate) {
            pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainSetCompactMode, value, 0, nullptr);
          }
        }
      },
      "Compact Mode",
      kSynapticStyle,
      "OFF",
      "ON"
    );
    ui.attach(compactToggle, ControlGroup::Brain);
    ui.setCompactModeToggle(compactToggle);

    colY[col] = managementCard.B + layout.sectionGap;
  }
}

} // namespace tabs
} // namespace ui
} // namespace synaptic

