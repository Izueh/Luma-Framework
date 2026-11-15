#pragma once

#include <dxgiformat.h>

// DXGI format facts shared by D3D11 and D3D12 (both describe textures with DXGI_FORMAT), 1.0's utils/format.hpp. Stateless.
namespace Luma {
namespace Utils {
namespace Format {
namespace DXGI {
bool IsBlockCompressed(DXGI_FORMAT format);
// Fully typeless (R16G16B16A16_TYPELESS); the partially typeless depth stencil read formats (R24_UNORM_X8_TYPELESS) aren't
bool IsTypeless(DXGI_FORMAT format);
// A format a depth stencil view can have, or its resource's typeless family (R24G8_TYPELESS)
bool IsDepthStencil(DXGI_FORMAT format);
// The typeless format of a format's family (R16G16B16A16_FLOAT -> R16G16B16A16_TYPELESS), UNKNOWN for a format without one
// (R11G11B10_FLOAT)
DXGI_FORMAT GetTypeless(DXGI_FORMAT format);
// A typed format for a typeless one (1.0's GetDefaultTypedFormat without its sRGB choice), the format itself otherwise. Depth stencil
// formats give the one a shader reads their depth with.
DXGI_FORMAT GetDefaultTyped(DXGI_FORMAT format);
// A format to read a depth stencil resource's depth (or stencil) as: R24G8_TYPELESS -> R24_UNORM_X8_TYPELESS (X24_TYPELESS_G8_UINT)
DXGI_FORMAT GetDepthReadFormat(DXGI_FORMAT format, bool stencil);
// CopyResource and CopySubresourceRegion accept them together: the same family, or two 8 bit RGBA/BGRA ones (1.0's
// AreFormatsCopyCompatible, except that two formats without a family are compatible only when equal)
bool AreCopyCompatible(DXGI_FORMAT a, DXGI_FORMAT b);
} // namespace DXGI
} // namespace Format
} // namespace Utils
} // namespace Luma
