#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace Luma {
enum class Api : uint8_t
{
   D3D11,
   D3D12,
   Vulkan,
};

inline constexpr size_t API_COUNT = 3;

constexpr std::string_view GetName(Api api)
{
   switch (api)
   {
   case Api::D3D11: return "D3D11";
   case Api::D3D12: return "D3D12";
   case Api::Vulkan: return "Vulkan";
   }
   return "unknown";
}

struct ApiSet
{
   std::bitset<API_COUNT> bits;

   constexpr ApiSet() = default;

   constexpr ApiSet(std::initializer_list<Api> apis)
   {
      for (const Api api : apis)
         bits.set(size_t(api));
   }

   constexpr bool Has(Api api) const { return bits.test(size_t(api)); }

   constexpr bool Intersects(ApiSet other) const { return (bits & other.bits).any(); }
};

template <typename... Fs>
struct Overloaded : Fs...
{
   using Fs::operator()...;
};
} // namespace Luma
