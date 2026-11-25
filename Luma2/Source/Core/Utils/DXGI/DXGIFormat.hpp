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
enum class Encoding : uint8_t
{
   Typeless,
   Unorm,
   UnormSrgb,
   Snorm,
   Uint,
   Sint,
   UFloat,
   SFloat,
   Other,
};

bool IsBlockCompressed(DXGI_FORMAT format);
// Fully typeless (R16G16B16A16_TYPELESS); the partially typeless depth stencil read formats (R24_UNORM_X8_TYPELESS) aren't
bool IsTypeless(DXGI_FORMAT format);
// A format a depth stencil view can have, or its resource's typeless family (R24G8_TYPELESS)
bool IsDepthStencil(DXGI_FORMAT format);
// The typeless format of a format's family (R16G16B16A16_FLOAT -> R16G16B16A16_TYPELESS), UNKNOWN for a format without one
// (R11G11B10_FLOAT)
DXGI_FORMAT GetTypeless(DXGI_FORMAT format);
Encoding GetEncoding(DXGI_FORMAT format);
// The typed format of `typeless_format`'s family with `encoding`; `UNKNOWN` (or `typeless_format` when `!unknown_on_failure`)
// when the family has no such encoding. Not for depth formats.
DXGI_FORMAT GetTyped(DXGI_FORMAT typeless_format, Encoding encoding, bool unknown_on_failure = false);
// `target_format`'s family with `source_format`'s encoding (e.g. R8G8B8A8_UNORM + R16G16B16A16_TYPELESS -> R16G16B16A16_UNORM)
DXGI_FORMAT GetRemapped(DXGI_FORMAT source_format, DXGI_FORMAT target_format);
// A typed format (never sRGB) for a typeless one, the format itself otherwise. Depth stencil
// formats give the one a shader reads their depth with.
DXGI_FORMAT GetDefaultTyped(DXGI_FORMAT format);
// A format to read a depth stencil resource's depth (or stencil) as: R24G8_TYPELESS -> R24_UNORM_X8_TYPELESS (X24_TYPELESS_G8_UINT)
DXGI_FORMAT GetDepthReadFormat(DXGI_FORMAT format, bool stencil);
// CopyResource and CopySubresourceRegion accept them together: the same family, or two 8 bit RGBA/BGRA ones. Two
// formats without a family are compatible only when equal.
bool AreCopyCompatible(DXGI_FORMAT a, DXGI_FORMAT b);
bool IsInteger(DXGI_FORMAT format);
// 4-channel RGBA/BGRA format (or 3-channel RGB when `include_rgb` is true)
bool IsRgba(DXGI_FORMAT format, bool include_rgb = true);

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
