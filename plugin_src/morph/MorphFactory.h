#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "IMorph.h"
// Mode implementations
#include "modes/NoneMorph.h"
#include "modes/CrossSynthesisMorph.h"
#include "modes/SpectralVocoderMorph.h"
#include "modes/WaveMorph.h"

namespace synaptic
{
  struct MorphInfo
  {
    const char* id;
    const char* label;
    std::function<std::shared_ptr<IMorph>()> create;
    bool includeInUI = true;
  };

  class MorphFactory
  {
  public:
    static const std::vector<MorphInfo>& GetAll()
    {
      static const std::vector<MorphInfo> kAll = {
        { "none", "None", []{ return std::make_shared<NoneMorph>(); }, true },
        { "cross", "Cross Synthesis", []{ return std::make_shared<CrossSynthesisMorph>(); }, true },
        { "vocoder", "Spectral Vocoder", []{ return std::make_shared<SpectralVocoderMorph>(); }, true },
        { "wave", "Wave Morph", []{ return std::make_shared<WaveMorph>(); }, true },
      };
      return kAll;
    }

    static std::vector<const MorphInfo*> GetUiList()
    {
      std::vector<const MorphInfo*> out;
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

    static std::shared_ptr<IMorph> CreateById(const std::string& id)
    {
      for (const auto& t : GetAll())
        if (id == t.id) return t.create();
      return nullptr;
    }

    static std::shared_ptr<IMorph> CreateByUiIndex(int index)
    {
      if (index < 0) return nullptr;
      const auto list = GetUiList();
      if (index >= (int) list.size()) return nullptr;
      return list[index]->create();
    }
  };
}


