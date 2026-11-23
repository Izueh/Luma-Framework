#pragma once

#include <cstdint>

#include <d3d11_4.h>

#include "Common/ShaderStage.hpp"
#include "Hooks/VtableSlots.h"

// What differs between the six shader stages of a D3D11 context: the shader interface and the per-stage methods, their names
// and vtable slots. One template per method kind then covers every stage.
namespace Luma {
namespace Hooks {
namespace D3D11 {
template <ShaderStage Stage>
struct StageTraits;

template <>
struct StageTraits<ShaderStage::Vertex>
{
   using Shader = ID3D11VertexShader;
   static constexpr auto GetShader = &ID3D11DeviceContext::VSGetShader;
   static constexpr auto SetShader = &ID3D11DeviceContext::VSSetShader;
   static constexpr auto GetShaderResources = &ID3D11DeviceContext::VSGetShaderResources;
   static constexpr auto SetShaderResources = &ID3D11DeviceContext::VSSetShaderResources;
   static constexpr auto GetConstantBuffers = &ID3D11DeviceContext::VSGetConstantBuffers;
   static constexpr auto SetConstantBuffers = &ID3D11DeviceContext::VSSetConstantBuffers;
   static constexpr auto GetConstantBuffers1 = &ID3D11DeviceContext1::VSGetConstantBuffers1;
   static constexpr auto SetConstantBuffers1 = &ID3D11DeviceContext1::VSSetConstantBuffers1;
   static constexpr auto GetSamplers = &ID3D11DeviceContext::VSGetSamplers;
   static constexpr auto SetSamplers = &ID3D11DeviceContext::VSSetSamplers;
   static constexpr const char* NAMES[] = {"VSSetShader", "VSSetShaderResources", "VSSetConstantBuffers", "VSSetConstantBuffers1", "VSSetSamplers"};

   static const uint32_t& SetShaderSlot() { return LumaSlot_ID3D11DeviceContext_VSSetShader; }

   static const uint32_t& SetShaderResourcesSlot() { return LumaSlot_ID3D11DeviceContext_VSSetShaderResources; }

   static const uint32_t& SetConstantBuffersSlot() { return LumaSlot_ID3D11DeviceContext_VSSetConstantBuffers; }

   static const uint32_t& SetConstantBuffers1Slot() { return LumaSlot_ID3D11DeviceContext1_VSSetConstantBuffers1; }

   static const uint32_t& SetSamplersSlot() { return LumaSlot_ID3D11DeviceContext_VSSetSamplers; }
};

template <>
struct StageTraits<ShaderStage::Hull>
{
   using Shader = ID3D11HullShader;
   static constexpr auto GetShader = &ID3D11DeviceContext::HSGetShader;
   static constexpr auto SetShader = &ID3D11DeviceContext::HSSetShader;
   static constexpr auto GetShaderResources = &ID3D11DeviceContext::HSGetShaderResources;
   static constexpr auto SetShaderResources = &ID3D11DeviceContext::HSSetShaderResources;
   static constexpr auto GetConstantBuffers = &ID3D11DeviceContext::HSGetConstantBuffers;
   static constexpr auto SetConstantBuffers = &ID3D11DeviceContext::HSSetConstantBuffers;
   static constexpr auto GetConstantBuffers1 = &ID3D11DeviceContext1::HSGetConstantBuffers1;
   static constexpr auto SetConstantBuffers1 = &ID3D11DeviceContext1::HSSetConstantBuffers1;
   static constexpr auto GetSamplers = &ID3D11DeviceContext::HSGetSamplers;
   static constexpr auto SetSamplers = &ID3D11DeviceContext::HSSetSamplers;
   static constexpr const char* NAMES[] = {"HSSetShader", "HSSetShaderResources", "HSSetConstantBuffers", "HSSetConstantBuffers1", "HSSetSamplers"};

   static const uint32_t& SetShaderSlot() { return LumaSlot_ID3D11DeviceContext_HSSetShader; }

   static const uint32_t& SetShaderResourcesSlot() { return LumaSlot_ID3D11DeviceContext_HSSetShaderResources; }

   static const uint32_t& SetConstantBuffersSlot() { return LumaSlot_ID3D11DeviceContext_HSSetConstantBuffers; }

   static const uint32_t& SetConstantBuffers1Slot() { return LumaSlot_ID3D11DeviceContext1_HSSetConstantBuffers1; }

