#include "Display/Levels.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <optional>
#include <string>

#include "Resources/LumaCBuffers.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Shaders/Defines.hpp"
#include "Utils/Log.hpp"

namespace Luma {
namespace Display {
namespace Levels {
namespace {
// Presenting thread
bool output_hdr = true;
System::Display::Info display;
// The game's own switch: -1 unknown (HDR when the output is), 0 SDR, 1 HDR. Any thread.
std::atomic<int32_t> game_hdr = -1;
int32_t applied_game_hdr = -1;

bool HasSeparateUiPaperWhite()
{
   const std::optional<std::string> text = Shaders::Defines::GetCompiledText(Shaders::Defines::UI_DRAW_TYPE.name);
   int32_t ui_draw_type = 0;
   if (text)
      std::from_chars(text->data(), text->data() + text->size(), ui_draw_type);
   return ui_draw_type >= 1;
}

bool IsHdr()
{
   return output_hdr && applied_game_hdr != 0;
}

const std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::PEAK_WHITE.key,
      .label = "Peak White",
      .tooltip = "The brightest your display (TV or monitor) can show, in nits. 0 uses what Windows has for the display (its HDR "
                 "calibration or EDID): calibrating in Windows is suggested.",
      .category = "Display",
      .default_value = 0.f,
      .range = Settings::Range{0.f, 10000.f},
      .format = "%.0f nits",
      .live = true,
      .is_visible = [] { return IsHdr(); },
   },
   Settings::Setting{
      .key = Settings::Core::PAPER_WHITE.key,
      .label = "Paper White",
      .tooltip = "The brightness of the game's white (and of the UI unless it has its own), in nits. 203 is BT.2408's reference white.",
      .category = "Display",
      .default_value = System::Display::DEFAULT_PAPER_WHITE_NITS,
      .range = Settings::Range{80.f, 500.f},
      .format = "%.0f nits",
      .live = true,
      .is_visible = [] { return IsHdr(); },
   },
   Settings::Setting{
      .key = Settings::Core::UI_PAPER_WHITE.key,
      .label = "UI Paper White",
      .tooltip = "The brightness of the game's UI, in nits",
      .category = "Display",
      .default_value = System::Display::DEFAULT_PAPER_WHITE_NITS,
      .range = Settings::Range{80.f, 500.f},
      .format = "%.0f nits",
      .live = true,
      .is_visible = [] { return IsHdr() && HasSeparateUiPaperWhite(); },
   },
   Settings::Setting{
      .key = Settings::Core::SDR_ON_HDR.key,
      .label = "SDR on HDR",
      .tooltip = "Shows the game as SDR on the HDR output, at the paper white (1.0's \"SDR in HDR\" display mode)",
      .category = "Display",
      .visibility = Settings::Visibility::Test,
      .default_value = false,
      .is_visible = [] { return output_hdr; },
   },
};

// LumaSettings from the state and the settings
void Write()
{
   CB::LumaGlobalSettings& settings = Resources::LumaCBuffers::cb_luma_global_settings;
   if (!IsHdr())
   {
      settings.DisplayMode = 0;
      settings.PeakWhiteNits = System::Display::SRGB_WHITE_NITS;
      settings.GamePaperWhiteNits = System::Display::SRGB_WHITE_NITS;
      settings.UIPaperWhiteNits = System::Display::SRGB_WHITE_NITS;
   }
   else
   {
      const float peak = Settings::Get(Settings::Core::PEAK_WHITE);
      const float paper = Settings::Get(Settings::Core::PAPER_WHITE);
      settings.GamePaperWhiteNits = paper;
      settings.UIPaperWhiteNits = HasSeparateUiPaperWhite() ? Settings::Get(Settings::Core::UI_PAPER_WHITE) : paper;
      settings.PeakWhiteNits = peak > 0.f ? peak : display.peak_nits;
      settings.DisplayMode = 1;
      // 1.0 aligns everything to the paper white in this mode
      if (Settings::Get(Settings::Core::SDR_ON_HDR))
      {
         settings.DisplayMode = 2;
         settings.UIPaperWhiteNits = paper;
         settings.PeakWhiteNits = paper;
      }
   }
   Resources::LumaCBuffers::MarkSettingsDirty();
}
} // namespace

void Register()
{
   Settings::Register(SETTINGS, Settings::Kind::Luma);
   Settings::AddChangeListener([](const Settings::Setting& setting, const Settings::Value&) {
      for (const Settings::Setting& own : SETTINGS)
      {
         if (setting.key == own.key)
            return Write();
      }
   });
}

void ApplyOutputMode(bool hdr, const System::Display::Info& info)
{
   output_hdr = hdr;
   display = info;
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
   Utils::Log::Info("Display: the game's image is {} now", value ? "HDR" : "SDR");
   applied_game_hdr = value;
   Write();
}
} // namespace Levels
} // namespace Display
} // namespace Luma
