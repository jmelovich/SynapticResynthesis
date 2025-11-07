#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "IconsForkAwesome.h"
#include "../SynapticResynthesis.h"
#include <vector>
#include <functional>

using namespace iplug;
using namespace igraphics;

namespace synaptic {

// ========== THEME COLORS (WCAG 2.1 AA Compliant) ==========
static const IColor kBGDark       = IColor(255, 20, 20, 20);      // Main background
static const IColor kPanelDark    = IColor(255, 32, 32, 35);      // Card background
static const IColor kPanelBorder  = IColor(255, 70, 70, 75);      // Card borders
static const IColor kTextPrimary  = IColor(255, 245, 245, 245);   // Primary text (high contrast 7:1+)
static const IColor kTextSecond   = IColor(255, 180, 180, 185);   // Secondary text (4.5:1+)
static const IColor kWarnBG       = IColor(120, 255, 180, 80);    // Warning box background (amber, translucent)
static const IColor kWarnText     = IColor(255, 255, 200, 100);   // Warning text
static const IColor kAccentBlue   = IColor(255, 80, 160, 220);    // Active/hover blue accent
static const IColor kControlBG    = IColor(255, 45, 45, 48);      // Control backgrounds
static const IColor kControlBorder= IColor(255, 100, 100, 105);   // Control borders

// Tab button colors
static const IColor kTabActive    = IColor(255, 80, 160, 220);    // Active tab
static const IColor kTabInactive  = IColor(255, 60, 60, 65);      // Inactive tab
static const IColor kTabHover     = IColor(255, 70, 70, 75);      // Hover state

// ========== TEXT STYLES ==========
static const IText kTitleText = IText(26.f, kTextPrimary, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
static const IText kSectionHeaderText = IText(15.f, kTextSecond, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
static const IText kLabelText = IText(14.f, kTextPrimary, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
static const IText kWarnTextStyle = IText(12.f, kWarnText, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
static const IText kSmallText = IText(12.f, kTextSecond, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
static const IText kButtonTextStyle = IText(14.f, kTextPrimary, "Roboto-Regular", EAlign::Center, EVAlign::Middle, 0);
static const IText kValueText = IText(14.f, kTextPrimary, "Roboto-Regular", EAlign::Center, EVAlign::Middle, 0);

// ========== CUSTOM IVSTYLE FOR CONTROLS ==========
static const IVStyle kSynapticStyle = IVStyle{
  true,  // Show label
  true,  // Show value
  {      // Colors (only 9 colors allowed: kBG, kFG, kPR, kFR, kHL, kSH, kX1, kX2, kX3)
    DEFAULT_BGCOLOR,           // kBG - Background
    kControlBG,                // kFG - Foreground/OFF
    kControlBorder,            // kPR - Pressed/ON
    kAccentBlue,               // kFR - Frame
    kAccentBlue,               // kHL - Highlight
    DEFAULT_SHCOLOR,           // kSH - Shadow
    kTextPrimary,              // kX1 - Extra 1 (used for text)
    kControlBorder,            // kX2 - Extra 2
    kAccentBlue                // kX3 - Extra 3
  },
  {      // Label text
    14.f,                      // Size (increased from 13)
    kTextPrimary,              // Color
    "Roboto-Regular",          // Font
    EAlign::Center,            // Horizontal align
    EVAlign::Bottom,           // Vertical align (labels below controls)
    0                          // Angle
  },
  {      // Value text
    13.f,                      // Size (increased from 12)
    kTextPrimary,              // Color
    "Roboto-Regular",          // Font
    EAlign::Center,            // Horizontal align
    EVAlign::Middle,           // Vertical align
    0                          // Angle
  },
  DEFAULT_HIDE_CURSOR,
  true,   // Show frame
  false,  // Draw shadows
  DEFAULT_EMBOSS,
  0.2f,  // Rounding (for rounded corners)
  3.f,   // Frame thickness (increased from 2 for better visibility)
  4.f,   // Shadow offset
  DEFAULT_WIDGET_FRAC,
  DEFAULT_WIDGET_ANGLE
};

// Style for tab switches with spacing between tabs
static const IVStyle kTabSwitchStyle = IVStyle{
  true,  // Show label
  true,  // Show value
  {      // Colors
    DEFAULT_BGCOLOR,           // kBG - Background
    kControlBG,                // kFG - Foreground/OFF
    kControlBorder,            // kPR - Pressed/ON
    kAccentBlue,               // kFR - Frame
    kAccentBlue,               // kHL - Highlight
    DEFAULT_SHCOLOR,           // kSH - Shadow
    kTextPrimary,              // kX1 - Extra 1 (used for text)
    kControlBorder,            // kX2 - Extra 2
    kAccentBlue                // kX3 - Extra 3
  },
  {      // Label text
    14.f,
    kTextPrimary,
    "Roboto-Regular",
    EAlign::Center,
    EVAlign::Bottom,
    0
  },
  {      // Value text
    13.f,
    kTextPrimary,
    "Roboto-Regular",
    EAlign::Center,
    EVAlign::Middle,
    0
  },
  DEFAULT_HIDE_CURSOR,
  true,   // Show frame
  false,  // Draw shadows
  DEFAULT_EMBOSS,
  0.2f,  // Rounding
  2.f,   // Frame thickness - thinner to create visual gaps
  4.f,   // Shadow offset
  0.70f, // Widget frac - reduced significantly to create larger gaps between tabs
  DEFAULT_WIDGET_ANGLE
};

// Style for buttons with centered text
static const IVStyle kButtonStyle = IVStyle{
  true,  // Show label
  true,  // Show value
  {      // Colors
    DEFAULT_BGCOLOR,           // kBG - Background
    kControlBG,                // kFG - Foreground/OFF
    kControlBorder,            // kPR - Pressed/ON
    kAccentBlue,               // kFR - Frame
    kAccentBlue,               // kHL - Highlight
    DEFAULT_SHCOLOR,           // kSH - Shadow
    kTextPrimary,              // kX1 - Extra 1 (used for text)
    kControlBorder,            // kX2 - Extra 2
    kAccentBlue                // kX3 - Extra 3
  },
  {      // Label text - CENTERED vertically for buttons
    14.f,
    kTextPrimary,
    "Roboto-Regular",
    EAlign::Center,
    EVAlign::Middle,           // Middle instead of Bottom for centered text
    0
  },
  {      // Value text
    13.f,
    kTextPrimary,
    "Roboto-Regular",
    EAlign::Center,
    EVAlign::Middle,
    0
  },
  DEFAULT_HIDE_CURSOR,
  true,   // Show frame
  false,  // Draw shadows
  DEFAULT_EMBOSS,
  0.2f,  // Rounding
  3.f,   // Frame thickness
  4.f,   // Shadow offset
  DEFAULT_WIDGET_FRAC,
  DEFAULT_WIDGET_ANGLE
};

// =====================================================
// CUSTOM CONTROLS
// =====================================================

// Simple rounded panel for card backgrounds
class CardPanel : public IControl
{
public:
  CardPanel(const IRECT& bounds, const char* title = nullptr)
  : IControl(bounds)
  , mTitle(title)
  {
    SetIgnoreMouse(true);
  }

  void Draw(IGraphics& g) override
  {
    // Draw card background
    g.FillRoundRect(kPanelDark, mRECT, 6.f);
    g.DrawRoundRect(kPanelBorder, mRECT, 6.f, nullptr, 1.5f);

    // Draw title if provided
    if (mTitle)
    {
      IRECT titleRect = mRECT.GetPadded(-12.f).GetFromTop(20.f);
      g.DrawText(kSectionHeaderText, mTitle, titleRect);
    }
  }

private:
  const char* mTitle;
};

// Warning box for Brain reanalysis parameters
class WarningBox : public IControl
{
public:
  WarningBox(const IRECT& bounds, const char* text)
  : IControl(bounds)
  , mText(text)
  {
    SetIgnoreMouse(true);
  }

  void Draw(IGraphics& g) override
  {
    // Semi-transparent amber background
    g.FillRoundRect(kWarnBG, mRECT, 4.f);
    g.DrawRoundRect(kWarnText, mRECT, 4.f, nullptr, 1.f);

    // Warning icon (left side, vertically centered)
    IRECT iconRect = mRECT.GetFromLeft(30.f);
    IText iconStyle = IText(14.f, kWarnText, "ForkAwesome", EAlign::Center, EVAlign::Middle, 0);
    g.DrawText(iconStyle, ICON_FK_EXCLAMATION_TRIANGLE, iconRect);

    // Warning text (to the right of icon, vertically centered)
    IRECT textRect = mRECT;
    textRect.L += 32.f;  // Start after icon
    textRect.R -= 8.f;   // Right padding
    // Use higher contrast color for better readability
    IText textStyle = IText(12.f, IColor(255, 255, 230, 140), "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
    g.DrawText(textStyle, mText, textRect);
  }

private:
  const char* mText;
};

// Custom tab button with active state
class TabButton : public IControl
{
public:
  TabButton(const IRECT& bounds, const char* label, std::function<void()> onClick)
  : IControl(bounds)
  , mLabel(label)
  , mOnClick(onClick)
  , mIsActive(false)
  , mIsHovered(false)
  {}

  void Draw(IGraphics& g) override
  {
    IColor bgColor = mIsActive ? kTabActive : (mIsHovered ? kTabHover : kTabInactive);

    g.FillRoundRect(bgColor, mRECT, 4.f);
    if (mIsActive)
    {
      g.DrawRoundRect(kAccentBlue, mRECT, 4.f, nullptr, 2.f);
    }

    g.DrawText(kButtonTextStyle, mLabel, mRECT);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (mOnClick)
      mOnClick();
    SetDirty(false);
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    mIsHovered = true;
    SetDirty(false);
  }

  void OnMouseOut() override
  {
    mIsHovered = false;
    SetDirty(false);
  }

  void SetActive(bool active)
  {
    if (mIsActive != active)
    {
      mIsActive = active;
      SetDirty(false);
    }
  }

private:
  const char* mLabel;
  std::function<void()> mOnClick;
  bool mIsActive;
  bool mIsHovered;
};

// Section label control
class SectionLabel : public IControl
{
public:
  SectionLabel(const IRECT& bounds, const char* text)
  : IControl(bounds)
  , mText(text)
  {
    SetIgnoreMouse(true);
  }

  void Draw(IGraphics& g) override
  {
    g.DrawText(kSectionHeaderText, mText, mRECT);

    // Draw subtle underline
    IRECT lineRect = mRECT.GetFromBottom(1.f).GetTranslated(0.f, -8.f);
    lineRect.R = lineRect.L + 40.f; // Short underline
    g.FillRect(kPanelBorder, lineRect);
  }

private:
  const char* mText;
};

// =====================================================
// TAB MANAGEMENT
// =====================================================

enum class Tab { DSP, Brain };

static Tab sCurrentTab = Tab::DSP;
static std::vector<IControl*> sDSPControls;
static std::vector<IControl*> sBrainControls;
static TabButton* sDSPTabButton = nullptr;
static TabButton* sBrainTabButton = nullptr;

inline void SetActiveTab(Tab tab)
{
  sCurrentTab = tab;

  // Show/hide controls based on tab
  // Use indexed loop to avoid iterator invalidation issues
  for (size_t i = 0; i < sDSPControls.size(); ++i)
  {
    if (sDSPControls[i]) {
      sDSPControls[i]->Hide(tab != Tab::DSP);
      sDSPControls[i]->SetDisabled(tab != Tab::DSP);  // Also disable to prevent interaction
    }
  }

  for (size_t i = 0; i < sBrainControls.size(); ++i)
  {
    if (sBrainControls[i]) {
      sBrainControls[i]->Hide(tab != Tab::Brain);
      sBrainControls[i]->SetDisabled(tab != Tab::Brain);  // Also disable to prevent interaction
    }
  }

  // Update tab button states
  if (sDSPTabButton) sDSPTabButton->SetActive(tab == Tab::DSP);
  if (sBrainTabButton) sBrainTabButton->SetActive(tab == Tab::Brain);
}

inline IControl* AddToDSP(IGraphics* g, IControl* ctrl)
{
  IControl* added = g->AttachControl(ctrl);
  if (added) {
    sDSPControls.push_back(added);
    // Initially hide if not on DSP tab
    if (sCurrentTab != Tab::DSP) {
      added->Hide(true);
      added->SetDisabled(true);
    }
  }
  return added;
}

inline IControl* AddToBrain(IGraphics* g, IControl* ctrl)
{
  IControl* added = g->AttachControl(ctrl);
  if (added) {
    sBrainControls.push_back(added);
    // Initially hide if not on Brain tab
    if (sCurrentTab != Tab::Brain) {
      added->Hide(true);
      added->SetDisabled(true);
    }
  }
  return added;
}

inline IControl* AddGlobal(IGraphics* g, IControl* ctrl)
{
  return g->AttachControl(ctrl);
}

// =====================================================
// LAYOUT SYSTEM
// =====================================================

struct Layout
{
  float width;
  float height;
  float padding;
  float cardPadding;
  float lineHeight;
  float sectionGap;
  float controlHeight;

  // Calculate scaled layout based on window size
  static Layout Calculate(const IRECT& bounds)
  {
    Layout layout;
    layout.width = bounds.W();
    layout.height = bounds.H();
    layout.padding = 18.f;          // Outer padding
    layout.cardPadding = 16.f;      // Inner card padding
    layout.lineHeight = 36.f;       // Line height
    layout.sectionGap = 24.f;       // Gap between cards (increased from 18 for better separation)
    layout.controlHeight = 32.f;    // Standard control height
    return layout;
  }

  IRECT GetContentArea(const IRECT& bounds) const
  {
    return bounds.GetPadded(-padding);
  }
};

// Forward declarations for tab builders
inline void BuildDSPTab(IGraphics* pGraphics, const IRECT& bounds, const Layout& layout, float startY);
inline void BuildBrainTab(IGraphics* pGraphics, const IRECT& bounds, const Layout& layout, float startY);

// Helper to recalculate bounds for resize
inline IRECT GetHeaderRowBounds(const IRECT& bounds, const Layout& layout) {
  return IRECT(layout.padding, layout.padding, bounds.W() - layout.padding, layout.padding + 40.f);
}

inline IRECT GetTitleBounds(const IRECT& headerRow) {
  return headerRow.GetFromLeft(300.f);
}

inline IRECT GetDSPTabBounds(const IRECT& headerRow) {
  return headerRow.GetFromRight(180.f).GetFromLeft(85.f);
}

inline IRECT GetBrainTabBounds(const IRECT& headerRow) {
  return headerRow.GetFromRight(85.f);
}

// =====================================================
// MAIN LAYOUT BUILDER
// =====================================================

inline void BuildIGraphicsLayout(IGraphics* pGraphics)
{
#if IPLUG_EDITOR
  if (!pGraphics) return;

  const IRECT bounds = pGraphics->GetBounds();
  Layout layout = Layout::Calculate(bounds);

  // ========== PHASE 1: RESIZE EXISTING CONTROLS ==========
  // If controls already exist, just update their bounds (prevents flickering)
  if (pGraphics->NControls() > 0)
  {
    // This is a resize event - rebuild UI completely to avoid corruption
    // Store the current tab before clearing
    Tab previousTab = sCurrentTab;

    // Clear previous controls
    sDSPControls.clear();
    sBrainControls.clear();
    sDSPTabButton = nullptr;
    sBrainTabButton = nullptr;

    // Remove all existing controls for rebuild
    pGraphics->RemoveAllControls();

    // Fall through to Phase 2 to rebuild with new bounds

    // Rebuild entire UI with new bounds (continue to Phase 2)
    // Create background layer
    pGraphics->AttachPanelBackground(kBGDark);

    float yPos = layout.padding;

    // ========== HEADER SECTION ==========

    IRECT headerRow = GetHeaderRowBounds(bounds, layout);

    // Plugin title
    IRECT titleRect = GetTitleBounds(headerRow);
    AddGlobal(pGraphics, new ITextControl(titleRect, "Synaptic Resynthesis", kTitleText));

    // Tab buttons
    IRECT dspTabRect = GetDSPTabBounds(headerRow);
    IRECT brainTabRect = GetBrainTabBounds(headerRow);

    sDSPTabButton = new TabButton(dspTabRect, "DSP", []() {
      SetActiveTab(Tab::DSP);
    });
    AddGlobal(pGraphics, sDSPTabButton);

    sBrainTabButton = new TabButton(brainTabRect, "Brain", []() {
      SetActiveTab(Tab::Brain);
    });
    AddGlobal(pGraphics, sBrainTabButton);

    yPos = headerRow.B + layout.sectionGap;

    // Build tab content
    BuildDSPTab(pGraphics, bounds, layout, yPos);
    BuildBrainTab(pGraphics, bounds, layout, yPos);

    // Restore tab state
    SetActiveTab(previousTab);
    return;
  }

  // ========== PHASE 2: INITIAL CREATION ==========

  // Enable layout on future resizes
  pGraphics->SetLayoutOnResize(true);

  // Load fonts
  pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
  pGraphics->LoadFont("ForkAwesome", FORK_AWESOME_FN);

  // Enable features
  pGraphics->EnableMouseOver(true);
  pGraphics->EnableTooltips(true);
  pGraphics->AttachTextEntryControl();

  // Create background layer
  pGraphics->AttachPanelBackground(kBGDark);

  float yPos = layout.padding;

  // ========== HEADER SECTION ==========

  IRECT headerRow = GetHeaderRowBounds(bounds, layout);

  // Plugin title
  IRECT titleRect = GetTitleBounds(headerRow);
  AddGlobal(pGraphics, new ITextControl(titleRect, "Synaptic Resynthesis", kTitleText));

  // Tab buttons
  IRECT dspTabRect = GetDSPTabBounds(headerRow);
  IRECT brainTabRect = GetBrainTabBounds(headerRow);

  sDSPTabButton = new TabButton(dspTabRect, "DSP", []() {
    SetActiveTab(Tab::DSP);
  });
  AddGlobal(pGraphics, sDSPTabButton);

  sBrainTabButton = new TabButton(brainTabRect, "Brain", []() {
    SetActiveTab(Tab::Brain);
  });
  AddGlobal(pGraphics, sBrainTabButton);

  yPos = headerRow.B + layout.sectionGap;

  // ========== DSP TAB CONTENT ==========

  BuildDSPTab(pGraphics, bounds, layout, yPos);

  // ========== BRAIN TAB CONTENT ==========

  BuildBrainTab(pGraphics, bounds, layout, yPos);

  // ========== INITIAL STATE ==========

  SetActiveTab(Tab::DSP);

#endif // IPLUG_EDITOR
}

// =====================================================
// DSP TAB LAYOUT
// =====================================================

inline void BuildDSPTab(IGraphics* pGraphics, const IRECT& bounds, const Layout& layout, float startY)
{
  float yPos = startY;

  // ========== BRAIN ANALYSIS CARD (only params that trigger reanalysis) ==========

  float analysisCardHeight = 165.f;  // Increased from 150
  IRECT analysisCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + analysisCardHeight);

  AddToDSP(pGraphics, new CardPanel(analysisCard, "BRAIN ANALYSIS"));

  // Warning box
  IRECT warnRect = analysisCard.GetPadded(-layout.cardPadding).GetFromTop(34.f).GetTranslated(0.f, 28.f);
  AddToDSP(pGraphics, new WarningBox(warnRect, "Changing these settings triggers Brain reanalysis"));

  float rowY = warnRect.B + 14.f;  // Increased spacing from 12
  float labelWidth = 180.f;
  float controlWidth = 200.f;

  // Chunk Size
  IRECT chunkSizeRow = IRECT(analysisCard.L + layout.cardPadding, rowY, analysisCard.R - layout.cardPadding, rowY + layout.controlHeight);
  AddToDSP(pGraphics, new ITextControl(chunkSizeRow.GetFromLeft(labelWidth), "Chunk Size", kLabelText));

  auto* chunkSizeControl = new IVNumberBoxControl(
    chunkSizeRow.GetFromLeft(controlWidth).GetTranslated(labelWidth + 8.f, 0.f),
    kChunkSize,
    nullptr,
    "",
    kSynapticStyle
  );
  AddToDSP(pGraphics, chunkSizeControl);

  rowY += layout.controlHeight + 10.f;  // Increased spacing from 8

  // Analysis Window
  IRECT analysisWindowRow = IRECT(analysisCard.L + layout.cardPadding, rowY, analysisCard.R - layout.cardPadding, rowY + layout.controlHeight);
  AddToDSP(pGraphics, new ITextControl(analysisWindowRow.GetFromLeft(labelWidth), "Analysis Window", kLabelText));

  // Wider rect for tab switches to spread them out
  float tabSwitchWidth = controlWidth + 80.f;  // Extra width to create spacing
  auto* analysisWindowControl = new IVTabSwitchControl(
    analysisWindowRow.GetFromLeft(tabSwitchWidth).GetTranslated(labelWidth + 12.f, 0.f),
    kAnalysisWindow,
    {"Hann", "Hamming", "Blackman", "Rect"},
    "",
    kSynapticStyle,  // Back to regular style
    EVShape::Rectangle,
    EDirection::Horizontal
  );
  AddToDSP(pGraphics, analysisWindowControl);

  yPos = analysisCard.B + layout.sectionGap;

  // ========== TRANSFORMER CARD ==========

  float transformerCardHeight = 90.f;  // Increased to accommodate taller centered control
  IRECT transformerCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + transformerCardHeight);

  AddToDSP(pGraphics, new CardPanel(transformerCard, "TRANSFORMER"));

  rowY = transformerCard.T + layout.cardPadding + 24.f;

  // Transformer dropdown - centered, square-ish proportions
  float dropdownHeight = 48.f;
  float dropdownWidth = transformerCard.W() * 0.5f;  // 50% of card width (reduced from 60%)
  float dropdownStartX = transformerCard.L + (transformerCard.W() - dropdownWidth) / 2.f;  // Center horizontally
  IRECT transformerRow = IRECT(dropdownStartX, rowY, dropdownStartX + dropdownWidth, rowY + dropdownHeight);

  auto* transformerControl = new IVMenuButtonControl(
    transformerRow,
    kAlgorithm,
    "Algorithm",
    kSynapticStyle
  );
  AddToDSP(pGraphics, transformerControl);

  yPos = transformerCard.B + layout.sectionGap;

  // ========== MORPH CARD ==========

  float morphCardHeight = 90.f;  // Increased to accommodate taller centered control
  IRECT morphCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + morphCardHeight);

  AddToDSP(pGraphics, new CardPanel(morphCard, "MORPH"));

  rowY = morphCard.T + layout.cardPadding + 24.f;

  // Morph Mode dropdown - centered, square-ish proportions
  float morphDropdownHeight = 48.f;
  float morphDropdownWidth = morphCard.W() * 0.5f;  // 50% of card width (reduced from 60%)
  float morphDropdownStartX = morphCard.L + (morphCard.W() - morphDropdownWidth) / 2.f;  // Center horizontally
  IRECT morphRow = IRECT(morphDropdownStartX, rowY, morphDropdownStartX + morphDropdownWidth, rowY + morphDropdownHeight);

  auto* morphControl = new IVMenuButtonControl(
    morphRow,
    kMorphMode,
    "Morph Mode",
    kSynapticStyle
  );
  AddToDSP(pGraphics, morphControl);

  yPos = morphCard.B + layout.sectionGap;

  // ========== AUDIO PROCESSING CARD (Gain + Output controls) ==========

  float audioCardHeight = 225.f;  // Increased from 210
  IRECT audioCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + audioCardHeight);

  AddToDSP(pGraphics, new CardPanel(audioCard, "AUDIO PROCESSING"));

  rowY = audioCard.T + layout.cardPadding + 28.f;  // Increased from 26

  // Output Window
  IRECT outputWindowRow = IRECT(audioCard.L + layout.cardPadding, rowY, audioCard.R - layout.cardPadding, rowY + layout.controlHeight);
  AddToDSP(pGraphics, new ITextControl(outputWindowRow.GetFromLeft(labelWidth), "Output Window", kLabelText));

  // Wider rect for tab switches to spread them out
  float outputTabSwitchWidth = controlWidth + 80.f;  // Extra width to create spacing
  auto* outputWindowControl = new IVTabSwitchControl(
    outputWindowRow.GetFromLeft(outputTabSwitchWidth).GetTranslated(labelWidth + 12.f, 0.f),
    kOutputWindow,
    {"Hann", "Hamming", "Blackman", "Rect"},
    "",
    kSynapticStyle,  // Back to regular style
    EVShape::Rectangle,
    EDirection::Horizontal
  );
  AddToDSP(pGraphics, outputWindowControl);

  rowY += layout.controlHeight + 12.f;

  // Toggle controls - centered with square-ish proportions
  float toggleHeight = 48.f;  // Taller for square-ish proportions
  float toggleWidth = 180.f;   // Fixed width for consistent sizing
  float toggleGap = 24.f;      // Gap between toggles

  // Center the toggle group horizontally
  float toggleGroupWidth = (toggleWidth * 2) + toggleGap;
  float toggleStartX = audioCard.L + (audioCard.W() - toggleGroupWidth) / 2.f;

  // Enable Overlap-Add
  IRECT overlapRect = IRECT(toggleStartX, rowY, toggleStartX + toggleWidth, rowY + toggleHeight);
  auto* overlapCheckbox = new IVToggleControl(
    overlapRect,
    kEnableOverlap,
    "Overlap-Add",
    kSynapticStyle,
    "OFF",
    "ON"
  );
  AddToDSP(pGraphics, overlapCheckbox);

  // AGC
  IRECT agcRect = IRECT(toggleStartX + toggleWidth + toggleGap, rowY, toggleStartX + toggleWidth + toggleGap + toggleWidth, rowY + toggleHeight);
  auto* agcCheckbox = new IVToggleControl(
    agcRect,
    kAGC,
    "AGC",
    kSynapticStyle,
    "OFF",
    "ON"
  );
  AddToDSP(pGraphics, agcCheckbox);

  rowY += layout.controlHeight + 22.f;  // Increased from 20

  // Gain knobs centered horizontally
  float knobSize = 75.f;  // Increased from 70
  float knobSpacing = 160.f;
  float knobAreaWidth = (knobSize * 2) + knobSpacing;
  float knobStartX = (bounds.W() - knobAreaWidth) / 2.f;
  float knobY = rowY;

  // Input Gain Knob
  IRECT inGainRect = IRECT(knobStartX, knobY, knobStartX + knobSize, knobY + knobSize);
  auto* inGainKnob = new IVKnobControl(inGainRect, kInGain, "Input Gain", kSynapticStyle);
  AddToDSP(pGraphics, inGainKnob);

  // Output Gain Knob
  IRECT outGainRect = IRECT(knobStartX + knobSize + knobSpacing, knobY, knobStartX + knobSize + knobSpacing + knobSize, knobY + knobSize);
  auto* outGainKnob = new IVKnobControl(outGainRect, kOutGain, "Output Gain", kSynapticStyle);
  AddToDSP(pGraphics, outGainKnob);
}

// =====================================================
// BRAIN TAB LAYOUT
// =====================================================

inline void BuildBrainTab(IGraphics* pGraphics, const IRECT& bounds, const Layout& layout, float startY)
{
  float yPos = startY;

  // ========== SAMPLE LIBRARY CARD ==========

  float libraryCardHeight = 195.f;  // Increased from 180
  IRECT libraryCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + libraryCardHeight);

  AddToBrain(pGraphics, new CardPanel(libraryCard, "SAMPLE LIBRARY"));

  // Placeholder text for drag-drop area
  IRECT dropArea = libraryCard.GetPadded(-layout.cardPadding).GetFromTop(85.f).GetTranslated(0.f, 28.f);

  // Larger text for better readability
  IText largerText = IText(13.f, kTextSecond, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
  auto* dropZone = new ITextControl(dropArea, "Drag audio files here or use Brain management buttons below\n\n(File management will be implemented in Phase 3)", largerText);
  AddToBrain(pGraphics, dropZone);

  IRECT statusRect = IRECT(libraryCard.L + layout.cardPadding, dropArea.B + 8.f, libraryCard.R - layout.cardPadding, dropArea.B + 24.f);
  IText statusText = IText(13.f, kTextSecond, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
  AddToBrain(pGraphics, new ITextControl(statusRect, "Brain Status: Ready | Files: 0 | Chunks: 0", statusText));

  yPos = libraryCard.B + layout.sectionGap;

  // ========== MANAGEMENT CARD ==========

  float managementCardHeight = 160.f;  // Increased to prevent overflow
  IRECT managementCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + managementCardHeight);

  AddToBrain(pGraphics, new CardPanel(managementCard, "BRAIN MANAGEMENT"));

  // Management buttons in a 2x2 grid - centered with square-ish proportions
  float btnWidth = 200.f;   // Fixed width for consistent sizing
  float btnHeight = 45.f;   // Taller for square-ish proportions
  float btnGapH = 20.f;     // Horizontal gap
  float btnGapV = 16.f;     // Vertical gap

  // Center the button grid horizontally
  float btnGridWidth = (btnWidth * 2) + btnGapH;
  float btnStartX = managementCard.L + (managementCard.W() - btnGridWidth) / 2.f;
  float btnY = managementCard.T + layout.cardPadding + 32.f;  // Increased from 28 for better spacing

  // Row 1: Import and Export (Import first for intuitive workflow)
  IRECT importBtnRect = IRECT(btnStartX, btnY, btnStartX + btnWidth, btnY + btnHeight);
  auto* importBtn = new IVButtonControl(importBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainImport, kNoTag, 0, nullptr);
    }
  }, "Import Brain", kButtonStyle);  // Use button style for centered text
  AddToBrain(pGraphics, importBtn);