   static const uint32_t& SetSamplersSlot() { return LumaSlot_ID3D11DeviceContext_HSSetSamplers; }
};

template <>
struct StageTraits<ShaderStage::Domain>
{
   using Shader = ID3D11DomainShader;
   static constexpr auto GetShader = &ID3D11DeviceContext::DSGetShader;
   static constexpr auto SetShader = &ID3D11DeviceContext::DSSetShader;
   static constexpr auto GetShaderResources = &ID3D11DeviceContext::DSGetShaderResources;
   static constexpr auto SetShaderResources = &ID3D11DeviceContext::DSSetShaderResources;
   static constexpr auto GetConstantBuffers = &ID3D11DeviceContext::DSGetConstantBuffers;
   static constexpr auto SetConstantBuffers = &ID3D11DeviceContext::DSSetConstantBuffers;
   static constexpr auto GetConstantBuffers1 = &ID3D11DeviceContext1::DSGetConstantBuffers1;
   static constexpr auto SetConstantBuffers1 = &ID3D11DeviceContext1::DSSetConstantBuffers1;
   static constexpr auto GetSamplers = &ID3D11DeviceContext::DSGetSamplers;
   static constexpr auto SetSamplers = &ID3D11DeviceContext::DSSetSamplers;
   static constexpr const char* NAMES[] = {"DSSetShader", "DSSetShaderResources", "DSSetConstantBuffers", "DSSetConstantBuffers1", "DSSetSamplers"};

   static const uint32_t& SetShaderSlot() { return LumaSlot_ID3D11DeviceContext_DSSetShader; }

   static const uint32_t& SetShaderResourcesSlot() { return LumaSlot_ID3D11DeviceContext_DSSetShaderResources; }

   static const uint32_t& SetConstantBuffersSlot() { return LumaSlot_ID3D11DeviceContext_DSSetConstantBuffers; }

   static const uint32_t& SetConstantBuffers1Slot() { return LumaSlot_ID3D11DeviceContext1_DSSetConstantBuffers1; }

   static const uint32_t& SetSamplersSlot() { return LumaSlot_ID3D11DeviceContext_DSSetSamplers; }
};

template <>
struct StageTraits<ShaderStage::Geometry>
{
   using Shader = ID3D11GeometryShader;
   static constexpr auto GetShader = &ID3D11DeviceContext::GSGetShader;
   static constexpr auto SetShader = &ID3D11DeviceContext::GSSetShader;
   static constexpr auto GetShaderResources = &ID3D11DeviceContext::GSGetShaderResources;
   static constexpr auto SetShaderResources = &ID3D11DeviceContext::GSSetShaderResources;
   static constexpr auto GetConstantBuffers = &ID3D11DeviceContext::GSGetConstantBuffers;
   static constexpr auto SetConstantBuffers = &ID3D11DeviceContext::GSSetConstantBuffers;
   static constexpr auto GetConstantBuffers1 = &ID3D11DeviceContext1::GSGetConstantBuffers1;
   static constexpr auto SetConstantBuffers1 = &ID3D11DeviceContext1::GSSetConstantBuffers1;
   static constexpr auto GetSamplers = &ID3D11DeviceContext::GSGetSamplers;
   static constexpr auto SetSamplers = &ID3D11DeviceContext::GSSetSamplers;
   static constexpr const char* NAMES[] = {"GSSetShader", "GSSetShaderResources", "GSSetConstantBuffers", "GSSetConstantBuffers1", "GSSetSamplers"};

   static const uint32_t& SetShaderSlot() { return LumaSlot_ID3D11DeviceContext_GSSetShader; }

   static const uint32_t& SetShaderResourcesSlot() { return LumaSlot_ID3D11DeviceContext_GSSetShaderResources; }

   static const uint32_t& SetConstantBuffersSlot() { return LumaSlot_ID3D11DeviceContext_GSSetConstantBuffers; }

   static const uint32_t& SetConstantBuffers1Slot() { return LumaSlot_ID3D11DeviceContext1_GSSetConstantBuffers1; }

