#include "Hooks/HookManager.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>

#include <d3d11_4.h>
#include <wrl/client.h>

#include "Handlers/D3D11/D3D11Handlers.hpp"
#include "Handlers/DXGI/DXGIHandlers.hpp"
#include "Hooks/Detours.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/VtableSlots.h"
#include "Hooks/DepthGuard.hpp"
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Hooks {
namespace {
PFN_D3D11_CREATE_DEVICE original_create_device = nullptr;
PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN original_create_device_and_swapchain = nullptr;
std::atomic<bool> vtables_resolved = false;
// For the hooks of context variants, declared as they appear
D3D11::Functions installed_functions;

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

// The Graphics Tools optional feature may be missing
bool IsDebugLayerAdded(UINT game_flags, UINT flags)
{
   return (flags & D3D11_CREATE_DEVICE_DEBUG) && !(game_flags & D3D11_CREATE_DEVICE_DEBUG);
}

// A null or empty feature-level list defaults to 11.0 (not 11.1) in D3D11, so SR negotiation must supply an
// explicit list to request 11.1. HideRaisedFeatureLevel then preserves the highest level the game asked for
// (11.0 when null) so raising to 11.1 does not alter the game's own feature-level branches.
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
   Vtables::GetVersion<ID3D11Device>("ID3D11Device", LumaMethodCount_ID3D11Device),
   Vtables::GetVersion<ID3D11Device1>("ID3D11Device1", LumaMethodCount_ID3D11Device1),
   Vtables::GetVersion<ID3D11Device2>("ID3D11Device2", LumaMethodCount_ID3D11Device2),
   Vtables::GetVersion<ID3D11Device3>("ID3D11Device3", LumaMethodCount_ID3D11Device3),
   Vtables::GetVersion<ID3D11Device4>("ID3D11Device4", LumaMethodCount_ID3D11Device4),
   Vtables::GetVersion<ID3D11Device5>("ID3D11Device5", LumaMethodCount_ID3D11Device5),
};
const std::array CONTEXT_VERSIONS = {
   Vtables::GetVersion<ID3D11DeviceContext>("ID3D11DeviceContext", LumaMethodCount_ID3D11DeviceContext),
   Vtables::GetVersion<ID3D11DeviceContext1>("ID3D11DeviceContext1", LumaMethodCount_ID3D11DeviceContext1),
   Vtables::GetVersion<ID3D11DeviceContext2>("ID3D11DeviceContext2", LumaMethodCount_ID3D11DeviceContext2),
   Vtables::GetVersion<ID3D11DeviceContext3>("ID3D11DeviceContext3", LumaMethodCount_ID3D11DeviceContext3),
   Vtables::GetVersion<ID3D11DeviceContext4>("ID3D11DeviceContext4", LumaMethodCount_ID3D11DeviceContext4),
};
const std::array CHILD_VERSIONS = {
   Vtables::GetVersion<ID3D11DeviceChild>("ID3D11DeviceChild", LumaMethodCount_ID3D11DeviceChild),
};
const std::array TEXTURE1D_VERSIONS = {
   Vtables::GetVersion<ID3D11Texture1D>("ID3D11Texture1D", LumaMethodCount_ID3D11Texture1D),
};
const std::array TEXTURE2D_VERSIONS = {
   Vtables::GetVersion<ID3D11Texture2D>("ID3D11Texture2D", LumaMethodCount_ID3D11Texture2D),
   Vtables::GetVersion<ID3D11Texture2D1>("ID3D11Texture2D1", LumaMethodCount_ID3D11Texture2D1),
};
const std::array TEXTURE3D_VERSIONS = {
   Vtables::GetVersion<ID3D11Texture3D>("ID3D11Texture3D", LumaMethodCount_ID3D11Texture3D),
   Vtables::GetVersion<ID3D11Texture3D1>("ID3D11Texture3D1", LumaMethodCount_ID3D11Texture3D1),
};

// A 1 texel texture of each dimension, for their methods (GetDesc)
void ResolveTextureVtables(ID3D11Device* device)
{
   const D3D11_TEXTURE1D_DESC desc_1d = {.Width = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
   const D3D11_TEXTURE2D_DESC desc_2d = {.Width = 1, .Height = 1, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
   const D3D11_TEXTURE3D_DESC desc_3d = {.Width = 1, .Height = 1, .Depth = 1, .MipLevels = 1, .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
   ComPtr<ID3D11Texture1D> texture_1d;
   ComPtr<ID3D11Texture2D> texture_2d;
   ComPtr<ID3D11Texture3D> texture_3d;
   if (SUCCEEDED(device->CreateTexture1D(&desc_1d, nullptr, &texture_1d)))
      Vtables::Set(Vtables::Interface::D3D11Texture1D, texture_1d.Get(), TEXTURE1D_VERSIONS);
   if (SUCCEEDED(device->CreateTexture2D(&desc_2d, nullptr, &texture_2d)))
      Vtables::Set(Vtables::Interface::D3D11Texture2D, texture_2d.Get(), TEXTURE2D_VERSIONS);
   if (SUCCEEDED(device->CreateTexture3D(&desc_3d, nullptr, &texture_3d)))
      Vtables::Set(Vtables::Interface::D3D11Texture3D, texture_3d.Get(), TEXTURE3D_VERSIONS);
   if (!texture_1d || !texture_2d || !texture_3d)
      Log::Error("D3D11: failed to create temporary textures to read their vtables");
}

// Textures upgraded in place report the game's format, size and mips to the game when the rules spoof it (Upgrade::Rules::spoof_description)
void(STDMETHODCALLTYPE* original_get_texture1d_desc)(ID3D11Texture1D* texture, D3D11_TEXTURE1D_DESC* desc) = nullptr;
void(STDMETHODCALLTYPE* original_get_texture2d_desc)(ID3D11Texture2D* texture, D3D11_TEXTURE2D_DESC* desc) = nullptr;
void(STDMETHODCALLTYPE* original_get_texture2d_desc1)(ID3D11Texture2D1* texture, D3D11_TEXTURE2D_DESC1* desc) = nullptr;
void(STDMETHODCALLTYPE* original_get_texture3d_desc)(ID3D11Texture3D* texture, D3D11_TEXTURE3D_DESC* desc) = nullptr;
void(STDMETHODCALLTYPE* original_get_texture3d_desc1)(ID3D11Texture3D1* texture, D3D11_TEXTURE3D_DESC1* desc) = nullptr;

template <auto& Original, typename Texture, typename Desc>
void STDMETHODCALLTYPE GetTextureDesc(Texture* texture, Desc* desc)
{
   Original(texture, desc);
   if (desc && Detours::IsGameCall(_ReturnAddress()))
   {
      Scope::Guard scope;
      Handlers::PostGetDesc(texture, *desc);
   }
}

// Fails on single threaded devices: retried on the next device
bool ResolveDeferredContextVtable(ID3D11Device* device)
{
   ComPtr<ID3D11DeviceContext> deferred_context;
   if (const HRESULT result = device->CreateDeferredContext(0, &deferred_context); FAILED(result))
   {
      Log::Info("D3D11: no deferred context (0x{:08X}), deferred context hooks wait for another device", uint32_t(result));
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
      Log::Error("D3D11: failed to create a temporary query to read its vtable (0x{:08X})", uint32_t(result));
   ResolveTextureVtables(device);

   InstallPending();
   LogSummary();
}

void OnDeviceReleased(IUnknown* device)
{
   Handlers::OnDeviceDestroyed(static_cast<ID3D11Device*>(device));
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
         Log::Warning("D3D11: a device was created for its immediate context only, Luma ignores it");
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
      Handlers::PostCreateDevice(device);
   }
   return device;
}

HRESULT WINAPI CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver_type, HMODULE software, UINT flags,
   const D3D_FEATURE_LEVEL* feature_levels, UINT feature_level_count, UINT sdk_version,
   ID3D11Device** device, D3D_FEATURE_LEVEL* feature_level, ID3D11DeviceContext** immediate_context)
{
   const bool game_call = !Scope::IsActive();
   const D3D_FEATURE_LEVEL game_feature_level = GetGameFeatureLevel(feature_levels, feature_level_count);
   const UINT game_flags = flags;
   if (game_call)
   {
      Scope::Guard scope;
      Handlers::PreCreateDevice(flags, feature_levels, feature_level_count);
   }
   HRESULT result;
   {
      // What the runtime does inside this isn't the game's
      Scope::Guard scope;
      result = original_create_device(adapter, driver_type, software, flags, feature_levels, feature_level_count, sdk_version,
         device, feature_level, immediate_context);
      if (FAILED(result) && IsDebugLayerAdded(game_flags, flags))
      {
         Log::Warning("D3D11: device creation with the debug layer failed (0x{:08X}), creating it without", uint32_t(result));
         result = original_create_device(adapter, driver_type, software, flags & ~D3D11_CREATE_DEVICE_DEBUG, feature_levels, feature_level_count,
            sdk_version, device, feature_level, immediate_context);
      }
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
   // What the game asked for, before Luma's changes
   DXGI_SWAP_CHAIN_DESC requested_swapchain_desc = {};
   bool has_requested_swapchain_desc = false;
   const bool game_call = !Scope::IsActive();
   const D3D_FEATURE_LEVEL game_feature_level = GetGameFeatureLevel(feature_levels, feature_level_count);
   const UINT game_flags = flags;
   if (game_call)
   {
      Scope::Guard scope;
      Handlers::PreCreateDevice(flags, feature_levels, feature_level_count);
      if (swapchain_desc)
      {
         requested_swapchain_desc = *swapchain_desc;
         has_requested_swapchain_desc = true;
         game_swapchain_desc = *swapchain_desc;
         // The device doesn't exist yet: pass Api::D3D11 explicitly because creator is null here
         Handlers::PreCreateSwapChain(nullptr, game_swapchain_desc, Api::D3D11);
         swapchain_desc = &game_swapchain_desc;
      }
   }
   HRESULT result;
   {
      // Suppress nested IDXGIFactory::CreateSwapChain handling: the runtime creates the swapchain internally
      // before Luma has seen or initialized the device. We report the device first below, then the swapchain.
      Scope::Guard scope;
      result = original_create_device_and_swapchain(adapter, driver_type, software, flags, feature_levels, feature_level_count, sdk_version,
         swapchain_desc, swapchain, device, feature_level, immediate_context);
      if (FAILED(result) && IsDebugLayerAdded(game_flags, flags))
      {
         Log::Warning("D3D11: device creation with the debug layer failed (0x{:08X}), creating it without", uint32_t(result));
         result = original_create_device_and_swapchain(adapter, driver_type, software, flags & ~D3D11_CREATE_DEVICE_DEBUG, feature_levels,
            feature_level_count, sdk_version, swapchain_desc, swapchain, device, feature_level, immediate_context);
      }
   }
   if (game_call)
      HideRaisedFeatureLevel(result, game_feature_level, device, feature_level);
   ID3D11Device* real_device = OnDeviceCreated(result, device, immediate_context);
   if (real_device && swapchain && *swapchain)
      Hooks::OnSwapchainCreated(*swapchain, real_device, has_requested_swapchain_desc ? &requested_swapchain_desc : nullptr);
   return result;
}

template <size_t Index>
void STDMETHODCALLTYPE GetDevice(ID3D11DeviceChild* child, ID3D11Device** device)
{
   DepthGuard guard(get_device_depth);
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

void ObserveContext(ID3D11DeviceContext* context)
{
   // Before the first device resolved the methods nothing is hooked yet
   if (!context || !Vtables::Get(Vtables::Interface::D3D11ImmediateContext))
      return;
   const auto variant = [](size_t index) { return Vtables::Interface(size_t(Vtables::Interface::D3D11ContextVariant0) + index); };
   const auto is_hooked = [&] {
      if (Vtables::Matches(Vtables::Interface::D3D11ImmediateContext, context) || Vtables::Matches(Vtables::Interface::D3D11DeferredContext, context))
         return true;
      for (size_t i = 0; i < CONTEXT_VARIANT_COUNT; i++)
      {
         if (Vtables::Matches(variant(i), context))
            return true;
      }
      return false;
   };
   if (is_hooked())
      return;

   static std::mutex mutex;
   std::scoped_lock lock(mutex);
   if (is_hooked()) // Hooked by another thread meanwhile
      return;
   Scope::Guard scope;
   for (size_t i = 0; i < CONTEXT_VARIANT_COUNT; i++)
   {
      if (Vtables::Get(variant(i)))
         continue;
      Log::Info("D3D11: the context {} has methods no hook covers, hooking them (variant {})", static_cast<const void*>(context), i);
      Vtables::Set(variant(i), context, CONTEXT_VERSIONS);
      DeclareContextVariant(i, installed_functions);
      LogSummary();
      return;
   }
   static std::atomic<bool> warned = false;
   if (!warned.exchange(true))
      Log::Warning("D3D11: the context {} has methods no hook covers and all {} variants are used, its calls are not seen", static_cast<const void*>(context),
         CONTEXT_VARIANT_COUNT);
}

void InstallD3D11(const D3D11::Functions& functions)
{
   installed_functions = functions;
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

   if (functions.test(size_t(D3D11::Function::GetTextureDesc)))
   {
      const std::array get_texture_desc_hooks = {
         Hook{VtableTarget{Vtables::Interface::D3D11Texture1D, LumaSlot_ID3D11Texture1D_GetDesc, "ID3D11Texture1D::GetDesc"},
            reinterpret_cast<void*>(&GetTextureDesc<original_get_texture1d_desc, ID3D11Texture1D, D3D11_TEXTURE1D_DESC>),
            reinterpret_cast<void**>(&original_get_texture1d_desc)},
         Hook{VtableTarget{Vtables::Interface::D3D11Texture2D, LumaSlot_ID3D11Texture2D_GetDesc, "ID3D11Texture2D::GetDesc"},
            reinterpret_cast<void*>(&GetTextureDesc<original_get_texture2d_desc, ID3D11Texture2D, D3D11_TEXTURE2D_DESC>),
            reinterpret_cast<void**>(&original_get_texture2d_desc)},
         Hook{VtableTarget{Vtables::Interface::D3D11Texture2D, LumaSlot_ID3D11Texture2D1_GetDesc1, "ID3D11Texture2D1::GetDesc1", __uuidof(ID3D11Texture2D1)},
            reinterpret_cast<void*>(&GetTextureDesc<original_get_texture2d_desc1, ID3D11Texture2D1, D3D11_TEXTURE2D_DESC1>),
            reinterpret_cast<void**>(&original_get_texture2d_desc1)},
         Hook{VtableTarget{Vtables::Interface::D3D11Texture3D, LumaSlot_ID3D11Texture3D_GetDesc, "ID3D11Texture3D::GetDesc"},
            reinterpret_cast<void*>(&GetTextureDesc<original_get_texture3d_desc, ID3D11Texture3D, D3D11_TEXTURE3D_DESC>),
            reinterpret_cast<void**>(&original_get_texture3d_desc)},
         Hook{VtableTarget{Vtables::Interface::D3D11Texture3D, LumaSlot_ID3D11Texture3D1_GetDesc1, "ID3D11Texture3D1::GetDesc1", __uuidof(ID3D11Texture3D1)},
            reinterpret_cast<void*>(&GetTextureDesc<original_get_texture3d_desc1, ID3D11Texture3D1, D3D11_TEXTURE3D_DESC1>),
            reinterpret_cast<void**>(&original_get_texture3d_desc1)},
      };
      Declare(get_texture_desc_hooks);
   }

   InstallD3D11Device(functions);
   InstallD3D11Context(functions);
}
} // namespace Hooks
} // namespace Luma
