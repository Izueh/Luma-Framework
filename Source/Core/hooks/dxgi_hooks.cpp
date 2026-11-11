#include "hooks/hooks.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "hooks/callbacks.hpp"
#include "hooks/device_proxy.hpp"
#include "hooks/lifetime.hpp"
#include "hooks/scope.hpp"
#include "hooks/slots.h"
#include "utils/depth_guard.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Hooks {
namespace {
using CreateDXGIFactoryFunction = HRESULT(WINAPI*)(REFIID riid, void** factory);
using CreateDXGIFactory2Function = HRESULT(WINAPI*)(UINT flags, REFIID riid, void** factory);
using CreateSwapChainFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapchain);
using CreateSwapChainForHwndFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc,
   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc, IDXGIOutput* output, IDXGISwapChain1** swapchain);
using GetDeviceFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* swapchain, REFIID riid, void** device);
using GetDescFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* swapchain, DXGI_SWAP_CHAIN_DESC* desc);
using GetDesc1Function = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1* swapchain, DXGI_SWAP_CHAIN_DESC1* desc);
using PresentFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags);
using Present1Function = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1* swapchain, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters);
using SetFullscreenStateFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* swapchain, BOOL fullscreen, IDXGIOutput* output);
using ResizeBuffersFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
using ResizeBuffers1Function = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags,
   const UINT* node_masks, IUnknown* const* present_queues);

CreateDXGIFactoryFunction original_create_factory = nullptr;
CreateDXGIFactoryFunction original_create_factory1 = nullptr;
CreateDXGIFactory2Function original_create_factory2 = nullptr;
CreateSwapChainFunction original_create_swapchain = nullptr;
CreateSwapChainForHwndFunction original_create_swapchain_for_hwnd = nullptr;
GetDeviceFunction original_get_device = nullptr;
GetDescFunction original_get_desc = nullptr;
GetDesc1Function original_get_desc1 = nullptr;
PresentFunction original_present = nullptr;
Present1Function original_present1 = nullptr;
SetFullscreenStateFunction original_set_fullscreen_state = nullptr;
ResizeBuffersFunction original_resize_buffers = nullptr;
ResizeBuffers1Function original_resize_buffers1 = nullptr;

// These forward to each other inside DXGI, and overlays hooking them may call back into Luma's: only the outermost call
// is the game's
thread_local uint32_t create_depth = 0;
thread_local uint32_t swapchain_create_depth = 0;
thread_local uint32_t present_depth = 0;
thread_local uint32_t resize_depth = 0;

const std::array FACTORY_VERSIONS = {
   LUMA_INTERFACE_VERSION(IDXGIFactory),
   LUMA_INTERFACE_VERSION(IDXGIFactory1),
   LUMA_INTERFACE_VERSION(IDXGIFactory2),
   LUMA_INTERFACE_VERSION(IDXGIFactory3),
   LUMA_INTERFACE_VERSION(IDXGIFactory4),
   LUMA_INTERFACE_VERSION(IDXGIFactory5),
   LUMA_INTERFACE_VERSION(IDXGIFactory6),
   LUMA_INTERFACE_VERSION(IDXGIFactory7),
};
const std::array SWAPCHAIN_VERSIONS = {
   LUMA_INTERFACE_VERSION(IDXGISwapChain),
   LUMA_INTERFACE_VERSION(IDXGISwapChain1),
   LUMA_INTERFACE_VERSION(IDXGISwapChain2),
   LUMA_INTERFACE_VERSION(IDXGISwapChain3),
   LUMA_INTERFACE_VERSION(IDXGISwapChain4),
};

// What the game asked for when it created or resized a swapchain. Luma changes these (flip model, more buffers, flags, a float format),
// and the game keeps seeing what it asked for through "GetDesc" and "GetDesc1" (inside a Luma scope, Luma sees the real swapchain).
// ReShade did the same, and a game that passes back what it reads to "ResizeBuffers" would otherwise resize the swapchain to Luma's own format.
struct GameSwapchainDesc
{
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   UINT buffer_count = 0;
   DXGI_SWAP_EFFECT swap_effect = DXGI_SWAP_EFFECT_DISCARD;
   UINT flags = 0;
};

