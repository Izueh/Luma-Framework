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

// One callback with a stage argument stands for the per-stage copies of a native function (VSSetConstantBuffers...)
enum class ShaderStage : uint8_t
{
   Vertex,
   Hull,
   Domain,
   Geometry,
   Pixel,
   Compute,
};

// LumaData's per pass values (1.0's CustomData1 to 4)
struct LumaCustomData
{
   uint32_t custom_data_1 = 0;
   uint32_t custom_data_2 = 0;
   float custom_data_3 = 0.f;
   float custom_data_4 = 0.f;

   friend bool operator==(const LumaCustomData&, const LumaCustomData&) = default;
};
} // namespace Luma
