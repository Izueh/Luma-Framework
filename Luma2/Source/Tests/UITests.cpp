#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string_view>
#include <vector>

#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Settings/SettingsStorage.hpp"
#include "Shaders/ShaderDefines.hpp"
#include "UI/OverlayEncoding.hpp"
#include "UI/OverlayWindow.hpp"

using namespace Luma;
using Settings::Kind;
using Settings::Setting;

namespace {
std::vector<std::string_view> GetTitles(UI::SettingsTab tab = UI::SettingsTab::Settings)
{
   std::vector<std::string_view> titles;
   for (const UI::SettingsSection& section : UI::CollectSettingsSections(tab))
      titles.push_back(section.title);
   return titles;
}
} // namespace

TEST_CASE("UI: settings tabs split Display/Game/Recipes, Framework, Overlay, and Development cleanly")
{
   Settings::Clear();
   const std::array<Setting, 2> game = {{
      {.key = "Brightness", .category = "Post Processing", .default_value = 1.f},
      {.key = "Saturation", .category = "Color Grading", .default_value = 1.f},
   }};
   const std::array<Setting, 7> luma = {{
      {.key = "Luma.Overlay.FontSize", .category = "Overlay", .default_value = 16.f},
      {.key = Settings::Core::AUTO_COMPILE.key, .category = "Shaders", .default_value = false},
      {.key = "Luma.Advanced.Thing", .category = "Advanced", .default_value = false},
      {.key = "Luma.Display.OutputMode", .category = "Display", .default_value = int32_t(0)},
      {.key = "Luma.Enabled", .category = "Luma", .default_value = true},
      {.key = "Luma.EnabledKey", .category = "Luma", .default_value = Settings::Key{0x78}},
      {.key = "Luma.Hidden", .category = "Luma", .visibility = Settings::Visibility::Development, .is_visible = [] { return false; }},
   }};
   const std::array<Setting, 1> defines = {{
      {.key = "Defines.TEST_UI_SECTIONS", .category = "Shader Defines", .default_value = int32_t(0), .source = Settings::Sources::Define{"TEST_UI_SECTIONS"}},
   }};
   const std::array<Setting, 2> recipes = {{
      {.key = "Recipes.UserRecipe", .category = "Recipes", .visibility = Settings::Visibility::Release, .default_value = true, .source = Settings::Sources::Recipe{"UserRecipe", {}}},
      {.key = "Recipes.DevRecipe", .category = "Recipes", .visibility = Settings::Visibility::Development, .default_value = false, .source = Settings::Sources::Recipe{"DevRecipe", {}}},
   }};
   const std::array<Setting, 2> dev = {{
      {.key = "Luma.Development.Sample", .category = "Display Composition", .visibility = Settings::Visibility::Development, .default_value = false},
      {.key = "Luma.Development.DebugLayer", .category = "Validation", .visibility = Settings::Visibility::Development, .default_value = false},
   }};
   REQUIRE(Settings::Register(game, Kind::Game));
   REQUIRE(Settings::Register(luma, Kind::Luma));
   REQUIRE(Settings::Register(defines, Kind::Define));
   REQUIRE(Settings::Register(recipes, Kind::Recipe));
   REQUIRE(Settings::Register(dev, Kind::Development));

   // Settings::IsEnabled tracks Luma.Enabled lock-free
   CHECK(Settings::IsEnabled());
   REQUIRE(Settings::Set("Luma.Enabled", false));
   CHECK_FALSE(Settings::IsEnabled());
   REQUIRE(Settings::Set("Luma.Enabled", true));
   CHECK(Settings::IsEnabled());

   // Settings tab: Display, Game categories, and user-visible Recipes (no General section; Enabled/Key live in the header bar)
#if DEVELOPMENT
   CHECK(GetTitles(UI::SettingsTab::Settings) == std::vector<std::string_view>{"Display", "Post Processing", "Color Grading", "Recipes"});
#else
   CHECK(GetTitles(UI::SettingsTab::Settings) == std::vector<std::string_view>{"Display", "Post Processing", "Color Grading", "Recipes", "Advanced Settings", "Advanced"});
#endif
   const std::vector<UI::SettingsSection> settings_sections = UI::CollectSettingsSections(UI::SettingsTab::Settings);
   CHECK(settings_sections[0].default_open);
   CHECK(settings_sections[1].role == UI::SettingsSection::Role::Game);
   CHECK(settings_sections[1].default_open);
   // Only the user-visible recipe appears on the Settings tab
   CHECK(settings_sections[3].title == "Recipes");
   REQUIRE(settings_sections[3].settings.size() == 1);
   CHECK(settings_sections[3].settings[0]->key == "Recipes.UserRecipe");

   // Overlay tab
   CHECK(GetTitles(UI::SettingsTab::Overlay) == std::vector<std::string_view>{"Overlay"});

   // Framework tab (Advanced + Display Composition in Dev builds + Shader Defines with Auto Compile first + ALL Recipes including dev-only)
#if DEVELOPMENT
   CHECK(GetTitles(UI::SettingsTab::Framework) == std::vector<std::string_view>{"Advanced", "Display Composition", "Shader Defines", "Recipes"});
   const std::vector<UI::SettingsSection> framework_sections = UI::CollectSettingsSections(UI::SettingsTab::Framework);
   const UI::SettingsSection& shader_defines = framework_sections[2];
   CHECK(shader_defines.role == UI::SettingsSection::Role::Defines);
   REQUIRE(shader_defines.settings.size() == 2);
   CHECK(shader_defines.settings[0]->key == Settings::Core::AUTO_COMPILE.key);
   CHECK(shader_defines.settings[1]->key == "Defines.TEST_UI_SECTIONS");
   CHECK(framework_sections[3].settings.size() == 2);

   // Development tab
   CHECK(GetTitles(UI::SettingsTab::Development) == std::vector<std::string_view>{"Validation"});
#else
   CHECK(GetTitles(UI::SettingsTab::Framework) == std::vector<std::string_view>{"Advanced", "Shader Defines", "Recipes"});
   const std::vector<UI::SettingsSection> framework_sections = UI::CollectSettingsSections(UI::SettingsTab::Framework);
   const UI::SettingsSection& shader_defines = framework_sections[1];
   CHECK(shader_defines.role == UI::SettingsSection::Role::Defines);
   REQUIRE(shader_defines.settings.size() == 2);
   CHECK(shader_defines.settings[0]->key == Settings::Core::AUTO_COMPILE.key);
   CHECK(shader_defines.settings[1]->key == "Defines.TEST_UI_SECTIONS");
   CHECK(framework_sections[2].settings.size() == 2);
#endif
   Settings::Clear();
}