struct GameDescs
{
   std::mutex mutex;
   std::unordered_map<const void*, GameSwapchainDesc> by_swapchain; // By the swapchain's address, COM interfaces are single inheritance
};

// Leaked: hooked functions can still run while the process exits
GameDescs& GetGameDescs()
{
   static GameDescs* descs = new GameDescs();
   return *descs;
}

void SetGameDesc(const void* swapchain, const GameSwapchainDesc& desc)
{
   GameDescs& descs = GetGameDescs();
   std::scoped_lock lock(descs.mutex);
   descs.by_swapchain[swapchain] = desc;
}

std::optional<GameSwapchainDesc> GetGameDesc(const void* swapchain)
{
   GameDescs& descs = GetGameDescs();
   std::scoped_lock lock(descs.mutex);
   const auto it = descs.by_swapchain.find(swapchain);
   return it != descs.by_swapchain.end() ? std::optional(it->second) : std::nullopt;
}

void ForgetGameDesc(const void* swapchain)
{
   GameDescs& descs = GetGameDescs();
   std::scoped_lock lock(descs.mutex);
   descs.by_swapchain.erase(swapchain);
}

// Resolves a game resize the way DXGI would against what the game asked for before (zero keeps the buffer count, unknown the format), and records it.
// Returns the previous desc, to restore if the resize fails. Empty if the swapchain isn't tracked, the arguments are left as they are.
std::optional<GameSwapchainDesc> TrackGameResize(const void* swapchain, UINT& count, DXGI_FORMAT& format, UINT flags)
{
   GameDescs& descs = GetGameDescs();
   std::scoped_lock lock(descs.mutex);
   const auto it = descs.by_swapchain.find(swapchain);
   if (it == descs.by_swapchain.end())
      return std::nullopt;
   const GameSwapchainDesc previous = it->second;
   if (count == 0)
      count = previous.buffer_count;
   if (format == DXGI_FORMAT_UNKNOWN)
      format = previous.format;
   it->second.buffer_count = count;
   it->second.format = format;
   it->second.flags = flags;
   return previous;
}

GameSwapchainDesc ToGameDesc(const DXGI_SWAP_CHAIN_DESC& desc)
{
   return {desc.BufferDesc.Format, desc.BufferCount, desc.SwapEffect, desc.Flags};
}

GameSwapchainDesc ToGameDesc(const DXGI_SWAP_CHAIN_DESC1& desc)
{
   return {desc.Format, desc.BufferCount, desc.SwapEffect, desc.Flags};
}

void OnFactoryCreated(HRESULT result, void** factory_pointer)
{
   if (FAILED(result) || !factory_pointer || !*factory_pointer)
      return;
   // Also inside a scope: a game may only ever use the factory D3D11CreateDevice made, through IDXGIAdapter::GetParent
   ComPtr<IDXGIFactory> factory;
   if (FAILED(static_cast<IUnknown*>(*factory_pointer)->QueryInterface(IID_PPV_ARGS(&factory))))
      return;
   const bool is_first = !Vtables::Get(Vtables::Interface::DXGIFactory);
   Vtables::Set(Vtables::Interface::DXGIFactory, factory.Get(), FACTORY_VERSIONS);
   if (is_first)
      InstallPending();
}

HRESULT WINAPI CreateFactory(REFIID riid, void** factory)
{
   Utils::DepthGuard guard(create_depth);
   const HRESULT result = original_create_factory(riid, factory);
   if (guard.IsOutermost())
      OnFactoryCreated(result, factory);
   return result;
}

HRESULT WINAPI CreateFactory1(REFIID riid, void** factory)
{
   Utils::DepthGuard guard(create_depth);
   const HRESULT result = original_create_factory1(riid, factory);
   if (guard.IsOutermost())
      OnFactoryCreated(result, factory);
   return result;
}

HRESULT WINAPI CreateFactory2(UINT flags, REFIID riid, void** factory)
{
   Utils::DepthGuard guard(create_depth);
   const HRESULT result = original_create_factory2(flags, riid, factory);
   if (guard.IsOutermost())
      OnFactoryCreated(result, factory);
   return result;
}

void OnSwapchainReleased(ID3D11Device* device, const IUnknown* swapchain)
{
   Scope::Guard scope;
   ForgetGameDesc(swapchain);
   Callbacks::OnSwapChainDestroyed(device, static_cast<const IDXGISwapChain*>(swapchain));
}

