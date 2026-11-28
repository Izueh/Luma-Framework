#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include <Windows.h>

#include "Display/DisplayComposition.hpp"
#include "Display/DisplayMode.hpp"
#include "Display/DXGI/DXGISpoofing.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Settings/SettingsStorage.hpp"
#include "Display/Swapchain.hpp"
#include "System/WindowPolicy.hpp"

using namespace Luma;

namespace {
// The swapchain's settings registered, and the game's options back to the defaults after
struct Fixture
{
   Fixture()
   {
      Settings::Clear();
      Display::Swapchain::Register();
      Display::Swapchain::SetOptions({});
   }

   ~Fixture()
   {
      Display::Swapchain::SetOptions({});
      Settings::Clear();
   }
};
} // namespace

TEST_CASE("Swapchain: the Mode setting picks Native over the game's mode, and the game can pick it")
{
   Fixture fixture;
   using Display::Swapchain::Mode;
   CHECK(Display::Swapchain::GetMode() == Mode::Mirror);
   Display::Swapchain::SetOptions({.mode = Mode::Native});
   CHECK(Display::Swapchain::GetMode() == Mode::Native);

   REQUIRE(Settings::Set(Settings::Core::SWAPCHAIN_MODE, Settings::EnumIndex{0}));
   CHECK(Display::Swapchain::GetMode() == Mode::Mirror);
   REQUIRE(Settings::Set(Settings::Core::SWAPCHAIN_MODE, Settings::EnumIndex{1}));
   CHECK(Display::Swapchain::GetMode() == Mode::InPlace);
   // Resetting returns to the game's configured default
   REQUIRE(Settings::Reset(Settings::Core::SWAPCHAIN_MODE.key));
   CHECK(Display::Swapchain::GetMode() == Mode::Native);

   Display::Swapchain::SetOptions({});
   REQUIRE(Settings::Set(Settings::Core::SWAPCHAIN_MODE, Settings::EnumIndex{2}));
   CHECK(Display::Swapchain::GetMode() == Mode::Native);
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
   CHECK(Display::Swapchain::SuppressesFocusLoss());
#else
   CHECK_FALSE(Display::Swapchain::SuppressesFocusLoss());
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
   System::Window::SetFocusLossSuppression(window, Display::Swapchain::SuppressesFocusLoss());
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
   CHECK_FALSE(Display::Swapchain::SuppressesFocusLoss());
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
   using Display::Swapchain::Mode;
   CHECK(Display::Swapchain::GetName(Mode::Mirror) == "Mirror");
   CHECK(Display::Swapchain::GetName(Mode::InPlace) == "InPlace");
   CHECK(Display::Swapchain::GetName(Mode::Native) == "Native");
   for (const Mode mode : {Mode::Mirror, Mode::InPlace})
   {
      CHECK(Display::Swapchain::ManagesOutput(mode));
   }
   CHECK_FALSE(Display::Swapchain::ManagesOutput(Mode::Native));
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

TEST_CASE("DisplayMode: PeakWhite defaults and resets to the display's peak nits, stores 0 at default, and LinkOsWhiteLevel syncs paper white")
{
   Settings::Clear();
   Settings::Storage::Reset();
   Display::DisplayMode::Register();
   Display::Composition::RegisterSettings();
   Display::Composition::SetOptions({.ui_draw_type = 1});

   const std::filesystem::path path = std::filesystem::temp_directory_path() / "luma2_display_mode_test.toml";
   std::error_code error;
   std::filesystem::remove(path, error);

   const Settings::Setting* peak_setting = Settings::Find(Settings::Core::PEAK_WHITE.key);
   REQUIRE(peak_setting);
   REQUIRE(peak_setting->range.has_value());
   CHECK(std::get<float>(peak_setting->range->min) == 400.f);
   CHECK(std::get<float>(peak_setting->range->max) == 10000.f);
   CHECK(Settings::Get(Settings::Core::PEAK_WHITE) >= 400.f);

   // ApplyOutputMode updates PeakWhite's default and live value to the display's calibrated peak nits
   const System::Display::Info display_970 = {.hdr_supported = true, .hdr_enabled = true, .peak_nits = 970.f, .sdr_white_nits = 240.f};
   Display::DisplayMode::ApplyOutputMode(true, display_970);
   CHECK(Settings::Get(Settings::Core::PEAK_WHITE) == 970.f);
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.PeakWhiteNits == 970.f);

   // User override updates LumaSettings, and Reset restores the display's 970 nits (never 0)
   REQUIRE(Settings::Set(Settings::Core::PEAK_WHITE, 1200.f));
   CHECK(Settings::Get(Settings::Core::PEAK_WHITE) == 1200.f);
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.PeakWhiteNits == 1200.f);
   REQUIRE(Settings::Reset(Settings::Core::PEAK_WHITE.key));
   CHECK(Settings::Get(Settings::Core::PEAK_WHITE) == 970.f);
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.PeakWhiteNits == 970.f);

   // Saving while at the display's default writes 0 to Luma.toml so future launches and display changes follow the display
   REQUIRE(Settings::Storage::Save(path));
   {
      std::stringstream stream;
      stream << std::ifstream(path).rdbuf();
      CHECK(stream.str().find("PeakWhite = 0") != std::string::npos);
   }

   // Next boot: loading PeakWhite = 0 keeps it at default, and a display with 800 nits peak updates it to 800 nits
   Settings::Clear();
   Settings::Storage::Reset();
   Display::DisplayMode::Register();
   Display::Composition::RegisterSettings();
   Display::Composition::SetOptions({.ui_draw_type = 1});
   REQUIRE(Settings::Storage::Load(path));
   const System::Display::Info display_800 = {.hdr_supported = true, .hdr_enabled = true, .peak_nits = 800.f, .sdr_white_nits = 240.f};
   Display::DisplayMode::ApplyOutputMode(true, display_800);
   CHECK(Settings::Get(Settings::Core::PEAK_WHITE) == 800.f);
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.PeakWhiteNits == 800.f);

   // Saving a customized PeakWhite (1500 nits) persists 1500 across boots and display changes, while Reset returns to 800 nits
   REQUIRE(Settings::Set(Settings::Core::PEAK_WHITE, 1500.f));
   REQUIRE(Settings::Set(Settings::Core::LINK_OS_WHITE_LEVEL, true));
   REQUIRE(Settings::Storage::Save(path));

   Settings::Clear();
   Settings::Storage::Reset();
   Display::DisplayMode::Register();
   Display::Composition::RegisterSettings();
   Display::Composition::SetOptions({.ui_draw_type = 1});
   REQUIRE(Settings::Storage::Load(path));
   Display::DisplayMode::ApplyOutputMode(true, display_800);
   CHECK(Settings::Get(Settings::Core::PEAK_WHITE) == 1500.f);
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.PeakWhiteNits == 1500.f);
   // LinkOsWhiteLevel loaded true: initial ApplyOutputMode syncs PaperWhite and UIPaperWhite to display.sdr_white_nits (240 nits) and hides UIPaperWhite
   CHECK(Settings::Get(Settings::Core::PAPER_WHITE) == 240.f);
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.GamePaperWhiteNits == 240.f);
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.UIPaperWhiteNits == 240.f);
   const Settings::Setting* ui_paper = Settings::Find(Settings::Core::UI_PAPER_WHITE.key);
   REQUIRE(ui_paper);
   REQUIRE(ui_paper->is_visible);
   CHECK_FALSE(ui_paper->is_visible());

   REQUIRE(Settings::Reset(Settings::Core::PEAK_WHITE.key));
   CHECK(Settings::Get(Settings::Core::PEAK_WHITE) == 800.f);

   std::filesystem::remove(path, error);
   Display::Composition::SetOptions({});
   Settings::Storage::Reset();
   Settings::Clear();
}

