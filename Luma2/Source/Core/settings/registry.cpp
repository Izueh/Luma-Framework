#include "settings/registry.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils/log.hpp"

namespace Luma {
namespace Settings {
namespace {
struct Entry
{
   Setting setting;
   Value value;
};

struct StringHash
{
   using is_transparent = void;
   size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
};

std::vector<Entry> entries;
std::unordered_map<std::string, size_t, StringHash, std::equal_to<>> entry_indices_by_key;
std::vector<ChangeListener> change_listeners;

Entry* FindEntry(std::string_view key)
{
   const auto it = entry_indices_by_key.find(key);
   return it != entry_indices_by_key.end() ? &entries[it->second] : nullptr;
}

bool IsRangeValid(const Setting& setting)
{
   if (!setting.range)
      return true;
   const bool is_numeric = std::holds_alternative<int32_t>(setting.default_value) || std::holds_alternative<float>(setting.default_value);
   return is_numeric && setting.range->min.index() == setting.default_value.index() && setting.range->max.index() == setting.default_value.index();
}
} // namespace

bool Register(std::span<const Setting> settings)
{
   bool all_registered = true;
   for (const Setting& setting : settings)
   {
      if (setting.key.empty() || entry_indices_by_key.contains(setting.key))
      {
         Utils::Log::Error("Settings: skipping setting with empty or duplicate key \"{}\"", setting.key);
         all_registered = false;
         continue;
      }
      if (!IsRangeValid(setting))
      {
         Utils::Log::Error("Settings: skipping setting \"{}\", its range doesn't suit its type", setting.key);
         all_registered = false;
         continue;
      }
      entry_indices_by_key.emplace(std::string(setting.key), entries.size());
      entries.push_back({setting, Clamp(setting, setting.default_value)});
   }
   return all_registered;
}

void AddChangeListener(ChangeListener listener)
{
   change_listeners.push_back(listener);
}

void Clear()
{
   entries.clear();
   entry_indices_by_key.clear();
   change_listeners.clear();
}

const Setting* Find(std::string_view key)
{
   const Entry* entry = FindEntry(key);
   return entry ? &entry->setting : nullptr;
}

const Value* GetValue(std::string_view key)
{
   const Entry* entry = FindEntry(key);
   return entry ? &entry->value : nullptr;
}

bool Set(std::string_view key, const Value& value)
{
   Entry* entry = FindEntry(key);
   if (!entry || entry->setting.default_value.index() != value.index())
      return false;
   const Value clamped_value = Clamp(entry->setting, value);
   if (clamped_value == entry->value)
      return true;
   entry->value = clamped_value;
   for (const ChangeListener listener : change_listeners)
      listener(entry->setting, entry->value);
   return true;
}

bool Reset(std::string_view key)
{
   const Setting* setting = Find(key);
   return setting && Set(key, setting->default_value);
}

void ForEach(const std::function<void(const Setting& setting, const Value& value)>& function)
{
   for (const Entry& entry : entries)
      function(entry.setting, entry.value);
}

std::string_view GetCategory(const Setting& setting)
{
   return setting.category.empty() ? DEFAULT_CATEGORY : setting.category;
}

Value Clamp(const Setting& setting, const Value& value)
{
   return std::visit([&setting]<typename T>(T typed) -> Value {
      if constexpr (std::is_same_v<T, EnumIndex>)
      {
         const int32_t max_index = std::max(int32_t(setting.enum_labels.size()) - 1, 0);
         return EnumIndex{std::clamp(typed.value, 0, max_index)};
      }
      else if constexpr (std::is_same_v<T, bool>)
      {
         return typed;
      }
      else
      {
         return setting.range ? std::clamp(typed, std::get<T>(setting.range->min), std::get<T>(setting.range->max)) : typed;
      }
   },
      value);
}

bool IsVisibleInBuild(Visibility visibility)
{
   switch (visibility)
   {
   case Visibility::Test:
#if TEST || DEVELOPMENT
      return true;
#else
      return false;
#endif
   case Visibility::Development:
#if DEVELOPMENT
      return true;
#else
      return false;
#endif
   default: return true;
   }
}
} // namespace Settings
} // namespace Luma
