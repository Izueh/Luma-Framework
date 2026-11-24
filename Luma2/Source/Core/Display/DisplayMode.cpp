#include "Display/DisplayMode.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <variant>

#include "Display/DisplayComposition.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Display {
namespace DisplayMode {
namespace {
// Presenting thread
bool managed = true;
bool output_hdr = true;
bool forced_shader_sdr = false;
bool has_output_mode = false;
bool syncing_os_white = false;
HWND output_window = nullptr;
System::Display::Info display;
// The game's own switch: -1 unknown (HDR when the output is), 0 SDR, 1 HDR. Any thread.
std::atomic<int32_t> game_hdr = -1;
int32_t applied_game_hdr = -1;

constexpr std::array<std::string_view, 2> DISPLAY_MODES = {"HDR", "SDR"};

bool HasSeparateUiPaperWhite()
{
   return Resources::LumaCBuffers::cb_luma_global_settings.DisplayComposition.UIDrawType >= 1;
}

bool IsShaderSdr()
{
   return HasHdrOutput() && (forced_shader_sdr || Settings::Get(Settings::Core::DISPLAY_MODE).value != 0);
}

const std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::DISPLAY_MODE.key,
      .label = "Display Mode",
      .tooltip = "Switches Luma between HDR and SDR in shaders without resizing the swapchain.\n"
                 "HDR - full HDR output\n"
                 "SDR - clamps and scales to SDR at paper white on an HDR swapchain",
      .category = "Display",
      .default_value = Settings::EnumIndex{0},
      .enum_labels = DISPLAY_MODES,
      .live = true,
      .is_visible = [] { return HasHdrOutput() && !forced_shader_sdr; },
   },
   Settings::Setting{
      .key = Settings::Core::PEAK_WHITE.key,
      .label = "Peak White",
      .tooltip = "The brightest your display (TV or monitor) can show, in nits. Defaults to what Windows has for the display (its HDR "
                 "calibration, HDR10+ Gaming or EDID): calibrating in Windows is suggested.",
      .category = "Display",
      .default_value = System::Display::DEFAULT_PEAK_NITS,
      .range = Settings::Range{System::Display::MIN_PEAK_NITS, System::Display::MAX_PEAK_NITS},
      .format = "%.0f nits",
      .live = true,
      .is_enabled = [] { return Composition::IsModActive(); },
      .is_visible = [] { return IsHdr(); },
   },
   Settings::Setting{
      .key = Settings::Core::LINK_OS_WHITE_LEVEL.key,
      .label = "Link to OS Reference White Level",
      .tooltip = "Links Luma's HDR Paper White value to the Windows SDR content brightness slider (Reference/Diffuse White Level). "
                 "This makes the game cursor brightness match the one of the game.",
      .category = "Display",
      .default_value = true, // Luma 1.0 defaulted to linked, falling back to unlinked if the OS value can't be read or set
      .is_visible = [] { return IsHdr() || (IsShaderSdr() && display.hdr_enabled); },
   },
   Settings::Setting{
      .key = Settings::Core::PAPER_WHITE.key,
      .label = "Paper White",
      .tooltip = "The brightness of the game's white (and of the UI unless it has its own), in nits. 203 is BT.2408's reference white.",
      .category = "Display",
      .default_value = System::Display::DEFAULT_PAPER_WHITE_NITS,
      .range = Settings::Range{System::Display::SRGB_WHITE_NITS, 500.f},
      .format = "%.0f nits",
      .live = true,
      .is_visible = [] { return IsHdr() || (IsShaderSdr() && display.hdr_enabled); },
   },
   Settings::Setting{
      .key = Settings::Core::UI_PAPER_WHITE.key,
      .label = "UI Paper White",
      .tooltip = "The brightness of the game's UI (except the 2D hardware cursor, which is driven by Windows' SDR White Level), in nits.",
      .category = "Display",
      .default_value = System::Display::DEFAULT_PAPER_WHITE_NITS,
      .range = Settings::Range{System::Display::SRGB_WHITE_NITS, 500.f},
      .format = "%.0f nits",
      .live = true,
      .is_enabled = [] { return Composition::IsModActive(); },
      .is_visible = [] { return IsHdr() && HasSeparateUiPaperWhite() && !Settings::Get(Settings::Core::LINK_OS_WHITE_LEVEL); },
   },
};

// LumaSettings from the state and the settings
void Write()
{
   CB::LumaGlobalSettings& settings = Resources::LumaCBuffers::cb_luma_global_settings;
   if (!HasHdrOutput())
   {
      settings.DisplayMode = 0;
      settings.PeakWhiteNits = System::Display::SRGB_WHITE_NITS;
      settings.GamePaperWhiteNits = System::Display::SRGB_WHITE_NITS;
      settings.UIPaperWhiteNits = System::Display::SRGB_WHITE_NITS;
   }
   else if (IsShaderSdr())
   {
      if (display.hdr_enabled)
      {
         // SDR on an HDR swapchain while Windows HDR is on: align peak and UI to paper white
         const float paper = Settings::Get(Settings::Core::PAPER_WHITE);
         settings.DisplayMode = 2;
         settings.GamePaperWhiteNits = paper;
         settings.UIPaperWhiteNits = paper;
         settings.PeakWhiteNits = paper;
      }
      else
      {
         // Windows HDR is off while the swapchain is still HDR (e.g. InPlace mode): 80 nits SDR
         settings.DisplayMode = 0;
         settings.PeakWhiteNits = System::Display::SRGB_WHITE_NITS;
         settings.GamePaperWhiteNits = System::Display::SRGB_WHITE_NITS;
         settings.UIPaperWhiteNits = System::Display::SRGB_WHITE_NITS;
      }
   }
   else
   {
      const float peak = Settings::Get(Settings::Core::PEAK_WHITE);
      const float paper = Settings::Get(Settings::Core::PAPER_WHITE);
      const bool link_os_white = Settings::Get(Settings::Core::LINK_OS_WHITE_LEVEL);
      settings.GamePaperWhiteNits = paper;
      settings.UIPaperWhiteNits = (HasSeparateUiPaperWhite() && !link_os_white) ? Settings::Get(Settings::Core::UI_PAPER_WHITE) : paper;
      settings.PeakWhiteNits = peak > 0.f ? peak : display.peak_nits;
      settings.DisplayMode = 1;
   }
   Resources::LumaCBuffers::MarkSettingsDirty();
}

