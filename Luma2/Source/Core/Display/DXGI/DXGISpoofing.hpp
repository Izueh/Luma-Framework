#pragma once

#include <dxgi1_6.h>

// What the game is told about the display, so a game without an HDR option of its own takes the HDR path Luma wants.
// mod wants. Only the game's calls are answered: Luma's own, inside its scope, see the real display.
namespace Luma {
namespace Display {
namespace Spoofing {
namespace DXGI {
// Its setting, before the game's Init (which sets its default with Settings::SetDefault)
void Register();
// IDXGIOutput6::GetDesc1: an HDR10 color space (and a peak) while reporting HDR
void OnOutputDesc(DXGI_OUTPUT_DESC1& desc);
// IDXGISwapChain3::CheckColorSpaceSupport: HDR10 and scRGB presentable while reporting HDR
void OnColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space, UINT& support);
} // namespace DXGI
} // namespace Spoofing
} // namespace Display
} // namespace Luma
