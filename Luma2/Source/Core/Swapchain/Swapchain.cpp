#include "Swapchain/Swapchain.hpp"

#include <array>
#include <span>

#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "System/WindowPolicy.hpp"

namespace Luma {
namespace Swapchain {
namespace {
Options options;

constexpr std::array<std::string_view, 4> OUTPUT_MODES = {"Auto", "HDR10", "scRGB", "SDR"};
constexpr std::array<std::string_view, 3> MODES = {"Mirror", "In place", "Native"};
constexpr std::array<std::string_view, 2> FULLSCREEN = {"Borderless", "Exclusive"};

constexpr std::array LUMA_SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::OUTPUT_MODE.key,
      .label = "Output Mode",
      .tooltip = "What the game is presented as. Switches live in Mirror mode; in In place mode applies on swapchain resize or restart.\n"
                 "Auto - HDR when Windows has it on for the display (the mod's HDR choice of HDR10 or scRGB), SDR otherwise\n"
                 "HDR10 - 10 bit PQ, BT.2020\nscRGB - 16 bit float, linear\nSDR - 8 bit sRGB",
      .category = "Display",
      .default_value = Settings::EnumIndex{0},
      .enum_labels = OUTPUT_MODES,
      .is_visible = [] { return ManagesOutput(GetMode()); },
   },
   Settings::Setting{
      .key = Settings::Core::HDR_METADATA.key,
      .label = "HDR10 Metadata",
      .tooltip = "Tells the display the image's peak brightness (the display's own) in HDR10",
      .category = "Display",
      .visibility = Settings::Visibility::Test,
      .default_value = true,
   },
   Settings::Setting{
      .key = Settings::Core::SUPPRESS_FOCUS_LOSS.key,
      .label = "Suppress Focus Loss",
      .tooltip = "Keeps the game from pausing when Alt-Tabbed by hiding focus-loss messages from its window.",
      .category = "Display",
#if DEVELOPMENT
      .default_value = true,
#else
      .default_value = false,
#endif
   },
   Settings::Setting{
      .key = Settings::Core::SWAPCHAIN_MODE.key,
      .label = "Mode",
      .tooltip = "Mirror - the game draws to Luma's textures and Luma presents\nIn place - the game draws to the real buffers, upgraded in place\nNative "
                 "- the game draws to the real buffers in its own format and color space (or a ReShade add-on's), nothing composed and no "
                 "output mode; flip model, tearing and fullscreen as below",
      .category = "Swapchain",
      .visibility = Settings::Visibility::Test,
      .default_value = Settings::EnumIndex{0},
      .enum_labels = MODES,
      .requires_restart = true,
   },
   Settings::Setting{
      .key = Settings::Core::SWAPCHAIN_FLIP_MODEL.key,
      .label = "Flip Model",
      .tooltip = "Present with the flip model (needed for HDR, and for independent flip in a borderless window). Off presents as the game "
                 "asked, in SDR.",
      .category = "Swapchain",
      .visibility = Settings::Visibility::Test,
      .default_value = true,
      .requires_restart = true,
   },
   Settings::Setting{
      .key = Settings::Core::SWAPCHAIN_TEARING.key,
      .label = "Tearing",
      .tooltip = "Allows tearing with V-Sync off in a window (variable refresh rate, uncapped frame rates).",
      .category = "Swapchain",
      .visibility = Settings::Visibility::Test,
      .default_value = true,
      .requires_restart = true,
   },
   Settings::Setting{
      .key = Settings::Core::SWAPCHAIN_WRAP.key,
      .label = "Wrap",
      .tooltip = "The game gets a wrapper of the swapchain: Luma composes before any overlay's Present runs, and only the game "
                 "sees its own back buffers. Off, the game holds the real swapchain.",
      .category = "Swapchain",
      .visibility = Settings::Visibility::Test,
      .default_value = true,
      .requires_restart = true,
   },
   Settings::Setting{
      .key = Settings::Core::SWAPCHAIN_FULLSCREEN.key,
      .label = "Fullscreen",
      .tooltip = "Borderless - exclusive fullscreen becomes a borderless window covering the display\nExclusive - the game's exclusive "
                 "fullscreen is kept",
      .category = "Swapchain",
      .visibility = Settings::Visibility::Test,
      .default_value = Settings::EnumIndex{0},
      .enum_labels = FULLSCREEN,
      .requires_restart = true,
   },
};

#if DEVELOPMENT
constexpr std::array DEV_SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::SWAPCHAIN_SCALE.key,
      .label = "Swapchain Scale",
      .tooltip = "The real swapchain's size over the game's back buffer: composition scales the image (Mirror mode only).",
      .category = "Display Composition",
      .visibility = Settings::Visibility::Development,
      .default_value = 1.f,
      .range = Settings::Range{0.25f, 2.f},
      .format = "%.2f",
   },
};
#endif

