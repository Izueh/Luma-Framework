#include <doctest/doctest.h>

#include <Windows.h>

#include "Display/DisplayComposition.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Swapchain/Swapchain.hpp"
#include "System/WindowPolicy.hpp"

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

   // Suppress Focus Loss defaults to true in Development and false otherwise, lives in Display, and is live (no restart required)
   const Settings::Setting* suppress_focus = Settings::Find(Settings::Core::SUPPRESS_FOCUS_LOSS.key);
   REQUIRE(suppress_focus);
   CHECK(suppress_focus->kind == Settings::Kind::Luma);
   CHECK(suppress_focus->category == "Display");
   CHECK(suppress_focus->visibility == Settings::Visibility::Release);
   CHECK_FALSE(suppress_focus->requires_restart);
#if DEVELOPMENT
   CHECK(Swapchain::SuppressesFocusLoss());
#else
   CHECK_FALSE(Swapchain::SuppressesFocusLoss());
#endif
}

namespace {
struct FocusCounts
{
   int kill_focus = 0;
   int set_focus = 0;
   int activate_inactive = 0;
   int activate_active = 0;
   int activate_app_inactive = 0;
   int activate_app_active = 0;
   int overlay_calls = 0;
   WNDPROC before_overlay = nullptr;
};

LRESULT CALLBACK FocusTestWndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
   if (auto* counts = reinterpret_cast<FocusCounts*>(GetWindowLongPtrW(window, GWLP_USERDATA)))
   {
      if (message == WM_KILLFOCUS)
      {
         counts->kill_focus++;
         return 0;
      }
      if (message == WM_SETFOCUS)
      {
         counts->set_focus++;
         return 0;
      }
      if (message == WM_ACTIVATE)
      {
         if (LOWORD(wparam) == WA_INACTIVE)
            counts->activate_inactive++;
         else
            counts->activate_active++;
         return 0;
      }
      if (message == WM_ACTIVATEAPP)
      {
         if (wparam == FALSE)
            counts->activate_app_inactive++;
         else
            counts->activate_app_active++;
         return 0;
      }
   }
   return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK OverlayTestWndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
   auto* counts = reinterpret_cast<FocusCounts*>(GetWindowLongPtrW(window, GWLP_USERDATA));
   if (counts)
      counts->overlay_calls++;
   return counts && counts->before_overlay ? CallWindowProcW(counts->before_overlay, window, message, wparam, lparam)
                                           : DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

TEST_CASE("Swapchain: SuppressFocusLoss blocks focus-loss messages and toggles live without resetting")
{
   Fixture fixture;
   WNDCLASSW window_class = {
      .lpfnWndProc = &FocusTestWndProc,
      .hInstance = GetModuleHandleW(nullptr),
      .lpszClassName = L"Luma2FocusLossTestWindow",
   };
   RegisterClassW(&window_class);
   HWND window = CreateWindowExW(0, window_class.lpszClassName, L"FocusTest", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, window_class.hInstance, nullptr);
   REQUIRE(window != nullptr);

   FocusCounts counts;
   SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&counts));

   // Enabled: WM_KILLFOCUS, WM_ACTIVATE (WA_INACTIVE), and WM_ACTIVATEAPP (FALSE) are blocked, activation passes through
   REQUIRE(Settings::Set(Settings::Core::SUPPRESS_FOCUS_LOSS, true));
   System::Window::SetFocusLossSuppression(window, Swapchain::SuppressesFocusLoss());
   SendMessageW(window, WM_KILLFOCUS, 0, 0);
   SendMessageW(window, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 1), 0);
   SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
   SendMessageW(window, WM_SETFOCUS, 0, 0);
   SendMessageW(window, WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0), 0);
   SendMessageW(window, WM_ACTIVATEAPP, TRUE, 0);
   CHECK(counts.kill_focus == 0);
   CHECK(counts.activate_inactive == 0);
   CHECK(counts.activate_app_inactive == 0);
   CHECK(counts.set_focus == 1);
   CHECK(counts.activate_active == 1);
   CHECK(counts.activate_app_active == 1);

   // Toggled off live via Settings::Set: focus-loss messages reach the game window again without recreating the window
   REQUIRE(Settings::Set(Settings::Core::SUPPRESS_FOCUS_LOSS, false));
   CHECK_FALSE(Swapchain::SuppressesFocusLoss());
   SendMessageW(window, WM_KILLFOCUS, 0, 0);
   SendMessageW(window, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), 0);
   SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
   CHECK(counts.kill_focus == 1);
   CHECK(counts.activate_inactive == 1);
   CHECK(counts.activate_app_inactive == 1);
   CHECK(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC)) == &FocusTestWndProc);

   // Toggled back on live, then an overlay subclasses on top: toggling off/on preserves the overlay's WndProc chain
   REQUIRE(Settings::Set(Settings::Core::SUPPRESS_FOCUS_LOSS, true));
   counts.before_overlay = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&OverlayTestWndProc)));
   SendMessageW(window, WM_KILLFOCUS, 0, 0);
   CHECK(counts.overlay_calls == 1);
   CHECK(counts.kill_focus == 1);

   REQUIRE(Settings::Set(Settings::Core::SUPPRESS_FOCUS_LOSS, false));
   CHECK(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC)) == &OverlayTestWndProc);
   SendMessageW(window, WM_KILLFOCUS, 0, 0);
   CHECK(counts.overlay_calls == 2);
   CHECK(counts.kill_focus == 2);

   REQUIRE(Settings::Set(Settings::Core::SUPPRESS_FOCUS_LOSS, true));
   SendMessageW(window, WM_KILLFOCUS, 0, 0);
   CHECK(counts.overlay_calls == 3);
   CHECK(counts.kill_focus == 2);

   SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(counts.before_overlay));
   DestroyWindow(window);
   UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
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
