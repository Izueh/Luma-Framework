#include "System/MemoryScan.hpp"

#include <cstring>

namespace Luma {
namespace System {
namespace Memory {
std::vector<const std::byte*> ScanForPattern(std::span<const std::byte> memory, std::span<const BytePattern> pattern, bool first_only)
{
   std::vector<const std::byte*> matches;
   if (pattern.empty() || pattern.size() > memory.size())
      return matches;
   for (size_t i = 0; i <= memory.size() - pattern.size(); i++)
   {
      bool found = true;
      for (size_t j = 0; j < pattern.size() && found; j++)
         found = pattern[j].wildcard || memory[i + j] == pattern[j].value;
      if (!found)
         continue;
      matches.push_back(memory.data() + i);
      if (first_only)
         break;
   }
   return matches;
}

std::vector<const std::byte*> ScanForPattern(std::span<const std::byte> memory, std::span<const std::byte> pattern, bool first_only)
{
   std::vector<const std::byte*> matches;
   if (pattern.empty() || pattern.size() > memory.size())
      return matches;
   for (size_t i = 0; i <= memory.size() - pattern.size(); i++)
   {
      if (std::memcmp(memory.data() + i, pattern.data(), pattern.size()) != 0)
         continue;
      matches.push_back(memory.data() + i);
      if (first_only)
         break;
   }
   return matches;
}
} // namespace Memory
} // namespace System
} // namespace Luma