TEST_CASE("DisplayMode: Display Mode switches HDR/SDR in shaders without changing swapchain, and shader_sdr fallback tracks Windows HDR")
{
   Settings::Clear();
   Display::DisplayMode::Register();
   const System::Display::Info hdr_display = {.hdr_supported = true, .hdr_enabled = true, .peak_nits = 1000.f, .sdr_white_nits = 200.f};
   Display::DisplayMode::ApplyOutputMode(true, hdr_display);
   CHECK(Display::DisplayMode::IsHdr());
   CHECK(Display::DisplayMode::HasHdrOutput());
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.DisplayMode == 1u);

   // User switches Display Mode to SDR on an HDR swapchain: DisplayMode == 2 aligned to paper white
   REQUIRE(Settings::Set(Settings::Core::DISPLAY_MODE, Settings::EnumIndex{1}));
   CHECK_FALSE(Display::DisplayMode::IsHdr());
   CHECK(Display::DisplayMode::HasHdrOutput());
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.DisplayMode == 2u);
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.PeakWhiteNits == Resources::LumaCBuffers::cb_luma_global_settings.GamePaperWhiteNits);
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.UIPaperWhiteNits == Resources::LumaCBuffers::cb_luma_global_settings.GamePaperWhiteNits);

   // Switching back to HDR restores DisplayMode == 1
   REQUIRE(Settings::Set(Settings::Core::DISPLAY_MODE, Settings::EnumIndex{0}));
   CHECK(Display::DisplayMode::IsHdr());
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.DisplayMode == 1u);

   // InPlace fallback when Windows HDR is turned off on an HDR swapchain: DisplayMode == 0 at 80 nits
   const System::Display::Info sdr_display = {.hdr_supported = true, .hdr_enabled = false, .peak_nits = 270.f, .sdr_white_nits = 80.f};
   Display::DisplayMode::ApplyOutputMode(true, sdr_display, nullptr, true);
   CHECK_FALSE(Display::DisplayMode::IsHdr());
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.DisplayMode == 0u);
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.PeakWhiteNits == System::Display::SRGB_WHITE_NITS);
   CHECK(Resources::LumaCBuffers::cb_luma_global_settings.GamePaperWhiteNits == System::Display::SRGB_WHITE_NITS);

   Settings::Clear();
}

