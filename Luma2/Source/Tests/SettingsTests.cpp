#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

#include <Windows.h>

#include "Settings/SettingsRegistry.hpp"
#include "Settings/SettingsStorage.hpp"
#include "Shaders/ShaderDefines.hpp"

using namespace Luma;
using namespace Luma::Settings;

namespace {
constexpr std::array<std::string_view, 3> QUALITY_LABELS = {"Low", "Medium", "High"};

constexpr std::array<Setting, 4> TEST_SETTINGS = {{
   {.key = "Enabled", .label = "Enabled", .category = "Display", .default_value = true},
   {.key = "Count", .label = "Count", .category = "Display", .default_value = int32_t(5), .range = Range{0, 10}},
   {.key = "Brightness", .label = "Brightness", .category = "Display", .default_value = 1.f, .range = Range{0.f, 2.f}},
   {.key = "Quality", .label = "Quality", .default_value = EnumIndex{1}, .enum_labels = QUALITY_LABELS},
}};

// Fresh registry and a unique temporary file for each test
struct Fixture
{
   std::filesystem::path path;

   Fixture()
   {
      Settings::Clear();
      Storage::Reset();
      REQUIRE(Register(TEST_SETTINGS));
      path = std::filesystem::temp_directory_path() / std::format("luma2_settings_test_{}.toml", GetCurrentProcessId());
      std::filesystem::remove(path);
   }

   ~Fixture()
   {
      std::error_code error;
      std::filesystem::remove(path, error);
      Settings::Clear();
      Storage::Reset();
   }

   void WriteFile(std::string_view content) const
   {
      std::ofstream(path) << content;
   }

   std::string ReadFile() const
   {
      std::stringstream stream;
      stream << std::ifstream(path).rdbuf();
      return stream.str();
   }
};

int listener_calls = 0;

void CountChanges(const Setting&, const Value&) { listener_calls++; }
} // namespace

TEST_CASE_FIXTURE(Fixture, "Settings: defaults are registered and typed")
{
   CHECK(Get<bool>("Enabled") == true);
   CHECK(Get<int32_t>("Count") == 5);
   CHECK(Get<float>("Brightness") == 1.f);
   CHECK(Get<EnumIndex>("Quality").value == 1);
   // Wrong type or missing key give a default constructed value
   CHECK(Get<int32_t>("Enabled") == 0);
   CHECK(Get<bool>("Missing") == false);
   CHECK(std::string_view(GetCategory(*Find("Quality"))) == DEFAULT_CATEGORY);
}

TEST_CASE_FIXTURE(Fixture, "Settings: duplicate keys are rejected")
{
   const std::array<Setting, 1> duplicate = {{{.key = "Count", .default_value = int32_t(0)}}};
   CHECK_FALSE(Register(duplicate));
   CHECK(Get<int32_t>("Count") == 5);
}

TEST_CASE_FIXTURE(Fixture, "Settings: a range that doesn't suit the setting's type is rejected")
{
   const std::array<Setting, 3> invalid = {{
      {.key = "OtherType", .default_value = int32_t(0), .range = Range{0.f, 1.f}},
      {.key = "MixedTypes", .default_value = int32_t(0), .range = Range{0, 1.f}},
      {.key = "NotNumeric", .default_value = true, .range = Range{false, true}},
   }};
   CHECK_FALSE(Register(invalid));
   for (const Setting& setting : invalid)
      CHECK_FALSE(Find(setting.key));
}

TEST_CASE("Settings: visibility follows the build")
{
   CHECK(IsVisibleInBuild(Visibility::Release));
#if DEVELOPMENT
   CHECK(IsVisibleInBuild(Visibility::Test));
   CHECK(IsVisibleInBuild(Visibility::Development));
#elif TEST
   CHECK(IsVisibleInBuild(Visibility::Test));
   CHECK_FALSE(IsVisibleInBuild(Visibility::Development));
#else
   CHECK_FALSE(IsVisibleInBuild(Visibility::Test));
   CHECK_FALSE(IsVisibleInBuild(Visibility::Development));
#endif
}

