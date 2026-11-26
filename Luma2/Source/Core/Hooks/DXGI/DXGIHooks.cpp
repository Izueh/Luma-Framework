#include "Hooks/HookManager.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <intrin.h>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "Handlers/DXGI/DXGIHandlers.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "System/Modules.hpp"
#include "Hooks/DXGI/DXGIHooks.hpp"
#include "Hooks/DXGI/DXGISwapchainProxy.hpp"
#include "Hooks/ThirdPartyModules.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/VtableSlots.h"
#include "Hooks/DepthGuard.hpp"
#include "Log/Log.hpp"

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
using SetColorSpace1Function = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space);
using GetBufferFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* swapchain, UINT index, REFIID riid, void** buffer);
using GetCurrentBackBufferIndexFunction = UINT(STDMETHODCALLTYPE*)(IDXGISwapChain3* swapchain);
using GetFullscreenStateFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* swapchain, BOOL* fullscreen, IDXGIOutput** output);
using CheckColorSpaceSupportFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space, UINT* support);
using GetOutputDesc1Function = HRESULT(STDMETHODCALLTYPE*)(IDXGIOutput6* output, DXGI_OUTPUT_DESC1* desc);
using SetHDRMetaDataFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain4* swapchain, DXGI_HDR_METADATA_TYPE type, UINT size, void* metadata);

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
SetColorSpace1Function original_set_color_space1 = nullptr;
GetBufferFunction original_get_buffer = nullptr;
GetCurrentBackBufferIndexFunction original_get_current_back_buffer_index = nullptr;
GetFullscreenStateFunction original_get_fullscreen_state = nullptr;
SetHDRMetaDataFunction original_set_hdr_metadata = nullptr;
CheckColorSpaceSupportFunction original_check_color_space_support = nullptr;
GetOutputDesc1Function original_get_output_desc1 = nullptr;

// These forward to each other inside DXGI, and overlays hooking them may call back into Luma's: only the outermost call
// is the game's
thread_local uint32_t create_depth = 0;
thread_local uint32_t swapchain_create_depth = 0;
thread_local uint32_t present_depth = 0;
thread_local uint32_t resize_depth = 0;

const std::array FACTORY_VERSIONS = {
   Vtables::GetVersion<IDXGIFactory>("IDXGIFactory", LumaMethodCount_IDXGIFactory),
   Vtables::GetVersion<IDXGIFactory1>("IDXGIFactory1", LumaMethodCount_IDXGIFactory1),
   Vtables::GetVersion<IDXGIFactory2>("IDXGIFactory2", LumaMethodCount_IDXGIFactory2),
   Vtables::GetVersion<IDXGIFactory3>("IDXGIFactory3", LumaMethodCount_IDXGIFactory3),
   Vtables::GetVersion<IDXGIFactory4>("IDXGIFactory4", LumaMethodCount_IDXGIFactory4),
   Vtables::GetVersion<IDXGIFactory5>("IDXGIFactory5", LumaMethodCount_IDXGIFactory5),
   Vtables::GetVersion<IDXGIFactory6>("IDXGIFactory6", LumaMethodCount_IDXGIFactory6),
   Vtables::GetVersion<IDXGIFactory7>("IDXGIFactory7", LumaMethodCount_IDXGIFactory7),
};
const std::array OUTPUT_VERSIONS = {
   Vtables::GetVersion<IDXGIOutput>("IDXGIOutput", LumaMethodCount_IDXGIOutput),
   Vtables::GetVersion<IDXGIOutput1>("IDXGIOutput1", LumaMethodCount_IDXGIOutput1),
   Vtables::GetVersion<IDXGIOutput2>("IDXGIOutput2", LumaMethodCount_IDXGIOutput2),
   Vtables::GetVersion<IDXGIOutput3>("IDXGIOutput3", LumaMethodCount_IDXGIOutput3),
   Vtables::GetVersion<IDXGIOutput4>("IDXGIOutput4", LumaMethodCount_IDXGIOutput4),
   Vtables::GetVersion<IDXGIOutput5>("IDXGIOutput5", LumaMethodCount_IDXGIOutput5),
   Vtables::GetVersion<IDXGIOutput6>("IDXGIOutput6", LumaMethodCount_IDXGIOutput6),
};
const std::array SWAPCHAIN_VERSIONS = {
   Vtables::GetVersion<IDXGISwapChain>("IDXGISwapChain", LumaMethodCount_IDXGISwapChain),
   Vtables::GetVersion<IDXGISwapChain1>("IDXGISwapChain1", LumaMethodCount_IDXGISwapChain1),
   Vtables::GetVersion<IDXGISwapChain2>("IDXGISwapChain2", LumaMethodCount_IDXGISwapChain2),
   Vtables::GetVersion<IDXGISwapChain3>("IDXGISwapChain3", LumaMethodCount_IDXGISwapChain3),
   Vtables::GetVersion<IDXGISwapChain4>("IDXGISwapChain4", LumaMethodCount_IDXGISwapChain4),
};

