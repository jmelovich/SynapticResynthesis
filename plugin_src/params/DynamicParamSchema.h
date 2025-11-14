#pragma once

#include <string>
#include <vector>

namespace synaptic
{
  // Shared dynamic parameter schema used by transformers and morph modes
  enum class ParamType { Number, Boolean, Enum, Text };
  enum class ControlType { Slider, NumberBox, Select, Checkbox, TextBox };

  struct ParamOption
  {
    std::string value;  // internal value
    std::string label;  // human-readable label
  };

  struct ExposedParamDesc
  {
    std::string id;           // unique, stable identifier
    std::string label;        // display name
    ParamType type = ParamType::Number;
    ControlType control = ControlType::NumberBox;
    // Numeric constraints (for Number)
    double minValue = 0.0;
    double maxValue = 1.0;
    double step = 0.01;
    // Options (for Enum)
    std::vector<ParamOption> options;
    // Defaults
    double defaultNumber = 0.0;
    bool defaultBool = false;
    std::string defaultString;
  };

  // Owners expose dynamic params through this interface
  struct IDynamicParamOwner
  {
    virtual ~IDynamicParamOwner() {}

    // Describe all exposed parameters (schema)
    // Set includeAll=true to bypass conditional visibility checks and return ALL parameters
    virtual void GetParamDescs(std::vector<ExposedParamDesc>& out, bool includeAll = false) const = 0;

    // Get current value by id
    virtual bool GetParamAsNumber(const std::string& id, double& out) const = 0;
    virtual bool GetParamAsBool(const std::string& id, bool& out) const = 0;
    virtual bool GetParamAsString(const std::string& id, std::string& out) const = 0;

    // Set value by id
    virtual bool SetParamFromNumber(const std::string& id, double v) = 0;
    virtual bool SetParamFromBool(const std::string& id, bool v) = 0;
    virtual bool SetParamFromString(const std::string& id, const std::string& v) = 0;

    // Check if changing this parameter requires a UI rebuild (e.g., when it controls visibility of other params)
    virtual bool ParamChangeRequiresUIRebuild(const std::string& id) const { return false; }
  };
}


