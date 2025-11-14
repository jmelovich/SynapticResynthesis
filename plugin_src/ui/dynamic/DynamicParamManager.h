/**
 * @file DynamicParamManager.h
 * @brief Creates and layouts UI controls for dynamic transformer/morph parameters
 *
 * Responsibilities:
 * - Queries transformers and morphs for their exposed parameters
 * - Creates appropriate IPlug controls based on parameter type (number, boolean, enum)
 * - Layouts controls in a 2-column grid
 * - Calculates required height for parameter sections
 * - Maps parameter IDs to IParam indices
 *
 * This class bridges the dynamic parameter system (ExposedParamDesc) with
 * IPlug's static control system, generating UI controls on-the-fly as
 * transformers and morphs are switched.
 */

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "plugin_src/params/DynamicParamSchema.h"
#include "plugin_src/params/ParameterManager.h"
#include "../layout/UILayout.h"
#include "../styles/UIStyles.h"
#include <vector>
#include <memory>

namespace synaptic {
namespace ui {
// Note: namespace ig alias is defined in UITheme.h

/**
 * @brief Manages dynamic parameter control generation and lifecycle
 *
 * Creates IPlug controls for transformer and morph parameters based on
 * ExposedParamDesc schema. Handles control creation, layout, and cleanup.
 */
class DynamicParamManager
{
public:
  DynamicParamManager();

  /**
   * @brief Build transformer parameter controls
   * @param graphics IGraphics instance
   * @param bounds Rectangle containing all parameter controls
   * @param layout UI layout configuration
   * @param transformer Transformer to query for parameters
   * @param paramManager ParameterManager to get IParam bindings
   * @param plugin Plugin instance to access IParams
   * @return Vector of created controls (caller should attach to DSP tab)
   */
  std::vector<ig::IControl*> BuildTransformerParams(
    ig::IGraphics* graphics,
    const ig::IRECT& bounds,
    const UILayout& layout,
    const synaptic::IChunkBufferTransformer* transformer,
    const synaptic::ParameterManager& paramManager,
    iplug::Plugin* plugin);

  /**
   * @brief Build morph parameter controls
   * @param graphics IGraphics instance
   * @param bounds Rectangle containing all parameter controls
   * @param layout UI layout configuration
   * @param morph Morph instance to query for parameters
   * @param paramManager ParameterManager to get IParam bindings
   * @param plugin Plugin instance to access IParams
   * @return Vector of created controls (caller should attach to DSP tab)
   */
  std::vector<ig::IControl*> BuildMorphParams(
    ig::IGraphics* graphics,
    const ig::IRECT& bounds,
    const UILayout& layout,
    const synaptic::IMorph* morph,
    const synaptic::ParameterManager& paramManager,
    iplug::Plugin* plugin);

  /**
   * @brief Calculate height needed for parameter controls
   * @param paramCount Number of parameters
   * @param layout UI layout configuration
   * @return Height in pixels needed for controls
   */
  static float CalcRequiredHeight(int paramCount, const UILayout& layout);

private:
  /**
   * @brief Generic method to build parameter controls from descriptions
   * @param graphics IGraphics instance
   * @param bounds Rectangle containing all parameter controls
   * @param layout UI layout configuration
   * @param descs Parameter descriptions
   * @param paramManager ParameterManager to get IParam bindings
   * @param plugin Plugin instance to access IParams
   * @return Vector of created controls
   */
  std::vector<ig::IControl*> BuildParamControls(
    ig::IGraphics* graphics,
    const ig::IRECT& bounds,
    const UILayout& layout,
    const std::vector<ExposedParamDesc>& descs,
    const synaptic::ParameterManager& paramManager,
    iplug::Plugin* plugin);

  /**
   * @brief Create a single control from parameter description
   * @param bounds Rectangle for control
   * @param desc Parameter description
   * @param paramIdx IParam index to bind to
   * @param layout UI layout configuration
   * @return Created control (or nullptr on failure)
   */
  ig::IControl* CreateControlForParam(
    const ig::IRECT& bounds,
    const ExposedParamDesc& desc,
    int paramIdx,
    const UILayout& layout);

  /**
   * @brief Find IParam index for a parameter ID
   * @param paramId Parameter string ID
   * @param paramManager ParameterManager instance
   * @return IParam index or -1 if not found
   */
  int FindParamIndex(
    const std::string& paramId,
    const synaptic::ParameterManager& paramManager) const;
};

} // namespace ui
} // namespace synaptic

