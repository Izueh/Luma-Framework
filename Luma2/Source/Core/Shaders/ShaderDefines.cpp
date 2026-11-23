#include "Shaders/ShaderDefines.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Game/GameRegistry.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Settings/SettingKeys.hpp"
#include "Utils/Debug.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Shaders {
namespace Defines {
namespace {
using Settings::Range;
using Settings::Setting;
using Settings::Visibility;
using Settings::Sources::Define;

constexpr std::string_view CATEGORY = "Shader Defines";

const std::array SETTINGS = {
   Setting{
      .key = Settings::Core::AUTO_COMPILE.key,
      .label = "Auto Compile",
      .tooltip = "Compiles the shaders as soon as a define changes. Off, the Compile Shaders button does.",
      .category = "Shaders",
#if DEVELOPMENT
      .default_value = false,
#else
      .default_value = true,
#endif
   },
};

// What AddDefine registers, copied: the registry keeps views. A deque, so they never move.
struct GameDefine
{
   std::string key; // "Defines.<NAME>"
   std::string name;
   std::string label;
   std::string tooltip;
};

std::deque<GameDefine> game_defines;
// The names SetDefault was given that have no setting in this build, warned about once each
std::unordered_set<std::string> missing_defines;

// What the last build started with, by name. Presenting thread.
std::unordered_map<std::string, std::string> compiled;
bool has_compiled = false;

// "GAME_<NAME>": the game's name upper case, other characters than letters and digits as underscores
std::string GetGameDefine()
{
   std::string define = "GAME_";
   for (const char c : Game::Registry::GetInfo().name)
      define += std::isalnum(uint8_t(c)) ? char(std::toupper(uint8_t(c))) : '_';
   return define;
}

bool IsPending(const Setting& setting, const Settings::Value& value)
{
   const Define* define = std::get_if<Define>(&setting.source);
   if (!define || !has_compiled)
      return false;
   const auto it = compiled.find(std::string(define->name));
   return it == compiled.end() || it->second != GetText(value);
}
} // namespace

void Register()
{
   Settings::Register(SETTINGS, Settings::Kind::Luma);
}

bool Add(const Definition& definition)
{
   const bool typed = std::holds_alternative<int32_t>(definition.default_value) || std::holds_alternative<bool>(definition.default_value);
   ASSERT_ONCE_MSG(typed, "A shader define's value is an int or a bool");
   if (!typed || definition.name.empty())
   {
      Log::Error("Shaders: define \"{}\" is skipped, its value must be an int or a bool", definition.name);
      return false;
   }
   const GameDefine& owned = game_defines.emplace_back(std::format("Defines.{}", definition.name), std::string(definition.name),
      std::string(definition.label.empty() ? definition.name : definition.label), std::string(definition.tooltip));
   const Setting setting = {
      .key = owned.key,
      .label = owned.label,
      .tooltip = owned.tooltip,
      .category = CATEGORY,
      .visibility = definition.visibility,
      .default_value = definition.default_value,
      .range = definition.range,
      .source = Define{owned.name},
   };
   if (Settings::Register({&setting, 1}, Settings::Kind::Define))
      return true;
   game_defines.pop_back();
   return false;
}

bool SetDefault(std::string_view key, std::string_view name, const Settings::Value& value)
{
   if (Settings::SetDefault(key, value))
      return true;
   if (missing_defines.emplace(name).second)
      Log::Warning("Shaders: the game's value for define {} is ignored: this build has no such define, or the value isn't of its type", name);
   return false;
}

std::vector<Compiler::Define> Collect()
{
#if DEVELOPMENT
   std::vector<Compiler::Define> defines = {{"DEVELOPMENT", "1"}, {"TEST", "1"}};
#elif TEST
   std::vector<Compiler::Define> defines = {{"DEVELOPMENT", "0"}, {"TEST", "1"}};
#else
   std::vector<Compiler::Define> defines = {{"DEVELOPMENT", "0"}, {"TEST", "0"}};
#endif
   Settings::ForEach([&](const Setting& setting, const Settings::Value& value) {
      if (const Define* define = std::get_if<Define>(&setting.source))
         defines.push_back({std::string(define->name), GetText(value)});
   });
   defines.push_back({GetGameDefine(), "1"});
#if DEVELOPMENT
   // The dev settings are in LumaSettings whatever the DEVELOPMENT define says, so the CPU and GPU layouts agree
   defines.push_back({"CPU_DEVELOPMENT", "1"});
#endif
   defines.append_range(Resources::LumaCBuffers::GetDefines());
   return defines;
}

void SetCompiled(const std::vector<Compiler::Define>& defines)
{
   compiled.clear();
   for (const Compiler::Define& define : defines)
      compiled.emplace(define.name, define.value);
   has_compiled = true;
}

std::optional<std::string> GetCompiledText(std::string_view name)
{
   const auto it = compiled.find(std::string(name));
   return it != compiled.end() ? std::optional(it->second) : std::nullopt;
}

bool IsPending(const Setting& setting)
{
   const std::optional<Settings::Value> value = Settings::GetValue(setting.key);
   return value && IsPending(setting, *value);
}

bool NeedsCompile()
{
   bool pending = false;
   Settings::ForEach([&](const Setting& setting, const Settings::Value& value) { pending |= IsPending(setting, value); });
   return pending;
}

std::string GetText(const Settings::Value& value)
{
   if (const bool* typed = std::get_if<bool>(&value))
      return *typed ? "1" : "0";
   if (const int32_t* typed = std::get_if<int32_t>(&value))
      return std::to_string(*typed);
   ASSERT_ONCE_MSG(false, "A define's setting is a bool or an integer");
   return {};
}
} // namespace Defines
} // namespace Shaders
} // namespace Luma