TEST_CASE("UI: Auto Compile alone makes no Shader Defines section")
{
   Settings::Clear();
   const std::array<Setting, 1> luma = {{{.key = Settings::Core::AUTO_COMPILE.key, .category = "Shaders", .default_value = false}}};
   REQUIRE(Settings::Register(luma, Kind::Luma));
   for (const UI::SettingsSection& section : UI::CollectSettingsSections(UI::SettingsTab::Framework))
      CHECK(section.title != "Shader Defines");
   for (const UI::SettingsSection& section : UI::CollectSettingsSections(UI::SettingsTab::Settings))
      CHECK(section.title != "Shader Defines");
   Settings::Clear();
}

TEST_CASE("UI: overlay ImGui layout persists in Luma.toml and clears cleanly")
{
   Settings::Clear();
   Settings::Storage::Reset();
   const std::filesystem::path temp = std::filesystem::temp_directory_path() / "luma2_ui_layout_test.toml";
   std::error_code error;
   std::filesystem::remove(temp, error);

   constexpr std::string_view SAMPLE_INI = "[Window][Luma]\nPos=40,40\nSize=640,720\nCollapsed=0\n";
   Settings::Storage::SetOverlayLayout(SAMPLE_INI);
   REQUIRE(Settings::Storage::Save(temp));

   Settings::Storage::Reset();
   CHECK(Settings::Storage::GetOverlayLayout().empty());
   REQUIRE(Settings::Storage::Load(temp));
   CHECK(Settings::Storage::GetOverlayLayout() == SAMPLE_INI);

   Settings::Storage::SetOverlayLayout({});
   REQUIRE(Settings::Storage::Save(temp));
   Settings::Storage::Reset();
   REQUIRE(Settings::Storage::Load(temp));
   CHECK(Settings::Storage::GetOverlayLayout().empty());

   std::filesystem::remove(temp, error);
   Settings::Storage::Reset();
}