// What the game asked for when it created or resized a swapchain. Luma changes it (flip model, more buffers, flags, a float format, samples,
// windowed), and the game keeps seeing all of what it asked for through "GetDesc" and "GetDesc1", as with ReShade (inside a Luma scope, Luma
// sees the real swapchain). A game that compares it with what it wants recreates its swapchain on any difference (dgVoodoo does, every frame),
// and one that passes it back to "ResizeBuffers" would resize the swapchain to Luma's own format.
struct GameSwapchainDesc
{
   DXGI_SWAP_CHAIN_DESC desc = {}; // As "CreateSwapChain" takes it. A zero width or height stands for the swapchain's real one.
   // Only in "DXGI_SWAP_CHAIN_DESC1"
   DXGI_SCALING scaling = DXGI_SCALING_STRETCH;
   DXGI_ALPHA_MODE alpha_mode = DXGI_ALPHA_MODE_UNSPECIFIED;
   BOOL stereo = FALSE;
};

// A call is the game's when its proxy forwards it, or on a swapchain the game holds itself (no proxy). Anyone else calling a proxied
// swapchain's real object (an overlay, a capture tool) sees the real swapchain.
bool IsGameView(IDXGISwapChain* swapchain)
{
   return SwapchainProxy::IsForwarding() || !SwapchainProxy::HasProxy(swapchain);
}

void SetGameDesc(IDXGISwapChain* swapchain, const GameSwapchainDesc& desc)
{
   Scope::Guard scope;
   Lifetime::SetState<GameSwapchainDesc>(swapchain, desc);
}

std::optional<GameSwapchainDesc> GetGameDesc(IDXGISwapChain* swapchain)
{
   Scope::Guard scope;
   const GameSwapchainDesc* desc = Lifetime::GetState<GameSwapchainDesc>(swapchain);
   return desc ? std::optional(*desc) : std::nullopt;
}

// Resolves a game resize the way DXGI would against what the game asked for before (zero keeps the buffer count, unknown the format), and records it.
// Returns the previous desc, to restore if the resize fails. Empty if the swapchain isn't tracked, the arguments are left as they are.
// Resizes run on the thread presenting, which is the only writer
std::optional<GameSwapchainDesc> TrackGameResize(IDXGISwapChain* swapchain, UINT& count, UINT width, UINT height, DXGI_FORMAT& format, UINT flags)
{
   Scope::Guard scope;
   GameSwapchainDesc* game_desc = Lifetime::GetState<GameSwapchainDesc>(swapchain);
   if (!game_desc)
      return std::nullopt;
   const GameSwapchainDesc previous = *game_desc;
   DXGI_SWAP_CHAIN_DESC& desc = game_desc->desc;
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
   // An output's, which the display spoofing hooks: games ask about the display before they create a swapchain
   bool is_first_output = false;
   ComPtr<IDXGIAdapter> adapter;
   ComPtr<IDXGIOutput> output;
   if (!Vtables::Get(Vtables::Interface::DXGIOutput) && SUCCEEDED(factory->EnumAdapters(0, &adapter)) && SUCCEEDED(adapter->EnumOutputs(0, &output)))
   {
      Vtables::Set(Vtables::Interface::DXGIOutput, output.Get(), OUTPUT_VERSIONS);
      is_first_output = true;
   }
   if (is_first || is_first_output)
      InstallPending();
}

