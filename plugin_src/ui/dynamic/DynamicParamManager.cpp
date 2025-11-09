#include "DynamicParamManager.h"
#include "../styles/UITheme.h"
#include "plugin_src/ChunkBufferTransformer.h"
#include "plugin_src/morph/IMorph.h"

using namespace iplug;
using namespace igraphics;

namespace synaptic {
namespace ui {

DynamicParamManager::DynamicParamManager()
{
}

std::vector<IControl*> DynamicParamManager::BuildTransformerParams(
  IGraphics* graphics,
  const IRECT& bounds,
  const UILayout& layout,
  const synaptic::IChunkBufferTransformer* transformer,
  const synaptic::ParameterManager& paramManager,
  Plugin* plugin)
{
  if (!transformer)
    return {};

  std::vector<ExposedParamDesc> descs;
  transformer->GetParamDescs(descs);
  return BuildParamControls(graphics, bounds, layout, descs, paramManager, plugin);
}

std::vector<IControl*> DynamicParamManager::BuildMorphParams(
  IGraphics* graphics,
  const IRECT& bounds,
  const UILayout& layout,
  const synaptic::IMorph* morph,
  const synaptic::ParameterManager& paramManager,
  Plugin* plugin)
{
  if (!morph)
    return {};

  std::vector<ExposedParamDesc> descs;
  morph->GetParamDescs(descs);
  return BuildParamControls(graphics, bounds, layout, descs, paramManager, plugin);
}

std::vector<IControl*> DynamicParamManager::BuildParamControls(
  IGraphics* graphics,
  const IRECT& bounds,
  const UILayout& layout,
  const std::vector<ExposedParamDesc>& descs,
  const synaptic::ParameterManager& paramManager,
  Plugin* plugin)
{
  std::vector<IControl*> controls;

  if (!graphics || !plugin || descs.empty())
    return controls;

  // Layout parameters in 2-column grid
  const float labelWidth = 160.f;
  const float rowHeight = layout.controlHeight;
  const float rowGap = 10.f;
  const float colGap = 20.f;
  const int cols = 2;

  float x = bounds.L;
  float y = bounds.T;
  int col = 0;

  for (const auto& desc : descs)
  {
    // Find corresponding IParam index
    int paramIdx = FindParamIndex(desc.id, paramManager);
    if (paramIdx < 0)
      continue; // Skip if no binding found

    // Calculate control bounds
    float colWidth = (bounds.W() - colGap) / 2.f;
    IRECT cellRect = IRECT(
      x + col * (colWidth + colGap),
      y,
      x + col * (colWidth + colGap) + colWidth,
      y + rowHeight
    );

    // Create label
    IRECT labelRect = cellRect.GetFromLeft(labelWidth);
    IControl* label = new ITextControl(labelRect, desc.label.c_str(), kLabelText);
    controls.push_back(label);

    // Create control
    IRECT controlRect = cellRect;
    controlRect.L = labelRect.R + 8.f;
    IControl* control = CreateControlForParam(controlRect, desc, paramIdx, layout);
    if (control)
      controls.push_back(control);

    // Advance to next row/column
    col++;
    if (col >= cols)
    {
      col = 0;
      y += rowHeight + rowGap;
    }
  }

  return controls;
}

float DynamicParamManager::CalcRequiredHeight(int paramCount, const UILayout& layout)
{
  if (paramCount == 0)
    return 0.f;

  const float rowHeight = layout.controlHeight;
  const float rowGap = 10.f;
  const int cols = 2;
  const int rows = (paramCount + cols - 1) / cols; // ceiling division

  return rows * rowHeight + (rows - 1) * rowGap + 20.f; // 20px padding
}

IControl* DynamicParamManager::CreateControlForParam(
  const IRECT& bounds,
  const ExposedParamDesc& desc,
  int paramIdx,
  const UILayout& layout)
{
  // Create appropriate control based on parameter type and control type
  switch (desc.type)
  {
    case ParamType::Number:
    {
      if (desc.control == ControlType::Slider)
      {
        // Create horizontal slider for slider-type parameters
        return new IVSliderControl(bounds, paramIdx, "", kSynapticStyle, true, EDirection::Horizontal);
      }
      else // NumberBox
      {
        return new IVNumberBoxControl(bounds, paramIdx, nullptr, "", kSynapticStyle);
      }
    }

    case ParamType::Boolean:
    {
      return new IVToggleControl(bounds, paramIdx, "", kSynapticStyle, "OFF", "ON");
    }

    case ParamType::Enum:
    {
      if ((int)desc.options.size() <= 4)
      {
        // Use tab switch for few options
        std::vector<const char*> labels;
        for (const auto& opt : desc.options)
          labels.push_back(opt.label.c_str());
        return new IVTabSwitchControl(
          bounds, paramIdx, labels, "", kSynapticStyle,
          EVShape::Rectangle, EDirection::Horizontal);
      }
      else
      {
        // Use menu button for many options
        return new IVMenuButtonControl(bounds, paramIdx, "", kSynapticStyle);
      }
    }

    case ParamType::Text:
    {
      // Text parameters are not fully supported in IPlug controls
      // Use a disabled text control as placeholder
      return new ITextControl(bounds, "[Text param]", kLabelText);
    }

    default:
      return nullptr;
  }
}

int DynamicParamManager::FindParamIndex(
  const std::string& paramId,
  const synaptic::ParameterManager& paramManager) const
{
  const auto& bindings = paramManager.GetBindings();
  for (const auto& binding : bindings)
  {
    if (binding.id == paramId)
      return binding.paramIdx;
  }
  return -1;
}

} // namespace ui
} // namespace synaptic

