#include <doctest/doctest.h>

#include "Display/Composition.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
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

   REQUIRE(Settings::Set(Settings::Core::SWAPCHAIN_MODE, Settings::EnumIndex{1}));
   CHECK(Swapchain::GetMode() == Mode::Mirror);
   REQUIRE(Settings::Set(Settings::Core::SWAPCHAIN_MODE, Settings::EnumIndex{2}));
   CHECK(Swapchain::GetMode() == Mode::InPlace);
   Swapchain::SetOptions({});
   REQUIRE(Settings::Set(Settings::Core::SWAPCHAIN_MODE, Settings::EnumIndex{3}));
   CHECK(Swapchain::GetMode() == Mode::Native);
   // The labels match the modes
   const Settings::Setting* setting = Settings::Find(Settings::Core::SWAPCHAIN_MODE.key);
   REQUIRE(setting);
   REQUIRE(setting->enum_labels.size() == 4);
   CHECK(setting->enum_labels[3] == "Native");
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
