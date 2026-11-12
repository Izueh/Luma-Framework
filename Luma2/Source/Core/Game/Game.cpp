#include "Game/Game.hpp"

#include <format>
#include <initializer_list>
#include <utility>

#include <wrl/client.h>

#include "Hooks/DeviceProxy.hpp"
#include "Hooks/Lifetime.hpp"
#include "Utils/Log.hpp"

namespace Luma {
namespace Game {
namespace {
struct Declarations
{
   Info info;
   D3D11::Callbacks d3d11;
   D3D12::Callbacks d3d12;
   Panels panels;
   DeviceDataType device_data;
};

Declarations declarations;

struct DeviceData
{
   void* data = nullptr;
   void (*destroy)(void* data) = nullptr;

   DeviceData(void* data_, void (*destroy_)(void*)) : data(data_), destroy(destroy_) {}

   DeviceData(DeviceData&& other) noexcept : data(std::exchange(other.data, nullptr)), destroy(other.destroy) {}

   ~DeviceData()
   {
      if (data)
         destroy(data);
   }
};

template <typename Device>
void* GetDeviceData(Device* device)
{
   const DeviceData* state = Hooks::Lifetime::GetState<DeviceData>(device);
   return state ? state->data : nullptr;
}

template <typename Device>
void SetDeviceData(Device* device)
{
   Hooks::Lifetime::SetState<DeviceData>(device, declarations.device_data.create(), declarations.device_data.destroy);
}

template <typename Callback>
void Request(Hooks::D3D11::Functions& functions, Callback callback, std::initializer_list<Hooks::D3D11::Function> required)
{
   if (!callback)
      return;
   for (const Hooks::D3D11::Function function : required)
      functions.set(size_t(function));
}

template <typename Callbacks>
size_t CountCallbacks(const Callbacks& callbacks)
{
   // Every field is a function pointer
   const auto* fields = reinterpret_cast<void* const*>(&callbacks);
   size_t count = 0;
   for (size_t i = 0; i < sizeof(Callbacks) / sizeof(void*); i++)
      count += fields[i] != nullptr;
   return count;
}
} // namespace

void SetInfo(const Info& info)
{
   declarations.info = info;
}

void SetCallbacks(const D3D11::Callbacks& callbacks)
{
   declarations.d3d11 = callbacks;
}

void SetCallbacks(const D3D12::Callbacks& callbacks)
{
   declarations.d3d12 = callbacks;
}

void OnDraw(std::span<const uint32_t> hashes, D3D11DrawCallback callback)
{
   (void)hashes;
   (void)callback;
   LUMA_UNIMPLEMENTED("Game::OnDraw (Step 1)");
}

void SetPanels(const Panels& panels)
{
   declarations.panels = panels;
}

void SetDeviceDataType(const DeviceDataType& type)
{
   declarations.device_data = type;
}

void* GetDeviceDataPointer(IUnknown* device)
{
   if (!device)
      return nullptr;
   device = Hooks::DeviceProxy::Unwrap(device);
   Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device;
   if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3d11_device))))
      return GetDeviceData(d3d11_device.Get());
   Microsoft::WRL::ComPtr<ID3D12Device> d3d12_device;
   if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3d12_device))))
      return GetDeviceData(d3d12_device.Get());
   return nullptr;
}

const Info& GetInfo()
{
   return declarations.info;
}

const D3D11::Callbacks& GetD3D11()
{
   return declarations.d3d11;
}

const D3D12::Callbacks& GetD3D12()
{
   return declarations.d3d12;
}

const Panels& GetPanels()
{
   return declarations.panels;
}

