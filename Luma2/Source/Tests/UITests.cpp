#include <doctest/doctest.h>

#include <array>
#include <ostream>
#include <cstdint>
#include <string_view>
#include <vector>

#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Shaders/Defines.hpp"
#include "UI/Encoding.hpp"
#include "UI/Shell.hpp"

using namespace Luma;
using Settings::Kind;
using Settings::Setting;

namespace {
std::vector<std::string_view> GetTitles()
{
   std::vector<std::string_view> titles;
   for (const UI::SettingsSection& section : UI::CollectSettingsSections())
      titles.push_back(section.title);
   return titles;
}
} // namespace

TEST_CASE("UI: the settings view puts Luma and Overlay first, then the game, the defines with Auto Compile, then the rest")
{
   Settings::Clear();
   const std::array<Setting, 2> game = {{
      {.key = "Brightness", .category = "Display", .default_value = 1.f},
      {.key = "Saturation", .category = "Color Grading", .default_value = 1.f},
   }};
   const std::array<Setting, 5> luma = {{
      {.key = "Luma.Overlay.FontSize", .category = "Overlay", .default_value = 16.f},
      {.key = Settings::Core::AUTO_COMPILE.key, .category = "Shaders", .default_value = false},
      {.key = "Luma.Advanced.Thing", .category = "Advanced", .default_value = false},
      {.key = "Luma.Enabled", .category = "Luma", .default_value = true},
      {.key = "Luma.Hidden", .category = "Luma", .visibility = Settings::Visibility::Development, .is_visible = [] { return false; }},
   }};
   const std::array<Setting, 1> defines = {{
      {.key = "Defines.TEST_UI_SECTIONS", .category = "Shader Defines", .default_value = int32_t(0), .source = Settings::Sources::Define{"TEST_UI_SECTIONS"}},
   }};
   // Registered in another order than they're shown
   REQUIRE(Settings::Register(game, Kind::Game));
   REQUIRE(Settings::Register(luma, Kind::Luma));
   REQUIRE(Settings::Register(defines, Kind::Define));

#if DEVELOPMENT
   const std::vector<std::string_view> expected = {"Luma", "Overlay", "Display", "Color Grading", "Shader Defines", "Advanced", "Development Settings"};
#else
   const std::vector<std::string_view> expected = {"Luma", "Overlay", "Display", "Color Grading", "Shader Defines", "Advanced"};
#endif
   CHECK(GetTitles() == expected);

   const std::vector<UI::SettingsSection> sections = UI::CollectSettingsSections();
   const UI::SettingsSection& shader_defines = sections[4];
   CHECK(shader_defines.role == UI::SettingsSection::Role::Defines);
   REQUIRE(shader_defines.settings.size() == 2);
   CHECK(shader_defines.settings[0]->key == Settings::Core::AUTO_COMPILE.key);
   CHECK(shader_defines.settings[1]->key == "Defines.TEST_UI_SECTIONS");
   // Hidden settings aren't collected
   CHECK(sections[0].settings.size() == 1);
   CHECK(sections[2].role == UI::SettingsSection::Role::Game);
   Settings::Clear();
}

TEST_CASE("UI: Auto Compile alone makes no Shader Defines section")
{
   Settings::Clear();
   const std::array<Setting, 1> luma = {{{.key = Settings::Core::AUTO_COMPILE.key, .category = "Shaders", .default_value = false}}};
   REQUIRE(Settings::Register(luma, Kind::Luma));
   for (const UI::SettingsSection& section : UI::CollectSettingsSections())
      CHECK(section.title != "Shader Defines");
   Settings::Clear();
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
