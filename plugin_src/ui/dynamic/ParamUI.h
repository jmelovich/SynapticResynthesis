#pragma once

#include <functional>
#include <vector>
#include <unordered_map>
#include <string>
#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"
#include "../layout/UILayout.h"
#include "../styles/UIStyles.h"

namespace synaptic {
namespace ui {
namespace ig = iplug::igraphics; // shorten qualifiers in header

enum class ParamType { Bool, Enum, Int, Float, String };
enum class Scale { Linear, Log, Exp };

struct EnumOption { int value; const char* label; };

struct ParamSpec {
  int paramId;
  const char* name;
  const char* group; // optional
  ParamType type;
  double min = 0.0, max = 1.0;
  double step = 0.0;
  double defaultValue = 0.0;
  Scale scale = Scale::Linear;
  const char* unit = "";
  std::vector<EnumOption> options;
  const char* uiHint = ""; // e.g., "slider", "knob", "tabs", "menu"
};

struct ParamSchema {
  int ownerTag = 0; // algorithm/morph id
  std::vector<ParamSpec> params;
};

using ControlBuilder = std::function<ig::IControl*(const ig::IRECT&, const ParamSpec&, const ig::IVStyle&)>;

class ParamRendererRegistry {
public:
  static ParamRendererRegistry& instance();
  void registerBuilder(ParamType type, const char* hint, ControlBuilder builder);
  ControlBuilder resolve(const ParamSpec& spec) const;
private:
  std::unordered_map<std::string, ControlBuilder> mBuilders; // key: type|hint
};

class DynamicParamPanel : public ig::IControl {
public:
  explicit DynamicParamPanel(const ig::IRECT& bounds);
  void setSchema(const ParamSchema& schema);
  void setStyle(const ig::IVStyle& style);
  void Rebuild(ig::IGraphics* g, const UILayout& layout);
  void Clear(ig::IGraphics* g);
private:
  std::string makeKey(const ParamSpec& spec) const;
private:
  ParamSchema mSchema;
  ig::IVStyle mStyle;
  std::vector<ig::IControl*> mChildren;
};

} // namespace ui
} // namespace synaptic


