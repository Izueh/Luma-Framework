#pragma once

#include "dxgiformat.h"

#include <iomanip>
#include <ios>
#include <ostream>


#include "./shader_types.hpp"

#define PRINT_CRC32(crc32) "0x" << std::hex << std::setw(8) << std::setfill('0') << crc32 << std::setfill(' ') << std::dec

// ReShade specific
namespace
{
   inline std::ostream& operator<<(std::ostream& os, const ShaderStages value)
   {
      switch (value)
      {
      case ShaderStages::Vertex:      return os << "vertex";
      case ShaderStages::Hull:        return os << "hull";
      case ShaderStages::Domain:      return os << "domain";
      case ShaderStages::Geometry:    return os << "geometry";
      case ShaderStages::Pixel:       return os << "pixel";
      case ShaderStages::Compute:     return os << "compute";
      case ShaderStages::All:         return os << "all";
      case ShaderStages::AllGraphics: return os << "all_graphics";
      default:                        return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const D3D11_SHADER_VERSION_TYPE value)
   {
      switch (value)
      {
      case D3D11_SHVER_VERTEX_SHADER:   return os << "vertex_shader";
      case D3D11_SHVER_HULL_SHADER:     return os << "hull_shader";
      case D3D11_SHVER_DOMAIN_SHADER:   return os << "domain_shader";
      case D3D11_SHVER_GEOMETRY_SHADER: return os << "geometry_shader";
      case D3D11_SHVER_PIXEL_SHADER:    return os << "pixel_shader";
      case D3D11_SHVER_COMPUTE_SHADER:  return os << "compute_shader";
      default:                          return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const boolean value)
   {
      return os << ((value != 0u) ? "true" : "false");
   }

   inline std::ostream& operator<<(std::ostream& os, const DXGI_FORMAT value)
   {
      switch (value)
      {
      case DXGI_FORMAT_R1_UNORM:              return os << "R1 UNORM";
      case DXGI_FORMAT_A8_UNORM:              return os << "A8 UNORM";
      case DXGI_FORMAT_R8_TYPELESS:           return os << "R8 TYPELESS";
      case DXGI_FORMAT_R8_UINT:               return os << "R8 UINT";
      case DXGI_FORMAT_R8_SINT:               return os << "R8 SINT";
      case DXGI_FORMAT_R8_UNORM:              return os << "R8 UNORM";
      case DXGI_FORMAT_R8_SNORM:              return os << "R8 SNORM";
      case DXGI_FORMAT_R8G8_TYPELESS:         return os << "R8G8 TYPELESS";
      case DXGI_FORMAT_R8G8_UINT:             return os << "R8G8 UINT";
      case DXGI_FORMAT_R8G8_SINT:             return os << "R8G8 SINT";
      case DXGI_FORMAT_R8G8_UNORM:            return os << "R8G8 UNORM";
      case DXGI_FORMAT_R8G8_SNORM:            return os << "R8G8 SNORM";
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return os << "R8G8B8A8 TYPELESS";
      case DXGI_FORMAT_R8G8B8A8_UINT:         return os << "R8G8B8A8 UINT";
      case DXGI_FORMAT_R8G8B8A8_SINT:         return os << "R8G8B8A8 SINT";
      case DXGI_FORMAT_R8G8B8A8_UNORM:        return os << "R8G8B8A8 UNORM";
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return os << "R8G8B8A8 UNORM SRGB";
      case DXGI_FORMAT_R8G8B8A8_SNORM:        return os << "R8G8B8A8 SNORM";
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:     return os << "B8G8R8A8 TYPELESS";
      case DXGI_FORMAT_B8G8R8A8_UNORM:        return os << "B8G8R8A8 UNORM";
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:   return os << "B8G8R8A8 UNORM SRGB";
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:     return os << "B8G8R8X8 TYPELESS";
      case DXGI_FORMAT_B8G8R8X8_UNORM:        return os << "B8G8R8X8 UNORM";
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:   return os << "B8G8R8X8 UNORM SRGB";
      case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return os << "R10G10B10A2 TYPELESS";
      case DXGI_FORMAT_R10G10B10A2_UINT:      return os << "R10G10B10A2 UINT";
      case DXGI_FORMAT_R10G10B10A2_UNORM:     return os << "R10G10B10A2 UNORM";
      case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:   return os << "R10G10B10A2 XR BIAS";
      case DXGI_FORMAT_R16_TYPELESS:          return os << "R16 TYPELESS";
      case DXGI_FORMAT_R16_UINT:              return os << "R16 UINT";
      case DXGI_FORMAT_R16_SINT:              return os << "R16 SINT";
      case DXGI_FORMAT_R16_UNORM:             return os << "R16 UNORM";
      case DXGI_FORMAT_R16_SNORM:             return os << "R16 SNORM";
      case DXGI_FORMAT_R16_FLOAT:             return os << "R16 FLOAT";
      case DXGI_FORMAT_R16G16_TYPELESS:       return os << "R16G16 TYPELESS";
      case DXGI_FORMAT_R16G16_UINT:           return os << "R16G16 UINT";
      case DXGI_FORMAT_R16G16_SINT:           return os << "R16G16 SINT";
      case DXGI_FORMAT_R16G16_UNORM:          return os << "R16G16 UNORM";
      case DXGI_FORMAT_R16G16_SNORM:          return os << "R16G16 SNORM";
      case DXGI_FORMAT_R16G16_FLOAT:          return os << "R16G16 FLOAT";
      case DXGI_FORMAT_R16G16B16A16_TYPELESS: return os << "R16G16B16A16 TYPELESS";
      case DXGI_FORMAT_R16G16B16A16_UINT:     return os << "R16G16B16A16 UINT";
      case DXGI_FORMAT_R16G16B16A16_SINT:     return os << "R16G16B16A16 SINT";
      case DXGI_FORMAT_R16G16B16A16_UNORM:    return os << "R16G16B16A16 UNORM";
      case DXGI_FORMAT_R16G16B16A16_SNORM:    return os << "R16G16B16A16 SNORM";
      case DXGI_FORMAT_R16G16B16A16_FLOAT:    return os << "R16G16B16A16 FLOAT";
      case DXGI_FORMAT_R32_TYPELESS:          return os << "R32 TYPELESS";
      case DXGI_FORMAT_R32_UINT:              return os << "R32 UINT";
      case DXGI_FORMAT_R32_SINT:              return os << "R32 SINT";
      case DXGI_FORMAT_R32_FLOAT:             return os << "R32 FLOAT";
      case DXGI_FORMAT_R32G32_TYPELESS:       return os << "R32G32 TYPELESS";
      case DXGI_FORMAT_R32G32_UINT:           return os << "R32G32 UINT";
      case DXGI_FORMAT_R32G32_SINT:           return os << "R32G32 SINT";
      case DXGI_FORMAT_R32G32_FLOAT:          return os << "R32G32 FLOAT";
      case DXGI_FORMAT_R32G32B32_TYPELESS:    return os << "R32G32B32 TYPELESS";
      case DXGI_FORMAT_R32G32B32_UINT:        return os << "R32G32B32 UINT";
      case DXGI_FORMAT_R32G32B32_SINT:        return os << "R32G32B32 SINT";
      case DXGI_FORMAT_R32G32B32_FLOAT:       return os << "R32G32B32 FLOAT";
      case DXGI_FORMAT_R32G32B32A32_TYPELESS: return os << "R32G32B32A32 TYPELESS";
      case DXGI_FORMAT_R32G32B32A32_UINT:     return os << "R32G32B32A32 UINT";
      case DXGI_FORMAT_R32G32B32A32_SINT:     return os << "R32G32B32A32 SINT";
      case DXGI_FORMAT_R32G32B32A32_FLOAT:    return os << "R32G32B32A32 FLOAT";
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:              return os << "R9G9B9E5";
      case DXGI_FORMAT_R11G11B10_FLOAT:       return os << "R11G11B10 FLOAT";
      case DXGI_FORMAT_B5G6R5_UNORM:          return os << "B5G6R5 UNORM";
      case DXGI_FORMAT_B5G5R5A1_UNORM:        return os << "B5G5R5A1 UNORM";
      case DXGI_FORMAT_B4G4R4A4_UNORM:        return os << "B4G4R4A4 UNORM";
      case DXGI_FORMAT_A4B4G4R4_UNORM:        return os << "A4B4G4R4 UNORM";
      case DXGI_FORMAT_D16_UNORM:             return os << "D16 UNORM";
      case DXGI_FORMAT_D24_UNORM_S8_UINT:     return os << "D24 UNORM S8 UINT";
      case DXGI_FORMAT_D32_FLOAT:             return os << "D32 FLOAT";
      case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:     return os << "D32 FLOAT S8 UINT";
      case DXGI_FORMAT_R24G8_TYPELESS:       return os << "R24 G8 TYPELESS";
      case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:     return os << "R24 UNORM X8 UINT";
      case DXGI_FORMAT_X24_TYPELESS_G8_UINT:     return os << "X24 UNORM G8 UINT";
      case DXGI_FORMAT_R32G8X24_TYPELESS:       return os << "R32 G8 TYPELESS";
      case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:     return os << "R32 FLOAT X8 UINT";
      case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:     return os << "X32 FLOAT G8 UINT";
      case DXGI_FORMAT_BC1_TYPELESS:          return os << "BC1 TYPELESS";
      case DXGI_FORMAT_BC1_UNORM:             return os << "BC1 UNORM";
      case DXGI_FORMAT_BC1_UNORM_SRGB:        return os << "BC1 UNORM SRGB";
      case DXGI_FORMAT_BC2_TYPELESS:          return os << "BC2 TYPELESS";
      case DXGI_FORMAT_BC2_UNORM:             return os << "BC2 UNORM";
      case DXGI_FORMAT_BC2_UNORM_SRGB:        return os << "BC2 UNORM SRGB";
      case DXGI_FORMAT_BC3_TYPELESS:          return os << "BC3 TYPELESS";
      case DXGI_FORMAT_BC3_UNORM:             return os << "BC3 UNORM";
      case DXGI_FORMAT_BC3_UNORM_SRGB:        return os << "BC3 UNORM SRGB";
      case DXGI_FORMAT_BC4_TYPELESS:          return os << "BC4 TYPELESS";
      case DXGI_FORMAT_BC4_UNORM:             return os << "BC4 UNORM";
      case DXGI_FORMAT_BC4_SNORM:             return os << "BC4 SNORM";
      case DXGI_FORMAT_BC5_TYPELESS:          return os << "BC5 TYPELESS";
      case DXGI_FORMAT_BC5_UNORM:             return os << "BC5 UNORM";
      case DXGI_FORMAT_BC5_SNORM:             return os << "BC5 SNORM";
      case DXGI_FORMAT_BC6H_TYPELESS:         return os << "BC6H TYPELESS";
      case DXGI_FORMAT_BC6H_UF16:           return os << "BC6H UFLOAT";
      case DXGI_FORMAT_BC6H_SF16:           return os << "BC6H SFLOAT";
      case DXGI_FORMAT_BC7_TYPELESS:          return os << "BC7 TYPELESS";
      case DXGI_FORMAT_BC7_UNORM:             return os << "BC7 UNORM";
      case DXGI_FORMAT_BC7_UNORM_SRGB:        return os << "BC7 UNORM SRGB";
      case DXGI_FORMAT_R8G8_B8G8_UNORM:       return os << "R8G8 B8G8 UNORM";
      case DXGI_FORMAT_G8R8_G8B8_UNORM:       return os << "G8R8 G8B8 UNORM";
      case DXGI_FORMAT_UNKNOWN:               return os << "UNKNOWN";
      default:                                          assert(false); return os << "UNKNOWN"; // We shouldn't get here unless we are missing a format
      }
   }

}

// TODO: move these utilities to texture.hpp or resource.hpp or something, together with mip utilities and other stuff
// DirectX specific
namespace
{
   // All float, sRGB and depth formats are returned as linear here.
   // Pass in the view format if possible.
   bool IsLinearFormat(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_D32_FLOAT:
      case DXGI_FORMAT_D16_UNORM:
      case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: // Not a depth format directly but realistically only used for depth
      case DXGI_FORMAT_D24_UNORM_S8_UINT:
      case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: // Not a depth format directly but realistically only used for depth
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
      case DXGI_FORMAT_R16_FLOAT:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R32G32_FLOAT:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_R11G11B10_FLOAT:
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      case DXGI_FORMAT_BC1_UNORM_SRGB:
      case DXGI_FORMAT_BC2_UNORM_SRGB:
      case DXGI_FORMAT_BC3_UNORM_SRGB:
      case DXGI_FORMAT_BC7_UNORM_SRGB:
         return true;
      }
      return false;
   }

   // Returns true if a format has RGB channels, and optional A too (in any order).
   // Can be used to determine if a texture is meant to be storing a RGB color or not.
   bool IsRGBAFormat(DXGI_FORMAT format, bool ignore_alpha = false)
   {
      switch (format)
      {
      case DXGI_FORMAT_B4G4R4A4_UNORM:
      case DXGI_FORMAT_A4B4G4R4_UNORM:
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      case DXGI_FORMAT_R8G8B8A8_UINT:
      case DXGI_FORMAT_R8G8B8A8_SNORM:
      case DXGI_FORMAT_R8G8B8A8_SINT:
      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_UNORM:
      case DXGI_FORMAT_R32G32B32A32_TYPELESS:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_UINT:
      case DXGI_FORMAT_R32G32B32A32_SINT:
      case DXGI_FORMAT_BC2_TYPELESS:
      case DXGI_FORMAT_BC2_UNORM:
      case DXGI_FORMAT_BC2_UNORM_SRGB:
      case DXGI_FORMAT_BC3_TYPELESS:
      case DXGI_FORMAT_BC3_UNORM:
      case DXGI_FORMAT_BC3_UNORM_SRGB:
      case DXGI_FORMAT_BC7_TYPELESS:
      case DXGI_FORMAT_BC7_UNORM:
      case DXGI_FORMAT_BC7_UNORM_SRGB:
         return true;

      // Alpha not present or not used:
      case DXGI_FORMAT_B5G6R5_UNORM:
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
      case DXGI_FORMAT_B8G8R8X8_UNORM:
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      case DXGI_FORMAT_R32G32B32_TYPELESS:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32_UINT:
      case DXGI_FORMAT_R32G32B32_SINT:
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
      case DXGI_FORMAT_R11G11B10_FLOAT:
         return ignore_alpha;

      // Limited alpha cases:
      case DXGI_FORMAT_B5G5R5A1_UNORM:
      case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_TYPELESS:
      case DXGI_FORMAT_R10G10B10A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_UINT:
      case DXGI_FORMAT_BC1_TYPELESS:
      case DXGI_FORMAT_BC1_UNORM:
      case DXGI_FORMAT_BC1_UNORM_SRGB:
         return true;
      }
      return false;
   }

   // If true, the format is "HDR" and can go beyond 1. Depth formats are ignored.
   bool IsFloatFormat(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
      case DXGI_FORMAT_R16_FLOAT:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R32G32_FLOAT:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_R11G11B10_FLOAT:
         return true;
      }
      return false;
   }