HRESULT WINAPI CreateFactory(REFIID riid, void** factory)
{
   DepthGuard guard(create_depth);
   const HRESULT result = original_create_factory(riid, factory);
   if (guard.IsOutermost())
      OnFactoryCreated(result, factory);
   return result;
}

HRESULT WINAPI CreateFactory1(REFIID riid, void** factory)
{
   DepthGuard guard(create_depth);
   const HRESULT result = original_create_factory1(riid, factory);
   if (guard.IsOutermost())
      OnFactoryCreated(result, factory);
   return result;
}

HRESULT WINAPI CreateFactory2(UINT flags, REFIID riid, void** factory)
{
   DepthGuard guard(create_depth);
   const HRESULT result = original_create_factory2(flags, riid, factory);
   if (guard.IsOutermost())
      OnFactoryCreated(result, factory);
   return result;
}

void OnSwapchainReleased(IUnknown* creator, const IUnknown* swapchain)
{
   (void)creator;
   Scope::Guard scope;
   Handlers::OnSwapChainDestroyed(static_cast<const IDXGISwapChain*>(swapchain));
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
   Scope::Guard scope;
   // Already reported (D3D11CreateDeviceAndSwapChain)
   if (!Lifetime::Track(base.Get(), creator, &OnSwapchainReleased))
      return;
   if (requested)
      SetGameDesc(base.Get(), *requested);
   Handlers::PostCreateSwapChain(base.Get(), creator);
}

// Code of the game's (or its engine's) is on the stack. A swapchain an overlay creates for itself (to find DXGI's methods) has only the
// overlay's, the system's and Luma's: the return address alone can't tell, an overlay hooking the creation above Luma is the caller of the
// game's creation too (Steam).
bool IsCreatedByGame()
{
   std::array<void*, 48> frames = {};
   const USHORT count = CaptureStackBackTrace(1, ULONG(frames.size()), frames.data(), nullptr);
   const HMODULE self = System::Modules::GetSelf();
   for (USHORT i = 0; i < count; i++)
   {
      HMODULE module = nullptr;
      if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(frames[i]), &module))
         continue;
      if (module != self && !System::Modules::IsSystemModule(module) && !ThirdPartyModules::FindBase(frames[i]))
         return true;
   }
   return false;
}

// The game gets a proxy of a swapchain Luma owns (SwapchainProxy), unless an overlay created it
bool ShouldWrap(IDXGISwapChain* swapchain)
{
   if (!IsCreatedByGame())
      return false;
   Scope::Guard scope;
   return Handlers::ShouldWrapSwapChain(swapchain);
}

