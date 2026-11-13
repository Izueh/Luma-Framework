#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Settings/Registry.hpp"
#include "Shaders/Compiler.hpp"

// Shader defines are settings of Kind::Define with a Settings::Sources::Define source: Luma's (1.0's shader_defines_data, ids in
// Settings/Keys.hpp), registered by Core before the game's Init, which sets their values with Game::Shaders::SetDefine and adds
// its own with Game::Shaders::AddDefine. With the build's fixed ones (DEVELOPMENT, the game's name, the cbuffer slots) they make
// the defines every shader is compiled with.
namespace Luma {
namespace Shaders {
namespace Defines {
// A game's own define (Game::Shaders::AddDefine). Its key ("Defines.<name>"), category and source are generated.
struct Definition
{
   std::string_view name; // As the shaders read it, global to the compiler: a name registered twice is refused
   std::string_view label; // The name when empty
   std::string_view tooltip;
   Settings::Value default_value = 0; // An int32_t or a bool
   std::optional<Settings::Range> range;
   Settings::Visibility visibility = Settings::Visibility::Release;
};

// Luma's defines and Auto Compile, before the game's Init
void Register();
// Init only. False, registering nothing, for a value that isn't an int or a bool, or a name already registered. The strings are
// copied.
bool Add(const Definition& definition);
// Init only: a define's default (Game::Shaders::SetDefine). False, with a warning once per name, for a define this build doesn't
// have (a Development one in another build) or a value of another type.
bool SetDefault(std::string_view key, std::string_view name, const Settings::Value& value);

// Presenting thread: the defines every shader is compiled with now
std::vector<Compiler::Define> Collect();
// Presenting thread: a build started with `defines` (Collect's)
void SetCompiled(const std::vector<Compiler::Define>& defines);
// Presenting thread: the define setting's value differs from the one the shaders were last built with (never before the first
// build)
bool IsPending(const Settings::Setting& setting);
// The text it was last built with, empty before the first build or for a define the build didn't have
std::optional<std::string> GetCompiledText(std::string_view name);
// Presenting thread: a define is pending
bool NeedsCompile();

// The text a define's setting compiles to: bools as 0 and 1, integers as they are
std::string GetText(const Settings::Value& value);
} // namespace Defines
} // namespace Shaders
} // namespace Luma
