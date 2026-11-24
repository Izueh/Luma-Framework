#include "Display/DXGI/DXGISpoofing.hpp"

#include <algorithm>
#include <array>

#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "System/MonitorDisplay.hpp"

namespace Luma {
namespace Display {
namespace Spoofing {
namespace DXGI {
namespace {
constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::SPOOF_SDR_DISPLAY.key,
      .label = "Spoof SDR Display",
      .tooltip = "Tells the game the display is SDR and does not support HDR color spaces, so games that force HDR on without an in-game toggle take their SDR path.",
      .category = "Display",
      .visibility = Settings::Visibility::Test,
      .default_value = false,
      .requires_restart = true,
   },
};

bool IsHdrColorSpace(DXGI_COLOR_SPACE_TYPE color_space)
{
   switch (color_space)
   {
   case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
   case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
   case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: // scRGB
   case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
   case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
   case DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
   case DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
      return true;
   default:
      return false;
   }
}
} // namespace

void Register()
{
   Settings::Register(SETTINGS, Settings::Kind::Luma);
}

void OnOutputDesc(DXGI_OUTPUT_DESC1& desc)
{
   if (!Settings::Get(Settings::Core::SPOOF_SDR_DISPLAY) || !IsHdrColorSpace(desc.ColorSpace))
      return;
   desc.ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
   desc.BitsPerColor = std::min(desc.BitsPerColor, 8u);
   desc.MaxLuminance = std::min(desc.MaxLuminance, System::Display::SRGB_WHITE_NITS);
   desc.MaxFullFrameLuminance = std::min(desc.MaxFullFrameLuminance, System::Display::SRGB_WHITE_NITS);
}

void OnColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space, UINT& support)
{
   if (Settings::Get(Settings::Core::SPOOF_SDR_DISPLAY) && IsHdrColorSpace(color_space))
      support = 0;
}
} // namespace DXGI
} // namespace Spoofing
} // namespace Display
} // namespace Luma