   static const uint32_t& SetSamplersSlot() { return LumaSlot_ID3D11DeviceContext_GSSetSamplers; }
};

template <>
struct StageTraits<ShaderStage::Pixel>
{
   using Shader = ID3D11PixelShader;
   static constexpr auto GetShader = &ID3D11DeviceContext::PSGetShader;
   static constexpr auto SetShader = &ID3D11DeviceContext::PSSetShader;
   static constexpr auto GetShaderResources = &ID3D11DeviceContext::PSGetShaderResources;
   static constexpr auto SetShaderResources = &ID3D11DeviceContext::PSSetShaderResources;
   static constexpr auto GetConstantBuffers = &ID3D11DeviceContext::PSGetConstantBuffers;
   static constexpr auto SetConstantBuffers = &ID3D11DeviceContext::PSSetConstantBuffers;
   static constexpr auto GetConstantBuffers1 = &ID3D11DeviceContext1::PSGetConstantBuffers1;
   static constexpr auto SetConstantBuffers1 = &ID3D11DeviceContext1::PSSetConstantBuffers1;
   static constexpr auto GetSamplers = &ID3D11DeviceContext::PSGetSamplers;
   static constexpr auto SetSamplers = &ID3D11DeviceContext::PSSetSamplers;
   static constexpr const char* NAMES[] = {"PSSetShader", "PSSetShaderResources", "PSSetConstantBuffers", "PSSetConstantBuffers1", "PSSetSamplers"};

   static const uint32_t& SetShaderSlot() { return LumaSlot_ID3D11DeviceContext_PSSetShader; }

   static const uint32_t& SetShaderResourcesSlot() { return LumaSlot_ID3D11DeviceContext_PSSetShaderResources; }

   static const uint32_t& SetConstantBuffersSlot() { return LumaSlot_ID3D11DeviceContext_PSSetConstantBuffers; }

   static const uint32_t& SetConstantBuffers1Slot() { return LumaSlot_ID3D11DeviceContext1_PSSetConstantBuffers1; }

   static const uint32_t& SetSamplersSlot() { return LumaSlot_ID3D11DeviceContext_PSSetSamplers; }
};

template <>
struct StageTraits<ShaderStage::Compute>
{
   using Shader = ID3D11ComputeShader;
   static constexpr auto GetShader = &ID3D11DeviceContext::CSGetShader;
   static constexpr auto SetShader = &ID3D11DeviceContext::CSSetShader;
   static constexpr auto GetShaderResources = &ID3D11DeviceContext::CSGetShaderResources;
   static constexpr auto SetShaderResources = &ID3D11DeviceContext::CSSetShaderResources;
   static constexpr auto GetConstantBuffers = &ID3D11DeviceContext::CSGetConstantBuffers;
   static constexpr auto SetConstantBuffers = &ID3D11DeviceContext::CSSetConstantBuffers;
   static constexpr auto GetConstantBuffers1 = &ID3D11DeviceContext1::CSGetConstantBuffers1;
   static constexpr auto SetConstantBuffers1 = &ID3D11DeviceContext1::CSSetConstantBuffers1;
   static constexpr auto GetSamplers = &ID3D11DeviceContext::CSGetSamplers;
   static constexpr auto SetSamplers = &ID3D11DeviceContext::CSSetSamplers;
   static constexpr const char* NAMES[] = {"CSSetShader", "CSSetShaderResources", "CSSetConstantBuffers", "CSSetConstantBuffers1", "CSSetSamplers"};

   static const uint32_t& SetShaderSlot() { return LumaSlot_ID3D11DeviceContext_CSSetShader; }

   static const uint32_t& SetShaderResourcesSlot() { return LumaSlot_ID3D11DeviceContext_CSSetShaderResources; }

   static const uint32_t& SetConstantBuffersSlot() { return LumaSlot_ID3D11DeviceContext_CSSetConstantBuffers; }

   static const uint32_t& SetConstantBuffers1Slot() { return LumaSlot_ID3D11DeviceContext1_CSSetConstantBuffers1; }

   static const uint32_t& SetSamplersSlot() { return LumaSlot_ID3D11DeviceContext_CSSetSamplers; }
};

// Calls `function` with the stage as a template argument: the bridge from a runtime stage to the traits
template <typename Function>
decltype(auto) WithStage(ShaderStage stage, Function&& function)
{
   switch (stage)
   {
   case ShaderStage::Vertex: return function.template operator()<ShaderStage::Vertex>();
   case ShaderStage::Hull: return function.template operator()<ShaderStage::Hull>();
   case ShaderStage::Domain: return function.template operator()<ShaderStage::Domain>();
   case ShaderStage::Geometry: return function.template operator()<ShaderStage::Geometry>();
   case ShaderStage::Pixel: return function.template operator()<ShaderStage::Pixel>();
   default: return function.template operator()<ShaderStage::Compute>();
   }
}
} // namespace D3D11
} // namespace Hooks
} // namespace Luma
