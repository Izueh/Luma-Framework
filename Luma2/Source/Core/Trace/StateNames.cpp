#include "Trace/StateNames.hpp"

#if DEVELOPMENT
#include <array>
#include <string_view>

namespace Luma {
namespace Trace {
namespace StateNames {
namespace {
template <size_t N>
std::string LookupName(const std::array<std::string_view, N>& names, uint32_t value)
{
   return value < N && !names[value].empty() ? std::string(names[value]) : std::to_string(value);
}

constexpr std::array<std::string_view, 20> BLEND = {"", "ZERO", "ONE", "SRC_COLOR", "INV_SRC_COLOR", "SRC_ALPHA", "INV_SRC_ALPHA", "DEST_ALPHA",
   "INV_DEST_ALPHA", "DEST_COLOR", "INV_DEST_COLOR", "SRC_ALPHA_SAT", "", "", "BLEND_FACTOR", "INV_BLEND_FACTOR", "SRC1_COLOR", "INV_SRC1_COLOR", "SRC1_ALPHA",
   "INV_SRC1_ALPHA"};
constexpr std::array<std::string_view, 6> BLEND_OP = {"", "ADD", "SUBTRACT", "REV_SUBTRACT", "MIN", "MAX"};
constexpr std::array<std::string_view, 9> COMPARISON = {"", "NEVER", "LESS", "EQUAL", "LESS_EQUAL", "GREATER", "NOT_EQUAL", "GREATER_EQUAL", "ALWAYS"};
constexpr std::array<std::string_view, 9> STENCIL_OP = {"", "KEEP", "ZERO", "REPLACE", "INCR_SAT", "DECR_SAT", "INVERT", "INCR", "DECR"};
constexpr std::array<std::string_view, 4> CULL = {"", "NONE", "FRONT", "BACK"};
constexpr std::array<std::string_view, 4> FILL = {"", "", "WIREFRAME", "SOLID"};
constexpr std::array<std::string_view, 6> ADDRESS = {"", "WRAP", "MIRROR", "CLAMP", "BORDER", "MIRROR_ONCE"};
} // namespace

std::string GetBlendName(uint32_t value)
{
   return LookupName(BLEND, value);
}

std::string GetBlendOpName(uint32_t value)
{
   return LookupName(BLEND_OP, value);
}

std::string GetComparisonName(uint32_t value)
{
   return LookupName(COMPARISON, value);
}

std::string GetStencilOpName(uint32_t value)
{
   return LookupName(STENCIL_OP, value);
}

std::string GetCullName(uint32_t value)
{
   return LookupName(CULL, value);
}

std::string GetFillName(uint32_t value)
{
   return LookupName(FILL, value);
}

std::string GetAddressName(uint32_t value)
{
   return LookupName(ADDRESS, value);
}

std::string GetFilterName(uint32_t value)
{
   constexpr std::array<std::string_view, 4> REDUCTIONS = {"", "COMPARISON_", "MINIMUM_", "MAXIMUM_"};
   const std::string_view reduction = REDUCTIONS[(value >> 7) & 3];
   if (value & 0x40)
      return std::format("{}ANISOTROPIC", reduction);
   const auto mode = [](uint32_t bit) { return bit ? "LINEAR" : "POINT"; };
   return std::format("{}MIN_{}_MAG_{}_MIP_{}", reduction, mode((value >> 4) & 1), mode((value >> 2) & 1), mode(value & 1));
}
} // namespace StateNames
} // namespace Trace
} // namespace Luma
#endif