  IRECT exportBtnRect = IRECT(btnStartX + btnWidth + btnGapH, btnY, btnStartX + btnWidth + btnGapH + btnWidth, btnY + btnHeight);
  auto* exportBtn = new IVButtonControl(exportBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainExport, kNoTag, 0, nullptr);
    }
  }, "Export Brain", kButtonStyle);  // Use button style for centered text
  AddToBrain(pGraphics, exportBtn);

  // Row 2: Reset and Detach
  btnY += btnHeight + btnGapV;

  IRECT resetBtnRect = IRECT(btnStartX, btnY, btnStartX + btnWidth, btnY + btnHeight);
  auto* resetBtn = new IVButtonControl(resetBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainReset, kNoTag, 0, nullptr);
    }
  }, "Reset Brain", kButtonStyle);  // Use button style for centered text
  AddToBrain(pGraphics, resetBtn);

  IRECT detachBtnRect = IRECT(btnStartX + btnWidth + btnGapH, btnY, btnStartX + btnWidth + btnGapH + btnWidth, btnY + btnHeight);
  auto* detachBtn = new IVButtonControl(detachBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainDetach, kNoTag, 0, nullptr);
    }
  }, "Detach File Ref", kButtonStyle);  // Use button style for centered text
  AddToBrain(pGraphics, detachBtn);
}

} // namespace synaptic
