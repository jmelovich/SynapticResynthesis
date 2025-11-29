/**
 * @file GenericFactory.h
 * @brief Generic factory template for plugin component registration
 *
 * Provides a reusable factory pattern for transformers, morphs, and other
 * pluggable components.
 *
 * Usage:
 *   1. Define an Info struct with: id, label, create function, includeInUI flag
 *   2. Create a derived factory class that provides GetAllEntries()
 *   3. Use the provided static methods for UI integration
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace synaptic
{

/**
 * @brief Generic registration info for factory entries
 *
 * @tparam ProductT The product type (e.g., IChunkBufferTransformer, IMorph)
 */
template<typename ProductT>
struct FactoryEntry
{
  const char* id;                                         ///< Stable string identifier
  const char* label;                                      ///< Human-readable label for UI
  std::function<std::shared_ptr<ProductT>()> create;      ///< Factory function
  bool includeInUI = true;                                ///< Whether to show in UI dropdown
};

/**
 * @brief Generic factory providing common operations for component registration
 *
 * Derived classes must implement GetAllEntries() to provide their specific registrations.
 *
 * @tparam ProductT The product type being created
 * @tparam DerivedT The CRTP derived class (for static polymorphism)
 */
template<typename ProductT, typename DerivedT>
class GenericFactory
{
public:
  using Entry = FactoryEntry<ProductT>;
  using ProductPtr = std::shared_ptr<ProductT>;

  /**
   * @brief Get cached filtered list of UI-visible entries
   *
   * The list is computed once on first access and cached for efficiency.
   * Thread-safe due to C++11 static local initialization guarantees.
   */
  static const std::vector<const Entry*>& GetUiList()
  {
    static const std::vector<const Entry*> cached = []() {
      std::vector<const Entry*> out;
      const auto& all = DerivedT::GetAllEntries();
      out.reserve(all.size());
      for (const auto& entry : all)
      {
        if (entry.includeInUI)
          out.push_back(&entry);
      }
      return out;
    }();
    return cached;
  }

  /**
   * @brief Get count of UI-visible entries (cached)
   */
  static int GetUiCount()
  {
    return static_cast<int>(GetUiList().size());
  }

  /**
   * @brief Get cached labels for UI-visible entries
   */
  static const std::vector<std::string>& GetUiLabels()
  {
    static const std::vector<std::string> cached = []() {
      std::vector<std::string> labels;
      const auto& list = GetUiList();
      labels.reserve(list.size());
      for (const auto* entry : list)
        labels.push_back(entry->label);
      return labels;
    }();
    return cached;
  }

  /**
   * @brief Get cached IDs for UI-visible entries
   */
  static const std::vector<std::string>& GetUiIds()
  {
    static const std::vector<std::string> cached = []() {
      std::vector<std::string> ids;
      const auto& list = GetUiList();
      ids.reserve(list.size());
      for (const auto* entry : list)
        ids.push_back(entry->id);
      return ids;
    }();
    return cached;
  }

  /**
   * @brief Find index of an ID in the UI list
   * @return Index if found, -1 otherwise
   */
  static int IndexOfIdInUi(const std::string& id)
  {
    const auto& list = GetUiList();
    for (int i = 0; i < static_cast<int>(list.size()); ++i)
    {
      if (id == list[i]->id)
        return i;
    }
    return -1;
  }

  /**
   * @brief Create a product by its stable ID
   * @return New instance, or nullptr if ID not found
   */
  static ProductPtr CreateById(const std::string& id)
  {
    for (const auto& entry : DerivedT::GetAllEntries())
    {
      if (id == entry.id)
        return entry.create();
    }
    return nullptr;
  }

  /**
   * @brief Create a product by its UI dropdown index
   * @return New instance, or nullptr if index out of range
   */
  static ProductPtr CreateByUiIndex(int index)
  {
    if (index < 0)
      return nullptr;
    const auto& list = GetUiList();
    if (index >= static_cast<int>(list.size()))
      return nullptr;
    return list[index]->create();
  }

  /**
   * @brief Get all registered entries (alias for derived GetAllEntries)
   */
  static const std::vector<Entry>& GetAll()
  {
    return DerivedT::GetAllEntries();
  }
};

} // namespace synaptic
