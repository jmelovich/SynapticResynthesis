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
    auto* algoControl = new IVMenuButtonControl(
      transformerRow,
      kAlgorithm,
      "Algorithm",
      kSynapticStyle
    );
    algoControl->SetTooltip("Select the algorithm used to transform audio chunks (typically by replacing chunks from Brain, like Samplebrain transformers.)");
    ui.attach(algoControl, ControlGroup::DSP);

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
    const float cardH = 190.f;
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
    auto* autotuneSlider = new IVSliderControl(autotuneRect, kAutotuneBlend, "Autotune Blend", kSynapticStyle, true, EDirection::Horizontal);
    autotuneSlider->SetTooltip("Blends between unpitched & pitched transformer-output chunks. The source chunks are analyzed for pitch, and the transformed chunks are repitched to match.");
    ui.attach(autotuneSlider, ControlGroup::DSP);

    rowY += sliderHeight + 18.f;

    // Autotune Mode (FFT Peak / HPS)
    IRECT modeRow = IRECT(autotuneCard.L + layout.cardPadding, rowY, autotuneCard.R - layout.cardPadding, rowY + layout.controlHeight);
    ui.attach(new ITextControl(modeRow.GetFromLeft(180.f), "Autotune Mode", kLabelText), ControlGroup::DSP);
    const float modeSwitchWidth = 220.f;
    auto* autotuneModeSwitch = new IVTabSwitchControl(
      modeRow.GetFromLeft(modeSwitchWidth).GetTranslated(180.f + 12.f, 0.f),
      kAutotuneMode,
      {"FFT Peak", "HPS"},
      "",
      kSynapticStyle,
      EVShape::Rectangle,
      EDirection::Horizontal
    );
    autotuneModeSwitch->SetTooltip("Choose pitch detection algorithm: FFT Peak (faster) or HPS (fundamental frequency detection, can be more accurate for complex tones)");
    ui.attach(autotuneModeSwitch, ControlGroup::DSP);

    rowY += layout.controlHeight + 12.f;

    // Autotune Range (Octaves)
    IRECT tolRow = IRECT(autotuneCard.L + layout.cardPadding, rowY, autotuneCard.R - layout.cardPadding, rowY + layout.controlHeight);
    ui.attach(new ITextControl(tolRow.GetFromLeft(180.f), "Autotune Range (Octaves)", kLabelText), ControlGroup::DSP);
    const float tolSwitchWidth = 220.f;
    auto* autotuneRangeSwitch = new IVTabSwitchControl(
      tolRow.GetFromLeft(tolSwitchWidth).GetTranslated(180.f + 12.f, 0.f),
      kAutotuneToleranceOctaves,
      {"1", "2", "3", "4", "5"},
      "",
      kSynapticStyle,
      EVShape::Rectangle,
      EDirection::Horizontal
    );
    autotuneRangeSwitch->SetTooltip("Maximum pitch shift range in octaves. Higher values allow larger pitch corrections but may be less stable");
    ui.attach(autotuneRangeSwitch, ControlGroup::DSP);

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
    auto* morphControl = new IVMenuButtonControl(
      morphRow,
      kMorphMode,
      "Morph Mode",
      kSynapticStyle
    );
    morphControl->SetTooltip("Select a spectral blend method, for blending between transformed chunks and source chunks.");
    ui.attach(morphControl, ControlGroup::DSP);

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

    // Output Window with lock
    IRECT outputWindowRow = IRECT(audioCard.L + layout.cardPadding, rowY, audioCard.R - layout.cardPadding, rowY + layout.controlHeight);
    const float labelWidth = 180.f;
    const float lockSize = 24.f;
    const float lockGap = 8.f;

    ui.attach(new ITextControl(outputWindowRow.GetFromLeft(labelWidth), "Output Window", kLabelText), ControlGroup::DSP);

    // Tab switch centered in remaining space (ignore label for centering calculation)
    const float outputTabSwitchWidth = 200.f + 80.f;
    const float availableWidth = outputWindowRow.W();
    const float tabSwitchStartX = audioCard.L + layout.cardPadding + (availableWidth - outputTabSwitchWidth) * 0.5f;

    IRECT tabSwitchRect = IRECT(
      tabSwitchStartX,
      rowY,
      tabSwitchStartX + outputTabSwitchWidth,
      rowY + layout.controlHeight
    );
    auto* outputWindowSwitch = new IVTabSwitchControl(
      tabSwitchRect,
      kOutputWindow,
      {"Hann", "Hamming", "Blackman", "Rect"},
      "",
      kSynapticStyle,
      EVShape::Rectangle,
      EDirection::Horizontal
    );
    outputWindowSwitch->SetTooltip("Window function applied to output audio chunks. Affects smoothness and frequency response. Typically you'd want this to match the analysis window (when spectral processing active, this control is overriden to match analysis window)");
    ui.attach(outputWindowSwitch, ControlGroup::DSP);

    // Lock button positioned to the left of tab switch
    IRECT lockButtonRect = IRECT(
      tabSwitchStartX - lockSize - lockGap,
      rowY + (layout.controlHeight - lockSize) * 0.5f,
      tabSwitchStartX - lockGap,
      rowY + (layout.controlHeight - lockSize) * 0.5f + lockSize
    );
    auto* outputLockButton = new LockButtonControl(lockButtonRect, kWindowLock, kOutputWindow);
    outputLockButton->SetTooltip("Lock/unlock synchronization between Output Window and Analysis Window");
    ui.attach(outputLockButton, ControlGroup::DSP);

    rowY += layout.controlHeight + 12.f;

    // Toggles
    const float toggleHeight = 48.f;
    const float toggleWidth = 180.f;
    const float toggleGap = 24.f;
    const float toggleGroupWidth = (toggleWidth * 2) + toggleGap;
    const float toggleStartX = audioCard.L + (audioCard.W() - toggleGroupWidth) / 2.f;

    IRECT overlapRect = IRECT(toggleStartX, rowY, toggleStartX + toggleWidth, rowY + toggleHeight);
    auto* overlapToggle = new IVToggleControl(
      overlapRect,
      kEnableOverlap,
      "Overlap-Add",
      kSynapticStyle,
      "OFF",
      "ON"
    );
    overlapToggle->SetTooltip("Enable overlap-add processing for smoother transitions between chunks. Reduces clicks and pops, typically you want this enabled. Does increase performance cost.");
    ui.attach(overlapToggle, ControlGroup::DSP);

    IRECT agcRect = IRECT(toggleStartX + toggleWidth + toggleGap, rowY, toggleStartX + toggleWidth + toggleGap + toggleWidth, rowY + toggleHeight);
    auto* agcToggle = new IVToggleControl(
      agcRect,
      kAGC,
      "AGC",
      kSynapticStyle,
      "OFF",
      "ON"
    );
    agcToggle->SetTooltip("Match RMS amplitude of output chunks with input chunks.");
    ui.attach(agcToggle, ControlGroup::DSP);

    rowY += layout.controlHeight + 22.f;

    // Gain knobs
    const float knobSize = 75.f;
    const float knobSpacing = 160.f;
    const float knobAreaWidth = (knobSize * 2) + knobSpacing;
    const float knobStartX = audioCard.L + (audioCard.W() - knobAreaWidth) / 2.f;
    const float knobY = rowY;

    IRECT inGainRect = IRECT(knobStartX, knobY, knobStartX + knobSize, knobY + knobSize);
    auto* inGainKnob = new IVKnobControl(inGainRect, kInGain, "Input Gain", kSynapticStyle);
    inGainKnob->SetTooltip("Adjust input signal level before processing. Range: -70dB to +12dB");
    ui.attach(inGainKnob, ControlGroup::DSP);

    IRECT outGainRect = IRECT(knobStartX + knobSize + knobSpacing, knobY, knobStartX + knobSize + knobSpacing + knobSize, knobY + knobSize);
    auto* outGainKnob = new IVKnobControl(outGainRect, kOutGain, "Output Gain", kSynapticStyle);
    outGainKnob->SetTooltip("Adjust output signal level after processing. Range: -70dB to +12dB");
    ui.attach(outGainKnob, ControlGroup::DSP);

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
    dropControl->SetTooltip("Drag and drop audio files here to add them to the brain. Supported formats: WAV, AIFF, FLAC");
    ui.attach(dropControl, ControlGroup::Brain);
    ui.setBrainDropControl(dropControl); // Store reference for state updates

    // Status line
    IRECT statusRect = IRECT(libraryCard.L + layout.cardPadding, dropArea.B + 8.f, libraryCard.R - layout.cardPadding, dropArea.B + 24.f);
    auto* statusControl = new BrainStatusControl(statusRect);
    statusControl->SetTooltip("Shows number of files in brain and storage mode (inline or external file)");
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
    fileList->SetTooltip("List of audio files in the brain. Click the X button to remove a file");
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
    createButton->SetTooltip("Initialize a new brain for storing audio samples. Drag and drop audio files to populate it");
    ui.attach(createButton, ControlGroup::Brain);
    ui.setCreateNewBrainButton(createButton); // Store reference for state updates

    colY[col] = libraryCard.B + layout.sectionGap;
  }

  // BRAIN ANALYSIS CARD
  {
    const float cardH = 175.f; // Increased by 10px for bottom padding
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
    auto* chunkSizeControl = new DeferredNumberBoxControl(
      chunkSizeRow.GetFromLeft(controlWidth).GetTranslated(labelWidth + 8.f, 0.f),
      kChunkSize,
      nullptr,
      "",
      kSynapticStyle
    );
    chunkSizeControl->SetTooltip("Number of audio samples in each chunk, in Brain AND processing. Larger chunks are quicker, but the resynthesized sound is less granular. Changing this triggers rechunking.");
    ui.attach(chunkSizeControl, ControlGroup::Brain);

    rowY += layout.controlHeight + 10.f;

    // Analysis Window with lock
    IRECT analysisWindowRow = IRECT(analysisCard.L + layout.cardPadding, rowY, analysisCard.R - layout.cardPadding, rowY + layout.controlHeight);
    const float lockSize = 24.f;
    const float lockGap = 8.f;

    ui.attach(new ITextControl(analysisWindowRow.GetFromLeft(labelWidth), "Analysis Window", kLabelText), ControlGroup::Brain);

    // Tab switch centered in remaining space (ignore label for centering calculation)
    float tabSwitchWidth = controlWidth + 80.f;
    const float availableWidth = analysisWindowRow.W();
    const float tabSwitchStartX = analysisCard.L + layout.cardPadding + (availableWidth - tabSwitchWidth) * 0.5f;

    IRECT tabSwitchRect = IRECT(
      tabSwitchStartX,
      rowY,
      tabSwitchStartX + tabSwitchWidth,
      rowY + layout.controlHeight
    );
    auto* analysisWindowSwitch = new IVTabSwitchControl(
      tabSwitchRect,
      kAnalysisWindow,
      {"Hann", "Hamming", "Blackman", "Rect"},
      "",
      kSynapticStyle,
      EVShape::Rectangle,
      EDirection::Horizontal
    );
    analysisWindowSwitch->SetTooltip("Window function used for brain chunk analysis. Affects frequency content detection. Changing this triggers reanalysis. (When spectral processing is active, this control also doubles as the output windowing function)");
    ui.attach(analysisWindowSwitch, ControlGroup::Brain);

    // Lock button positioned to the left of tab switch
    IRECT lockButtonRect = IRECT(
      tabSwitchStartX - lockSize - lockGap,
      rowY + (layout.controlHeight - lockSize) * 0.5f,
      tabSwitchStartX - lockGap,
      rowY + (layout.controlHeight - lockSize) * 0.5f + lockSize
    );
    auto* analysisLockButton = new LockButtonControl(lockButtonRect, kWindowLock, kAnalysisWindow);
    analysisLockButton->SetTooltip("Lock/unlock synchronization between Output Window and Analysis Window");
    ui.attach(analysisLockButton, ControlGroup::Brain);

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
    auto* importBtn = new IVButtonControl(importBtnRect, [](IControl* pCaller) {
      auto* pGraphics = pCaller->GetUI();
      auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
      if (pDelegate) {
        pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainImport, kNoTag, 0, nullptr);
      }
    }, "Import Brain", kButtonStyle);
    importBtn->SetTooltip("Load a brain file from disk. Brain stores analyzed audio samples for synthesis");
    ui.attach(importBtn, ControlGroup::Brain);

    IRECT exportBtnRect = IRECT(btnStartX + btnWidth + btnGapH, btnY, btnStartX + btnWidth + btnGapH + btnWidth, btnY + btnHeight);
    auto* exportBtn = new IVButtonControl(exportBtnRect, [](IControl* pCaller) {
      auto* pGraphics = pCaller->GetUI();
      auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
      if (pDelegate) {
        pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainExport, kNoTag, 0, nullptr);
      }
    }, "Export Brain", kButtonStyle);
    exportBtn->SetTooltip("Save current brain to disk. Allows reusing analyzed samples across projects");
    ui.attach(exportBtn, ControlGroup::Brain);

    btnY += btnHeight + btnGapV;

    IRECT ejectBtnRect = IRECT(btnStartX, btnY, btnStartX + btnWidth, btnY + btnHeight);
    auto* ejectBtn = new IVButtonControl(ejectBtnRect, [](IControl* pCaller) {
      auto* pGraphics = pCaller->GetUI();
      auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
      if (pDelegate) {
        pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainEject, kNoTag, 0, nullptr);
      }
    }, "Eject Brain", kButtonStyle);
    ejectBtn->SetTooltip("Ejects the current Brain file. This unreferences the external brain file, and clears the loaded brain data.");
    ui.attach(ejectBtn, ControlGroup::Brain);

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
    compactToggle->SetTooltip("Enable compact storage format for brain files. Reduces file size but may slightly increase load times");
    ui.attach(compactToggle, ControlGroup::Brain);
    ui.setCompactModeToggle(compactToggle);

    colY[col] = managementCard.B + layout.sectionGap;
  }
}

} // namespace tabs
} // namespace ui
} // namespace synaptic

