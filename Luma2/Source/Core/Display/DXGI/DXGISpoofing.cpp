#include "Display/DXGI/DXGISpoofing.hpp"

#include <algorithm>
#include <array>

#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "System/Display.hpp"

namespace Luma {
namespace Display {
namespace Spoofing {
namespace DXGI {
namespace {
constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::REPORT_HDR.key,
      .label = "Report HDR to the Game",
      .tooltip = "Tells the game the display is in HDR (HDR10 color space, presentable HDR swapchains), for games that only offer HDR then",
      .category = "Display",
      .visibility = Settings::Visibility::Test,
      .default_value = false,
   },
};
} // namespace

void Register()
{
   Settings::Register(SETTINGS, Settings::Kind::Luma);
}

void OnOutputDesc(DXGI_OUTPUT_DESC1& desc)
{
   if (!Settings::Get(Settings::Core::REPORT_HDR))
      return;
   desc.ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
   desc.BitsPerColor = std::max(desc.BitsPerColor, 10u);
   // An SDR display reports 270 nits or so: a game's HDR path wants a peak of an HDR one
   if (desc.MaxLuminance < System::Display::DEFAULT_PEAK_NITS)
   {
      desc.MaxLuminance = System::Display::DEFAULT_PEAK_NITS;
      desc.MaxFullFrameLuminance = std::max(desc.MaxFullFrameLuminance, System::Display::DEFAULT_PEAK_NITS);
   }
}

void OnColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space, UINT& support)
{
   if (Settings::Get(Settings::Core::REPORT_HDR) && (color_space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 || color_space == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709))
      support |= DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
}
} // namespace DXGI
} // namespace Spoofing
} // namespace Display
} // namespace Luma
