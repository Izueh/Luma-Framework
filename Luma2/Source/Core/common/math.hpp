#pragma once

#include <cstdint>

namespace Luma {
namespace Math {

struct int2
{
   int32_t x = 0;
   int32_t y = 0;

   friend constexpr bool operator==(const int2&, const int2&) = default;
};

struct uint2
{
   uint32_t x = 0;
   uint32_t y = 0;

   friend constexpr bool operator==(const uint2&, const uint2&) = default;
};

struct float2
{
   float x = 0.f;
   float y = 0.f;

   friend constexpr bool operator==(const float2&, const float2&) = default;
};

struct float3
{
   float x = 0.f;
   float y = 0.f;
   float z = 0.f;

   friend constexpr bool operator==(const float3&, const float3&) = default;
};

struct float4
{
   float x = 0.f;
   float y = 0.f;
   float z = 0.f;
   float w = 0.f;

   friend constexpr bool operator==(const float4&, const float4&) = default;
};
} // namespace Math
} // namespace Luma
