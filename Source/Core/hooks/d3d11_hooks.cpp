#include "hooks/hooks.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

#include <d3d11_4.h>
#include <wrl/client.h>

#include "hooks/callbacks.hpp"
#include "hooks/device_proxy.hpp"
#include "hooks/scope.hpp"
#include "hooks/slots.h"
#include "utils/depth_guard.hpp"
#include "utils/log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Hooks {
namespace {
PFN_D3D11_CREATE_DEVICE original_create_device = nullptr;
PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN original_create_device_and_swapchain = nullptr;
std::atomic<bool> vtables_resolved = false;

// Children return the real device: GetDevice hands the game its proxy instead. Contexts and other children have different
// functions, and one may jump into another (with the debug layer), so each has a detour and only the outermost call swaps.
using GetDeviceFunction = void(STDMETHODCALLTYPE*)(ID3D11DeviceChild* child, ID3D11Device** device);
struct GetDeviceTarget
{
   Vtables::Interface vtable;
   const char* name;
};
constexpr std::array GET_DEVICE_TARGETS = {
   GetDeviceTarget{Vtables::Interface::D3D11DeviceChild, "ID3D11DeviceChild::GetDevice"},
   GetDeviceTarget{Vtables::Interface::D3D11ImmediateContext, "ID3D11DeviceContext::GetDevice (immediate)"},
   GetDeviceTarget{Vtables::Interface::D3D11DeferredContext, "ID3D11DeviceContext::GetDevice (deferred)"},
};
std::array<GetDeviceFunction, GET_DEVICE_TARGETS.size()> original_get_device = {};
thread_local uint32_t get_device_depth = 0;

// Luma may raise the feature level (11.1 for FSR 3): the game keeps seeing the highest one it asked for, as other levels change its rendering paths
// {6B1F4E52-3C2D-4B8E-9A47-1E5C8D2F7A61}
constexpr GUID GAME_FEATURE_LEVEL_GUID = {0x6b1f4e52, 0x3c2d, 0x4b8e, {0x9a, 0x47, 0x1e, 0x5c, 0x8d, 0x2f, 0x7a, 0x61}};
using GetFeatureLevelFunction = D3D_FEATURE_LEVEL(STDMETHODCALLTYPE*)(ID3D11Device* device);
GetFeatureLevelFunction original_get_feature_level = nullptr;

// The level the game gets when it passes no list
D3D_FEATURE_LEVEL GetGameFeatureLevel(const D3D_FEATURE_LEVEL* feature_levels, UINT feature_level_count)
{
   return (feature_levels && feature_level_count > 0) ? feature_levels[0] : D3D_FEATURE_LEVEL_11_0;
}

void HideRaisedFeatureLevel(HRESULT result, D3D_FEATURE_LEVEL game_feature_level, ID3D11Device** device, D3D_FEATURE_LEVEL* feature_level)
{
   if (FAILED(result))
      return;
   if (feature_level && *feature_level > game_feature_level)
      *feature_level = game_feature_level;
   if (device && *device)
   {
      Scope::Guard scope;
      if ((*device)->GetFeatureLevel() > game_feature_level)
         (*device)->SetPrivateData(GAME_FEATURE_LEVEL_GUID, sizeof(game_feature_level), &game_feature_level);
   }
}

D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel(ID3D11Device* device)
{
   const D3D_FEATURE_LEVEL feature_level = original_get_feature_level(device);
   if (Scope::IsActive())
      return feature_level;
   D3D_FEATURE_LEVEL game_feature_level;
   UINT size = sizeof(game_feature_level);
   if (SUCCEEDED(device->GetPrivateData(GAME_FEATURE_LEVEL_GUID, &size, &game_feature_level)) && size == sizeof(game_feature_level))
      return game_feature_level;
   return feature_level;
}

const std::array DEVICE_VERSIONS = {
   LUMA_INTERFACE_VERSION(ID3D11Device),
   LUMA_INTERFACE_VERSION(ID3D11Device1),
   LUMA_INTERFACE_VERSION(ID3D11Device2),
   LUMA_INTERFACE_VERSION(ID3D11Device3),
   LUMA_INTERFACE_VERSION(ID3D11Device4),
   LUMA_INTERFACE_VERSION(ID3D11Device5),
};
const std::array CONTEXT_VERSIONS = {
   LUMA_INTERFACE_VERSION(ID3D11DeviceContext),
   LUMA_INTERFACE_VERSION(ID3D11DeviceContext1),
   LUMA_INTERFACE_VERSION(ID3D11DeviceContext2),
   LUMA_INTERFACE_VERSION(ID3D11DeviceContext3),
   LUMA_INTERFACE_VERSION(ID3D11DeviceContext4),
};
const std::array CHILD_VERSIONS = {
   LUMA_INTERFACE_VERSION(ID3D11DeviceChild),
};

// Fails on single threaded devices: retried on the next device
bool ResolveDeferredContextVtable(ID3D11Device* device)
{
   ComPtr<ID3D11DeviceContext> deferred_context;
   if (const HRESULT result = device->CreateDeferredContext(0, &deferred_context); FAILED(result))
   {
      Utils::Log::Info("D3D11: no deferred context (0x{:08X}), deferred context hooks wait for another device", uint32_t(result));
      return false;
   }
   Vtables::Set(Vtables::Interface::D3D11DeferredContext, deferred_context.Get(), CONTEXT_VERSIONS);
   return true;
}

void ResolveVtables(ID3D11Device* device)
{
   Scope::Guard scope;
   Vtables::Set(Vtables::Interface::D3D11Device, device, DEVICE_VERSIONS);

   ComPtr<ID3D11DeviceContext> immediate_context;
   device->GetImmediateContext(&immediate_context);
   Vtables::Set(Vtables::Interface::D3D11ImmediateContext, immediate_context.Get(), CONTEXT_VERSIONS);

   ResolveDeferredContextVtable(device);

   // Resources, views, states, shaders and queries share their GetDevice
   const D3D11_QUERY_DESC query_desc = {.Query = D3D11_QUERY_EVENT};
   ComPtr<ID3D11Query> query;
   if (const HRESULT result = device->CreateQuery(&query_desc, &query); SUCCEEDED(result))
      Vtables::Set(Vtables::Interface::D3D11DeviceChild, query.Get(), CHILD_VERSIONS);
   else
      Utils::Log::Error("D3D11: failed to create a temporary query to read its vtable (0x{:08X})", uint32_t(result));

   InstallPending();
   LogSummary();
}

void OnDeviceReleased(IUnknown* device)
{
   Callbacks::OnDeviceDestroyed(static_cast<ID3D11Device*>(device));
}

// Gives the game the proxy. Returns the real device, null if Luma ignores it.
ID3D11Device* OnDeviceCreated(HRESULT result, ID3D11Device** device_pointer, ID3D11DeviceContext** context_pointer)
{
   if (FAILED(result) || Scope::IsActive())
      return nullptr;
   // Callers that only check support pass no device
   if (!device_pointer || !*device_pointer)
   {
      if (context_pointer && *context_pointer)
         Utils::Log::Warning("D3D11: a device was created for its immediate context only, Luma ignores it");
      return nullptr;
   }
   ID3D11Device* device = *device_pointer;
   if (!vtables_resolved.exchange(true))
      ResolveVtables(device);
   else if (Vtables::Set(Vtables::Interface::D3D11Device, device, DEVICE_VERSIONS) && !Vtables::Get(Vtables::Interface::D3D11DeferredContext))
   {
      Scope::Guard scope;
      if (ResolveDeferredContextVtable(device))
      {
         InstallPending();
         LogSummary();
      }
   }

   bool created = false;
   DeviceProxy::Wrap(reinterpret_cast<void**>(device_pointer), __uuidof(ID3D11Device), device, &OnDeviceReleased, created);
   if (created)
   {
      Scope::Guard scope;
      Callbacks::PostCreateDevice(device);
   }
   return device;
}

HRESULT WINAPI CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver_type, HMODULE software, UINT flags,
   const D3D_FEATURE_LEVEL* feature_levels, UINT feature_level_count, UINT sdk_version,
   ID3D11Device** device, D3D_FEATURE_LEVEL* feature_level, ID3D11DeviceContext** immediate_context)
{
   const bool game_call = !Scope::IsActive();
   const D3D_FEATURE_LEVEL game_feature_level = GetGameFeatureLevel(feature_levels, feature_level_count);
   if (game_call)
   {
      Scope::Guard scope;
      Callbacks::PreCreateDevice(flags, feature_levels, feature_level_count);
   }
   HRESULT result;
   {
      // What the runtime does inside this isn't the game's
      Scope::Guard scope;
      result = original_create_device(adapter, driver_type, software, flags, feature_levels, feature_level_count, sdk_version,
         device, feature_level, immediate_context);
   }
   if (game_call)
      HideRaisedFeatureLevel(result, game_feature_level, device, feature_level);
   OnDeviceCreated(result, device, immediate_context);
   return result;
}

