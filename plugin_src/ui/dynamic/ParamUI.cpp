#include "ParamUI.h"
#include "../styles/UITheme.h"

using namespace iplug;
using namespace igraphics;

namespace synaptic {
namespace ui {

static std::string keyFor(ParamType t, const char* hint)
{
  return std::to_string(static_cast<int>(t)) + std::string("|") + (hint ? hint : "");
}

ParamRendererRegistry& ParamRendererRegistry::instance()
{
  static ParamRendererRegistry inst;
  return inst;
}

void ParamRendererRegistry::registerBuilder(ParamType type, const char* hint, ControlBuilder builder)
{
  mBuilders[keyFor(type, hint)] = std::move(builder);
}

ControlBuilder ParamRendererRegistry::resolve(const ParamSpec& spec) const
{
  auto it = mBuilders.find(keyFor(spec.type, spec.uiHint));
  if (it != mBuilders.end()) return it->second;
  // Fallback with empty hint
  auto it2 = mBuilders.find(keyFor(spec.type, ""));
  if (it2 != mBuilders.end()) return it2->second;
  return {};
}

DynamicParamPanel::DynamicParamPanel(const IRECT& bounds)
: IControl(bounds)
{
  SetIgnoreMouse(true);
}

void DynamicParamPanel::setSchema(const ParamSchema& schema)
{
  mSchema = schema;
}

void DynamicParamPanel::setStyle(const IVStyle& style)
{
  mStyle = style;
}

std::string DynamicParamPanel::makeKey(const ParamSpec& spec) const
{
  return std::string(spec.name ? spec.name : "");
}

void DynamicParamPanel::Clear(IGraphics* g)
{
  for (auto* c : mChildren) {
    if (c) g->RemoveControl(c);
  }
  mChildren.clear();
}

void DynamicParamPanel::Rebuild(IGraphics* g, const UILayout& layout)
{
  if (!g) return;
  Clear(g);

  // Basic grid: 2 columns, responsive could be added later
  const int cols = 2;
  const float gapX = 16.f;
  const float gapY = 10.f;
  const float labelW = 160.f;

  IRECT inner = mRECT.GetPadded(-layout.cardPadding);
  float x = inner.L;
  float y = inner.T;
  float colW = (inner.W() - gapX) / static_cast<float>(cols);
  int col = 0;

  for (const auto& spec : mSchema.params)
  {
    ControlBuilder builder = ParamRendererRegistry::instance().resolve(spec);
    if (!builder) continue;

    IRECT cell = IRECT(x + col * (colW + gapX), y, x + col * (colW + gapX) + colW, y + layout.controlHeight);
    // Label on left
    IRECT labelRect = cell.GetFromLeft(labelW);
    IRECT controlRect = cell;
    controlRect.L = labelRect.R + 8.f;

    auto* label = new ITextControl(labelRect, spec.name ? spec.name : "", kLabelText);
    auto* control = builder(controlRect, spec, mStyle);
    if (label) mChildren.push_back(label);
    if (control) mChildren.push_back(control);
    if (label) g->AttachControl(label);
    if (control) g->AttachControl(control);

    col++;
    if (col >= cols)
    {
      col = 0;
      y += layout.controlHeight + gapY;
    }
  }
}

} // namespace ui
} // namespace synaptic


