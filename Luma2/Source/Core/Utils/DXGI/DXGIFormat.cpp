#include "Utils/DXGI/DXGIFormat.hpp"

#include <bit>
#include <cmath>
#include <cstring>

namespace Luma {
namespace Utils {
namespace Format {
namespace DXGI {
bool IsBlockCompressed(DXGI_FORMAT format)
{
   return (format >= DXGI_FORMAT_BC1_TYPELESS && format <= DXGI_FORMAT_BC5_SNORM) || (format >= DXGI_FORMAT_BC6H_TYPELESS && format <= DXGI_FORMAT_BC7_UNORM_SRGB);
}

bool IsTypeless(DXGI_FORMAT format)
{
   switch (format)
   {
   case DXGI_FORMAT_R32G32B32A32_TYPELESS:
   case DXGI_FORMAT_R32G32B32_TYPELESS:
   case DXGI_FORMAT_R16G16B16A16_TYPELESS:
   case DXGI_FORMAT_R32G32_TYPELESS:
   case DXGI_FORMAT_R32G8X24_TYPELESS:
   case DXGI_FORMAT_R10G10B10A2_TYPELESS:
   case DXGI_FORMAT_R8G8B8A8_TYPELESS:
   case DXGI_FORMAT_B8G8R8A8_TYPELESS:
   case DXGI_FORMAT_B8G8R8X8_TYPELESS:
   case DXGI_FORMAT_R16G16_TYPELESS:
   case DXGI_FORMAT_R32_TYPELESS:
   case DXGI_FORMAT_R24G8_TYPELESS:
   case DXGI_FORMAT_R8G8_TYPELESS:
   case DXGI_FORMAT_R16_TYPELESS:
   case DXGI_FORMAT_R8_TYPELESS:
   case DXGI_FORMAT_BC1_TYPELESS:
   case DXGI_FORMAT_BC2_TYPELESS:
   case DXGI_FORMAT_BC3_TYPELESS:
   case DXGI_FORMAT_BC4_TYPELESS:
   case DXGI_FORMAT_BC5_TYPELESS:
   case DXGI_FORMAT_BC6H_TYPELESS:
   case DXGI_FORMAT_BC7_TYPELESS: return true;
   default: return false;
   }
}

bool IsDepthStencil(DXGI_FORMAT format)
{
   switch (format)
   {
   case DXGI_FORMAT_R32G8X24_TYPELESS:
   case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
   case DXGI_FORMAT_R24G8_TYPELESS:
   case DXGI_FORMAT_D24_UNORM_S8_UINT:
   case DXGI_FORMAT_D32_FLOAT:
   case DXGI_FORMAT_D16_UNORM: return true;
   default: return false;
   }
}

DXGI_FORMAT GetTypeless(DXGI_FORMAT format)
{
   switch (format)
   {
   case DXGI_FORMAT_R16G16B16A16_TYPELESS:
   case DXGI_FORMAT_R16G16B16A16_FLOAT:
   case DXGI_FORMAT_R16G16B16A16_UNORM:
   case DXGI_FORMAT_R16G16B16A16_SNORM:
   case DXGI_FORMAT_R16G16B16A16_UINT:
   case DXGI_FORMAT_R16G16B16A16_SINT: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
   case DXGI_FORMAT_R8G8B8A8_TYPELESS:
   case DXGI_FORMAT_R8G8B8A8_UNORM:
   case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
   case DXGI_FORMAT_R8G8B8A8_SNORM:
   case DXGI_FORMAT_R8G8B8A8_UINT:
   case DXGI_FORMAT_R8G8B8A8_SINT: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
   case DXGI_FORMAT_B8G8R8A8_TYPELESS:
   case DXGI_FORMAT_B8G8R8A8_UNORM:
   case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
   case DXGI_FORMAT_B8G8R8X8_TYPELESS:
   case DXGI_FORMAT_B8G8R8X8_UNORM:
   case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_TYPELESS;
   case DXGI_FORMAT_R32G32B32A32_TYPELESS:
   case DXGI_FORMAT_R32G32B32A32_FLOAT:
   case DXGI_FORMAT_R32G32B32A32_UINT:
   case DXGI_FORMAT_R32G32B32A32_SINT: return DXGI_FORMAT_R32G32B32A32_TYPELESS;
   case DXGI_FORMAT_R32G32B32_TYPELESS:
   case DXGI_FORMAT_R32G32B32_FLOAT:
   case DXGI_FORMAT_R32G32B32_UINT:
   case DXGI_FORMAT_R32G32B32_SINT: return DXGI_FORMAT_R32G32B32_TYPELESS;
   case DXGI_FORMAT_R32G32_TYPELESS:
   case DXGI_FORMAT_R32G32_FLOAT:
   case DXGI_FORMAT_R32G32_UINT:
   case DXGI_FORMAT_R32G32_SINT: return DXGI_FORMAT_R32G32_TYPELESS;
   case DXGI_FORMAT_R16G16_TYPELESS:
   case DXGI_FORMAT_R16G16_FLOAT:
   case DXGI_FORMAT_R16G16_UNORM:
   case DXGI_FORMAT_R16G16_SNORM:
   case DXGI_FORMAT_R16G16_UINT:
   case DXGI_FORMAT_R16G16_SINT: return DXGI_FORMAT_R16G16_TYPELESS;
   case DXGI_FORMAT_R8G8_TYPELESS:
   case DXGI_FORMAT_R8G8_UNORM:
   case DXGI_FORMAT_R8G8_SNORM:
   case DXGI_FORMAT_R8G8_UINT:
   case DXGI_FORMAT_R8G8_SINT: return DXGI_FORMAT_R8G8_TYPELESS;
   case DXGI_FORMAT_R32_TYPELESS:
   case DXGI_FORMAT_R32_FLOAT:
   case DXGI_FORMAT_R32_UINT:
   case DXGI_FORMAT_R32_SINT:
   case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
   case DXGI_FORMAT_R16_TYPELESS:
   case DXGI_FORMAT_R16_FLOAT:
   case DXGI_FORMAT_R16_UNORM:
   case DXGI_FORMAT_R16_SNORM:
   case DXGI_FORMAT_R16_UINT:
   case DXGI_FORMAT_R16_SINT:
   case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
   case DXGI_FORMAT_R8_TYPELESS:
   case DXGI_FORMAT_A8_UNORM:
   case DXGI_FORMAT_R8_UNORM:
   case DXGI_FORMAT_R8_SNORM:
   case DXGI_FORMAT_R8_UINT:
   case DXGI_FORMAT_R8_SINT: return DXGI_FORMAT_R8_TYPELESS;
   case DXGI_FORMAT_R10G10B10A2_TYPELESS:
   case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
   case DXGI_FORMAT_R10G10B10A2_UNORM:
   case DXGI_FORMAT_R10G10B10A2_UINT: return DXGI_FORMAT_R10G10B10A2_TYPELESS;
   case DXGI_FORMAT_R24G8_TYPELESS:
   case DXGI_FORMAT_D24_UNORM_S8_UINT:
   case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
   case DXGI_FORMAT_X24_TYPELESS_G8_UINT: return DXGI_FORMAT_R24G8_TYPELESS;
   case DXGI_FORMAT_R32G8X24_TYPELESS:
   case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
   case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
   case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: return DXGI_FORMAT_R32G8X24_TYPELESS;
   default: return DXGI_FORMAT_UNKNOWN;
   }
}

DXGI_FORMAT GetDefaultTyped(DXGI_FORMAT format)
{
   switch (format)
   {
   case DXGI_FORMAT_R8_TYPELESS: return DXGI_FORMAT_R8_UNORM;
   case DXGI_FORMAT_R8G8_TYPELESS: return DXGI_FORMAT_R8G8_UNORM;
   case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
   case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
   case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
   case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
   case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
   case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_FLOAT;
   case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
   case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
   case DXGI_FORMAT_D32_FLOAT:
   case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
   case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
   case DXGI_FORMAT_R32G32B32_TYPELESS: return DXGI_FORMAT_R32G32B32_FLOAT;
   case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
   case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
   case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
   case DXGI_FORMAT_D24_UNORM_S8_UINT:
   case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
   case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM;
   case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM;
   case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM;
   case DXGI_FORMAT_BC4_TYPELESS: return DXGI_FORMAT_BC4_UNORM;
   case DXGI_FORMAT_BC5_TYPELESS: return DXGI_FORMAT_BC5_UNORM;
   case DXGI_FORMAT_BC6H_TYPELESS: return DXGI_FORMAT_BC6H_UF16;
   case DXGI_FORMAT_BC7_TYPELESS: return DXGI_FORMAT_BC7_UNORM;
   default: return format;
   }
}

DXGI_FORMAT GetDepthReadFormat(DXGI_FORMAT format, bool stencil)
{
   switch (GetTypeless(format))
   {
   case DXGI_FORMAT_R24G8_TYPELESS: return stencil ? DXGI_FORMAT_X24_TYPELESS_G8_UINT : DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
   case DXGI_FORMAT_R32G8X24_TYPELESS: return stencil ? DXGI_FORMAT_X32_TYPELESS_G8X24_UINT : DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
   case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
   case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
   default: return format;
   }
}

bool AreCopyCompatible(DXGI_FORMAT a, DXGI_FORMAT b)
{
   if (a == b)
      return true;
   const auto is_8bit_rgba = [](DXGI_FORMAT typeless) {
      return typeless == DXGI_FORMAT_R8G8B8A8_TYPELESS || typeless == DXGI_FORMAT_B8G8R8A8_TYPELESS || typeless == DXGI_FORMAT_B8G8R8X8_TYPELESS;
   };
   const DXGI_FORMAT typeless_a = GetTypeless(a);
   const DXGI_FORMAT typeless_b = GetTypeless(b);
   if (typeless_a == DXGI_FORMAT_UNKNOWN || typeless_b == DXGI_FORMAT_UNKNOWN)
      return false;
   return typeless_a == typeless_b || (is_8bit_rgba(typeless_a) && is_8bit_rgba(typeless_b));
}

namespace {
// What one texel of a source format is in linear RGBA
using Reader = void (*)(const std::byte* texel, float rgba[4]);

void ReadRgba8(const std::byte* texel, float rgba[4])
{
   for (int i = 0; i < 4; i++)
      rgba[i] = float(uint8_t(texel[i])) / 255.f;
}

void ReadBgra8(const std::byte* texel, float rgba[4])
{
   rgba[0] = float(uint8_t(texel[2])) / 255.f;
   rgba[1] = float(uint8_t(texel[1])) / 255.f;
   rgba[2] = float(uint8_t(texel[0])) / 255.f;
   rgba[3] = float(uint8_t(texel[3])) / 255.f;
}

void ReadBgrx8(const std::byte* texel, float rgba[4])
{
   ReadBgra8(texel, rgba);
   rgba[3] = 1.f;
}

void ReadRgb10A2(const std::byte* texel, float rgba[4])
{
   uint32_t value;
   std::memcpy(&value, texel, 4);
   rgba[0] = float(value & 0x3FF) / 1023.f;
   rgba[1] = float((value >> 10) & 0x3FF) / 1023.f;
   rgba[2] = float((value >> 20) & 0x3FF) / 1023.f;
   rgba[3] = float(value >> 30) / 3.f;
}

// An unsigned float of `mantissa_bits` with fp16's exponent bias (R11G11B10's channels)
float SmallFloat(uint32_t bits, uint32_t mantissa_bits)
{
   const uint32_t exponent = bits >> mantissa_bits;
   const uint32_t mantissa = bits & ((1u << mantissa_bits) - 1);
   const float fraction = float(mantissa) / float(1u << mantissa_bits);
   if (exponent == 0)
      return std::ldexp(fraction, -14);
   if (exponent == 31)
      return mantissa ? NAN : INFINITY;
   return std::ldexp(1.f + fraction, int(exponent) - 15);
}

void ReadRg11B10(const std::byte* texel, float rgba[4])
{
   uint32_t value;
   std::memcpy(&value, texel, 4);
   rgba[0] = SmallFloat(value & 0x7FF, 6);
   rgba[1] = SmallFloat((value >> 11) & 0x7FF, 6);
   rgba[2] = SmallFloat(value >> 22, 5);
   rgba[3] = 1.f;
}

// IEEE fp32 to fp16, rounding to nearest even, with infinities and NaN kept
uint16_t ToHalf(float value)
{
   const uint32_t bits = std::bit_cast<uint32_t>(value);
   const uint32_t sign = (bits >> 16) & 0x8000;
   const uint32_t exponent = (bits >> 23) & 0xFF;
   uint32_t mantissa = bits & 0x7FFFFF;
   if (exponent == 0xFF)
      return uint16_t(sign | 0x7C00 | (mantissa ? 0x200 : 0));
   const int32_t half_exponent = int32_t(exponent) - 127 + 15;
   if (half_exponent >= 31)
      return uint16_t(sign | 0x7C00);
   if (half_exponent <= 0)
   {
      if (half_exponent < -10)
         return uint16_t(sign);
      mantissa |= 0x800000;
      const uint32_t shift = uint32_t(14 - half_exponent);
      uint32_t half = mantissa >> shift;
      const uint32_t remainder = mantissa & ((1u << shift) - 1);
      const uint32_t halfway = 1u << (shift - 1);
      if (remainder > halfway || (remainder == halfway && (half & 1)))
         half++;
      return uint16_t(sign | half);
   }
   uint32_t half = (uint32_t(half_exponent) << 10) | (mantissa >> 13);
   const uint32_t remainder = mantissa & 0x1FFF;
   if (remainder > 0x1000 || (remainder == 0x1000 && (half & 1)))
      half++;
   return uint16_t(sign | half);
}
} // namespace

std::optional<Converted> ConvertToRgba16Float(const void* data, uint32_t row_pitch, uint32_t depth_pitch, DXGI_FORMAT from, uint32_t width, uint32_t height,
   uint32_t depth)
{
   Reader read = nullptr;
   switch (from)
   {
   case DXGI_FORMAT_R8G8B8A8_UNORM:
   case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: read = &ReadRgba8; break;
   case DXGI_FORMAT_B8G8R8A8_UNORM:
   case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: read = &ReadBgra8; break;
   case DXGI_FORMAT_B8G8R8X8_UNORM:
   case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: read = &ReadBgrx8; break;
   case DXGI_FORMAT_R10G10B10A2_UNORM: read = &ReadRgb10A2; break;
   case DXGI_FORMAT_R11G11B10_FLOAT: read = &ReadRg11B10; break;
   default: return std::nullopt;
   }
   if (!data)
      return std::nullopt;
   Converted converted = {.row_pitch = width * 8, .depth_pitch = width * 8 * height};
   converted.bytes.resize(size_t(converted.depth_pitch) * depth);
   auto* out = reinterpret_cast<uint16_t*>(converted.bytes.data());
   for (uint32_t z = 0; z < depth; z++)
   {
      for (uint32_t y = 0; y < height; y++)
      {
         const auto* row = static_cast<const std::byte*>(data) + size_t(z) * depth_pitch + size_t(y) * row_pitch;
         for (uint32_t x = 0; x < width; x++)
         {
            float rgba[4];
            read(row + size_t(x) * 4, rgba);
            for (int c = 0; c < 4; c++)
               *out++ = ToHalf(rgba[c]);
         }
      }
   }
   return converted;
}
} // namespace DXGI
} // namespace Format
} // namespace Utils
} // namespace Luma
