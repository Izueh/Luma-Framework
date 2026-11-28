#pragma once

#include <variant>

#include "Common/Api.hpp"

struct ID3D11Device;
struct ID3D12CommandQueue;

namespace Luma {
namespace Display {
namespace Swapchain {
namespace DXGI {
// What a DXGI swapchain was created with: D3D12 presents through a queue
using Presenter = std::variant<ID3D11Device*, ID3D12CommandQueue*>;

constexpr Api GetApi(const Presenter& presenter)
{
   return std::visit(
      Overloaded{
         [](ID3D11Device*) { return Api::D3D11; },
         [](ID3D12CommandQueue*) { return Api::D3D12; },
      },
      presenter);
}
} // namespace DXGI
} // namespace Swapchain
} // namespace Display
} // namespace Luma
