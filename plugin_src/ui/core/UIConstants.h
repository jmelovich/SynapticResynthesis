/**
 * @file UIConstants.h
 * @brief Central UI constants and namespace aliases
 *
 * This header consolidates:
 * - The canonical namespace alias for iplug::igraphics (ig)
 * - Magic number constants used in UI layout and positioning
 * - Common tolerances and thresholds
 *
 * All UI files should include this header for consistent namespace usage.
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"

namespace synaptic {
namespace ui {

// ============================================================================
// Canonical namespace alias for IPlug graphics types
// ============================================================================
namespace ig = iplug::igraphics;

// ============================================================================
// Layout constants
// ============================================================================

namespace LayoutConstants
{
  // Card and control positioning tolerances
  constexpr float kColumnBoundsEpsilon = 1.f;       // Horizontal bounds tolerance for column detection
  constexpr float kVerticalPositionTolerance = 10.f; // Vertical tolerance for card repositioning

  // Card sizing
  constexpr float kMinCardHeight = 120.f;           // Minimum height for dynamic parameter cards
  constexpr float kCardPadding = 16.f;              // Internal padding within cards

  // Control sizing
  constexpr float kDropdownHeight = 48.f;           // Standard dropdown control height
  constexpr float kMorphDropdownWidthRatio = 0.5f;  // Morph dropdown width as ratio of card width

  // Spacing
  constexpr float kDynamicParamSpacing = 16.f;      // Vertical spacing after dynamic param controls

  // Window resize
  constexpr float kResizeThreshold = 10.f;          // Minimum height change to trigger window resize
}

// ============================================================================
// Numeric tolerances
// ============================================================================

namespace Tolerances
{
  constexpr double kRMSEpsilon = 1e-9;              // Minimum RMS for AGC calculations
  constexpr float kBlendMinimum = 1e-9f;            // Minimum blend value for rescaling
}

// ============================================================================
// Progress display
// ============================================================================

namespace Progress
{
  constexpr float kDefaultProgress = 50.0f;         // Default progress when total is unknown
  constexpr float kMaxProgress = 100.0f;            // Maximum progress value
}

} // namespace ui
} // namespace synaptic

