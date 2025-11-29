/**
 * @file UITheme.h
 * @brief Color palette and theme definitions for the Synaptic Resynthesis UI
 *
 * Defines all UI colors as named constants and text styles for different UI elements.
 * Colors are chosen to be WCAG 2.1 AA compliant where applicable.
 */

#pragma once

#include "plugin_src/ui/core/UIConstants.h"

namespace synaptic {
namespace ui {

// Theme colors (WCAG 2.1 AA compliant where applicable)
inline const ig::IColor kBGDark        = ig::IColor(255, 20, 20, 20);      // Main background
inline const ig::IColor kPanelDark     = ig::IColor(255, 32, 32, 35);      // Card background
inline const ig::IColor kPanelBorder   = ig::IColor(255, 70, 70, 75);      // Card borders
inline const ig::IColor kTextPrimary   = ig::IColor(255, 245, 245, 245);   // Primary text
inline const ig::IColor kTextSecond    = ig::IColor(255, 180, 180, 185);   // Secondary text
inline const ig::IColor kWarnBG        = ig::IColor(120, 255, 180, 80);    // Warning background
inline const ig::IColor kWarnText      = ig::IColor(255, 255, 200, 100);   // Warning text
inline const ig::IColor kAccentBlue    = ig::IColor(255, 80, 160, 220);    // Accent
inline const ig::IColor kControlBG     = ig::IColor(255, 45, 45, 48);      // Control backgrounds
inline const ig::IColor kControlBorder = ig::IColor(255, 100, 100, 105);   // Control borders

// Tab button colors
inline const ig::IColor kTabActive     = ig::IColor(255, 80, 160, 220);
inline const ig::IColor kTabInactive   = ig::IColor(255, 60, 60, 65);
inline const ig::IColor kTabHover      = ig::IColor(255, 70, 70, 75);

// Text styles
inline const ig::IText kTitleText           = ig::IText(26.f, kTextPrimary, "Roboto-Regular", ig::EAlign::Near, ig::EVAlign::Middle, 0);
inline const ig::IText kSectionHeaderText   = ig::IText(15.f, kTextSecond, "Roboto-Regular", ig::EAlign::Near, ig::EVAlign::Middle, 0);
inline const ig::IText kLabelText           = ig::IText(14.f, kTextPrimary, "Roboto-Regular", ig::EAlign::Near, ig::EVAlign::Middle, 0);
inline const ig::IText kWarnTextStyle       = ig::IText(12.f, kWarnText, "Roboto-Regular", ig::EAlign::Near, ig::EVAlign::Middle, 0);
inline const ig::IText kSmallText           = ig::IText(12.f, kTextSecond, "Roboto-Regular", ig::EAlign::Near, ig::EVAlign::Middle, 0);
inline const ig::IText kButtonTextStyle     = ig::IText(14.f, kTextPrimary, "Roboto-Regular", ig::EAlign::Center, ig::EVAlign::Middle, 0);
inline const ig::IText kValueText           = ig::IText(14.f, kTextPrimary, "Roboto-Regular", ig::EAlign::Center, ig::EVAlign::Middle, 0);

} // namespace ui
} // namespace synaptic