HRESULT WINAPI CreateDeviceAndSwapChain(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver_type, HMODULE software, UINT flags,
   const D3D_FEATURE_LEVEL* feature_levels, UINT feature_level_count, UINT sdk_version,
   const DXGI_SWAP_CHAIN_DESC* swapchain_desc, IDXGISwapChain** swapchain,
   ID3D11Device** device, D3D_FEATURE_LEVEL* feature_level, ID3D11DeviceContext** immediate_context)
{
   DXGI_SWAP_CHAIN_DESC game_swapchain_desc;
   const bool game_call = !Scope::IsActive();
   const D3D_FEATURE_LEVEL game_feature_level = GetGameFeatureLevel(feature_levels, feature_level_count);
   if (game_call)
   {
      Scope::Guard scope;
      Callbacks::PreCreateDevice(flags, feature_levels, feature_level_count);
      if (swapchain_desc)
      {
         game_swapchain_desc = *swapchain_desc;
         Callbacks::PreCreateSwapChain(game_swapchain_desc);
         swapchain_desc = &game_swapchain_desc;
      }
   }
   HRESULT result;
   {
      // What the runtime does inside this isn't the game's: it creates the swapchain before Luma has seen the device
      Scope::Guard scope;
      result = original_create_device_and_swapchain(adapter, driver_type, software, flags, feature_levels, feature_level_count, sdk_version,
         swapchain_desc, swapchain, device, feature_level, immediate_context);
   }
   if (game_call)
      HideRaisedFeatureLevel(result, game_feature_level, device, feature_level);
   ID3D11Device* real_device = OnDeviceCreated(result, device, immediate_context);
   if (real_device && swapchain && *swapchain)
      Hooks::OnSwapchainCreated(*swapchain, real_device);
   return result;
}