   // Depth formats are ignored.
   bool IsSignedFloatFormat(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_R16_FLOAT:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R32G32_FLOAT:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
         return true;
      }
      return false;
   }
   
   bool IsIntFormat(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_R32G32B32A32_UINT:
      case DXGI_FORMAT_R32G32B32A32_SINT:
      case DXGI_FORMAT_R32G32B32_UINT:
      case DXGI_FORMAT_R32G32B32_SINT:
      case DXGI_FORMAT_R16G16B16A16_UINT:
      case DXGI_FORMAT_R16G16B16A16_SINT:
      case DXGI_FORMAT_R32G32_UINT:
      case DXGI_FORMAT_R32G32_SINT:
      case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
      case DXGI_FORMAT_R10G10B10A2_UINT:
      case DXGI_FORMAT_R8G8B8A8_UINT:
      case DXGI_FORMAT_R8G8B8A8_SINT:
      case DXGI_FORMAT_R16G16_UINT:
      case DXGI_FORMAT_R16G16_SINT:
      case DXGI_FORMAT_R32_UINT:
      case DXGI_FORMAT_R32_SINT:
      case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
      case DXGI_FORMAT_R8G8_UINT:
      case DXGI_FORMAT_R8G8_SINT:
      case DXGI_FORMAT_R16_UINT:
      case DXGI_FORMAT_R16_SINT:
      case DXGI_FORMAT_R8_UINT:
      case DXGI_FORMAT_R8_SINT:
         return true;
      // Partial int (stencil) depth formats
      case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      case DXGI_FORMAT_D24_UNORM_S8_UINT:
         return false;
      }
      return false;
   }

   bool IsTypelessFormat(DXGI_FORMAT format)
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
      case DXGI_FORMAT_BC7_TYPELESS:
         return true;
      // Partially typeless formats aren't really typeless, they are just allowing views to access part of a resource while ignoring another part, so ignore them
      case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
      case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
      case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
      case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
         return false;
      }
      return false;
   }

   DXGI_FORMAT GetTypelessFormat(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_UNORM:
      case DXGI_FORMAT_R16G16B16A16_SNORM:
      case DXGI_FORMAT_R16G16B16A16_UINT:
      case DXGI_FORMAT_R16G16B16A16_SINT:
         return DXGI_FORMAT_R16G16B16A16_TYPELESS;
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      case DXGI_FORMAT_R8G8B8A8_SNORM:
      case DXGI_FORMAT_R8G8B8A8_UINT:
      case DXGI_FORMAT_R8G8B8A8_SINT:
         return DXGI_FORMAT_R8G8B8A8_TYPELESS;
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
         return DXGI_FORMAT_B8G8R8A8_TYPELESS;
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
      case DXGI_FORMAT_B8G8R8X8_UNORM:
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
         return DXGI_FORMAT_B8G8R8X8_TYPELESS;
      case DXGI_FORMAT_R32G32B32A32_TYPELESS:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_UINT:
      case DXGI_FORMAT_R32G32B32A32_SINT:
         return DXGI_FORMAT_R32G32B32A32_TYPELESS;
      case DXGI_FORMAT_R32G32B32_TYPELESS:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32_UINT:
      case DXGI_FORMAT_R32G32B32_SINT:
         return DXGI_FORMAT_R32G32B32_TYPELESS;
      case DXGI_FORMAT_R32G32_TYPELESS:
      case DXGI_FORMAT_R32G32_FLOAT:
      case DXGI_FORMAT_R32G32_UINT:
      case DXGI_FORMAT_R32G32_SINT:
         return DXGI_FORMAT_R32G32_TYPELESS;
      case DXGI_FORMAT_R16G16_TYPELESS:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R16G16_UNORM:
      case DXGI_FORMAT_R16G16_SNORM:
      case DXGI_FORMAT_R16G16_UINT:
      case DXGI_FORMAT_R16G16_SINT:
         return DXGI_FORMAT_R16G16_TYPELESS;
      case DXGI_FORMAT_R8G8_TYPELESS:
      case DXGI_FORMAT_R8G8_UNORM:
      case DXGI_FORMAT_R8G8_SNORM:
      case DXGI_FORMAT_R8G8_UINT:
      case DXGI_FORMAT_R8G8_SINT:
         return DXGI_FORMAT_R8G8_TYPELESS;
      case DXGI_FORMAT_R32_TYPELESS:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R32_UINT:
      case DXGI_FORMAT_R32_SINT:
      case DXGI_FORMAT_D32_FLOAT:
         return DXGI_FORMAT_R32_TYPELESS;
      case DXGI_FORMAT_R16_TYPELESS:
      case DXGI_FORMAT_R16_FLOAT:
      case DXGI_FORMAT_R16_UNORM:
      case DXGI_FORMAT_R16_SNORM:
      case DXGI_FORMAT_R16_UINT:
      case DXGI_FORMAT_R16_SINT:
      case DXGI_FORMAT_D16_UNORM:
         return DXGI_FORMAT_R16_TYPELESS;
      case DXGI_FORMAT_R8_TYPELESS:
      case DXGI_FORMAT_A8_UNORM:
      case DXGI_FORMAT_R8_UNORM:
      case DXGI_FORMAT_R8_SNORM:
      case DXGI_FORMAT_R8_UINT:
      case DXGI_FORMAT_R8_SINT:
         return DXGI_FORMAT_R8_TYPELESS;
      case DXGI_FORMAT_R10G10B10A2_TYPELESS:
      case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_UINT:
         return DXGI_FORMAT_R10G10B10A2_TYPELESS;
      case DXGI_FORMAT_R1_UNORM:
      case DXGI_FORMAT_R8G8_B8G8_UNORM:
      case DXGI_FORMAT_G8R8_G8B8_UNORM:
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
      case DXGI_FORMAT_R11G11B10_FLOAT:
         return DXGI_FORMAT_UNKNOWN; // These formats don't have a TYPELESS version (usually because they are encoded or unique), so return an invalid format to highlight that. There's a few more not included here.
      }
      return DXGI_FORMAT_UNKNOWN;
   }

   DXGI_FORMAT GetDefaultTypedFormat(DXGI_FORMAT format, int srgb_variant = -1)
   {
      switch (format)
      {
      case DXGI_FORMAT_R8_TYPELESS:
         return DXGI_FORMAT_R8_UNORM;
      case DXGI_FORMAT_R8G8_TYPELESS:
         return DXGI_FORMAT_R8G8_UNORM;
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      case DXGI_FORMAT_R8G8B8A8_UNORM:
         return srgb_variant == 1 ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
         return srgb_variant != 0 ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
         return srgb_variant == 1 ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
         return srgb_variant != 0 ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
      case DXGI_FORMAT_B8G8R8X8_UNORM:
         return srgb_variant == 1 ? DXGI_FORMAT_B8G8R8X8_UNORM_SRGB : DXGI_FORMAT_B8G8R8X8_UNORM;
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
         return srgb_variant != 0 ? DXGI_FORMAT_B8G8R8X8_UNORM_SRGB : DXGI_FORMAT_B8G8R8X8_UNORM;
      case DXGI_FORMAT_R10G10B10A2_TYPELESS:
         return DXGI_FORMAT_R10G10B10A2_UNORM;
      case DXGI_FORMAT_D16_UNORM:
         return DXGI_FORMAT_R16_UNORM;
      case DXGI_FORMAT_R16_TYPELESS:
         return DXGI_FORMAT_R16_FLOAT;
      case DXGI_FORMAT_R16G16_TYPELESS:
         return DXGI_FORMAT_R16G16_FLOAT;
      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
         return DXGI_FORMAT_R16G16B16A16_FLOAT;
      case DXGI_FORMAT_D32_FLOAT:
      case DXGI_FORMAT_R32_TYPELESS:
         return DXGI_FORMAT_R32_FLOAT;
      case DXGI_FORMAT_R32G32_TYPELESS:
         return DXGI_FORMAT_R32G32_FLOAT;
      case DXGI_FORMAT_R32G32B32_TYPELESS:
         return DXGI_FORMAT_R32G32B32_FLOAT;
      case DXGI_FORMAT_R32G32B32A32_TYPELESS:
         return DXGI_FORMAT_R32G32B32A32_FLOAT;
      case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      case DXGI_FORMAT_R32G8X24_TYPELESS:
         return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
      case DXGI_FORMAT_D24_UNORM_S8_UINT:
      case DXGI_FORMAT_R24G8_TYPELESS:
         return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
      case DXGI_FORMAT_BC1_TYPELESS:
      case DXGI_FORMAT_BC1_UNORM:
         return srgb_variant == 1 ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
      case DXGI_FORMAT_BC1_UNORM_SRGB:
         return srgb_variant != 0 ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
      case DXGI_FORMAT_BC2_TYPELESS:
      case DXGI_FORMAT_BC2_UNORM:
         return srgb_variant == 1 ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM;
      case DXGI_FORMAT_BC2_UNORM_SRGB:
         return srgb_variant != 0 ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM;
      case DXGI_FORMAT_BC3_TYPELESS:
      case DXGI_FORMAT_BC3_UNORM:
         return srgb_variant == 1 ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
      case DXGI_FORMAT_BC3_UNORM_SRGB:
         return srgb_variant != 0 ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
      case DXGI_FORMAT_BC4_TYPELESS:
         return DXGI_FORMAT_BC4_UNORM;
      case DXGI_FORMAT_BC5_TYPELESS:
         return DXGI_FORMAT_BC5_UNORM;
      case DXGI_FORMAT_BC6H_TYPELESS:
         return DXGI_FORMAT_BC6H_UF16;
      case DXGI_FORMAT_BC7_TYPELESS:
      case DXGI_FORMAT_BC7_UNORM:
         return srgb_variant == 1 ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
      case DXGI_FORMAT_BC7_UNORM_SRGB:
         return srgb_variant != 0 ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
      }
      return format;
   }

   // TODO: add a strict flag to avoid copying between e.g. UNORM and FLOAT
   bool AreFormatsCopyCompatible(DXGI_FORMAT format_a, DXGI_FORMAT format_b)
   {
      auto Is32Bit4ChannelTypeless = [](DXGI_FORMAT format)
      {
         switch (format)
         {
         case DXGI_FORMAT_B8G8R8X8_TYPELESS:
         case DXGI_FORMAT_B8G8R8A8_TYPELESS:
         case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return true;
         }
         return false;
      };

      format_a = GetTypelessFormat(format_a);
      format_b = GetTypelessFormat(format_b);
      if (format_a == format_b || (Is32Bit4ChannelTypeless(format_a) && Is32Bit4ChannelTypeless(format_b)))
         return true;
      return false;
   }

   // If channels have a different bit size, it returns the overall one.
   bool GetFormatSizeInfo(DXGI_FORMAT format, uint8_t& channels, uint8_t& bits_per_channel)
   {
      channels = 0;
      bits_per_channel = 0;

      switch (format)
      {
      // 16-bit/channel, 4-channel
      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_UNORM:
      case DXGI_FORMAT_R16G16B16A16_SNORM:
      case DXGI_FORMAT_R16G16B16A16_UINT:
      case DXGI_FORMAT_R16G16B16A16_SINT:
         channels = 4; bits_per_channel = 16; break;
      // 8-bit/channel, 4-channel
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      case DXGI_FORMAT_R8G8B8A8_SNORM:
      case DXGI_FORMAT_R8G8B8A8_UINT:
      case DXGI_FORMAT_R8G8B8A8_SINT:
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
      case DXGI_FORMAT_B8G8R8X8_UNORM:
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
         channels = 4; bits_per_channel = 8; break;
      // 32-bit/channel, 4-channel
      case DXGI_FORMAT_R32G32B32A32_TYPELESS:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_UINT:
      case DXGI_FORMAT_R32G32B32A32_SINT:
         channels = 4; bits_per_channel = 32; break;
      // 32-bit/channel, 3-channel
      case DXGI_FORMAT_R32G32B32_TYPELESS:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32_UINT:
      case DXGI_FORMAT_R32G32B32_SINT:
         channels = 3; bits_per_channel = 32; break;
      // 32-bit/channel, 2-channel
      case DXGI_FORMAT_R32G32_TYPELESS:
      case DXGI_FORMAT_R32G32_FLOAT:
      case DXGI_FORMAT_R32G32_UINT:
      case DXGI_FORMAT_R32G32_SINT:
         channels = 2; bits_per_channel = 32; break;
      // 16-bit/channel, 2-channel
      case DXGI_FORMAT_R16G16_TYPELESS:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R16G16_UNORM:
      case DXGI_FORMAT_R16G16_SNORM:
      case DXGI_FORMAT_R16G16_UINT:
      case DXGI_FORMAT_R16G16_SINT:
         channels = 2; bits_per_channel = 16; break;
      // 8-bit/channel, 2-channel
      case DXGI_FORMAT_R8G8_TYPELESS:
      case DXGI_FORMAT_R8G8_UNORM:
      case DXGI_FORMAT_R8G8_SNORM:
      case DXGI_FORMAT_R8G8_UINT:
      case DXGI_FORMAT_R8G8_SINT:
         channels = 2; bits_per_channel = 8; break;
      // Single channel, 32-bit
      case DXGI_FORMAT_R32_TYPELESS:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R32_UINT:
      case DXGI_FORMAT_R32_SINT:
      case DXGI_FORMAT_D32_FLOAT:
         channels = 1; bits_per_channel = 32; break;
      // Single channel, 16-bit
      case DXGI_FORMAT_R16_TYPELESS:
      case DXGI_FORMAT_R16_FLOAT:
      case DXGI_FORMAT_R16_UNORM:
      case DXGI_FORMAT_R16_SNORM:
      case DXGI_FORMAT_R16_UINT:
      case DXGI_FORMAT_R16_SINT:
      case DXGI_FORMAT_D16_UNORM:
         channels = 1; bits_per_channel = 16; break;
      // Single channel, 8-bit
      case DXGI_FORMAT_R8_TYPELESS:
      case DXGI_FORMAT_R8_UNORM:
      case DXGI_FORMAT_R8_SNORM:
      case DXGI_FORMAT_R8_UINT:
      case DXGI_FORMAT_R8_SINT:
      case DXGI_FORMAT_A8_UNORM:
         channels = 1; bits_per_channel = 8; break;
      // 8-bit/channel, 4-channel - These are edge cases as the channels size isn't even
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
      case DXGI_FORMAT_R8G8_B8G8_UNORM:
      case DXGI_FORMAT_G8R8_G8B8_UNORM:
      case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_TYPELESS:
      case DXGI_FORMAT_R10G10B10A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_UINT:
      case DXGI_FORMAT_R11G11B10_FLOAT:
      // Single channel, 1-bit
      case DXGI_FORMAT_R1_UNORM:
         channels = 1; bits_per_channel = 1; break;

      default:
         ASSERT_ONCE_MSG(false, "Unsupported CloneTexture format"); // Please add support for more (formats with different sizes per channel might be hard)
         return false;
      }
      return true;
   }

   const char* GetFormatName(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_UNKNOWN:                              return "Unknown";

      case DXGI_FORMAT_A8_UNORM:                             return "A8 UNORM";
      case DXGI_FORMAT_D32_FLOAT:                            return "D32 FLOAT";
      case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:                return "R24 UNORM X8 TYPELESS";
      case DXGI_FORMAT_D24_UNORM_S8_UINT:                    return "D24 UNORM S8 UINT";
      case DXGI_FORMAT_R24G8_TYPELESS:                       return "R24G8 TYPELESS";
      case DXGI_FORMAT_X24_TYPELESS_G8_UINT:                 return "X24 TYPELESS G8 UINT";
      case DXGI_FORMAT_R32G8X24_TYPELESS:                    return "R32G8X24 TYPELESS";
      case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:                 return "D32 FLOAT S8X24 UINT";
      case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:             return "R32 FLOAT X8X24 TYPELESS";
      case DXGI_FORMAT_R1_UNORM:                             return "R1 UNORM";
      case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:              return "X32 TYPELESS G8X24 UINT";
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:                   return "R9G9B9E5 SHAREDEXP";
      case DXGI_FORMAT_R8G8_B8G8_UNORM:                      return "R8G8 B8G8 UNORM";
      case DXGI_FORMAT_G8R8_G8B8_UNORM:                      return "G8R8 G8B8 UNORM";
      case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:           return "R10G10B10 XR BIAS A2 UNORM";

      case DXGI_FORMAT_R8_TYPELESS:                          return "R8 TYPELESS";
      case DXGI_FORMAT_R8_UNORM:                             return "R8 UNORM";
      case DXGI_FORMAT_R8_UINT:                              return "R8 UINT";
      case DXGI_FORMAT_R8_SNORM:                             return "R8 SNORM";
      case DXGI_FORMAT_R8_SINT:                              return "R8 SINT";

      case DXGI_FORMAT_R16_TYPELESS:                         return "R16 TYPELESS";
      case DXGI_FORMAT_R16_UNORM:                            return "R16 UNORM";
      case DXGI_FORMAT_R16_UINT:                             return "R16 UINT";
      case DXGI_FORMAT_R16_SNORM:                            return "R16 SNORM";
      case DXGI_FORMAT_R16_SINT:                             return "R16 SINT";
      case DXGI_FORMAT_R16_FLOAT:                            return "R16 FLOAT";
      case DXGI_FORMAT_D16_UNORM:                            return "D16 UNORM";

      case DXGI_FORMAT_R8G8_TYPELESS:                        return "R8G8 TYPELESS";
      case DXGI_FORMAT_R8G8_UNORM:                           return "R8G8 UNORM";
      case DXGI_FORMAT_R8G8_UINT:                            return "R8G8 UINT";
      case DXGI_FORMAT_R8G8_SNORM:                           return "R8G8 SNORM";
      case DXGI_FORMAT_R8G8_SINT:                            return "R8G8 SINT";

      case DXGI_FORMAT_R16G16_TYPELESS:                      return "R16G16 TYPELESS";
      case DXGI_FORMAT_R16G16_FLOAT:                         return "R16G16 FLOAT";
      case DXGI_FORMAT_R16G16_UNORM:                         return "R16G16 UNORM";
      case DXGI_FORMAT_R16G16_UINT:                          return "R16G16 UINT";
      case DXGI_FORMAT_R16G16_SNORM:                         return "R16G16 SNORM";
      case DXGI_FORMAT_R16G16_SINT:                          return "R16G16 SINT";

      case DXGI_FORMAT_R16G16B16A16_TYPELESS:                return "R16G16B16A16 TYPELESS";
      case DXGI_FORMAT_R16G16B16A16_FLOAT:                   return "R16G16B16A16 FLOAT";
      case DXGI_FORMAT_R16G16B16A16_UNORM:                   return "R16G16B16A16 UNORM";
      case DXGI_FORMAT_R16G16B16A16_UINT:                    return "R16G16B16A16 UINT";
      case DXGI_FORMAT_R16G16B16A16_SNORM:                   return "R16G16B16A16 SNORM";
      case DXGI_FORMAT_R16G16B16A16_SINT:                    return "R16G16B16A16 SINT";

      case DXGI_FORMAT_R32_TYPELESS:                         return "R32 TYPELESS";
      case DXGI_FORMAT_R32_FLOAT:                            return "R32 FLOAT";
      case DXGI_FORMAT_R32_UINT:                             return "R32 UINT";
      case DXGI_FORMAT_R32_SINT:                             return "R32 SINT";

      case DXGI_FORMAT_R32G32_TYPELESS:                      return "R32G32 TYPELESS";
      case DXGI_FORMAT_R32G32_FLOAT:                         return "R32G32 FLOAT";
      case DXGI_FORMAT_R32G32_UINT:                          return "R32G32 UINT";
      case DXGI_FORMAT_R32G32_SINT:                          return "R32G32 SINT";

      case DXGI_FORMAT_R32G32B32_TYPELESS:                   return "R32G32B32 TYPELESS";
      case DXGI_FORMAT_R32G32B32_FLOAT:                      return "R32G32B32 FLOAT";
      case DXGI_FORMAT_R32G32B32_UINT:                       return "R32G32B32 UINT";
      case DXGI_FORMAT_R32G32B32_SINT:                       return "R32G32B32 SINT";

      case DXGI_FORMAT_R32G32B32A32_TYPELESS:                return "R32G32B32A32 TYPELESS";
      case DXGI_FORMAT_R32G32B32A32_FLOAT:                   return "R32G32B32A32 FLOAT";
      case DXGI_FORMAT_R32G32B32A32_UINT:                    return "R32G32B32A32 UINT";
      case DXGI_FORMAT_R32G32B32A32_SINT:                    return "R32G32B32A32 SINT";

      case DXGI_FORMAT_R10G10B10A2_TYPELESS:                 return "R10G10B10A2 TYPELESS";
      case DXGI_FORMAT_R10G10B10A2_UNORM:                    return "R10G10B10A2 UNORM";
      case DXGI_FORMAT_R10G10B10A2_UINT:                     return "R10G10B10A2 UINT";
      case DXGI_FORMAT_R11G11B10_FLOAT:                      return "R11G11B10 FLOAT";

      case DXGI_FORMAT_R8G8B8A8_TYPELESS:                    return "R8G8B8A8 TYPELESS";
      case DXGI_FORMAT_R8G8B8A8_UNORM:                       return "R8G8B8A8 UNORM";
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:                  return "R8G8B8A8 UNORM SRGB";
      case DXGI_FORMAT_R8G8B8A8_UINT:                        return "R8G8B8A8 UINT";
      case DXGI_FORMAT_R8G8B8A8_SNORM:                       return "R8G8B8A8 SNORM";
      case DXGI_FORMAT_R8G8B8A8_SINT:                        return "R8G8B8A8 SINT";

      case DXGI_FORMAT_B5G6R5_UNORM:                         return "B5G6R5 UNORM";
      case DXGI_FORMAT_B5G5R5A1_UNORM:                       return "B5G5R5A1 UNORM";

      case DXGI_FORMAT_B8G8R8A8_TYPELESS:                    return "B8G8R8A8 TYPELESS";
      case DXGI_FORMAT_B8G8R8A8_UNORM:                       return "B8G8R8A8 UNORM";
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:                  return "B8G8R8A8 UNORM SRGB";
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:                    return "B8G8R8X8 TYPELESS";
      case DXGI_FORMAT_B8G8R8X8_UNORM:                       return "B8G8R8X8 UNORM";
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:                  return "B8G8R8X8 UNORM SRGB";

      case DXGI_FORMAT_BC1_TYPELESS:                         return "BC1 TYPELESS";
      case DXGI_FORMAT_BC1_UNORM:                            return "BC1 UNORM";
      case DXGI_FORMAT_BC1_UNORM_SRGB:                       return "BC1 UNORM SRGB";
      case DXGI_FORMAT_BC2_TYPELESS:                         return "BC2 TYPELESS";
      case DXGI_FORMAT_BC2_UNORM:                            return "BC2 UNORM";
      case DXGI_FORMAT_BC2_UNORM_SRGB:                       return "BC2 UNORM SRGB";
      case DXGI_FORMAT_BC3_TYPELESS:                         return "BC3 TYPELESS";
      case DXGI_FORMAT_BC3_UNORM:                            return "BC3 UNORM";
      case DXGI_FORMAT_BC3_UNORM_SRGB:                       return "BC3 UNORM SRGB";
      case DXGI_FORMAT_BC4_TYPELESS:                         return "BC4 TYPELESS";
      case DXGI_FORMAT_BC4_UNORM:                            return "BC4 UNORM";
      case DXGI_FORMAT_BC4_SNORM:                            return "BC4 SNORM";
      case DXGI_FORMAT_BC5_TYPELESS:                         return "BC5 TYPELESS";
      case DXGI_FORMAT_BC5_UNORM:                            return "BC5 UNORM";
      case DXGI_FORMAT_BC5_SNORM:                            return "BC5 SNORM";
      case DXGI_FORMAT_BC6H_TYPELESS:                        return "BC6H TYPELESS";
      case DXGI_FORMAT_BC6H_UF16:                            return "BC6H UF16";
      case DXGI_FORMAT_BC6H_SF16:                            return "BC6H SF16";
      case DXGI_FORMAT_BC7_TYPELESS:                         return "BC7 TYPELESS";
      case DXGI_FORMAT_BC7_UNORM:                            return "BC7 UNORM";
      case DXGI_FORMAT_BC7_UNORM_SRGB:                       return "BC7 UNORM SRGB";

      case DXGI_FORMAT_AYUV:                                 return "AYUV";
      case DXGI_FORMAT_Y410:                                 return "Y410";
      case DXGI_FORMAT_Y416:                                 return "Y416";
      case DXGI_FORMAT_NV12:                                 return "NV12";
      case DXGI_FORMAT_P010:                                 return "P010";
      case DXGI_FORMAT_P016:                                 return "P016";
      case DXGI_FORMAT_420_OPAQUE:                           return "420 OPAQUE";
      case DXGI_FORMAT_YUY2:                                 return "YUY2";
      case DXGI_FORMAT_Y210:                                 return "Y210";
      case DXGI_FORMAT_Y216:                                 return "Y216";
      case DXGI_FORMAT_NV11:                                 return "NV11";
      case DXGI_FORMAT_AI44:                                 return "AI44";
      case DXGI_FORMAT_IA44:                                 return "IA44";
      case DXGI_FORMAT_P8:                                   return "P8";
      case DXGI_FORMAT_A8P8:                                 return "A8P8";
      case DXGI_FORMAT_P208:                                 return "P208";
      case DXGI_FORMAT_V208:                                 return "V208";
      case DXGI_FORMAT_V408:                                 return "V408";
      case DXGI_FORMAT_B4G4R4A4_UNORM:                       return "B4G4R4A4 UNORM";
      case DXGI_FORMAT_A4B4G4R4_UNORM:                       return "A4B4G4R4 UNORM";
      }
      return nullptr;
   }

   const char* GetFormatNameSafe(DXGI_FORMAT format)
   {
      const char* name = GetFormatName(format);
      return name ? name : "Unknown";
   }

   const char* GetBlendName(D3D11_BLEND blend, bool numerical = false)
   {
      switch (blend)
      {
      case D3D11_BLEND_ZERO:             return numerical ? "0" : "ZERO";
      case D3D11_BLEND_ONE:              return numerical ? "1" : "ONE";
      case D3D11_BLEND_SRC_COLOR:        return "SRC_COLOR";
      case D3D11_BLEND_INV_SRC_COLOR:    return "INV_SRC_COLOR";
      case D3D11_BLEND_SRC_ALPHA:        return "SRC_ALPHA";
      case D3D11_BLEND_INV_SRC_ALPHA:    return "INV_SRC_ALPHA";
      case D3D11_BLEND_DEST_ALPHA:       return "DEST_ALPHA";
      case D3D11_BLEND_INV_DEST_ALPHA:   return "INV_DEST_ALPHA";
      case D3D11_BLEND_DEST_COLOR:       return "DEST_COLOR";
      case D3D11_BLEND_INV_DEST_COLOR:   return "INV_DEST_COLOR";
      case D3D11_BLEND_SRC_ALPHA_SAT:    return "SRC_ALPHA_SAT";
      case D3D11_BLEND_BLEND_FACTOR:     return "BLEND_FACTOR";
      case D3D11_BLEND_INV_BLEND_FACTOR: return "INV_BLEND_FACTOR";
      case D3D11_BLEND_SRC1_COLOR:       return "SRC1_COLOR";
      case D3D11_BLEND_INV_SRC1_COLOR:   return "INV_SRC1_COLOR";
      case D3D11_BLEND_SRC1_ALPHA:       return "SRC1_ALPHA";
      case D3D11_BLEND_INV_SRC1_ALPHA:   return "INV_SRC1_ALPHA";
      default:                           return "UNKNOWN";
      }
   }

   const char* GetBlendOpName(D3D11_BLEND_OP blend_op)
   {
      switch (blend_op)
      {
      case D3D11_BLEND_OP_ADD:          return "ADD";
      case D3D11_BLEND_OP_SUBTRACT:     return "SUBTRACT";
      case D3D11_BLEND_OP_REV_SUBTRACT: return "REV_SUBTRACT";
      case D3D11_BLEND_OP_MIN:          return "MIN";
      case D3D11_BLEND_OP_MAX:          return "MAX";
      default:                          return "UNKNOWN";
      }
   }

   const char* GetBlendOpSigns(D3D11_BLEND_OP blend_op)
   {
      switch (blend_op)
      {
      case D3D11_BLEND_OP_ADD:          return "+";
      case D3D11_BLEND_OP_SUBTRACT:     return "-";
      case D3D11_BLEND_OP_REV_SUBTRACT: return "-";
      case D3D11_BLEND_OP_MIN:          return "min";
      case D3D11_BLEND_OP_MAX:          return "max";
      default:                          return "";
      }
   }

   const char* GetLogicOpName(D3D11_LOGIC_OP logicOp)
   {
      switch (logicOp)
      {
      case D3D11_LOGIC_OP_CLEAR:         return "CLEAR";
      case D3D11_LOGIC_OP_SET:           return "SET";
      case D3D11_LOGIC_OP_COPY:          return "COPY";
      case D3D11_LOGIC_OP_COPY_INVERTED: return "COPY_INVERTED";
      case D3D11_LOGIC_OP_NOOP:          return "NOOP";
      case D3D11_LOGIC_OP_INVERT:        return "INVERT";
      case D3D11_LOGIC_OP_AND:           return "AND";
      case D3D11_LOGIC_OP_NAND:          return "NAND";
      case D3D11_LOGIC_OP_OR:            return "OR";
      case D3D11_LOGIC_OP_NOR:           return "NOR";
      case D3D11_LOGIC_OP_XOR:           return "XOR";
      case D3D11_LOGIC_OP_EQUIV:         return "EQUIV";
      case D3D11_LOGIC_OP_AND_REVERSE:   return "AND_REVERSE";
      case D3D11_LOGIC_OP_AND_INVERTED:  return "AND_INVERTED";
      case D3D11_LOGIC_OP_OR_REVERSE:    return "OR_REVERSE";
      case D3D11_LOGIC_OP_OR_INVERTED:   return "OR_INVERTED";
      default:                           return "UNKNOWN";
      }
   }

   const char* GetFilterName(D3D11_FILTER filter)
   {
      switch (filter)
      {
      case D3D11_FILTER_MIN_MAG_MIP_POINT:                 return "MIN_MAG_MIP_POINT";
      case D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR:          return "MIN_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT:    return "MIN_POINT_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR:          return "MIN_POINT_MAG_MIP_LINEAR";
      case D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT:          return "MIN_LINEAR_MAG_MIP_POINT";
      case D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR:   return "MIN_LINEAR_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:          return "MIN_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MIN_MAG_MIP_LINEAR:                return "MIN_MAG_MIP_LINEAR";
      case D3D11_FILTER_ANISOTROPIC:                       return "ANISOTROPIC";
      case D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT:      return "CMP_MIN_MAG_MIP_POINT";
      case D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR: return "CMP_MIN_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT: return "CMP_MIN_POINT_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR: return "CMP_MIN_POINT_MAG_MIP_LINEAR";
      case D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT: return "CMP_MIN_LINEAR_MAG_MIP_POINT";
      case D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR: return "CMP_MIN_LINEAR_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT: return "CMP_MIN_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR:    return "CMP_MIN_MAG_MIP_LINEAR";
      case D3D11_FILTER_COMPARISON_ANISOTROPIC:           return "CMP_ANISOTROPIC";
      case D3D11_FILTER_MINIMUM_MIN_MAG_MIP_POINT:        return "MINIMUM_MIN_MAG_MIP_POINT";
      case D3D11_FILTER_MINIMUM_MIN_MAG_POINT_MIP_LINEAR: return "MINIMUM_MIN_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT: return "MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MINIMUM_MIN_POINT_MAG_MIP_LINEAR: return "MINIMUM_MIN_POINT_MAG_MIP_LINEAR";
      case D3D11_FILTER_MINIMUM_MIN_LINEAR_MAG_MIP_POINT: return "MINIMUM_MIN_LINEAR_MAG_MIP_POINT";
      case D3D11_FILTER_MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR: return "MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT: return "MINIMUM_MIN_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR:       return "MINIMUM_MIN_MAG_MIP_LINEAR";
      case D3D11_FILTER_MINIMUM_ANISOTROPIC:              return "MINIMUM_ANISOTROPIC";
      case D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT:        return "MAXIMUM_MIN_MAG_MIP_POINT";
      case D3D11_FILTER_MAXIMUM_MIN_MAG_POINT_MIP_LINEAR: return "MAXIMUM_MIN_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT: return "MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MAXIMUM_MIN_POINT_MAG_MIP_LINEAR: return "MAXIMUM_MIN_POINT_MAG_MIP_LINEAR";
      case D3D11_FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT: return "MAXIMUM_MIN_LINEAR_MAG_MIP_POINT";
      case D3D11_FILTER_MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR: return "MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT: return "MAXIMUM_MIN_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR:       return "MAXIMUM_MIN_MAG_MIP_LINEAR";
      case D3D11_FILTER_MAXIMUM_ANISOTROPIC:              return "MAXIMUM_ANISOTROPIC";
      default: return "UNKNOWN";
      }
   }

   const char* GetTextureAddressModeName(D3D11_TEXTURE_ADDRESS_MODE mode)
   {
      switch (mode)
      {
      case D3D11_TEXTURE_ADDRESS_WRAP:   return "WRAP";
      case D3D11_TEXTURE_ADDRESS_MIRROR: return "MIRROR";
      case D3D11_TEXTURE_ADDRESS_CLAMP:  return "CLAMP";
      case D3D11_TEXTURE_ADDRESS_BORDER: return "BORDER";
      case D3D11_TEXTURE_ADDRESS_MIRROR_ONCE: return "MIRROR_ONCE";
      default: return "UNKNOWN";
      }
   }
}