#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <dxgiformat.h>

// DXGI format facts shared by D3D11 and D3D12 (both describe textures with DXGI_FORMAT). Stateless.
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
// A typed format (never sRGB) for a typeless one, the format itself otherwise. Depth stencil
// formats give the one a shader reads their depth with.
DXGI_FORMAT GetDefaultTyped(DXGI_FORMAT format);
// A format to read a depth stencil resource's depth (or stencil) as: R24G8_TYPELESS -> R24_UNORM_X8_TYPELESS (X24_TYPELESS_G8_UINT)
DXGI_FORMAT GetDepthReadFormat(DXGI_FORMAT format, bool stencil);
// CopyResource and CopySubresourceRegion accept them together: the same family, or two 8 bit RGBA/BGRA ones. Two
// formats without a family are compatible only when equal.
bool AreCopyCompatible(DXGI_FORMAT a, DXGI_FORMAT b);

// CPU data in `from` (rows of `row_pitch` bytes, slices of `depth_pitch`) as R16G16B16A16_FLOAT, tightly packed: what lands in an
// upgraded texture's mirror (from 8 bit RGBA and BGRA, R10G10B10A2_UNORM and R11G11B10_FLOAT). Empty for a format it can't convert.
struct Converted
{
   std::vector<std::byte> bytes;
   uint32_t row_pitch = 0;
   uint32_t depth_pitch = 0;
};

std::optional<Converted> ConvertToRgba16Float(const void* data, uint32_t row_pitch, uint32_t depth_pitch, DXGI_FORMAT from, uint32_t width, uint32_t height,
   uint32_t depth);
} // namespace DXGI
} // namespace Format
} // namespace Utils
} // namespace Luma
