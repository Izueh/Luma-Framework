#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

struct ID3D11Device;
struct ID3D12CommandQueue;

namespace Luma {
enum class Api : uint8_t
{
   D3D11,
   D3D12,
};

constexpr std::string_view GetName(Api api)
{
   return api == Api::D3D11 ? "D3D11" : "D3D12";
}

// What a swapchain was created with. Weak: the swapchain holds it.
using Presenter = std::variant<ID3D11Device*, ID3D12CommandQueue*>;
} // namespace Luma
