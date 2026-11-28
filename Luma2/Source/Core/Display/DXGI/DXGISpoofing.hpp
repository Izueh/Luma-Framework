#pragma once

#include <dxgi1_6.h>

// What the game is told about the display, so a game that forces HDR on without an in-game toggle takes its SDR path.
// Only the game's calls are answered: Luma's own, inside its scope, see the real display.
namespace Luma {
namespace Display {
namespace Spoofing {
namespace DXGI {
// Its setting, before the game's Init (which sets its default with Settings::SetDefault)
void Register();
// IDXGIOutput6::GetDesc1: an SDR color space and luminance while spoofing SDR
void SpoofOutputDesc(DXGI_OUTPUT_DESC1& desc);
// IDXGISwapChain3::CheckColorSpaceSupport: HDR color spaces reported unsupported while spoofing SDR
void SpoofColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space, UINT& support);
} // namespace DXGI
} // namespace Spoofing
} // namespace Display
} // namespace Luma
