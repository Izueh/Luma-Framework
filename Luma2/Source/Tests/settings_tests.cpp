#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <Windows.h>

#include "settings/registry.hpp"
#include "settings/storage.hpp"

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