// D3D11 games pass their device's proxy, which DXGI must not see (D3D12 games pass a queue, never proxied)
HRESULT STDMETHODCALLTYPE CreateSwapChain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapchain)
{
   device = DeviceProxy::Unwrap(device);
   DepthGuard guard(swapchain_create_depth);
   DXGI_SWAP_CHAIN_DESC game_desc;
   std::optional<GameSwapchainDesc> requested;
   if (guard.IsOutermost() && desc && !Scope::IsActive())
   {
      requested = ToGameDesc(*desc);
      game_desc = *desc;
      Scope::Guard scope;
      Handlers::PreCreateSwapChain(device, game_desc);
      desc = &game_desc;
   }
   const HRESULT result = original_create_swapchain(factory, device, desc, swapchain);
   if (guard.IsOutermost())
   {
      ReportSwapchain(result, swapchain ? *swapchain : nullptr, device, requested);
      if (SUCCEEDED(result) && requested && ShouldWrap(*swapchain))
         SwapchainProxy::Wrap(swapchain);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc,
   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc, IDXGIOutput* output, IDXGISwapChain1** swapchain)
{
   device = DeviceProxy::Unwrap(device);
   DepthGuard guard(swapchain_create_depth);
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
         Handlers::PreCreateSwapChainForHwnd(device, game_desc, game_fullscreen_desc, hwnd);
      }
      desc = &game_desc;
      // A null fullscreen desc creates a windowed swapchain
      if (fullscreen_desc || !game_fullscreen_desc.Windowed)
         fullscreen_desc = &game_fullscreen_desc;
   }
   const HRESULT result = original_create_swapchain_for_hwnd(factory, device, hwnd, desc, fullscreen_desc, output, swapchain);
   if (guard.IsOutermost())
   {
      ReportSwapchain(result, swapchain ? *swapchain : nullptr, device, requested);
      if (SUCCEEDED(result) && requested && ShouldWrap(*swapchain))
         SwapchainProxy::Wrap(swapchain);
   }
   return result;
}