void OnSettingChanged(const Settings::Setting& setting, const Settings::Value& value)
{
   const bool is_own = std::ranges::any_of(SETTINGS, [&](const Settings::Setting& own) { return own.key == setting.key; });
   if (!is_own)
      return;
   if (!syncing_os_white)
   {
      syncing_os_white = true;
      if (setting.key == Settings::Core::LINK_OS_WHITE_LEVEL.key)
      {
         if (const bool* linked = std::get_if<bool>(&value); linked && *linked && has_output_mode)
         {
            const float clamped = std::clamp(Settings::Get(Settings::Core::PAPER_WHITE), System::Display::SRGB_WHITE_NITS, System::Display::MAX_OS_SDR_WHITE_NITS);
            if (output_window && !System::Display::SetSdrWhiteLevel(output_window, clamped))
               Settings::Set(Settings::Core::LINK_OS_WHITE_LEVEL, false);
            else if (clamped != Settings::Get(Settings::Core::PAPER_WHITE))
               Settings::Set(Settings::Core::PAPER_WHITE, clamped);
         }
      }
      else if (setting.key == Settings::Core::PAPER_WHITE.key && Settings::Get(Settings::Core::LINK_OS_WHITE_LEVEL))
      {
         const float clamped = std::clamp(Settings::Get(Settings::Core::PAPER_WHITE), System::Display::SRGB_WHITE_NITS, System::Display::MAX_OS_SDR_WHITE_NITS);
         if (clamped != Settings::Get(Settings::Core::PAPER_WHITE))
            Settings::Set(Settings::Core::PAPER_WHITE, clamped);
         if (has_output_mode && output_window)
            System::Display::SetSdrWhiteLevel(output_window, clamped);
      }
      syncing_os_white = false;
   }
   Write();
}
} // namespace

void Register()
{
   has_output_mode = false;
   forced_shader_sdr = false;
   output_window = nullptr;
   Settings::Register(SETTINGS, Settings::Kind::Luma);
   display = System::Display::Query(nullptr);
   if (display.peak_nits >= System::Display::MIN_PEAK_NITS)
      Settings::SetDefault(Settings::Core::PEAK_WHITE, display.peak_nits);
   Settings::AddChangeListener(&OnSettingChanged);
}

void ApplyOutputMode(bool hdr, const System::Display::Info& info, HWND window, bool shader_sdr)
{
   const bool first_apply = !has_output_mode;
   const bool sdr_white_changed = display.sdr_white_nits != info.sdr_white_nits;
   has_output_mode = true;
   managed = true;
   output_hdr = hdr;
   forced_shader_sdr = shader_sdr;
   output_window = window;
   display = info;
   if (info.peak_nits >= System::Display::MIN_PEAK_NITS)
      Settings::SetDefault(Settings::Core::PEAK_WHITE, info.peak_nits);
   if ((first_apply || sdr_white_changed) && Settings::Get(Settings::Core::LINK_OS_WHITE_LEVEL) && hdr && info.hdr_enabled && info.sdr_white_nits >= System::Display::SRGB_WHITE_NITS)
   {
      syncing_os_white = true;
      Settings::Set(Settings::Core::PAPER_WHITE, std::clamp(info.sdr_white_nits, System::Display::SRGB_WHITE_NITS, System::Display::MAX_OS_SDR_WHITE_NITS));
      syncing_os_white = false;
   }
   Write();
}

void SetUnmanaged()
{
   has_output_mode = true;
   managed = false;
   output_hdr = false;
   forced_shader_sdr = false;
   Write();
}

void Refresh()
{
   if (Settings::Find(Settings::Core::PEAK_WHITE.key))
      Write();
}

void SetGameHdr(bool hdr)
{
   game_hdr.store(hdr ? 1 : 0, std::memory_order_relaxed);
}

void Update()
{
   const int32_t value = game_hdr.load(std::memory_order_relaxed);
   if (value == applied_game_hdr)
      return;
   Log::Info("Display: the game's image is {} now", value ? "HDR" : "SDR");
   applied_game_hdr = value;
   Write();
}

bool HasHdrOutput()
{
   return managed && output_hdr && applied_game_hdr != 0;
}

bool IsHdr()
{
   return HasHdrOutput() && !IsShaderSdr();
}
} // namespace DisplayMode
} // namespace Display
} // namespace Luma
