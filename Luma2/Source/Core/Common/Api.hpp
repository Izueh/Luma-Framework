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

using Presenter = std::variant<ID3D11Device*, ID3D12CommandQueue*>;

template <typename... Fs>
struct Overloaded : Fs...
{
   using Fs::operator()...;
};

constexpr Api GetApi(const Presenter& presenter)
{
   return std::visit(
      Overloaded{
         [](ID3D11Device*) { return Api::D3D11; },
         [](ID3D12CommandQueue*) { return Api::D3D12; },
      },
      presenter);
}
} // namespace Luma
