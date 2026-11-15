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

// What the game asked for when it created or resized a swapchain. Luma changes it (flip model, more buffers, flags, a float format, no refresh
// rate, windowed), and the game keeps seeing all of what it asked for through "GetDesc" and "GetDesc1", as with ReShade (inside a Luma scope,
// Luma sees the real swapchain). A game that compares it with what it wants recreates its swapchain on any difference (dgVoodoo does, every
// frame, over the refresh rate), and one that passes it back to "ResizeBuffers" would resize the swapchain to Luma's own format.
struct GameSwapchainDesc
{
   DXGI_SWAP_CHAIN_DESC desc = {}; // As "CreateSwapChain" takes it. A zero width or height stands for the swapchain's real one.
   // Only in "DXGI_SWAP_CHAIN_DESC1"
   DXGI_SCALING scaling = DXGI_SCALING_STRETCH;
   DXGI_ALPHA_MODE alpha_mode = DXGI_ALPHA_MODE_UNSPECIFIED;
   BOOL stereo = FALSE;
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
std::optional<GameSwapchainDesc> TrackGameResize(const void* swapchain, UINT& count, UINT width, UINT height, DXGI_FORMAT& format, UINT flags)
{
   GameDescs& descs = GetGameDescs();
   std::scoped_lock lock(descs.mutex);
   const auto it = descs.by_swapchain.find(swapchain);
   if (it == descs.by_swapchain.end())
      return std::nullopt;
   const GameSwapchainDesc previous = it->second;
   DXGI_SWAP_CHAIN_DESC& desc = it->second.desc;
   if (count == 0)
      count = desc.BufferCount;
   if (format == DXGI_FORMAT_UNKNOWN)
      format = desc.BufferDesc.Format;
   desc.BufferCount = count;
   desc.BufferDesc.Width = width;
   desc.BufferDesc.Height = height;
   desc.BufferDesc.Format = format;
   desc.Flags = flags;
   return previous;
}

GameSwapchainDesc ToGameDesc(const DXGI_SWAP_CHAIN_DESC& desc)
{
   return {.desc = desc, .scaling = desc.BufferDesc.Scaling == DXGI_MODE_SCALING_CENTERED ? DXGI_SCALING_ASPECT_RATIO_STRETCH : DXGI_SCALING_STRETCH};
}

// A null fullscreen desc is a windowed swapchain
GameSwapchainDesc ToGameDesc(const DXGI_SWAP_CHAIN_DESC1& desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc, HWND window)
{
   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen = fullscreen_desc ? *fullscreen_desc : DXGI_SWAP_CHAIN_FULLSCREEN_DESC{.Windowed = TRUE};
   GameSwapchainDesc game_desc = {.scaling = desc.Scaling, .alpha_mode = desc.AlphaMode, .stereo = desc.Stereo};
   game_desc.desc.BufferDesc = {desc.Width, desc.Height, fullscreen.RefreshRate, desc.Format, fullscreen.ScanlineOrdering, fullscreen.Scaling};
   game_desc.desc.SampleDesc = desc.SampleDesc;
   game_desc.desc.BufferUsage = desc.BufferUsage;
   game_desc.desc.BufferCount = desc.BufferCount;
   game_desc.desc.OutputWindow = window;
   game_desc.desc.Windowed = fullscreen.Windowed;
   game_desc.desc.SwapEffect = desc.SwapEffect;
   game_desc.desc.Flags = desc.Flags;
   return game_desc;
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
      requested = ToGameDesc(*desc, fullscreen_desc, hwnd);
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
         const DXGI_SWAP_CHAIN_DESC real_desc = *desc;
         *desc = game_desc->desc;
         if (desc->BufferDesc.Width == 0 || desc->BufferDesc.Height == 0)
         {
            desc->BufferDesc.Width = real_desc.BufferDesc.Width;
            desc->BufferDesc.Height = real_desc.BufferDesc.Height;
         }
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
         const DXGI_SWAP_CHAIN_DESC& game = game_desc->desc;
         if (game.BufferDesc.Width != 0 && game.BufferDesc.Height != 0)
         {
            desc->Width = game.BufferDesc.Width;
            desc->Height = game.BufferDesc.Height;
         }
         desc->Format = game.BufferDesc.Format;
         desc->Stereo = game_desc->stereo;
         desc->SampleDesc = game.SampleDesc;
         desc->BufferUsage = game.BufferUsage;
         desc->BufferCount = game.BufferCount;
         desc->Scaling = game_desc->scaling;
         desc->SwapEffect = game.SwapEffect;
         desc->AlphaMode = game_desc->alpha_mode;
         desc->Flags = game.Flags;
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

// D3D11 may switch the immediate context to other instances of its methods while the game runs, and a present is the call every frame makes
void ObserveImmediateContext(IDXGISwapChain* swapchain)
{
   ComPtr<ID3D11Device> device;
   if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&device))))
      return;
   ComPtr<ID3D11DeviceContext> context;
   device->GetImmediateContext(&context);
   ObserveContext(context.Get());
}

// What runs inside the swapchain's own calls isn't the game's: DXGI, and the overlays hooked into them (Steam draws its overlay inside
// Present, with its own resources Luma never tracked). ReShade didn't see these either ("g_in_dxgi_runtime").
template <typename Function, typename... Args>
HRESULT CallRuntime(Function function, Args... args)
{
   Scope::Guard scope;
   return function(args...);
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
      ObserveImmediateContext(swapchain);
      Callbacks::PrePresent(swapchain);
   }
   const HRESULT result = CallRuntime(original_present, swapchain, sync_interval, flags);
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
      ObserveImmediateContext(swapchain);
      Callbacks::PrePresent(swapchain);
   }
   const HRESULT result = CallRuntime(original_present1, swapchain, sync_interval, flags, parameters);
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
   return CallRuntime(original_set_fullscreen_state, swapchain, fullscreen, output);
}

HRESULT STDMETHODCALLTYPE ResizeBuffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
   Utils::DepthGuard guard(resize_depth);
   const bool is_game = guard.IsOutermost() && !Scope::IsActive();
   std::optional<GameSwapchainDesc> previous_game_desc;
   if (is_game)
   {
      previous_game_desc = TrackGameResize(swapchain, count, width, height, format, flags);
      Scope::Guard scope;
      Callbacks::PreResizeBuffers(swapchain, count, width, height, format, flags);
   }
   const HRESULT result = CallRuntime(original_resize_buffers, swapchain, count, width, height, format, flags);
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
      previous_game_desc = TrackGameResize(swapchain, count, width, height, format, flags);
      Scope::Guard scope;
      Callbacks::PreResizeBuffers(swapchain, count, width, height, format, flags, node_masks, present_queues);
   }
   const HRESULT result = CallRuntime(original_resize_buffers1, swapchain, count, width, height, format, flags, node_masks, present_queues);
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
