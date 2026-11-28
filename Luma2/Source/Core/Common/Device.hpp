#pragma once

#include <variant>

#include "Common/Api.hpp"

struct ID3D11Device;
struct ID3D12Device;
struct VkDevice_T;

namespace Luma {
// The device a call is about, for code not tied to a single graphics API (dispatched by std::visit overload). A game's proxy of a device maps to that device.
// Empty: no device.
using Device = std::variant<std::monostate, ID3D11Device*, ID3D12Device*, VkDevice_T*>;

// Raw pointer identity of a device without proxy unwrapping (null for empty).
inline const void* GetRawDeviceKey(Device device)
{
   return std::visit(
      Overloaded{
         [](std::monostate) -> const void* { return nullptr; },
         [](auto* ptr) -> const void* { return ptr; },
      },
      device);
}
} // namespace Luma
