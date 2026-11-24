#include "Settings/SettingsStorage.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Log/Log.hpp"
#include "Utils/Toml.hpp"

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
      else if constexpr (std::is_same_v<T, std::string>)
      {
         if (const std::string* value = std::get_if<std::string>(&node.data))
            return Value(*value);
      }
      else if constexpr (std::is_same_v<T, Key>)
      {
         if (const int64_t* value = std::get_if<int64_t>(&node.data))
            return Value(Key{static_cast<uint32_t>(*value)});
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
      else if constexpr (std::is_same_v<T, std::string>)
         result.data = typed_value;
      else if constexpr (std::is_same_v<T, Key>)
         result.data = int64_t(typed_value.code);
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

std::string_view WithoutPrefix(std::string_view key, std::string_view prefix)
{
   if (key.size() > prefix.size() + 1 && key.starts_with(prefix) && key[prefix.size()] == '.')
      key.remove_prefix(prefix.size() + 1);
   return key;
}

// Where a setting is in the file, by its kind: a game's in [<Category>], Luma's in [Luma.<Category>] ([Luma] for its "Luma"
// category), defines in [Defines] by their name, recipes in [Recipes."<identity>"], development ones in [Development]. The key leaves out
// what the table already says: "Luma.Overlay.Key" is Key in [Luma.Overlay].
struct Place
{
   std::vector<std::string_view> tables;
   std::string_view key;
};

Place GetPlace(const Setting& setting)
{
   const std::string_view category = GetCategory(setting);
   switch (setting.kind)
   {
   case Kind::Luma:
   {
      const std::string_view key = WithoutPrefix(setting.key, "Luma");
      if (category == "Luma")
         return {{"Luma"}, key};
      return {{"Luma", category}, WithoutPrefix(key, category)};
   }
   case Kind::Define:
   {
      const Sources::Define* define = std::get_if<Sources::Define>(&setting.source);
      return {{"Defines"}, define ? define->name : WithoutPrefix(setting.key, "Defines")};
   }
   case Kind::Recipe:
   {
      // A table per recipe, by its identity: its switch is Enabled, its environment values by name
      if (const Sources::Recipe* recipe = std::get_if<Sources::Recipe>(&setting.source))
         return {{"Recipes", recipe->identity}, recipe->env.empty() ? std::string_view("Enabled") : recipe->env};
      return {{"Recipes"}, WithoutPrefix(setting.key, "Recipes")};
   }
   case Kind::Development: return {{"Development"}, WithoutPrefix(WithoutPrefix(setting.key, "Luma"), "Development")};
   default: return {{category}, WithoutPrefix(setting.key, category)};
   }
}

const TomlValue* FindNode(const Setting& setting)
{
   const Place place = GetPlace(setting);
   const Table* table = &document;
   for (const std::string_view name : place.tables)
   {
      const auto it = table->find(name);
      if (it == table->end() || !it->second.is_object())
         return nullptr;
      table = &it->second.get_object();
   }
   const auto node = table->find(place.key);
   return node != table->end() ? &node->second : nullptr;
}

TomlValue& GetNode(const Setting& setting)
{
   const Place place = GetPlace(setting);
   Table* table = &document;
   for (const std::string_view name : place.tables)
   {
      TomlValue& child = (*table)[std::string(name)];
      if (!child.is_object())
         child.data = Table{};
      table = &child.get_object();
   }
   return (*table)[std::string(place.key)];
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
         Log::Warning("Settings: ignoring \"{}\", it has the wrong type", setting.key);
   });
   return true;
}

bool Save(const std::filesystem::path& path)
{
   ForEach([](const Setting& setting, const Value& value) {
      // PeakWhite stores 0 when at the display's default so future launches and monitor changes auto-follow the display
      const bool store_zero_default = setting.key == Core::PEAK_WHITE.key && value == Clamp(setting, setting.default_value);
      GetNode(setting) = WriteValue(setting, store_zero_default ? Value(0.f) : value);
   });
   return Utils::Toml::Save(path, document);
}

std::string GetOverlayLayout()
{
   const Table* table = &document;
   for (const std::string_view name : {"Luma", "Overlay", "Layout"})
   {
      const auto it = table->find(name);
      if (it == table->end() || !it->second.is_object())
         return {};
      table = &it->second.get_object();
   }
   const auto it = table->find("ImGui");
   if (it == table->end())
      return {};
   if (const std::string* text = std::get_if<std::string>(&it->second.data))
      return *text;
   return {};
}

void SetOverlayLayout(std::string_view ini)
{
   if (ini.empty())
   {
      if (const auto luma = document.find("Luma"); luma != document.end() && luma->second.is_object())
      {
         Table& luma_table = luma->second.get_object();
         if (const auto overlay = luma_table.find("Overlay"); overlay != luma_table.end() && overlay->second.is_object())
            overlay->second.get_object().erase("Layout");
      }
      return;
   }
   Table* table = &document;
   for (const std::string_view name : {"Luma", "Overlay", "Layout"})
   {
      TomlValue& child = (*table)[std::string(name)];
      if (!child.is_object())
         child.data = Table{};
      table = &child.get_object();
   }
   TomlValue value;
   value.data = std::string(ini);
   (*table)["ImGui"] = std::move(value);
}

void Reset()
{
   document = {};
}
} // namespace Storage
} // namespace Settings
} // namespace Luma