TEST_CASE_FIXTURE(Fixture, "Settings: set clamps, validates the type and notifies listeners")
{
   listener_calls = 0;
   AddChangeListener(&CountChanges);

   CHECK(Set("Count", int32_t(50)));
   CHECK(Get<int32_t>("Count") == 10);
   CHECK(Set("Brightness", -1.f));
   CHECK(Get<float>("Brightness") == 0.f);
   CHECK(Set("Quality", EnumIndex{7}));
   CHECK(Get<EnumIndex>("Quality").value == 2);
   CHECK(listener_calls == 3);

   // Same value, no notification
   CHECK(Set("Count", int32_t(10)));
   CHECK(listener_calls == 3);

   CHECK_FALSE(Set("Count", 1.f));
   CHECK_FALSE(Set("Missing", true));

   CHECK(Reset("Count"));
   CHECK(Get<int32_t>("Count") == 5);
}

TEST_CASE_FIXTURE(Fixture, "Storage: a missing file is fine and keeps defaults")
{
   CHECK(Storage::Load(path));
   CHECK(Get<int32_t>("Count") == 5);
}

TEST_CASE_FIXTURE(Fixture, "Storage: every value type round trips")
{
   Set("Enabled", false);
   Set("Count", int32_t(7));
   Set("Brightness", 1.5f);
   Set("Quality", EnumIndex{2});
   REQUIRE(Storage::Save(path));

   // Enums are stored by label
   CHECK(ReadFile().find("Quality = \"High\"") != std::string::npos);

   for (const Setting& setting : TEST_SETTINGS)
      Reset(setting.key);
   REQUIRE(Storage::Load(path));

   CHECK(Get<bool>("Enabled") == false);
   CHECK(Get<int32_t>("Count") == 7);
   CHECK(Get<float>("Brightness") == 1.5f);
   CHECK(Get<EnumIndex>("Quality").value == 2);
}

