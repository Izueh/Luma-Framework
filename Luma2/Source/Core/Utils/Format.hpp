#pragma once

#include <cstdint>
#include <string_view>

namespace Luma {
namespace Utils {
namespace Format {
// A DXGI_FORMAT's name without its prefix ("R16G16B16A16_FLOAT"), "UNKNOWN" for 0 and for values it doesn't know
std::string_view GetName(uint32_t format);
} // namespace Format
} // namespace Utils
} // namespace Luma
