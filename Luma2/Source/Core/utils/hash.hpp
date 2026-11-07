#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Luma {
namespace Utils {
namespace Hash {
// Pass the previous result to hash data in parts
uint32_t CRC32(std::span<const uint8_t> data, uint32_t previous = 0);

constexpr uint64_t Name(std::string_view name)
{
   uint64_t hash = 0xCBF29CE484222325ull;
   for (const char c : name)
   {
      hash ^= static_cast<uint8_t>(c);
      hash *= 0x100000001B3ull;
   }
   return hash;
}
} // namespace Hash
} // namespace Utils

// In "Luma" so every Luma namespace finds it unqualified
consteval uint64_t operator""_h(const char* name, std::size_t size)
{
   return Utils::Hash::Name(std::string_view(name, size));
}
} // namespace Luma