// `requested` is what the game asked for, before Luma's changes (empty if it isn't known)
void ReportSwapchain(HRESULT result, IDXGISwapChain* swapchain, IUnknown* creator, const std::optional<GameSwapchainDesc>& requested)
{
   if (FAILED(result) || !creator || !swapchain || Scope::IsActive())
      return;
   ComPtr<IDXGISwapChain> base;
   if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&base))))
      return;
   const bool is_first = !Vtables::Get(Vtables::Interface::DXGISwapChain);
   Vtables::Set(Vtables::Interface::DXGISwapChain, base.Get(), SWAPCHAIN_VERSIONS);
   if (is_first)
   {
      InstallPending();
      LogSummary();
   }
   ComPtr<ID3D11Device> device;
   if (FAILED(creator->QueryInterface(IID_PPV_ARGS(&device))))
      return;
   Scope::Guard scope;
   if (requested)
      SetGameDesc(base.Get(), *requested);
   Lifetime::Track(base.Get(), device.Get(), &OnSwapchainReleased);
   Callbacks::PostCreateSwapChain(base.Get(), device.Get());
}

// D3D11 games pass their device's proxy, which DXGI must not see
HRESULT STDMETHODCALLTYPE CreateSwapChain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapchain)
{
   device = DeviceProxy::Unwrap(device);
   Utils::DepthGuard guard(swapchain_create_depth);
   DXGI_SWAP_CHAIN_DESC game_desc;
   std::optional<GameSwapchainDesc> requested;
   if (guard.IsOutermost() && desc && !Scope::IsActive())
   {
      requested = ToGameDesc(*desc);
      game_desc = *desc;
      Scope::Guard scope;
      Callbacks::PreCreateSwapChain(game_desc);
      desc = &game_desc;
   }
   const HRESULT result = original_create_swapchain(factory, device, desc, swapchain);
   if (guard.IsOutermost())
      ReportSwapchain(result, swapchain ? *swapchain : nullptr, device, requested);
   return result;
}

HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc,
   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc, IDXGIOutput* output, IDXGISwapChain1** swapchain)
{
   device = DeviceProxy::Unwrap(device);
   Utils::DepthGuard guard(swapchain_create_depth);
   DXGI_SWAP_CHAIN_DESC1 game_desc;
   DXGI_SWAP_CHAIN_FULLSCREEN_DESC game_fullscreen_desc;
   std::optional<GameSwapchainDesc> requested;
   if (guard.IsOutermost() && desc && !Scope::IsActive())
   {
      requested = ToGameDesc(*desc);
      game_desc = *desc;
      game_fullscreen_desc = fullscreen_desc ? *fullscreen_desc : DXGI_SWAP_CHAIN_FULLSCREEN_DESC{.Windowed = TRUE};
      {
         Scope::Guard scope;
         Callbacks::PreCreateSwapChainForHwnd(game_desc, game_fullscreen_desc, hwnd);
      }
      desc = &game_desc;
      // A null fullscreen desc creates a windowed swapchain
      if (fullscreen_desc || !game_fullscreen_desc.Windowed)
         fullscreen_desc = &game_fullscreen_desc;
   }
   const HRESULT result = original_create_swapchain_for_hwnd(factory, device, hwnd, desc, fullscreen_desc, output, swapchain);
   if (guard.IsOutermost())
      ReportSwapchain(result, swapchain ? *swapchain : nullptr, device, requested);
   return result;
}

// The game sees what it asked for, not what Luma changed (see "GameSwapchainDesc"). Luma's own calls, inside a scope, see the real one.
HRESULT STDMETHODCALLTYPE GetDesc(IDXGISwapChain* swapchain, DXGI_SWAP_CHAIN_DESC* desc)
{
   const HRESULT result = original_get_desc(swapchain, desc);
   if (SUCCEEDED(result) && desc && !Scope::IsActive())
   {
      if (const std::optional<GameSwapchainDesc> game_desc = GetGameDesc(swapchain))
      {
         if (game_desc->format != DXGI_FORMAT_UNKNOWN)
            desc->BufferDesc.Format = game_desc->format;
         if (game_desc->buffer_count != 0)
            desc->BufferCount = game_desc->buffer_count;
         desc->SwapEffect = game_desc->swap_effect;
         desc->Flags = game_desc->flags;
      }
   }
   return result;
}

