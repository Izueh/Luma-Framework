#include "Game/Registry.hpp"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <wrl/client.h>

#include "Hooks/DeviceProxy.hpp"
#include "Hooks/Lifetime.hpp"
#include "Resources/Bindings.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/Samplers.hpp"
#include "Resources/Textures.hpp"
#include "Resources/Upgrade.hpp"
#include "Shaders/Patching.hpp"
#include "Shaders/Database.hpp"
#include "Shaders/Files.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Log.hpp"

namespace Luma {
namespace Game {
namespace {
struct Declarations
{
   Info info;
   D3D11::Events d3d11_events;
   D3D12::Events d3d12_events;
   D3D11::Callbacks d3d11_callbacks;
   Panels panels;
   GameDeviceDataType device_data;
   bool options_set = false;
   std::unordered_map<uint32_t, D3D11::DrawCallback> draw_callbacks;
   std::unordered_map<uint32_t, D3D12::DrawCallback> d3d12_draw_callbacks;
};

Declarations declarations;

struct DeviceData
{
   GameDeviceData* data = nullptr;
   void (*destroy)(GameDeviceData* data) = nullptr;

   DeviceData(GameDeviceData* data_, void (*destroy_)(GameDeviceData*)) : data(data_), destroy(destroy_) {}

   DeviceData(DeviceData&& other) noexcept : data(std::exchange(other.data, nullptr)), destroy(other.destroy) {}

