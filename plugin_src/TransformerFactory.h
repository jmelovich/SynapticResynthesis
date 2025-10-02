#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "ChunkBufferTransformer.h"
#include "transformers/SimpleSampleBrainTransformer.h"
#include "transformers/ExpandedSimpleSampleBrainTransformer.h"

namespace synaptic
{
  // Information about a transformer implementation.
  struct TransformerInfo
  {
    // Stable string id (do not change once published).
    const char* id;
    // Human-readable label for UI.
    const char* label;
    // Factory to construct a new instance.
    std::function<std::unique_ptr<IChunkBufferTransformer>()> create;
    // Whether to include in the UI dropdown.
    bool includeInUI = true;
  };

  class TransformerFactory
  {
  public:
    // Deterministic list of all known transformer implementations.
    // Order is stable and defines UI order for entries with includeInUI=true.
    static const std::vector<TransformerInfo>& GetAll()
    {
      // Static list defined here; edit to add/remove/disable entries.
      static const std::vector<TransformerInfo> kAll = {
        { "passthrough", "Passthrough", []{ return std::make_unique<PassthroughTransformer>(); }, true },
        { "sinematch", "Simple Sine Match", []{ return std::make_unique<SineMatchTransformer>(); }, true },
        { "samplebrain", "Simple SampleBrain", []{ return std::make_unique<SimpleSampleBrainTransformer>(); }, true },
        { "expandedsamplebrain", "Expanded SampleBrain", []{ return std::make_unique<ExpandedSimpleSampleBrainTransformer>(); }, true },
      };
      return kAll;
    }

    // Filtered view for UI-visible transformers.
    static std::vector<const TransformerInfo*> GetUiList()
    {
      std::vector<const TransformerInfo*> out;
      const auto& all = GetAll();
      out.reserve(all.size());
      for (const auto& t : all)
        if (t.includeInUI) out.push_back(&t);
      return out;
    }

    static int GetUiCount()
    {
      int n = 0;
      for (const auto& t : GetAll()) if (t.includeInUI) ++n;
      return n;
    }

    static std::vector<std::string> GetUiLabels()
    {
      std::vector<std::string> labels;
      const auto list = GetUiList();
      labels.reserve(list.size());
      for (const auto* t : list) labels.push_back(t->label);
      return labels;
    }

    static std::vector<std::string> GetUiIds()
    {
      std::vector<std::string> ids;
      const auto list = GetUiList();
      ids.reserve(list.size());
      for (const auto* t : list) ids.push_back(t->id);
      return ids;
    }

    static int IndexOfIdInUi(const std::string& id)
    {
      const auto list = GetUiList();
      for (int i = 0; i < (int) list.size(); ++i)
        if (id == list[i]->id) return i;
      return -1;
    }

    static std::unique_ptr<IChunkBufferTransformer> CreateById(const std::string& id)
    {
      for (const auto& t : GetAll())
        if (id == t.id) return t.create();
      return nullptr;
    }

    static std::unique_ptr<IChunkBufferTransformer> CreateByUiIndex(int index)
    {
      if (index < 0) return nullptr;
      const auto list = GetUiList();
      if (index >= (int) list.size()) return nullptr;
      return list[index]->create();
    }
  };
}