TEST_CASE_FIXTURE(Fixture, "Storage: loading clamps, ignores wrong types and keeps unknown keys on save")
{
   WriteFile(R"(
[Display]
Count = 99
Brightness = 2
Enabled = "yes"
Unknown = 3

[General]
Quality = 0

[Other]
Kept = true
)");
   REQUIRE(Storage::Load(path));
   CHECK(Get<int32_t>("Count") == 10);
   CHECK(Get<float>("Brightness") == 2.f); // Integers are accepted for floats
   CHECK(Get<bool>("Enabled") == true); // Wrong type, default kept
   CHECK(Get<EnumIndex>("Quality").value == 0); // Indices are accepted for enums

   REQUIRE(Storage::Save(path));
   const std::string content = ReadFile();
   CHECK(content.find("Unknown = 3") != std::string::npos);
   CHECK(content.find("Kept = true") != std::string::npos);
   CHECK(content.find("Count = 10") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "Storage: a key's category prefix isn't repeated in the file")
{
   const std::array<Setting, 1> prefixed = {{{.key = "Display.Gamma", .category = "Display", .default_value = 2.2f}}};
   REQUIRE(Register(prefixed));
   WriteFile("[Display]\nGamma = 2.4\n");
   REQUIRE(Storage::Load(path));
   CHECK(Get<float>("Display.Gamma") == 2.4f);

   Set("Display.Gamma", 2.f);
   REQUIRE(Storage::Save(path));
   const std::string content = ReadFile();
   CHECK(content.find("Gamma = 2") != std::string::npos);
   CHECK(content.find("Display.Gamma") == std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "Storage: a malformed file fails to load and changes nothing")
{
   WriteFile("[Display\nCount = ");
   CHECK_FALSE(Storage::Load(path));
   CHECK(Get<int32_t>("Count") == 5);
}

TEST_CASE_FIXTURE(Fixture, "Settings: a later default replaces the registered one, and the value while it was at the default")
{
   const std::array settings = {
      Settings::Setting{.key = "Later.Unchanged", .default_value = int32_t(0), .range = Settings::Range{0, 9}},
      Settings::Setting{.key = "Later.Changed", .default_value = false},
   };
   REQUIRE(Settings::Register(settings));
   REQUIRE(Settings::Set("Later.Changed", true));
   CHECK(Settings::SetDefault("Later.Unchanged", 20));
   CHECK(Settings::Find("Later.Unchanged")->default_value == Settings::Value(int32_t(9)));
   CHECK(Settings::Get<int32_t>("Later.Unchanged") == 9);
   CHECK(Settings::SetDefault("Later.Changed", false));
   CHECK(Settings::Get<bool>("Later.Changed"));
   CHECK_FALSE(Settings::SetDefault("Later.Changed", 1));
   CHECK_FALSE(Settings::SetDefault("Later.Missing", 1));
}

namespace {
constexpr Id<int32_t> COUNT = {"Count"};
constexpr Id<float> BRIGHTNESS = {"Brightness"};

// A wrong value type for an id doesn't compile
template <typename T, typename U>
concept CanSetDefault = requires(Id<T> id, U value) { SetDefault(id, value); };
template <typename T, typename U>
concept CanSet = requires(Id<T> id, U value) { Set(id, value); };
static_assert(CanSetDefault<int32_t, int32_t> && !CanSetDefault<int32_t, bool> && !CanSetDefault<int32_t, float>);
static_assert(CanSet<float, float> && !CanSet<float, double> && !CanSet<bool, int32_t>);
} // namespace

TEST_CASE_FIXTURE(Fixture, "Settings: typed ids read, write and default with their type")
{
   CHECK(Get(COUNT) == 5);
   CHECK(Set(COUNT, int32_t(7)));
   CHECK(Get(COUNT) == 7);
   CHECK(Set(BRIGHTNESS, 5.f));
   CHECK(Get(BRIGHTNESS) == 2.f); // Clamped
   CHECK(SetDefault(COUNT, int32_t(3)));
   CHECK(Find("Count")->default_value == Value(int32_t(3)));
   CHECK(Get(COUNT) == 7); // Changed, so it keeps its value
   CHECK(Reset("Count"));
   CHECK(Get(COUNT) == 3);
   Queue(COUNT, int32_t(9));
   ApplyQueued();
   CHECK(Get(COUNT) == 9);
   // A missing key reads as the default constructed value
   CHECK(Get(Id<int32_t>{"Missing"}) == 0);
   CHECK_FALSE(SetDefault(Id<int32_t>{"Missing"}, int32_t(1)));
}

TEST_CASE_FIXTURE(Fixture, "Settings: a setting's kind is the one it was registered with")
{
   CHECK(Find("Count")->kind == Kind::Game);
   const std::array<Setting, 1> luma = {{{.key = "Luma.Test", .category = "Luma", .default_value = true, .kind = Kind::Development}}};
   REQUIRE(Register(luma, Kind::Luma));
   CHECK(Find("Luma.Test")->kind == Kind::Luma);
   CHECK(GetKindName(Kind::Define) == "Define");
}

TEST_CASE_FIXTURE(Fixture, "Settings: AddDefine generates the key, category, kind and source, and refuses a define twice")
{
   REQUIRE(Shaders::Defines::Add({.name = "TEST_DEFINE", .label = "Test Define", .default_value = 1, .range = Range{0, 2}}));
   const Setting* setting = Find("Defines.TEST_DEFINE");
   REQUIRE(setting);
   CHECK(setting->kind == Kind::Define);
   CHECK(setting->label == "Test Define");
   CHECK(!setting->category.empty());
   const Sources::Define* define = std::get_if<Sources::Define>(&setting->source);
   REQUIRE(define);
   CHECK(define->name == "TEST_DEFINE");
   CHECK(Get<int32_t>("Defines.TEST_DEFINE") == 1);

   // The name is global to the compiler: a second registration under any key is refused
   CHECK_FALSE(Shaders::Defines::Add({.name = "TEST_DEFINE", .default_value = true}));
   const std::array<Setting, 1> same_name = {{{.key = "Other.Key", .default_value = int32_t(0), .source = Sources::Define{"TEST_DEFINE"}}}};
   CHECK_FALSE(Register(same_name));
   CHECK_FALSE(Find("Other.Key"));
   // Only ints and bools compile to a define
   CHECK_FALSE(Shaders::Defines::Add({.name = "FLOAT_DEFINE", .default_value = 1.f}));
   CHECK_FALSE(Find("Defines.FLOAT_DEFINE"));
   // Unlabelled, its name is its label
   REQUIRE(Shaders::Defines::Add({.name = "UNLABELLED", .default_value = false}));
   CHECK(Find("Defines.UNLABELLED")->label == "UNLABELLED");
}

TEST_CASE_FIXTURE(Fixture, "Settings: AddDefine copies its strings")
{
   {
      std::string name = "TEST_OWNED_DEFINE";
      std::string label = "Owned Define";
      std::string tooltip = "A tooltip";
      REQUIRE(Shaders::Defines::Add({.name = name, .label = label, .tooltip = tooltip, .default_value = 1, .range = Range{0, 2}}));
      name.assign(name.size(), 'x');
      label.assign(label.size(), 'x');
      tooltip.assign(tooltip.size(), 'x');
   }
   const Setting* setting = Find("Defines.TEST_OWNED_DEFINE");
   REQUIRE(setting);
   CHECK(setting->label == "Owned Define");
   CHECK(setting->tooltip == "A tooltip");
   CHECK(std::get<Sources::Define>(setting->source).name == "TEST_OWNED_DEFINE");
}

TEST_CASE_FIXTURE(Fixture, "Settings: a define's default for a define the build doesn't have is refused")
{
   REQUIRE(Shaders::Defines::Add({.name = "TEST_DEFAULT_DEFINE", .default_value = 1, .range = Range{0, 9}}));
   CHECK(Shaders::Defines::SetDefault("Defines.TEST_DEFAULT_DEFINE", "TEST_DEFAULT_DEFINE", int32_t(3)));
   CHECK(Get<int32_t>("Defines.TEST_DEFAULT_DEFINE") == 3);
   CHECK_FALSE(Shaders::Defines::SetDefault("Defines.TEST_ABSENT_DEFINE", "TEST_ABSENT_DEFINE", int32_t(3)));
   CHECK_FALSE(Shaders::Defines::SetDefault("Defines.TEST_DEFAULT_DEFINE", "TEST_DEFAULT_DEFINE", true));
}

TEST_CASE_FIXTURE(Fixture, "Storage: each kind has its table")
{
   const std::array<Setting, 2> luma = {{
      {.key = "Luma.Overlay.Key", .category = "Overlay", .default_value = Key{45}},
      {.key = "Luma.TestSwitch", .category = "Luma", .default_value = true},
   }};
   const std::array<Setting, 1> development = {{{.key = "Luma.Development.Setting01", .category = "Development", .default_value = 0.f}}};
   REQUIRE(Register(luma, Kind::Luma));
   REQUIRE(Register(development, Kind::Development));
   REQUIRE(Shaders::Defines::Add({.name = "TEST_TABLE_DEFINE", .default_value = int32_t(0), .range = Range{0, 9}}));
   Set("Count", int32_t(6));
   Set("Luma.Overlay.Key", Key{46});
   Set("Luma.TestSwitch", false);
   Set("Luma.Development.Setting01", 0.5f);
   Set("Defines.TEST_TABLE_DEFINE", int32_t(4));
   REQUIRE(Storage::Save(path));

   const std::string content = ReadFile();
   CHECK(content.find("[Display]\nEnabled = true\nCount = 6\n") != std::string::npos);
   CHECK(content.find("[Luma]\nTestSwitch = false\n") != std::string::npos);
   CHECK(content.find("[Luma.Overlay]\nKey = 46\n") != std::string::npos);
   CHECK(content.find("[Development]\nSetting01 = 0.5\n") != std::string::npos);
   CHECK(content.find("[Defines]\nTEST_TABLE_DEFINE = 4\n") != std::string::npos);

   for (const std::string_view key : {"Count", "Luma.Overlay.Key", "Luma.TestSwitch", "Luma.Development.Setting01", "Defines.TEST_TABLE_DEFINE"})
      Reset(key);
   Storage::Reset();
   REQUIRE(Storage::Load(path));
   CHECK(Get<int32_t>("Count") == 6);
   CHECK(Get<Key>("Luma.Overlay.Key").code == 46);
   CHECK(Get<bool>("Luma.TestSwitch") == false);
   CHECK(Get<float>("Luma.Development.Setting01") == 0.5f);
   CHECK(Get<int32_t>("Defines.TEST_TABLE_DEFINE") == 4);
}