Hooks::D3D11::Functions GetRequestedFunctions()
{
   using enum Hooks::D3D11::Function;
   const D3D11::Callbacks& d3d11 = declarations.d3d11;
   const D3D12::Callbacks& d3d12 = declarations.d3d12;
   Hooks::D3D11::Functions functions;
   Request(functions, d3d11.pre_resize_buffers, {ResizeBuffers});
   Request(functions, d3d11.post_resize_buffers, {ResizeBuffers});
   Request(functions, d3d12.post_resize_buffers, {ResizeBuffers});
   Request(functions, d3d11.pre_present, {Present});
   Request(functions, d3d11.post_present, {Present});
   Request(functions, d3d12.pre_present, {Present});
   Request(functions, d3d12.post_present, {Present});
   Request(functions, d3d11.pre_create_buffer, {CreateBuffer});
   Request(functions, d3d11.post_create_buffer, {CreateBuffer});
   Request(functions, d3d11.pre_create_texture_2d, {CreateTexture2D});
   Request(functions, d3d11.post_create_texture_2d, {CreateTexture2D});
   Request(functions, d3d11.on_resource_destroyed, {CreateBuffer, CreateTexture1D, CreateTexture2D, CreateTexture3D, OpenSharedResource});
   Request(functions, d3d11.post_create_shader_resource_view, {CreateShaderResourceView});
   Request(functions, d3d11.post_create_render_target_view, {CreateRenderTargetView});
   Request(functions, d3d11.post_create_unordered_access_view, {CreateUnorderedAccessView});
   Request(functions, d3d11.post_create_depth_stencil_view, {CreateDepthStencilView});
   Request(functions, d3d11.on_view_destroyed, {CreateShaderResourceView, CreateRenderTargetView, CreateUnorderedAccessView, CreateDepthStencilView});
   Request(functions, d3d11.pre_create_sampler_state, {CreateSamplerState});
   Request(functions, d3d11.pre_create_blend_state, {CreateBlendState});
   const std::initializer_list<Hooks::D3D11::Function> CREATE_SHADER = {CreateVertexShader, CreateHullShader, CreateDomainShader, CreateGeometryShader,
      CreateGeometryShaderWithStreamOutput, CreatePixelShader, CreateComputeShader};
   Request(functions, d3d11.pre_create_shader, CREATE_SHADER);
   Request(functions, d3d11.post_create_shader, CREATE_SHADER);
   Request(functions, d3d11.pre_set_constant_buffers,
      {VSSetConstantBuffers, HSSetConstantBuffers, DSSetConstantBuffers, GSSetConstantBuffers, PSSetConstantBuffers, CSSetConstantBuffers});
   Request(functions, d3d11.pre_om_set_render_targets, {OMSetRenderTargets});
   Request(functions, d3d11.pre_om_set_render_targets_and_unordered_access_views, {OMSetRenderTargetsAndUnorderedAccessViews});
   Request(functions, d3d11.post_rs_set_viewports, {RSSetViewports});
   Request(functions, d3d11.post_rs_set_scissor_rects, {RSSetScissorRects});
   Request(functions, d3d11.post_om_set_blend_state, {OMSetBlendState});
   Request(functions, d3d11.post_om_set_depth_stencil_state, {OMSetDepthStencilState});
   Request(functions, d3d11.post_ia_set_vertex_buffers, {IASetVertexBuffers});
   Request(functions, d3d11.post_ia_set_index_buffer, {IASetIndexBuffer});
   Request(functions, d3d11.post_clear_state, {ClearState});
   Request(functions, d3d11.post_map, {Map});
   Request(functions, d3d11.pre_unmap, {Unmap});
   Request(functions, d3d11.post_unmap, {Unmap});
   Request(functions, d3d11.pre_update_subresource, {UpdateSubresource});
   Request(functions, d3d11.pre_copy_resource, {CopyResource});
   Request(functions, d3d11.pre_copy_subresource_region, {CopySubresourceRegion});
   Request(functions, d3d11.pre_resolve_subresource, {ResolveSubresource});
   Request(functions, d3d11.pre_clear_render_target_view, {ClearRenderTargetView});
   Request(functions, d3d11.pre_copy_structure_count, {CopyStructureCount});
   Request(functions, d3d11.pre_clear_view, {ClearView});
   Request(functions, d3d11.post_create_deferred_context, {CreateDeferredContext});
   Request(functions, d3d11.on_deferred_context_destroyed, {CreateDeferredContext});
   Request(functions, d3d11.post_finish_command_list, {FinishCommandList});
   Request(functions, d3d11.pre_execute_command_list, {ExecuteCommandList});
   return functions;
}

void LogDeclarations()
{
   const Info& info = declarations.info;
   Utils::Log::Info("Game: {} {} set {} D3D11 and {} D3D12 callbacks{}", info.name.empty() ? "(unnamed game)" : info.name, info.version,
      CountCallbacks(declarations.d3d11), CountCallbacks(declarations.d3d12), info.api ? std::format(", and uses {} only", GetName(*info.api)) : "");
}

void Reset()
{
   declarations = {};
}

void CreateDeviceData(IUnknown* device)
{
   if (!declarations.device_data.create)
      return;
   Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device;
   if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3d11_device))))
      return SetDeviceData(d3d11_device.Get());
   Microsoft::WRL::ComPtr<ID3D12Device> d3d12_device;
   if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3d12_device))))
      SetDeviceData(d3d12_device.Get());
}
} // namespace Game
} // namespace Luma
