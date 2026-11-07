#include "settings/storage.hpp"

#include <optional>
#include <string>
#include <string_view>

#include "settings/registry.hpp"
#include "utils/log.hpp"
#include "utils/toml.hpp"

namespace Luma {
namespace Settings {
namespace Storage {
namespace {
using Utils::Toml::Table;
using TomlValue = Utils::Toml::Value;

// The last loaded file, so unknown keys survive a save
Table document;

// Integers are accepted where floats are expected ("value = 1")
std::optional<double> ReadNumber(const TomlValue& node)
{
   if (const int64_t* integer = std::get_if<int64_t>(&node.data))
      return double(*integer);
   if (const double* number = std::get_if<double>(&node.data))
      return *number;
   return std::nullopt;
}

std::optional<Value> ReadValue(const Setting& setting, const TomlValue& node)
{
   return std::visit([&](const auto& default_value) -> std::optional<Value> {
      using T = std::decay_t<decltype(default_value)>;
      if constexpr (std::is_same_v<T, bool>)
      {
         if (const bool* value = std::get_if<bool>(&node.data))
            return Value(*value);
      }
      else if constexpr (std::is_same_v<T, int32_t>)
      {
         if (const int64_t* value = std::get_if<int64_t>(&node.data))
            return Value(static_cast<int32_t>(*value));
      }
      else if constexpr (std::is_same_v<T, float>)
      {
         if (const std::optional<double> value = ReadNumber(node))
            return Value(static_cast<float>(*value));
      }
      else if constexpr (std::is_same_v<T, EnumIndex>)
      {
         // Stored by label, indices are accepted too
         if (const std::string* label = std::get_if<std::string>(&node.data))
         {
            for (size_t i = 0; i < setting.enum_labels.size(); i++)
            {
               if (setting.enum_labels[i] == *label)
                  return Value(EnumIndex{static_cast<int32_t>(i)});
            }
         }
         else if (const int64_t* index = std::get_if<int64_t>(&node.data))
         {
            return Value(EnumIndex{static_cast<int32_t>(*index)});
         }
      }
      return std::nullopt;
   },
      setting.default_value);
}

TomlValue WriteValue(const Setting& setting, const Value& value)
{
   return std::visit([&](const auto& typed_value) -> TomlValue {
      using T = std::decay_t<decltype(typed_value)>;
      TomlValue result;
      if constexpr (std::is_same_v<T, bool>)
         result.data = typed_value;
      else if constexpr (std::is_same_v<T, int32_t>)
         result.data = int64_t(typed_value);
      else if constexpr (std::is_same_v<T, float>)
         result.data = double(typed_value);
      else if constexpr (std::is_same_v<T, EnumIndex>)
      {
         if (typed_value.value >= 0 && static_cast<size_t>(typed_value.value) < setting.enum_labels.size())
            result.data = std::string(setting.enum_labels[typed_value.value]);
         else
            result.data = int64_t(typed_value.value);
      }
      return result;
   },
      value);
}

// The key within its category's table, without a "<category>." prefix: "Luma.OverlayKey" is OverlayKey in [Luma]
std::string_view GetTableKey(const Setting& setting)
{
   const std::string_view category = GetCategory(setting);
   std::string_view key = setting.key;
   if (key.size() > category.size() + 1 && key.starts_with(category) && key[category.size()] == '.')
      key.remove_prefix(category.size() + 1);
   return key;
}

const TomlValue* FindNode(const Setting& setting)
{
   const auto category = document.find(GetCategory(setting));
   if (category == document.end() || !category->second.is_object())
      return nullptr;
   const Table& table = category->second.get_object();
   const auto node = table.find(GetTableKey(setting));
   return node != table.end() ? &node->second : nullptr;
}
} // namespace

bool Load(const std::filesystem::path& path)
{
   Table loaded;
   if (!Utils::Toml::Load(path, loaded))
      return false;
   document = std::move(loaded);

   ForEach([](const Setting& setting, const Value&) {
      const TomlValue* node = FindNode(setting);
      if (!node)
         return;
      if (const std::optional<Value> value = ReadValue(setting, *node))
         Set(setting.key, *value);
      else
         Utils::Log::Warning("Settings: ignoring \"{}.{}\", it has the wrong type", GetCategory(setting), GetTableKey(setting));
   });
   return true;
}

bool Save(const std::filesystem::path& path)
{
   ForEach([](const Setting& setting, const Value& value) {
      TomlValue& category = document[std::string(GetCategory(setting))];
      if (!category.is_object())
         category.data = Table{};
      category.get_object()[std::string(GetTableKey(setting))] = WriteValue(setting, value);
   });
   return Utils::Toml::Save(path, document);
}

void Reset()
{
   document = {};
}
} // namespace Storage
} // namespace Settings
} // namespace Luma