HRESULT STDMETHODCALLTYPE GetDesc1(IDXGISwapChain1* swapchain, DXGI_SWAP_CHAIN_DESC1* desc)
{
   const HRESULT result = original_get_desc1(swapchain, desc);
   if (SUCCEEDED(result) && desc && !Scope::IsActive())
   {
      if (const std::optional<GameSwapchainDesc> game_desc = GetGameDesc(swapchain))
      {
         if (game_desc->format != DXGI_FORMAT_UNKNOWN)
            desc->Format = game_desc->format;
         if (game_desc->buffer_count != 0)
            desc->BufferCount = game_desc->buffer_count;
         desc->SwapEffect = game_desc->swap_effect;
         desc->Flags = game_desc->flags;
      }
   }
   return result;
}

// A D3D11 swapchain returns the real device: the game gets its proxy instead
HRESULT STDMETHODCALLTYPE GetDevice(IDXGISwapChain* swapchain, REFIID riid, void** device)
{
   const HRESULT result = original_get_device(swapchain, riid, device);
   if (SUCCEEDED(result) && !Scope::IsActive())
      DeviceProxy::Replace(device);
   return result;
}

// DXGI_PRESENT_TEST only checks whether the swapchain could present
bool IsGamePresent(const Utils::DepthGuard& guard, UINT flags)
{
   return guard.IsOutermost() && !(flags & DXGI_PRESENT_TEST) && !Scope::IsActive();
}

HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags)
{
   Utils::DepthGuard guard(present_depth);
   const bool is_game = IsGamePresent(guard, flags);
   if (is_game)
   {
      Scope::Guard scope;
      Callbacks::PrePresent(swapchain);
   }
   const HRESULT result = original_present(swapchain, sync_interval, flags);
   if (is_game)
   {
      Scope::Guard scope;
      Callbacks::PostPresent(swapchain);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE Present1(IDXGISwapChain1* swapchain, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters)
{
   Utils::DepthGuard guard(present_depth);
   const bool is_game = IsGamePresent(guard, flags);
   if (is_game)
   {
      Scope::Guard scope;
      Callbacks::PrePresent(swapchain);
   }
   const HRESULT result = original_present1(swapchain, sync_interval, flags, parameters);
   if (is_game)
   {
      Scope::Guard scope;
      Callbacks::PostPresent(swapchain);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE SetFullscreenState(IDXGISwapChain* swapchain, BOOL fullscreen, IDXGIOutput* output)
{
   if (!Scope::IsActive())
   {
      Scope::Guard scope;
      if (Callbacks::PreSetFullscreenState(swapchain, fullscreen, output))
         return S_OK;
   }
   return original_set_fullscreen_state(swapchain, fullscreen, output);
}

HRESULT STDMETHODCALLTYPE ResizeBuffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
   Utils::DepthGuard guard(resize_depth);
   const bool is_game = guard.IsOutermost() && !Scope::IsActive();
   std::optional<GameSwapchainDesc> previous_game_desc;
   if (is_game)
   {
      previous_game_desc = TrackGameResize(swapchain, count, format, flags);
      Scope::Guard scope;
      Callbacks::PreResizeBuffers(swapchain, count, width, height, format, flags);
   }
   const HRESULT result = original_resize_buffers(swapchain, count, width, height, format, flags);
   if (is_game && FAILED(result) && previous_game_desc)
      SetGameDesc(swapchain, *previous_game_desc);
   if (is_game && SUCCEEDED(result))
   {
      Scope::Guard scope;
      Callbacks::PostResizeBuffers(swapchain, count, width, height, format, flags);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE ResizeBuffers1(IDXGISwapChain3* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags,
   const UINT* node_masks, IUnknown* const* present_queues)
{
   Utils::DepthGuard guard(resize_depth);
   const bool is_game = guard.IsOutermost() && !Scope::IsActive();
   std::optional<GameSwapchainDesc> previous_game_desc;
   if (is_game)
   {
      previous_game_desc = TrackGameResize(swapchain, count, format, flags);
      Scope::Guard scope;
      Callbacks::PreResizeBuffers(swapchain, count, width, height, format, flags, node_masks, present_queues);
   }
   const HRESULT result = original_resize_buffers1(swapchain, count, width, height, format, flags, node_masks, present_queues);
   if (is_game && FAILED(result) && previous_game_desc)
      SetGameDesc(swapchain, *previous_game_desc);
   if (is_game && SUCCEEDED(result))
   {
      Scope::Guard scope;
      Callbacks::PostResizeBuffers(swapchain, count, width, height, format, flags);
   }
   return result;
}
} // namespace

void InstallDXGI(const D3D11::Functions& functions)
{
   std::vector<Hook> hooks = {
      Hook{ExportTarget{L"dxgi.dll", "CreateDXGIFactory"}, reinterpret_cast<void*>(&CreateFactory), reinterpret_cast<void**>(&original_create_factory)},
      Hook{ExportTarget{L"dxgi.dll", "CreateDXGIFactory1"}, reinterpret_cast<void*>(&CreateFactory1), reinterpret_cast<void**>(&original_create_factory1)},
      Hook{ExportTarget{L"dxgi.dll", "CreateDXGIFactory2"}, reinterpret_cast<void*>(&CreateFactory2), reinterpret_cast<void**>(&original_create_factory2)},
      Hook{VtableTarget{Vtables::Interface::DXGIFactory, LumaSlot_IDXGIFactory_CreateSwapChain, "IDXGIFactory::CreateSwapChain"},
         reinterpret_cast<void*>(&CreateSwapChain), reinterpret_cast<void**>(&original_create_swapchain)},
      Hook{VtableTarget{Vtables::Interface::DXGIFactory, LumaSlot_IDXGIFactory2_CreateSwapChainForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd", __uuidof(IDXGIFactory2)},
         reinterpret_cast<void*>(&CreateSwapChainForHwnd), reinterpret_cast<void**>(&original_create_swapchain_for_hwnd)},
      Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain_GetDevice, "IDXGISwapChain::GetDevice"},
         reinterpret_cast<void*>(&GetDevice), reinterpret_cast<void**>(&original_get_device)},
      Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain_GetDesc, "IDXGISwapChain::GetDesc"},
         reinterpret_cast<void*>(&GetDesc), reinterpret_cast<void**>(&original_get_desc)},
      Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain1_GetDesc1, "IDXGISwapChain1::GetDesc1", __uuidof(IDXGISwapChain1)},
         reinterpret_cast<void*>(&GetDesc1), reinterpret_cast<void**>(&original_get_desc1)},
   };
   if (functions.test(size_t(D3D11::Function::SetFullscreenState)))
   {
      hooks.push_back(Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain_SetFullscreenState, "IDXGISwapChain::SetFullscreenState"},
         reinterpret_cast<void*>(&SetFullscreenState), reinterpret_cast<void**>(&original_set_fullscreen_state)});
   }
   if (functions.test(size_t(D3D11::Function::ResizeBuffers)))
   {
      hooks.push_back(Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain_ResizeBuffers, "IDXGISwapChain::ResizeBuffers"},
         reinterpret_cast<void*>(&ResizeBuffers), reinterpret_cast<void**>(&original_resize_buffers)});
      hooks.push_back(Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain3_ResizeBuffers1, "IDXGISwapChain3::ResizeBuffers1", __uuidof(IDXGISwapChain3)},
         reinterpret_cast<void*>(&ResizeBuffers1), reinterpret_cast<void**>(&original_resize_buffers1)});
   }
   if (functions.test(size_t(D3D11::Function::Present)))
   {
      hooks.push_back(Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain_Present, "IDXGISwapChain::Present"},
         reinterpret_cast<void*>(&Present), reinterpret_cast<void**>(&original_present)});
      hooks.push_back(Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain1_Present1, "IDXGISwapChain1::Present1", __uuidof(IDXGISwapChain1)},
         reinterpret_cast<void*>(&Present1), reinterpret_cast<void**>(&original_present1)});
   }
   Declare(hooks);
}

void OnSwapchainCreated(IDXGISwapChain* swapchain, IUnknown* creator, const DXGI_SWAP_CHAIN_DESC* requested)
{
   std::optional<GameSwapchainDesc> game_desc;
   if (requested)
      game_desc = ToGameDesc(*requested);
   ReportSwapchain(S_OK, swapchain, creator, game_desc);
}
} // namespace Hooks
} // namespace Luma
