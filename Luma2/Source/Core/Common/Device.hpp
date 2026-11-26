#pragma once

#include <variant>

struct ID3D11Device;
struct ID3D12Device;

namespace Luma {
// The device a call is about, for code not tied to a single graphics API (dispatched by std::visit overload). A game's proxy of a device maps to that device.
// Empty: no device.
using Device = std::variant<std::monostate, ID3D11Device*, ID3D12Device*>;
} // namespace Luma