template <size_t Index>
void STDMETHODCALLTYPE GetDevice(ID3D11DeviceChild* child, ID3D11Device** device)
{
   Utils::DepthGuard guard(get_device_depth);
   original_get_device[Index](child, device);
   if (guard.IsOutermost() && !Scope::IsActive())
      DeviceProxy::Replace(reinterpret_cast<void**>(device));
}

template <size_t... Indices>
constexpr std::array<void*, sizeof...(Indices)> GetDeviceDetours(std::index_sequence<Indices...>)
{
   return {reinterpret_cast<void*>(&GetDevice<Indices>)...};
}
} // namespace

void InstallD3D11(const D3D11::Functions& functions)
{
   const std::array hooks = {
      Hook{ExportTarget{L"d3d11.dll", "D3D11CreateDevice"}, reinterpret_cast<void*>(&CreateDevice), reinterpret_cast<void**>(&original_create_device)},
      Hook{ExportTarget{L"d3d11.dll", "D3D11CreateDeviceAndSwapChain"}, reinterpret_cast<void*>(&CreateDeviceAndSwapChain), reinterpret_cast<void**>(&original_create_device_and_swapchain)},
   };
   Declare(hooks);

   const auto detours = GetDeviceDetours(std::make_index_sequence<GET_DEVICE_TARGETS.size()>());
   std::array<Hook, GET_DEVICE_TARGETS.size()> get_device_hooks;
   for (size_t i = 0; i < GET_DEVICE_TARGETS.size(); i++)
   {
      get_device_hooks[i] = Hook{VtableTarget{GET_DEVICE_TARGETS[i].vtable, LumaSlot_ID3D11DeviceChild_GetDevice, GET_DEVICE_TARGETS[i].name}, detours[i],
         reinterpret_cast<void**>(&original_get_device[i]), true};
   }
   Declare(get_device_hooks);

   const std::array get_feature_level_hooks = {
      Hook{VtableTarget{Vtables::Interface::D3D11Device, LumaSlot_ID3D11Device_GetFeatureLevel, "ID3D11Device::GetFeatureLevel"},
         reinterpret_cast<void*>(&GetFeatureLevel), reinterpret_cast<void**>(&original_get_feature_level)},
   };
   Declare(get_feature_level_hooks);

   InstallD3D11Device(functions);
   InstallD3D11Context(functions);
}
} // namespace Hooks
} // namespace Luma