// The game sees what it asked for, not what Luma changed (see "GameSwapchainDesc"). Luma's own calls, inside a scope, see the real one.
HRESULT STDMETHODCALLTYPE GetDesc(IDXGISwapChain* swapchain, DXGI_SWAP_CHAIN_DESC* desc)
{
   const HRESULT result = original_get_desc(swapchain, desc);
   if (SUCCEEDED(result) && desc && !Scope::IsActive() && IsGameView(swapchain))
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
   if (SUCCEEDED(result) && desc && !Scope::IsActive() && IsGameView(swapchain))
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

// D3D11 may switch the immediate context to other instances of its methods while the game runs, and a present is the call every frame makes.
// D3D12 swapchains have no device.
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

// DXGI_PRESENT_TEST only checks whether the swapchain could present. A proxied swapchain's is handled by its proxy (PresentForGame): one
// reaching the real object from outside Luma's scope is another tool's.
bool IsGamePresent(const DepthGuard& guard, UINT flags, IDXGISwapChain* swapchain)
{
   return guard.IsOutermost() && !(flags & DXGI_PRESENT_TEST) && !Scope::IsActive() && !SwapchainProxy::HasProxy(swapchain);
}

// The overlay whose code called Luma's Present last: its hook is above Luma's (or it presents itself). Never held with another lock.
std::mutex present_caller_mutex;
std::string present_caller;
// Its dll's base, so a frame that finds the same overlay again takes no lock
std::atomic<uintptr_t> present_caller_base = 0;

// One relaxed load while no overlay is loaded
void NoteCaller(const void* return_address)
{
   const uintptr_t base = ThirdPartyModules::FindBase(return_address);
   if (!base || base == present_caller_base.load(std::memory_order_relaxed))
      return;
   std::string overlay = ThirdPartyModules::Find(return_address);
   if (overlay.empty())
      return;
   std::scoped_lock lock(present_caller_mutex);
   if (present_caller != overlay)
      Log::Info("DXGI: {} calls Present before Luma's handling, its drawing goes into the game's image", overlay);
   present_caller = std::move(overlay);
   present_caller_base = base;
}

// The swapchain whose present work (up to the display composition) ReShade's present event ran on this thread: the Present that
// closes the third-party work window skips it, any other Present drops it
thread_local IDXGISwapChain* present_work_swapchain = nullptr;

// The present work, unless the event ran it for this swapchain, then Luma's overlay
void HandleGamePresent(IDXGISwapChain* swapchain, UINT sync_interval, UINT& flags)
{
   const bool present_work_done = std::exchange(present_work_swapchain, nullptr) == swapchain;
   Scope::Guard scope;
   if (!present_work_done)
      ObserveImmediateContext(swapchain);
   Handlers::PrePresent(swapchain, sync_interval, flags, present_work_done);
}

HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags)
{
   DXGI::CloseThirdPartyWorkWindowAtPresent();
   DepthGuard guard(present_depth);
   const bool is_game = IsGamePresent(guard, flags, swapchain);
   if (is_game)
   {
      NoteCaller(_ReturnAddress());
      HandleGamePresent(swapchain, sync_interval, flags);
   }
   const HRESULT result = CallRuntime(original_present, swapchain, sync_interval, flags);
   if (is_game && SUCCEEDED(result))
   {
      Scope::Guard scope;
      Handlers::PostPresent(swapchain);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE Present1(IDXGISwapChain1* swapchain, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters)
{
   DXGI::CloseThirdPartyWorkWindowAtPresent();
   DepthGuard guard(present_depth);
   const bool is_game = IsGamePresent(guard, flags, swapchain);
   // Composition writes the whole buffer: the game's dirty rects and scroll don't describe it
   const DXGI_PRESENT_PARAMETERS whole = {};
   if (is_game)
   {
      NoteCaller(_ReturnAddress());
      HandleGamePresent(swapchain, sync_interval, flags);
      Scope::Guard scope;
      if (Handlers::ComposesWholeBuffer(swapchain))
         parameters = &whole;
   }
   const HRESULT result = CallRuntime(original_present1, swapchain, sync_interval, flags, parameters);
   if (is_game && SUCCEEDED(result))
   {
      Scope::Guard scope;
      Handlers::PostPresent(swapchain);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE SetFullscreenState(IDXGISwapChain* swapchain, BOOL fullscreen, IDXGIOutput* output)
{
   if (!Scope::IsActive() && IsGameView(swapchain))
   {
      Scope::Guard scope;
      if (Handlers::PreSetFullscreenState(swapchain, fullscreen, output))
         return S_OK;
   }
   return CallRuntime(original_set_fullscreen_state, swapchain, fullscreen, output);
}

HRESULT STDMETHODCALLTYPE ResizeBuffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
   DepthGuard guard(resize_depth);
   const bool is_game = guard.IsOutermost() && !Scope::IsActive() && IsGameView(swapchain);
   std::optional<GameSwapchainDesc> previous_game_desc;
   if (is_game)
   {
      previous_game_desc = TrackGameResize(swapchain, count, width, height, format, flags);
      Scope::Guard scope;
      Handlers::PreResizeBuffers(swapchain, count, width, height, format, flags, nullptr, nullptr);
   }
   const HRESULT result = CallRuntime(original_resize_buffers, swapchain, count, width, height, format, flags);
   if (is_game && FAILED(result) && previous_game_desc)
      SetGameDesc(swapchain, *previous_game_desc);
   if (is_game)
   {
      Scope::Guard scope;
      Handlers::PostResizeBuffers(swapchain, count, width, height, format, flags, SUCCEEDED(result));
   }
   return result;
}

HRESULT STDMETHODCALLTYPE ResizeBuffers1(IDXGISwapChain3* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags,
   const UINT* node_masks, IUnknown* const* present_queues)
{
   DepthGuard guard(resize_depth);
   const bool is_game = guard.IsOutermost() && !Scope::IsActive() && IsGameView(swapchain);
   std::optional<GameSwapchainDesc> previous_game_desc;
   if (is_game)
   {
      previous_game_desc = TrackGameResize(swapchain, count, width, height, format, flags);
      Scope::Guard scope;
      Handlers::PreResizeBuffers(swapchain, count, width, height, format, flags, node_masks, present_queues);
   }
   const HRESULT result = CallRuntime(original_resize_buffers1, swapchain, count, width, height, format, flags, node_masks, present_queues);
   if (is_game && FAILED(result) && previous_game_desc)
      SetGameDesc(swapchain, *previous_game_desc);
   if (is_game)
   {
      Scope::Guard scope;
      Handlers::PostResizeBuffers(swapchain, count, width, height, format, flags, SUCCEEDED(result));
   }
   return result;
}

// Recorded for what Luma draws on the swapchain (the overlay): DXGI can't be asked for it. Where Luma sets the real one, the game's is
// kept as what its image is in.
HRESULT STDMETHODCALLTYPE SetColorSpace1(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space)
{
   if (!Scope::IsActive() && IsGameView(swapchain))
   {
      Scope::Guard scope;
      if (Handlers::PreSetColorSpace(swapchain, color_space))
         return S_OK;
   }
   const HRESULT result = original_set_color_space1(swapchain, color_space);
   if (SUCCEEDED(result))
   {
      Scope::Guard scope;
      Handlers::PostSetColorSpace(swapchain, color_space);
   }
   return result;
}

// Where Luma owns the real buffers, the game's are Luma's textures; inside Luma's scope (composition, the overlay) they're the real ones
HRESULT STDMETHODCALLTYPE GetBuffer(IDXGISwapChain* swapchain, UINT index, REFIID riid, void** buffer)
{
   if (!Scope::IsActive() && IsGameView(swapchain))
   {
      const bool third_party = ThirdPartyModules::FindBase(_ReturnAddress()) != 0;
      Scope::Guard scope;
      if (const std::optional<HRESULT> result = Handlers::GetBuffer(swapchain, index, riid, buffer, third_party))
         return *result;
   }
   return original_get_buffer(swapchain, index, riid, buffer);
}

UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex(IDXGISwapChain3* swapchain)
{
   if (!Scope::IsActive() && IsGameView(swapchain))
   {
      const bool third_party = ThirdPartyModules::FindBase(_ReturnAddress()) != 0;
      Scope::Guard scope;
      if (const std::optional<UINT> index = Handlers::GetCurrentBackBufferIndex(swapchain, third_party))
         return *index;
   }
   return original_get_current_back_buffer_index(swapchain);
}

// Borderless: the game is told the fullscreen state it asked for
HRESULT STDMETHODCALLTYPE GetFullscreenState(IDXGISwapChain* swapchain, BOOL* fullscreen, IDXGIOutput** output)
{
   const HRESULT result = original_get_fullscreen_state(swapchain, fullscreen, output);
   if (SUCCEEDED(result) && fullscreen && !Scope::IsActive() && IsGameView(swapchain))
   {
      Scope::Guard scope;
      if (const std::optional<BOOL> game_fullscreen = Handlers::GetFullscreenState(swapchain))
         *fullscreen = *game_fullscreen;
   }
   return result;
}

// What the game is told about the display (Display::Spoofing). Luma's own queries, inside its scope, get the real answer.
HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space, UINT* support)
{
   const HRESULT result = original_check_color_space_support(swapchain, color_space, support);
   if (SUCCEEDED(result) && support && !Scope::IsActive() && IsGameView(swapchain))
   {
      Scope::Guard scope;
      Handlers::PostCheckColorSpaceSupport(swapchain, color_space, *support);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE GetOutputDesc1(IDXGIOutput6* output, DXGI_OUTPUT_DESC1* desc)
{
   const HRESULT result = original_get_output_desc1(output, desc);
   if (SUCCEEDED(result) && desc && !Scope::IsActive())
   {
      Scope::Guard scope;
      Handlers::PostGetOutputDesc1(output, *desc);
   }
   return result;
}

HRESULT STDMETHODCALLTYPE SetHDRMetaData(IDXGISwapChain4* swapchain, DXGI_HDR_METADATA_TYPE type, UINT size, void* metadata)
{
   if (!Scope::IsActive() && IsGameView(swapchain))
   {
      Scope::Guard scope;
      if (Handlers::PreSetHdrMetadata(swapchain, type, size, metadata))
         return S_OK;
   }
   return original_set_hdr_metadata(swapchain, type, size, metadata);
}
} // namespace

namespace DXGI {
void OnOverlayBufferRequest(IDXGISwapChain* swapchain)
{
   if (Scope::IsActive())
      return;
   Scope::Guard scope;
   Handlers::OnOverlayBufferRequest(swapchain);
}

SwapchainProxy::ReShadeSwapchainView GetReShadeSwapchainView(IDXGISwapChain* swapchain)
{
   Scope::Guard scope;
   return Handlers::GetReShadeSwapchainView(swapchain);
}

bool IsThirdPartyWorkWindowOpenFor(IDXGISwapChain* swapchain)
{
   return Scope::IsThirdPartyWorkOpen() && present_work_swapchain == swapchain;
}

bool PresentPrologue(IDXGISwapChain* swapchain, UINT sync_interval, UINT& flags)
{
   if (Scope::IsActive())
      return false;
   if (flags & DXGI_PRESENT_TEST)
   {
      present_work_swapchain = nullptr;
      return false;
   }
   HandleGamePresent(swapchain, sync_interval, flags);
   return true;
}

bool RunPresentWorkAtEvent(IDXGISwapChain* swapchain)
{
   // Only where a Luma Present follows (the proxy's, or the hook's) to draw the overlay and close the window
   if (!swapchain || Scope::IsActive() || (!SwapchainProxy::HasProxy(swapchain) && !original_present))
      return false;
   {
      Scope::Guard scope;
      ObserveImmediateContext(swapchain);
      Handlers::PrePresentWork(swapchain);
   }
   present_work_swapchain = swapchain;
   Scope::BeginThirdPartyWork();
   return true;
}

void CloseThirdPartyWorkWindowAtPresent()
{
   if (Scope::IsThirdPartyWorkOpen())
      Scope::EndThirdPartyWork();
   else
      present_work_swapchain = nullptr;
}

bool CloseThirdPartyWorkWindowAtFinishPresent()
{
   present_work_swapchain = nullptr;
   if (!Scope::IsThirdPartyWorkOpen())
      return false;
   Scope::EndThirdPartyWork();
   return true;
}

HRESULT PresentForGame(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters, bool present1,
   std::optional<bool> prologue)
{
   CloseThirdPartyWorkWindowAtPresent();
   DepthGuard guard(present_depth);
   // The proxy's outer Present usually ran the prologue already, before other tools' hooks on the proxy
   const bool is_game = prologue ? *prologue : (guard.IsOutermost() && PresentPrologue(swapchain, sync_interval, flags));
   // Composition writes the whole buffer: the game's dirty rects and scroll don't describe it
   const DXGI_PRESENT_PARAMETERS whole = {};
   if (is_game && present1)
   {
      Scope::Guard scope;
      if (Handlers::ComposesWholeBuffer(swapchain))
         parameters = &whole;
   }
   HRESULT result = E_FAIL;
   {
      // Through the vtable: every hook another tool put in DXGI's Present runs now, on the final image; Luma's lets it through
      Scope::Guard scope;
      ComPtr<IDXGISwapChain1> swapchain1;
      if (present1 && SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain1))))
         result = swapchain1->Present1(sync_interval, flags, parameters ? parameters : &whole);
      else
         result = swapchain->Present(sync_interval, flags);
   }
   if (is_game && SUCCEEDED(result))
   {
      Scope::Guard scope;
      Handlers::PostPresent(swapchain);
   }
   return result;
}
} // namespace DXGI

