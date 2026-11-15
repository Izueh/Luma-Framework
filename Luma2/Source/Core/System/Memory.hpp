#pragma once

#include <cstddef>
#include <span>
#include <vector>

// Searching memory for byte patterns: game code (later, with mid-function hooks) and shader bytecode (a game's bytecode patch
// callback finding the instructions it rewrites). Any thread.
namespace Luma {
namespace System {
namespace Memory {
// One byte of a pattern, or a wildcard ("??") matching any byte
struct BytePattern
{
   std::byte value{};
   bool wildcard = false;

   constexpr BytePattern(std::byte value_) : value(value_) {}

   constexpr BytePattern(unsigned int value_) : value(std::byte(value_)) {}

   enum class WildcardType
   {
      Wildcard,
   };

   constexpr explicit BytePattern(WildcardType) : wildcard(true) {}
};

inline constexpr BytePattern ANY{BytePattern::WildcardType::Wildcard};

// Every place `pattern` starts in `memory`, or the first only (1.0's ScanMemoryForPattern)
std::vector<const std::byte*> ScanForPattern(std::span<const std::byte> memory, std::span<const BytePattern> pattern, bool first_only = false);
std::vector<const std::byte*> ScanForPattern(std::span<const std::byte> memory, std::span<const std::byte> pattern, bool first_only = false);
} // namespace Memory
} // namespace System
} // namespace Luma