TEST_CASE("DXGISpoofing: SpoofSdrDisplay reports SDR output desc and unsupported HDR color spaces")
{
   Settings::Clear();
   Display::Spoofing::DXGI::Register();

   DXGI_OUTPUT_DESC1 desc = {};
   desc.ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
   desc.BitsPerColor = 10;
   desc.MaxLuminance = 1000.f;
   desc.MaxFullFrameLuminance = 600.f;

   // Default off: untouched
   Display::Spoofing::DXGI::SpoofOutputDesc(desc);
   CHECK(desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
   CHECK(desc.MaxLuminance == 1000.f);
   UINT support = DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
   Display::Spoofing::DXGI::SpoofColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, support);
   CHECK(support == DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT);

   // Enabled: reports SDR G22 P709, 8-bit, <= 80 nits, and clears HDR color space support
   REQUIRE(Settings::Set(Settings::Core::SPOOF_SDR_DISPLAY, true));
   Display::Spoofing::DXGI::SpoofOutputDesc(desc);
   CHECK(desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
   CHECK(desc.BitsPerColor == 8u);
   CHECK(desc.MaxLuminance == System::Display::SRGB_WHITE_NITS);
   CHECK(desc.MaxFullFrameLuminance == System::Display::SRGB_WHITE_NITS);

   UINT pq_support = DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
   Display::Spoofing::DXGI::SpoofColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, pq_support);
   CHECK(pq_support == 0u);

   UINT scrgb_support = DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
   Display::Spoofing::DXGI::SpoofColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, scrgb_support);
   CHECK(scrgb_support == 0u);

   UINT sdr_support = DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
   Display::Spoofing::DXGI::SpoofColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, sdr_support);
   CHECK(sdr_support == DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT);

   Settings::Clear();
}