void InstallDXGI(const DXGI::Functions& functions)
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
      Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain3_SetColorSpace1, "IDXGISwapChain3::SetColorSpace1", __uuidof(IDXGISwapChain3)},
         reinterpret_cast<void*>(&SetColorSpace1), reinterpret_cast<void**>(&original_set_color_space1)},
      // Swapchain: the game's own back buffers, the color space and HDR metadata Luma sets, the fullscreen state it asked for
      Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain_GetBuffer, "IDXGISwapChain::GetBuffer"},
         reinterpret_cast<void*>(&GetBuffer), reinterpret_cast<void**>(&original_get_buffer)},
      Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain3_GetCurrentBackBufferIndex, "IDXGISwapChain3::GetCurrentBackBufferIndex",
              __uuidof(IDXGISwapChain3)},
         reinterpret_cast<void*>(&GetCurrentBackBufferIndex), reinterpret_cast<void**>(&original_get_current_back_buffer_index)},
      Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain_GetFullscreenState, "IDXGISwapChain::GetFullscreenState"},
         reinterpret_cast<void*>(&GetFullscreenState), reinterpret_cast<void**>(&original_get_fullscreen_state)},
      Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain4_SetHDRMetaData, "IDXGISwapChain4::SetHDRMetaData", __uuidof(IDXGISwapChain4)},
         reinterpret_cast<void*>(&SetHDRMetaData), reinterpret_cast<void**>(&original_set_hdr_metadata)},
      // Display spoofing
      Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain3_CheckColorSpaceSupport, "IDXGISwapChain3::CheckColorSpaceSupport",
              __uuidof(IDXGISwapChain3)},
         reinterpret_cast<void*>(&CheckColorSpaceSupport), reinterpret_cast<void**>(&original_check_color_space_support)},
      Hook{VtableTarget{Vtables::Interface::DXGIOutput, LumaSlot_IDXGIOutput6_GetDesc1, "IDXGIOutput6::GetDesc1", __uuidof(IDXGIOutput6)},
         reinterpret_cast<void*>(&GetOutputDesc1), reinterpret_cast<void**>(&original_get_output_desc1)},
   };
   if (functions.test(size_t(DXGI::Function::SetFullscreenState)))
   {
      hooks.push_back(Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain_SetFullscreenState, "IDXGISwapChain::SetFullscreenState"},
         reinterpret_cast<void*>(&SetFullscreenState), reinterpret_cast<void**>(&original_set_fullscreen_state)});
   }
   if (functions.test(size_t(DXGI::Function::ResizeBuffers)))
   {
      hooks.push_back(Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain_ResizeBuffers, "IDXGISwapChain::ResizeBuffers"},
         reinterpret_cast<void*>(&ResizeBuffers), reinterpret_cast<void**>(&original_resize_buffers)});
      hooks.push_back(Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain3_ResizeBuffers1, "IDXGISwapChain3::ResizeBuffers1", __uuidof(IDXGISwapChain3)},
         reinterpret_cast<void*>(&ResizeBuffers1), reinterpret_cast<void**>(&original_resize_buffers1)});
   }
   if (functions.test(size_t(DXGI::Function::Present)))
   {
      hooks.push_back(Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain_Present, "IDXGISwapChain::Present"},
         reinterpret_cast<void*>(&Present), reinterpret_cast<void**>(&original_present)});
      hooks.push_back(Hook{VtableTarget{Vtables::Interface::DXGISwapChain, LumaSlot_IDXGISwapChain1_Present1, "IDXGISwapChain1::Present1", __uuidof(IDXGISwapChain1)},
         reinterpret_cast<void*>(&Present1), reinterpret_cast<void**>(&original_present1)});
   }
   Declare(hooks);
}

PresentChain GetPresentChain()
{
   const std::array states = {GetChainState(reinterpret_cast<const void*>(&Present)), GetChainState(reinterpret_cast<const void*>(&Present1))};
   if (!states[0] && !states[1])
      return {};
   const std::vector<std::string> loaded = ThirdPartyModules::GetLoaded();
   {
      std::scoped_lock lock(present_caller_mutex);
      // The overlay may have unloaded since
      if (!present_caller.empty() && !std::ranges::contains(loaded, present_caller))
      {
         present_caller.clear();
         present_caller_base = 0;
      }
      if (!present_caller.empty())
         return {PresentOrder::OverlayOutermost, present_caller};
   }
   for (const std::optional<ChainState>& state : states)
   {
      if (state && state->chain == Chain::Chained)
         return {PresentOrder::OverlayOutermost, state->chained_by};
   }
   for (const std::optional<ChainState>& state : states)
   {
      if (state && !state->detoured_by.empty())
         return {PresentOrder::LumaOutermost, state->detoured_by};
   }
   return {PresentOrder::Alone, {}};
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