TEST_CASE("Shaders: a define is pending while its value differs from the one the shaders were built with")
{
   Settings::Clear();
   const std::array<Setting, 1> defines = {{
      {.key = "Defines.TEST_PENDING", .category = "Shader Defines", .default_value = int32_t(1), .source = Settings::Sources::Define{"TEST_PENDING"}},
   }};
   REQUIRE(Settings::Register(defines, Kind::Define));
   const Setting& setting = *Settings::Find("Defines.TEST_PENDING");
   Shaders::Defines::SetCompiled({{"TEST_PENDING", "1"}});
   CHECK_FALSE(Shaders::Defines::IsPending(setting));
   CHECK_FALSE(Shaders::Defines::NeedsCompile());
   Settings::Set("Defines.TEST_PENDING", int32_t(2));
   CHECK(Shaders::Defines::IsPending(setting));
   CHECK(Shaders::Defines::NeedsCompile());
   CHECK(Shaders::Defines::GetCompiledText("TEST_PENDING") == "1");
   // A change back is nothing to compile
   Settings::Set("Defines.TEST_PENDING", int32_t(1));
   CHECK_FALSE(Shaders::Defines::NeedsCompile());
   Settings::Set("Defines.TEST_PENDING", int32_t(3));
   Shaders::Defines::SetCompiled(Shaders::Defines::Collect());
   CHECK_FALSE(Shaders::Defines::IsPending(setting));
   Settings::Clear();
}

TEST_CASE("UI: the overlay's encoding follows the swapchain's color space, else its format")
{
   using UI::Encoding::GetKind;
   using UI::Encoding::Kind;
   CHECK(GetKind(DXGI_FORMAT_R8G8B8A8_UNORM, std::nullopt) == Kind::Sdr);
   CHECK(GetKind(DXGI_FORMAT_R10G10B10A2_UNORM, std::nullopt) == Kind::Sdr);
   CHECK(GetKind(DXGI_FORMAT_R16G16B16A16_FLOAT, std::nullopt) == Kind::ScRgb);
   CHECK(GetKind(DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) == Kind::Pq);
   // FP16 can carry PQ
   CHECK(GetKind(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) == Kind::Pq);
   CHECK(GetKind(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) == Kind::ScRgb);
   CHECK(GetKind(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) == Kind::Sdr);
   // scRGB is only FP16's, PQ any format's
   CHECK(GetKind(DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) == Kind::Sdr);
   CHECK(GetKind(DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020) == Kind::Pq);
   CHECK(GetKind(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) == Kind::Sdr);
   // One shader for both backends, the kind and paper white are its constants
   CHECK(UI::Encoding::CompilePixelShader("ps_4_0") != nullptr);
   CHECK(UI::Encoding::CompilePixelShader("ps_5_0") != nullptr);
   const UI::Encoding::Constants constants = UI::Encoding::GetConstants({Kind::Pq, 250.f});
   CHECK(constants.kind == 2);
   CHECK(constants.paper_white == 250.f);
   static_assert(sizeof(UI::Encoding::Constants) == 16);
}
