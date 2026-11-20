#include "Swapchain/Swapchain.hpp"

#include <array>
#include <span>

#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"

namespace Luma {
namespace Swapchain {
namespace {
Options options;

constexpr std::array<std::string_view, 4> OUTPUT_MODES = {"Auto", "HDR10", "scRGB", "SDR"};
constexpr std::array<std::string_view, 4> MODES = {"Game default", "Mirror", "In place", "Native"};
constexpr std::array<std::string_view, 3> FULLSCREEN = {"Game default", "Borderless", "Exclusive"};

constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::OUTPUT_MODE.key,
      .label = "Output Mode",
      .tooltip = "What the game is presented as.\nAuto - HDR when Windows has it on for the display (the game's choice of HDR10 or scRGB), SDR "
                 "otherwise\nHDR10 - 10 bit PQ, BT.2020\nscRGB - 16 bit float, linear\nSDR - 8 bit sRGB",
      .category = "Display",
      .default_value = Settings::EnumIndex{0},
      .enum_labels = OUTPUT_MODES,
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
      .key = Settings::Core::SWAPCHAIN_MODE.key,
      .label = "Mode",
      .tooltip = "Mirror - the game draws to Luma's textures and Luma presents\nIn place - the game draws to the real buffers, upgraded (1.0)\nNative "
                 "- the game draws to the real buffers in its own format and color space (or a ReShade add-on's), nothing composed and no "
                 "output mode; flip model, tearing and fullscreen as below\nFrom the game's next swapchain, usually a restart",
      .category = "Swapchain",
      .visibility = Settings::Visibility::Test,
      .default_value = Settings::EnumIndex{0},
      .enum_labels = MODES,
   },
   Settings::Setting{
      .key = Settings::Core::SWAPCHAIN_FLIP_MODEL.key,
      .label = "Flip Model",
      .tooltip = "Present with the flip model (needed for HDR, and for independent flip in a borderless window). Off presents as the game "
                 "asked, in SDR.\nFrom the game's next swapchain, usually a restart",
      .category = "Swapchain",
      .visibility = Settings::Visibility::Test,
      .default_value = true,
   },
   Settings::Setting{
      .key = Settings::Core::SWAPCHAIN_TEARING.key,
      .label = "Tearing",
      .tooltip = "Allows tearing with V-Sync off in a window (variable refresh rate, uncapped frame rates).\nFrom the game's next swapchain, "
                 "usually a restart",
      .category = "Swapchain",
      .visibility = Settings::Visibility::Test,
      .default_value = true,
   },
   Settings::Setting{
      .key = Settings::Core::SWAPCHAIN_WRAP.key,
      .label = "Wrap",
      .tooltip = "The game gets a wrapper of the swapchain (as ReShade's): Luma composes before any overlay's Present runs, and only the game "
                 "sees its own back buffers. Off, the game holds the real swapchain.\nFrom the game's next swapchain, usually a restart",
      .category = "Swapchain",
      .visibility = Settings::Visibility::Test,
      .default_value = true,
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
   },
#if DEVELOPMENT
   Settings::Setting{
      .key = Settings::Core::SWAPCHAIN_SCALE.key,
      .label = "Swapchain Scale",
      .tooltip = "The real swapchain's size over the game's back buffer: composition scales the image (Mirror mode only).\nFrom the game's next swapchain or resize",
      .category = "Development",
      .visibility = Settings::Visibility::Development,
      .default_value = 1.f,
      .range = Settings::Range{0.25f, 2.f},
      .format = "%.2f",
   },
#endif
};
} // namespace

void SetOptions(const Options& value)
{
   options = value;
}

const Options& GetOptions()
{
   return options;
}

void Register()
{
   // The Development one is a Development kind: in that section of the view and the file
   Settings::Register(std::span(SETTINGS).first(6), Settings::Kind::Luma);
   Settings::Register(std::span(SETTINGS).subspan(6), Settings::Kind::Development);
}

Mode GetMode()
{
   switch (Settings::Get(Settings::Core::SWAPCHAIN_MODE).value)
   {
   case 1: return Mode::Mirror;
   case 2: return Mode::InPlace;
   case 3: return Mode::Native;
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
   switch (Settings::Get(Settings::Core::SWAPCHAIN_FULLSCREEN).value)
   {
   case 1: return true;
   case 2: return false;
   default: return options.borderless;
   }
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