   ~DeviceData()
   {
      if (data)
         destroy(data);
   }
};

template <typename Device>
GameDeviceData* GetDeviceData(Device* device)
{
   const DeviceData* state = Hooks::Lifetime::GetState<DeviceData>(device);
   return state ? state->data : nullptr;
}

template <typename Device>
void AttachGameDeviceData(Device* device)
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

void SetEvents(const D3D11::Events& events)
{
   declarations.d3d11_events = events;
}

void SetEvents(const D3D12::Events& events)
{
   declarations.d3d12_events = events;
}

void SetCallbacks(const D3D11::Callbacks& callbacks)
{
   declarations.d3d11_callbacks = callbacks;
}

void D3D11::OnShaderDraw(std::span<const uint32_t> hashes, DrawCallback callback)
{
   for (const uint32_t hash : hashes)
   {
      const auto [it, added] = declarations.draw_callbacks.insert_or_assign(hash, callback);
      if (!added)
         Utils::Log::Warning("Game: a second draw callback for 0x{:08X} replaces the first", hash);
   }
}

void D3D12::OnShaderDraw(std::span<const uint32_t> hashes, DrawCallback callback)
{
   for (const uint32_t hash : hashes)
   {
      const auto [it, added] = declarations.d3d12_draw_callbacks.insert_or_assign(hash, callback);
      if (!added)
         Utils::Log::Warning("Game: a second D3D12 draw callback for 0x{:08X} replaces the first", hash);
   }
}

void Shaders::SetDebugName(std::string_view name, std::span<const uint32_t> hashes)
{
   Luma::Shaders::Database::Name(name, hashes);
}

void Shaders::RedirectHashes(std::string_view file_name, std::span<const uint32_t> hashes)
{
   Luma::Shaders::Files::AddRedirect(file_name, hashes);
}

void Shaders::AddDefine(const Luma::Shaders::Defines::Definition& definition)
{
   Luma::Shaders::Defines::Add(definition);
}

void SetOptions(const Options& options)
{
   if (declarations.options_set)
   {
      ASSERT_ONCE_MSG(false, "Game::SetOptions is called once, in Init");
      Utils::Log::Error("Game: a second SetOptions is ignored, the first one's options stay");
      return;
   }
   declarations.options_set = true;
   Luma::Resources::LumaCBuffers::SetOptions(options.cbuffers);
   Luma::Resources::Bindings::SetOptions(options.bindings);
   Luma::Resources::Textures::SetOptions(options.textures);
   Luma::Shaders::Patching::SetOptions(options.patching);
   Luma::Resources::Samplers::SetOptions(options.samplers);
   Draw::SetOptions(options.draw);
   declarations.device_data = options.device_data;
}

void Resources::SetUpgrades(const Luma::Resources::Upgrade::Rules& rules)
{
   Luma::Resources::Upgrade::SetRules(rules);
}

void MarkLumaSettingsDirty()
{
   Luma::Resources::LumaCBuffers::MarkSettingsDirty();
}

void SetPanels(const Panels& panels)
{
   declarations.panels = panels;
}

namespace {
IUnknown* GetUnknown(Device device)
{
   if (ID3D11Device* const* d3d11 = std::get_if<ID3D11Device*>(&device))
      return *d3d11;
   if (ID3D12Device* const* d3d12 = std::get_if<ID3D12Device*>(&device))
      return *d3d12;
   return nullptr;
}
} // namespace

GameDeviceData* GetGameDeviceData(Device game_device)
{
   IUnknown* device = GetUnknown(game_device);
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

Luma::Frame::Resolutions Frame::Get(Device device)
{
   return Luma::Frame::Get(GetUnknown(device));
}

void Frame::SetRenderResolution(Device device, Math::uint2 size)
{
   Luma::Frame::SetRenderResolution(GetUnknown(device), size);
}

namespace Registry {
const Info& GetInfo()
{
   return declarations.info;
}

const D3D11::Events& GetD3D11Events()
{
   return declarations.d3d11_events;
}

const D3D12::Events& GetD3D12Events()
{
   return declarations.d3d12_events;
}

const D3D11::Callbacks& GetD3D11Callbacks()
{
   return declarations.d3d11_callbacks;
}

const Panels& GetPanels()
{
   return declarations.panels;
}

D3D11::DrawCallback FindDrawCallback(uint32_t hash)
{
   const auto it = declarations.draw_callbacks.find(hash);
   return it != declarations.draw_callbacks.end() ? it->second : nullptr;
}

bool HasDrawCallbacks()
{
   const D3D11::Events& events = declarations.d3d11_events;
   return !declarations.draw_callbacks.empty() || events.on_draw_or_dispatch || events.on_variant_draw;
}

D3D12::DrawCallback FindD3D12DrawCallback(uint32_t hash)
{
   const auto it = declarations.d3d12_draw_callbacks.find(hash);
   return it != declarations.d3d12_draw_callbacks.end() ? it->second : nullptr;
}

bool HasD3D12DrawCallbacks()
{
   const D3D12::Events& events = declarations.d3d12_events;
   return !declarations.d3d12_draw_callbacks.empty() || events.on_draw_or_dispatch || events.on_variant_draw;
}

Hooks::D3D12::Functions GetRequestedD3D12Functions()
{
   using enum Hooks::D3D12::Function;
   Hooks::D3D12::Functions functions;
   if (HasD3D12DrawCallbacks())
   {
      // The calls, and the binds that tell which pipeline they run
      for (const Hooks::D3D12::Function function : {Reset, ClearState, SetPipelineState, Draw, Dispatch, ExecuteIndirect, ExecuteBundle})
         functions.set(size_t(function));
   }
   return functions;
}

Hooks::D3D11::Functions GetCallbackFunctions()
{
   using enum Hooks::D3D11::Function;
   const D3D11::Callbacks& d3d11 = declarations.d3d11_callbacks;
   Hooks::D3D11::Functions functions;
   Request(functions, d3d11.pre_create_blend_state, {CreateBlendState});
   Request(functions, d3d11.post_ia_set_vertex_buffers, {IASetVertexBuffers});
   Request(functions, d3d11.post_ia_set_index_buffer, {IASetIndexBuffer});
   Request(functions, d3d11.pre_copy_structure_count, {CopyStructureCount});
   Request(functions, d3d11.post_create_deferred_context, {CreateDeferredContext});
   Request(functions, d3d11.on_deferred_context_destroyed, {CreateDeferredContext});
   return functions;
}

Hooks::D3D11::Functions GetRequestedFunctions()
{
   using enum Hooks::D3D11::Function;
   const D3D11::Events& d3d11 = declarations.d3d11_events;
   const D3D12::Events& d3d12 = declarations.d3d12_events;
   // Device and swapchain creation and destruction are always hooked
   Hooks::D3D11::Functions functions = GetCallbackFunctions();
   Request(functions, d3d11.on_create_swapchain, {ResizeBuffers});
   Request(functions, d3d12.on_create_swapchain, {ResizeBuffers});
   Request(functions, d3d11.on_init_swapchain, {ResizeBuffers});
   Request(functions, d3d12.on_init_swapchain, {ResizeBuffers});
   Request(functions, d3d11.on_present, {Present});
   Request(functions, d3d12.on_present, {Present});
   Request(functions, d3d11.on_create_shader,
      {CreateVertexShader, CreateHullShader, CreateDomainShader, CreateGeometryShader, CreateGeometryShaderWithStreamOutput, CreatePixelShader, CreateComputeShader});
   Request(functions, d3d11.on_create_buffer, {CreateBuffer});
   Request(functions, d3d11.on_init_buffer, {CreateBuffer});
   Request(functions, d3d11.on_create_texture_2d, {CreateTexture2D});
   Request(functions, d3d11.on_init_texture_2d, {CreateTexture2D});
   Request(functions, d3d11.on_resource_destroyed, {CreateBuffer, CreateTexture1D, CreateTexture2D, CreateTexture3D, OpenSharedResource});
   Request(functions, d3d11.on_init_shader_resource_view, {CreateShaderResourceView});
   Request(functions, d3d11.on_init_render_target_view, {CreateRenderTargetView});
   Request(functions, d3d11.on_init_unordered_access_view, {CreateUnorderedAccessView});
   Request(functions, d3d11.on_init_depth_stencil_view, {CreateDepthStencilView});
   Request(functions, d3d11.on_view_destroyed, {CreateShaderResourceView, CreateRenderTargetView, CreateUnorderedAccessView, CreateDepthStencilView});
   Request(functions, d3d11.on_create_sampler_state, {CreateSamplerState});
   Request(functions, d3d11.on_bind_render_targets, {OMSetRenderTargets});
   Request(functions, d3d11.on_bind_render_targets_and_unordered_access_views, {OMSetRenderTargetsAndUnorderedAccessViews});
   Request(functions, d3d11.on_map, {Map});
   Request(functions, d3d11.on_unmap, {Unmap});
   Request(functions, d3d11.on_unmapped, {Unmap});
   Request(functions, d3d11.on_update_subresource, {UpdateSubresource});
   Request(functions, d3d11.on_copy_resource, {CopyResource});
   Request(functions, d3d11.on_copy_subresource_region, {CopySubresourceRegion});
   Request(functions, d3d11.on_resolve_subresource, {ResolveSubresource});
   Request(functions, d3d11.on_clear_render_target_view, {ClearRenderTargetView});
   Request(functions, d3d11.on_clear_view, {ClearView});
   Request(functions, d3d11.on_bind_constant_buffers,
      {VSSetConstantBuffers, HSSetConstantBuffers, DSSetConstantBuffers, GSSetConstantBuffers, PSSetConstantBuffers, CSSetConstantBuffers});
   Request(functions, d3d11.on_bind_viewports, {RSSetViewports});
   Request(functions, d3d11.on_bind_scissor_rects, {RSSetScissorRects});
   Request(functions, d3d11.on_bind_blend_state, {OMSetBlendState});
   Request(functions, d3d11.on_bind_depth_stencil_state, {OMSetDepthStencilState});
   Request(functions, d3d11.on_clear_state, {ClearState});
   Request(functions, d3d11.on_finish_command_list, {FinishCommandList});
   Request(functions, d3d11.on_execute_command_list, {ExecuteCommandList});
   // The draw events, and the callbacks of OnShaderDraw
   Request(functions, HasDrawCallbacks(),
      {Draw, DrawInstanced, DrawIndexed, DrawIndexedInstanced, DrawAuto, DrawInstancedIndirect, DrawIndexedInstancedIndirect, Dispatch, DispatchIndirect});
   return functions;
}

void LogDeclarations()
{
   const Info& info = declarations.info;
   Utils::Log::Info("Game: {} {} set {} D3D11 and {} D3D12 events, {} D3D11 callbacks{}", info.name.empty() ? "(unnamed game)" : info.name,
      info.version, CountCallbacks(declarations.d3d11_events), CountCallbacks(declarations.d3d12_events), CountCallbacks(declarations.d3d11_callbacks),
      info.api ? std::format(", and uses {} only", GetName(*info.api)) : "");
}

void Reset()
{
   declarations = {};
   Luma::Resources::LumaCBuffers::SetOptions({});
   Luma::Resources::Bindings::SetOptions({});
   Luma::Resources::Textures::SetOptions({});
   Luma::Shaders::Patching::SetOptions({});
   Luma::Resources::Samplers::SetOptions({});
   Luma::Resources::Upgrade::SetRules({});
   Draw::SetOptions({});
}

void CreateDeviceData(IUnknown* device)
{
   if (!declarations.device_data.create)
      return;
   Microsoft::WRL::ComPtr<ID3D11Device> d3d11_device;
   if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3d11_device))))
      return AttachGameDeviceData(d3d11_device.Get());
   Microsoft::WRL::ComPtr<ID3D12Device> d3d12_device;
   if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3d12_device))))
      AttachGameDeviceData(d3d12_device.Get());
}
} // namespace Registry
} // namespace Game
} // namespace Luma