void ApplyOptionDefaults()
{
   if (Settings::Find(Settings::Core::SWAPCHAIN_MODE.key))
      Settings::SetDefault(Settings::Core::SWAPCHAIN_MODE, Settings::EnumIndex{int32_t(options.mode)});
   if (Settings::Find(Settings::Core::SWAPCHAIN_FULLSCREEN.key))
      Settings::SetDefault(Settings::Core::SWAPCHAIN_FULLSCREEN, Settings::EnumIndex{options.borderless ? 0 : 1});
}

void OnSettingChanged(const Settings::Setting& setting, const Settings::Value& value)
{
   if (setting.key == Settings::Core::SUPPRESS_FOCUS_LOSS.key)
   {
      if (const bool* enabled = std::get_if<bool>(&value))
         System::Window::SetFocusLossSuppression(*enabled);
   }
}
} // namespace

void SetOptions(const Options& value)
{
   options = value;
   ApplyOptionDefaults();
}

const Options& GetOptions()
{
   return options;
}

void Register()
{
   Settings::Register(LUMA_SETTINGS, Settings::Kind::Luma);
#if DEVELOPMENT
   Settings::Register(DEV_SETTINGS, Settings::Kind::Development);
#endif
   ApplyOptionDefaults();
   System::Window::SetFocusLossSuppression(SuppressesFocusLoss());
   Settings::AddChangeListener(&OnSettingChanged);
}

Mode GetMode()
{
   switch (Settings::Get(Settings::Core::SWAPCHAIN_MODE).value)
   {
   case 0: return Mode::Mirror;
   case 1: return Mode::InPlace;
   case 2: return Mode::Native;
   default: return options.mode;
   }
}

bool ManagesOutput(Mode mode)
{
   return mode != Mode::Native;
}

bool UsesFlipModel()
{
   return Settings::Get(Settings::Core::SWAPCHAIN_FLIP_MODEL);
}

bool Wraps()
{
   return Settings::Get(Settings::Core::SWAPCHAIN_WRAP);
}

bool AllowsTearing()
{
   return UsesFlipModel() && Settings::Get(Settings::Core::SWAPCHAIN_TEARING);
}

bool IsBorderless()
{
   return Settings::Get(Settings::Core::SWAPCHAIN_FULLSCREEN).value == 0;
}

bool SuppressesFocusLoss()
{
   return Settings::Get(Settings::Core::SUPPRESS_FOCUS_LOSS);
}

bool SetsHdrMetadata()
{
   return Settings::Get(Settings::Core::HDR_METADATA);
}

float GetScale()
{
#if DEVELOPMENT
   return Settings::Get(Settings::Core::SWAPCHAIN_SCALE);
#else
   return 1.f;
#endif
}

OutputMode GetOutputMode(bool hdr_display)
{
   if (!UsesFlipModel())
      return OutputMode::Sdr;
   switch (Settings::Get(Settings::Core::OUTPUT_MODE).value)
   {
   case 1: return hdr_display ? OutputMode::Hdr10 : OutputMode::Sdr;
   case 2: return hdr_display ? OutputMode::ScRgb : OutputMode::Sdr;
   case 3: return OutputMode::Sdr;
   default: return hdr_display ? options.hdr_output : OutputMode::Sdr;
   }
}

bool RequestsHdr()
{
   const int32_t picked = Settings::Get(Settings::Core::OUTPUT_MODE).value;
   return UsesFlipModel() && (picked == 1 || picked == 2);
}

std::string_view GetName(Mode mode)
{
   switch (mode)
   {
   case Mode::InPlace: return "InPlace";
   case Mode::Native: return "Native";
   default: return "Mirror";
   }
}

std::string_view GetName(OutputMode mode)
{
   switch (mode)
   {
   case OutputMode::Hdr10: return "HDR10";
   case OutputMode::ScRgb: return "scRGB";
   default: return "SDR";
   }
}
} // namespace Swapchain
} // namespace Luma
