#include "Shaders/D3D11/D3D11Shaders.hpp"

#include "Hooks/Scope.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Shaders {
namespace D3D11 {
namespace {
template <typename Shader, typename Method>
ComPtr<ID3D11DeviceChild> Create(ID3D11Device* device, Method create, std::span<const uint8_t> bytecode)
{
   ComPtr<Shader> shader;
   if (FAILED((device->*create)(bytecode.data(), bytecode.size(), nullptr, &shader)))
      return nullptr;
   return shader;
}

// A bind may still be passing a replaced object to the API on another thread this many presents later, at most
constexpr uint64_t RETIRED_PRESENTS = 2;
} // namespace

ComPtr<ID3D11DeviceChild> CreateShader(ID3D11Device* device, ShaderStage stage, std::span<const uint8_t> bytecode)
{
   Hooks::Scope::Guard scope;
   switch (stage)
   {
   case ShaderStage::Vertex: return Create<ID3D11VertexShader>(device, &ID3D11Device::CreateVertexShader, bytecode);
   case ShaderStage::Hull: return Create<ID3D11HullShader>(device, &ID3D11Device::CreateHullShader, bytecode);
   case ShaderStage::Domain: return Create<ID3D11DomainShader>(device, &ID3D11Device::CreateDomainShader, bytecode);
   case ShaderStage::Geometry: return Create<ID3D11GeometryShader>(device, &ID3D11Device::CreateGeometryShader, bytecode);
   case ShaderStage::Pixel: return Create<ID3D11PixelShader>(device, &ID3D11Device::CreatePixelShader, bytecode);
   case ShaderStage::Compute: return Create<ID3D11ComputeShader>(device, &ID3D11Device::CreateComputeShader, bytecode);
   }
   return nullptr;
}

// COM interfaces are single inheritance: a shader is its ID3D11DeviceChild
void Bind(ID3D11DeviceContext* context, ShaderStage stage, ID3D11DeviceChild* shader)
{
   switch (stage)
   {
   case ShaderStage::Vertex: context->VSSetShader(static_cast<ID3D11VertexShader*>(shader), nullptr, 0); break;
   case ShaderStage::Hull: context->HSSetShader(static_cast<ID3D11HullShader*>(shader), nullptr, 0); break;
   case ShaderStage::Domain: context->DSSetShader(static_cast<ID3D11DomainShader*>(shader), nullptr, 0); break;
   case ShaderStage::Geometry: context->GSSetShader(static_cast<ID3D11GeometryShader*>(shader), nullptr, 0); break;
   case ShaderStage::Pixel: context->PSSetShader(static_cast<ID3D11PixelShader*>(shader), nullptr, 0); break;
   case ShaderStage::Compute: context->CSSetShader(static_cast<ID3D11ComputeShader*>(shader), nullptr, 0); break;
   }
}

bool Publish(ID3D11Device* device, ShaderStage stage, Published& published, std::shared_ptr<const CompiledShader> compiled, Retired& retired)
{
   const bool same = published.compiled == compiled || (published.compiled && compiled && published.compiled->bytecode == compiled->bytecode);
   if (same)
   {
      published.compiled = std::move(compiled);
      return true;
   }
   // Refused bytecode is kept too, so it isn't tried again before it changes
   ComPtr<ID3D11DeviceChild> object = compiled ? CreateShader(device, stage, compiled->bytecode) : nullptr;
   std::unique_ptr<Live> live = object ? std::make_unique<Live>(std::move(object), compiled) : nullptr;
   published.current.store(live.get(), std::memory_order_release);
   if (published.live)
      retired.lives.emplace_back(std::move(published.live), GetFrame());
   const bool created = live || !compiled;
   published.live = std::move(live);
   published.compiled = std::move(compiled);
   return created;
}

void ReleaseRetired(Retired& retired)
{
   const uint64_t frame = GetFrame();
   std::erase_if(retired.lives, [frame](const auto& live) { return frame >= live.second + RETIRED_PRESENTS; });
}

void Release(Published& published)
{
   published.current.store(nullptr, std::memory_order_release);
   published.live.reset();
   published.compiled.reset();
}
} // namespace D3D11
} // namespace Shaders
} // namespace Luma
