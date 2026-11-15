#pragma once

#include <variant>

struct ID3D11Device;
struct ID3D12Device;

namespace Luma {
// The main device, for code not tied to an API (Guidelines.md: dispatch by overload). A game's proxy of a device is that device.
// Empty: no device.
using Device = std::variant<std::monostate, ID3D11Device*, ID3D12Device*>;
} // namespace Luma
