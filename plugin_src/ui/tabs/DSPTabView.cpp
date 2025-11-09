#include "DSPTabView.h"
#include "../styles/UIStyles.h"
#include "../controls/UIControls.h"
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
  ui.attachDSP(new CardPanel(analysisCard, "BRAIN ANALYSIS"));

  IRECT warnRect = analysisCard.GetPadded(-layout.cardPadding).GetFromTop(34.f).GetTranslated(0.f, 28.f);
  ui.attachDSP(new WarningBox(warnRect, "Changing these settings triggers Brain reanalysis"));

  float rowY = warnRect.B + 14.f;
  float labelWidth = 180.f;
  float controlWidth = 200.f;

  // Chunk Size
  IRECT chunkSizeRow = IRECT(analysisCard.L + layout.cardPadding, rowY, analysisCard.R - layout.cardPadding, rowY + layout.controlHeight);
  ui.attachDSP(new ITextControl(chunkSizeRow.GetFromLeft(labelWidth), "Chunk Size", kLabelText));
  ui.attachDSP(new IVNumberBoxControl(
    chunkSizeRow.GetFromLeft(controlWidth).GetTranslated(labelWidth + 8.f, 0.f),
    kChunkSize,
    nullptr,
    "",
    kSynapticStyle
  ));

  rowY += layout.controlHeight + 10.f;

  // Analysis Window
  IRECT analysisWindowRow = IRECT(analysisCard.L + layout.cardPadding, rowY, analysisCard.R - layout.cardPadding, rowY + layout.controlHeight);
  ui.attachDSP(new ITextControl(analysisWindowRow.GetFromLeft(labelWidth), "Analysis Window", kLabelText));
  float tabSwitchWidth = controlWidth + 80.f;
  ui.attachDSP(new IVTabSwitchControl(
    analysisWindowRow.GetFromLeft(tabSwitchWidth).GetTranslated(labelWidth + 12.f, 0.f),
    kAnalysisWindow,
    {"Hann", "Hamming", "Blackman", "Rect"},
    "",
    kSynapticStyle,
    EVShape::Rectangle,
    EDirection::Horizontal
  ));

  yPos = analysisCard.B + layout.sectionGap;

  // TRANSFORMER CARD (with space for dynamic params)
  // Base height for dropdown, will be expanded by dynamic params if needed
  float transformerCardBaseHeight = 120.f;
  float transformerCardMaxHeight = 450.f; // Max space for dynamic params
  IRECT transformerCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + transformerCardMaxHeight);
  auto* transformerCardPanel = new CardPanel(transformerCard, "TRANSFORMER");
  ui.attachDSP(transformerCardPanel);
  ui.mTransformerCardPanel = transformerCardPanel; // Store reference for resizing

  rowY = transformerCard.T + layout.cardPadding + 24.f;
  float dropdownHeight = 48.f;
  float dropdownWidth = transformerCard.W() * 0.5f;
  float dropdownStartX = transformerCard.L + (transformerCard.W() - dropdownWidth) / 2.f;
  IRECT transformerRow = IRECT(dropdownStartX, rowY, dropdownStartX + dropdownWidth, rowY + dropdownHeight);
  ui.attachDSP(new IVMenuButtonControl(
    transformerRow,
    kAlgorithm,
    "Algorithm",
    kSynapticStyle
  ));

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
  ui.attachDSP(morphCardPanel);
  ui.mMorphCardPanel = morphCardPanel; // Store reference for resizing

  rowY = morphCard.T + layout.cardPadding + 24.f;
  float morphDropdownHeight = 48.f;
  float morphDropdownWidth = morphCard.W() * 0.5f;
  float morphDropdownStartX = morphCard.L + (morphCard.W() - morphDropdownWidth) / 2.f;
  IRECT morphRow = IRECT(morphDropdownStartX, rowY, morphDropdownStartX + morphDropdownWidth, rowY + morphDropdownHeight);
  ui.attachDSP(new IVMenuButtonControl(
    morphRow,
    kMorphMode,
    "Morph Mode",
    kSynapticStyle
  ));

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
  ui.attachDSP(audioCardPanel);
  ui.mAudioProcessingCardPanel = audioCardPanel; // Store reference for repositioning

  rowY = audioCard.T + layout.cardPadding + 28.f;

  // Output Window
  IRECT outputWindowRow = IRECT(audioCard.L + layout.cardPadding, rowY, audioCard.R - layout.cardPadding, rowY + layout.controlHeight);
  ui.attachDSP(new ITextControl(outputWindowRow.GetFromLeft(180.f), "Output Window", kLabelText));
  float outputTabSwitchWidth = 200.f + 80.f;
  ui.attachDSP(new IVTabSwitchControl(
    outputWindowRow.GetFromLeft(outputTabSwitchWidth).GetTranslated(180.f + 12.f, 0.f),
    kOutputWindow,
    {"Hann", "Hamming", "Blackman", "Rect"},
    "",
    kSynapticStyle,
    EVShape::Rectangle,
    EDirection::Horizontal
  ));

  rowY += layout.controlHeight + 12.f;

  // Toggles
  float toggleHeight = 48.f;
  float toggleWidth = 180.f;
  float toggleGap = 24.f;
  float toggleGroupWidth = (toggleWidth * 2) + toggleGap;
  float toggleStartX = audioCard.L + (audioCard.W() - toggleGroupWidth) / 2.f;

  IRECT overlapRect = IRECT(toggleStartX, rowY, toggleStartX + toggleWidth, rowY + toggleHeight);
  ui.attachDSP(new IVToggleControl(
    overlapRect,
    kEnableOverlap,
    "Overlap-Add",
    kSynapticStyle,
    "OFF",
    "ON"
  ));

  IRECT agcRect = IRECT(toggleStartX + toggleWidth + toggleGap, rowY, toggleStartX + toggleWidth + toggleGap + toggleWidth, rowY + toggleHeight);
  ui.attachDSP(new IVToggleControl(
    agcRect,
    kAGC,
    "AGC",
    kSynapticStyle,
    "OFF",
    "ON"
  ));

  rowY += layout.controlHeight + 22.f;

  // Gain knobs
  float knobSize = 75.f;
  float knobSpacing = 160.f;
  float knobAreaWidth = (knobSize * 2) + knobSpacing;
  float knobStartX = (bounds.W() - knobAreaWidth) / 2.f;
  float knobY = rowY;

  IRECT inGainRect = IRECT(knobStartX, knobY, knobStartX + knobSize, knobY + knobSize);
  ui.attachDSP(new IVKnobControl(inGainRect, kInGain, "Input Gain", kSynapticStyle));

  IRECT outGainRect = IRECT(knobStartX + knobSize + knobSpacing, knobY, knobStartX + knobSize + knobSpacing + knobSize, knobY + knobSize);
  ui.attachDSP(new IVKnobControl(outGainRect, kOutGain, "Output Gain", kSynapticStyle));
}

} // namespace tabs
} // namespace ui
} // namespace synaptic


