#pragma once

#include <cstdint>

#include <d3d11shader.h>

constexpr D3D11_SHADER_VERSION_TYPE SHADER_VERSION_TYPE_UNKNOWN = D3D11_SHADER_VERSION_TYPE(0xFFFF);

enum class ShaderStages : uint32_t
{
   None = 0,
   Pixel = 1u << D3D11_SHVER_PIXEL_SHADER,
   Vertex = 1u << D3D11_SHVER_VERTEX_SHADER,
   Geometry = 1u << D3D11_SHVER_GEOMETRY_SHADER,
   Hull = 1u << D3D11_SHVER_HULL_SHADER,
   Domain = 1u << D3D11_SHVER_DOMAIN_SHADER,
   Compute = 1u << D3D11_SHVER_COMPUTE_SHADER,
   AllGraphics = Pixel | Vertex | Geometry | Hull | Domain,
   All = AllGraphics | Compute,
};

constexpr ShaderStages operator~(ShaderStages a) { return static_cast<ShaderStages>(~static_cast<uint32_t>(a)); }
constexpr ShaderStages operator&(ShaderStages a, ShaderStages b) { return static_cast<ShaderStages>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); }
constexpr ShaderStages operator|(ShaderStages a, ShaderStages b) { return static_cast<ShaderStages>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
constexpr ShaderStages operator^(ShaderStages a, ShaderStages b) { return static_cast<ShaderStages>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b)); }
inline ShaderStages& operator&=(ShaderStages& a, ShaderStages b) { return a = a & b; }
inline ShaderStages& operator|=(ShaderStages& a, ShaderStages b) { return a = a | b; }
inline ShaderStages& operator^=(ShaderStages& a, ShaderStages b) { return a = a ^ b; }
constexpr bool operator==(ShaderStages lhs, uint32_t rhs) { return static_cast<uint32_t>(lhs) == rhs; }
constexpr bool operator!=(ShaderStages lhs, uint32_t rhs) { return static_cast<uint32_t>(lhs) != rhs; }

constexpr ShaderStages GetShaderStages(D3D11_SHADER_VERSION_TYPE type)
{
   return type == SHADER_VERSION_TYPE_UNKNOWN ? ShaderStages::None : static_cast<ShaderStages>(1u << type);
}
