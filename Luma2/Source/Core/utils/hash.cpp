#include "utils/hash.hpp"

#include <array>

namespace {
consteval std::array<uint32_t, 256> MakeCRC32Table()
{
   std::array<uint32_t, 256> table{};
   for (uint32_t i = 0; i < 256; i++)
   {
      uint32_t crc = i;
      for (uint32_t bit = 0; bit < 8; bit++)
         crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
      table[i] = crc;
   }
   return table;
}

constexpr std::array<uint32_t, 256> CRC32_TABLE = MakeCRC32Table();
} // namespace

namespace Luma {
namespace Utils {
namespace Hash {
uint32_t CRC32(std::span<const uint8_t> data, uint32_t previous)
{
   uint32_t crc = ~previous;
   for (const uint8_t byte : data)
      crc = CRC32_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8);
   return ~crc;
}
} // namespace Hash
} // namespace Utils
} // namespace Luma
