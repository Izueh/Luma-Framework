#include <doctest/doctest.h>

#include "Display/DisplayComposition.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Swapchain/Swapchain.hpp"

using namespace Luma;

namespace {
// The swapchain's settings registered, and the game's options back to the defaults after
struct Fixture
{
   Fixture()
   {
      Settings::Clear();
      Swapchain::Register();
      Swapchain::SetOptions({});
   }

   ~Fixture()
   {
      Swapchain::SetOptions({});
      Settings::Clear();
   }
};
} // namespace

TEST_CASE("Swapchain: the Mode setting picks Native over the game's mode, and the game can pick it")
{
   Fixture fixture;
   using Swapchain::Mode;
   CHECK(Swapchain::GetMode() == Mode::Mirror);
   Swapchain::SetOptions({.mode = Mode::Native});
   CHECK(Swapchain::GetMode() == Mode::Native);

   REQUIRE(Settings::Set(Settings::Core::SWAPCHAIN_MODE, Settings::EnumIndex{0}));
   CHECK(Swapchain::GetMode() == Mode::Mirror);
   REQUIRE(Settings::Set(Settings::Core::SWAPCHAIN_MODE, Settings::EnumIndex{1}));
   CHECK(Swapchain::GetMode() == Mode::InPlace);
   // Resetting returns to the game's configured default
   REQUIRE(Settings::Reset(Settings::Core::SWAPCHAIN_MODE.key));
   CHECK(Swapchain::GetMode() == Mode::Native);

   Swapchain::SetOptions({});
   REQUIRE(Settings::Set(Settings::Core::SWAPCHAIN_MODE, Settings::EnumIndex{2}));
   CHECK(Swapchain::GetMode() == Mode::Native);
   // The labels match the modes (no separate "Game default" entry)
   const Settings::Setting* setting = Settings::Find(Settings::Core::SWAPCHAIN_MODE.key);
   REQUIRE(setting);
   REQUIRE(setting->enum_labels.size() == 3);
   CHECK(setting->enum_labels[0] == "Mirror");
   CHECK(setting->enum_labels[1] == "In place");
   CHECK(setting->enum_labels[2] == "Native");
   // Fullscreen must be registered as Kind::Luma, not Development
   const Settings::Setting* fullscreen = Settings::Find(Settings::Core::SWAPCHAIN_FULLSCREEN.key);
   REQUIRE(fullscreen);
   CHECK(fullscreen->kind == Settings::Kind::Luma);
   CHECK(fullscreen->enum_labels.size() == 2);
}

TEST_CASE("Swapchain: each mode has its name, and only Native keeps the game's format and color space and doesn't compose")
{
   using Swapchain::Mode;
   CHECK(Swapchain::GetName(Mode::Mirror) == "Mirror");
   CHECK(Swapchain::GetName(Mode::InPlace) == "InPlace");
   CHECK(Swapchain::GetName(Mode::Native) == "Native");
   for (const Mode mode : {Mode::Mirror, Mode::InPlace})
   {
      CHECK(Swapchain::ManagesOutput(mode));
   }
   CHECK_FALSE(Swapchain::ManagesOutput(Mode::Native));
}

TEST_CASE("Composition: off, the plan is empty even where Mirror always composes and the image would be scaled or encoded")
{
   using Display::Composition::Encoding;
   using Display::Composition::Inputs;
   const Inputs mirror = {.encoding = Encoding::Pq, .always = true, .source_scale = {0.5f, 0.5f}};
   CHECK(Display::Composition::Plan(mirror).has_value());
   Inputs off = mirror;
   off.off = true;
   CHECK_FALSE(Display::Composition::Plan(off).has_value());
   for (const Encoding encoding : {Encoding::Linear, Encoding::Pq, Encoding::Srgb})
      CHECK_FALSE(Display::Composition::Plan({.encoding = encoding, .off = true}).has_value());
}

TEST_CASE("Composition: InPlace SDR passthrough skips unnecessary re-encoding, and native PQ fallback uses mode 3")
{
   using Display::Composition::Encoding;
   using Display::Composition::Inputs;
   using Display::Composition::Pass;

   Settings::Clear();
   Display::Composition::RegisterSettings();
   // Vanilla SDR gamma input -> SDR sRGB output with matching encoding: nothing to do in InPlace
   Display::Composition::SetOptions({.post_process_space = 0, .early_display_encoding = false, .vanilla_encoding = 0, .gamma_correction = 0});
   Resources::LumaCBuffers::cb_luma_global_settings.DisplayMode = 0;
   Resources::LumaCBuffers::cb_luma_global_settings.GamePaperWhiteNits = 80.f;
   CHECK_FALSE(Display::Composition::Plan({.encoding = Encoding::Srgb, .game_linear = false, .always = false}).has_value());

   // InPlace scRGB HDR with paper white != 80 nits needs composition even when other flags match
   Resources::LumaCBuffers::cb_luma_global_settings.DisplayMode = 1;
   Resources::LumaCBuffers::cb_luma_global_settings.GamePaperWhiteNits = 203.f;
   Display::Composition::SetOptions({.post_process_space = 1, .early_display_encoding = false, .vanilla_encoding = 1, .gamma_correction = 1});
   CHECK(Display::Composition::Plan({.encoding = Encoding::Linear, .game_linear = true, .always = false}).has_value());

   // Mirror mode with native-HDR PQ game when mod is inactive signals custom_data_1 == 3
   const std::optional<Pass> pq_pass = Display::Composition::Plan({.encoding = Encoding::Pq, .game_linear = false, .game_pq = true, .always = true});
   REQUIRE(pq_pass.has_value());
   CHECK(pq_pass->custom.custom_data_1 == 3u);

   Display::Composition::SetOptions({});
   Settings::Clear();
}
