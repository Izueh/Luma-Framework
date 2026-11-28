#include "Mcp/McpTools.hpp"

#if DEVELOPMENT
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Settings/SettingsRegistry.hpp"
#include "Utils/Json.hpp"

namespace Luma {
namespace Mcp {
// In a named namespace: glaze can't reflect types with internal linkage
namespace Replies {
struct Setting
{
   std::string key;
   std::string label;
   std::string tooltip;
   std::string category;
   std::string kind; // Game, Luma, Define, Recipe, Development: the table it's in
   std::string type;
   std::string visibility;
   std::string source;
   Utils::Json::Value value;
   Utils::Json::Value default_value;
   std::optional<Utils::Json::Value> min;
   std::optional<Utils::Json::Value> max;
   std::vector<std::string> options; // Enum labels
};

struct Settings
{
   bool ok = true;
   std::vector<Setting> settings;
   std::vector<std::string> ignored_args;
};

struct SetSetting
{
   bool ok = true;
   std::string key;
   Utils::Json::Value value; // After clamping
   std::vector<std::string> ignored_args;
};
} // namespace Replies

namespace {
Utils::Json::Value ToJson(const Settings::Setting& setting, const Settings::Value& value)
{
   Utils::Json::Value json;
   std::visit([&]<typename T>(const T& typed) {
      if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string>)
         json.data = typed;
      else if constexpr (std::is_same_v<T, int32_t>)
         json.data = int64_t(typed);
      else if constexpr (std::is_same_v<T, float>)
         json.data = double(typed);
      else if constexpr (std::is_same_v<T, Settings::Key>)
         json.data = int64_t(typed.code);
      else if (typed.value >= 0 && size_t(typed.value) < setting.enum_labels.size())
         json.data = std::string(setting.enum_labels[typed.value]);
      else
         json.data = int64_t(typed.value);
   },
      value);
   return json;
}

std::string_view GetVisibilityName(Settings::Visibility visibility)
{
   switch (visibility)
   {
   case Settings::Visibility::Test: return "test";
   case Settings::Visibility::Development: return "development";
   default: return "release";
   }
}

std::string_view GetSourceName(const Settings::Source& source)
{
   constexpr std::string_view NAMES[] = {"value", "binding", "define", "recipe"};
   return NAMES[source.index()];
}
} // namespace

namespace Tools {
std::string GetSettings(const Request& request)
{
   Replies::Settings reply;
   reply.ignored_args = GetIgnoredArgs(request, {});
   Settings::ForEach([&](const Settings::Setting& setting, const Settings::Value& value) {
      if (!Settings::IsVisibleInBuild(setting.visibility))
         return;
      Replies::Setting& info = reply.settings.emplace_back();
      info.key = setting.key;
      info.label = setting.label;
      info.tooltip = setting.tooltip;
      info.category = Settings::GetCategory(setting);
      info.kind = Settings::GetKindName(setting.kind);
      info.type = Settings::GetTypeName(setting.default_value);
      info.visibility = GetVisibilityName(setting.visibility);
      info.source = GetSourceName(setting.source);
      info.value = ToJson(setting, value);
      info.default_value = ToJson(setting, setting.default_value);
      if (setting.range)
      {
         info.min = ToJson(setting, setting.range->min);
         info.max = ToJson(setting, setting.range->max);
      }
      for (const std::string_view label : setting.enum_labels)
         info.options.emplace_back(label);
   });
   return Utils::Json::Write(reply);
}

std::string SetSetting(const Request& request)
{
   const auto name = request.args.find("name");
   const auto text = request.args.find("value");
   if (name == request.args.end() || text == request.args.end())
      return Error("set_setting takes a 'name' (the setting's key, see get_settings) and a 'value'");
   const Settings::Setting* setting = Settings::Find(name->second);
   if (!setting || !Settings::IsVisibleInBuild(setting->visibility))
      return Error(std::format("no setting '{}' (see get_settings for the keys)", name->second));
   const std::optional<Settings::Value> value = Settings::Parse(*setting, text->second);
   if (!value)
      return Error(std::format("'{}' isn't a {} value", text->second, Settings::GetTypeName(setting->default_value)));
   Settings::Set(setting->key, *value);
   Replies::SetSetting reply;
   reply.key = setting->key;
   reply.value = ToJson(*setting, *Settings::GetValue(setting->key));
   reply.ignored_args = GetIgnoredArgs(request, {"name", "value"});
   return Utils::Json::Write(reply);
}
} // namespace Tools
} // namespace Mcp
} // namespace Luma
#endif
