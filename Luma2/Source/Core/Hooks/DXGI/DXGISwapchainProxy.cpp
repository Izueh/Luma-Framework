#include "Hooks/DXGI/DXGISwapchainProxy.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>
#include <atomic>
#include <cstdint>

#include <intrin.h>
#include <wrl/client.h>

#include "Hooks/DXGI/DXGIHooks.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "Hooks/Slots.h"
#include "Hooks/Scope.hpp"
#include "Hooks/ThirdPartyModules.hpp"
#include "Utils/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Hooks {
namespace SwapchainProxy {
namespace {
// On the real swapchain: the proxy the game holds (weak), cleared when the proxy goes
constexpr GUID PROXY_GUID = {0x4c554d41, 0x5357, 0x5052, {0x4f, 0x58, 0x59, 0x00, 0x00, 0x00, 0x00, 0x01}};
// On the real swapchain, set by ReShade's wrapper through the proxy: a pointer to the wrapper. Its __uuidof(DXGISwapChain) in ReShade
// 6.8.0, an internal detail to check against each release.
constexpr GUID RESHADE_WRAPPER_GUID = {0x1f445f9f, 0x9887, 0x4c4c, {0x90, 0x55, 0x4e, 0x3b, 0xad, 0xaf, 0xcc, 0xa8}};
// Special K's color space private data, which ReShade reads and writes
constexpr GUID COLOR_SPACE_GUID = {0x18b57e4, 0x1493, 0x4953, {0xad, 0xf2, 0xde, 0x6d, 0x99, 0xcc, 0x5, 0xe5}};

thread_local uint32_t forwarding_depth = 0;
// Luma's own calls go through the proxy (ResizeBuffers and the others below), so tools hooking it see them: its methods then call the real
// swapchain inside Luma's scope
thread_local bool is_luma_own_call = false;

// ReShade's code, written once it's known
std::atomic<uintptr_t> reshade_begin = 0;
std::atomic<uintptr_t> reshade_end = 0;
std::atomic<bool> reshade_bridged = false;

bool IsReShadeCode(const void* address)
{
   const uintptr_t value = reinterpret_cast<uintptr_t>(address);
   return value >= reshade_begin.load(std::memory_order_relaxed) && value < reshade_end.load(std::memory_order_relaxed);
}

// The real swapchain whose init window is open on this thread
thread_local IDXGISwapChain* init_window_swapchain = nullptr;

// Luma's resize through ReShade's wrapper: what the real swapchain gets, and ReShade's color space read once it's resized
struct LumaResizeThroughReShade
{
   ResizeArguments real;
   DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
};

thread_local std::optional<LumaResizeThroughReShade> pending_luma_resize;

// What ReShade's color space read answers inside its window, until Luma's call through its wrapper returns: the color space the real
// swapchain is getting (Luma sets it below ReShade after the resize, and it isn't in the private data yet)
struct ColorSpaceAnswer
{
   IDXGISwapChain* swapchain = nullptr;
   DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
};

thread_local std::optional<ColorSpaceAnswer> color_space_answer;

// The code that called ReShade's function is ReShade's too: its runtime reading the swapchain, not the game calling through its wrapper
// (whose forwarder returns to the game). Read from the stack, after the proxy method's own return address.
__declspec(noinline) bool IsCalledFromReShade(const void* return_address)
{
   std::array<void*, 8> frames = {};
   const USHORT count = CaptureStackBackTrace(0, ULONG(frames.size()), frames.data(), nullptr);
   for (USHORT i = 0; i + 1 < count; i++)
   {
      if (frames[i] == return_address)
         return IsReShadeCode(frames[i + 1]);
   }
   return false;
}

HRESULT AnswerColorSpace(DXGI_COLOR_SPACE_TYPE color_space, UINT* size, void* data)
{
   if (!size)
      return E_INVALIDARG;
   if (data && *size < sizeof(color_space))
   {
      *size = sizeof(color_space);
      return DXGI_ERROR_MORE_DATA;
   }
   if (data)
      std::memcpy(data, &color_space, sizeof(color_space));
   *size = sizeof(color_space);
   return S_OK;
}

IDXGISwapChain* FindReShadeWrapper(IDXGISwapChain* swapchain)
{
   IDXGISwapChain* wrapper = nullptr;
   UINT size = sizeof(wrapper);
   if (FAILED(swapchain->GetPrivateData(RESHADE_WRAPPER_GUID, &size, &wrapper)) || size != sizeof(wrapper))
      return nullptr;
   return wrapper;
}

// Identical copies of Luma's outer Present: a tool hooks the function in the Present slot when the swapchain is created (Steam patches its
// code), so when the one in the slot has been patched, the slot moves to an untouched copy, which runs first and calls the patched one
constexpr size_t OUTER_COPIES = 4;
// The first bytes of each copy, before any tool saw them
constexpr size_t SIGNATURE_SIZE = 16;
using Signature = std::array<uint8_t, SIGNATURE_SIZE>;

Signature ReadSignature(const void* function)
{
   Signature signature;
   std::memcpy(signature.data(), function, SIGNATURE_SIZE);
   return signature;
}

// The outer Present ran Luma's prologue on this thread: the proxy's own Present, reached through other tools' hooks, doesn't again
thread_local std::optional<bool> prologue_done;
thread_local bool in_outer_present = false;
// Which copy of the outer Present ran last: each writes its own number, so the linker can't fold the copies into one function
thread_local uint32_t outer_copy = 0;

struct ForwardingDepthGuard
{
   ForwardingDepthGuard() { forwarding_depth++; }

   ~ForwardingDepthGuard() { forwarding_depth--; }

   ForwardingDepthGuard(const ForwardingDepthGuard&) = delete;
   ForwardingDepthGuard& operator=(const ForwardingDepthGuard&) = delete;
};

// Only the interfaces the real swapchain has are the proxy's: a game asking for IDXGISwapChain4 on an older DXGI must fail
bool IsProxied(REFIID riid)
{
   return riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) || riid == __uuidof(IDXGISwapChain) ||
      riid == __uuidof(IDXGISwapChain1) || riid == __uuidof(IDXGISwapChain2) || riid == __uuidof(IDXGISwapChain3) || riid == __uuidof(IDXGISwapChain4);
}

// A COM interface needs its virtuals. Every method forwards the game's call to the real swapchain through its vtable, so Luma's hooks and
// every other tool's see it, marked as the game's.
class ProxiedSwapchain final : public IDXGISwapChain4
{
public:
   explicit ProxiedSwapchain(IDXGISwapChain* real) : real(real)
   {
      real->QueryInterface(IID_PPV_ARGS(&real1));
      real->QueryInterface(IID_PPV_ARGS(&real2));
      real->QueryInterface(IID_PPV_ARGS(&real3));
      real->QueryInterface(IID_PPV_ARGS(&real4));
      // Weak: the proxy holds the real swapchain, not the reverse
      const std::array<IUnknown*, 4> versions = {real1, real2, real3, real4};
      for (IUnknown* version : versions)
      {
         if (version)
            version->Release();
      }
      ProxiedSwapchain* self = this;
      real->SetPrivateData(PROXY_GUID, sizeof(self), &self);
      // Its own vtable, Luma's outer Present in its Present slots, the compiler's methods after them
      void* const* compiled = *reinterpret_cast<void* const* const*>(static_cast<IDXGISwapChain4*>(this));
      vtable.assign(compiled, compiled + LumaMethodCount_IDXGISwapChain4);
      inner_present = vtable[LumaSlot_IDXGISwapChain_Present];
      inner_present1 = vtable[LumaSlot_IDXGISwapChain1_Present1];
      next_present = inner_present;
      next_present1 = inner_present1;
      vtable[LumaSlot_IDXGISwapChain_Present] = GetOuters().present[0];
      vtable[LumaSlot_IDXGISwapChain1_Present1] = GetOuters().present1[0];
      *reinterpret_cast<void**>(static_cast<IDXGISwapChain4*>(this)) = vtable.data();
   }

   // IUnknown
   HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
   {
      if (!object)
         return E_POINTER;
      if (IsProxied(riid))
      {
         IUnknown* version = nullptr;
         if (FAILED(real->QueryInterface(riid, reinterpret_cast<void**>(&version))))
         {
            *object = nullptr;
            return E_NOINTERFACE;
         }
         version->Release();
         AddRef();
         *object = static_cast<IDXGISwapChain4*>(this);
         return S_OK;
      }
      // Private and other interfaces (IDXGISwapChainMedia, a driver's) are the real swapchain's, as with ReShade
      ForwardingDepthGuard forwarding;
      return real->QueryInterface(riid, object);
   }

   ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }

   ULONG STDMETHODCALLTYPE Release() override
   {
      const ULONG count = --references;
      if (count == 0)
      {
         real->SetPrivateData(PROXY_GUID, 0, nullptr);
         IDXGISwapChain* released = real;
         delete this;
         released->Release();
      }
      return count;
   }

   // IDXGIObject
   HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name, UINT size, const void* data) override
   {
      // In Mirror the real swapchain's color space is Luma's (its own SetColorSpace1 records it): ReShade's wrapper writes the game's
      if (name == COLOR_SPACE_GUID && IsReShadeCode(_ReturnAddress()) && DXGI::GetReShadeSwapchainView(real) != ReShadeSwapchainView::SameAsOverlay)
         return S_OK;
      return ForwardKeepingInitWindow([&] { return real->SetPrivateData(name, size, data); });
   }

   HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name, const IUnknown* unknown) override
   {
      return ForwardEndingInitWindow([&] { return real->SetPrivateDataInterface(name, unknown); });
   }

   HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name, UINT* size, void* data) override
   {
      const void* caller = _ReturnAddress();
      if (name == COLOR_SPACE_GUID && color_space_answer && color_space_answer->swapchain == real && IsReShadeCode(caller) &&
         DXGI::GetReShadeSwapchainView(real) == ReShadeSwapchainView::RealInsideWindows && IsInReShadeWindow(caller))
         return AnswerColorSpace(color_space_answer->color_space, size, data);
      return ForwardKeepingInitWindow([&] { return real->GetPrivateData(name, size, data); });
   }

   HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** parent) override
   {
      return ForwardEndingInitWindow([&] { return real->GetParent(riid, parent); });
   }

   // IDXGIDeviceSubObject
   HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** device) override
   {
      return ForwardEndingInitWindow([&] { return real->GetDevice(riid, device); });
   }

   // IDXGISwapChain
   HRESULT STDMETHODCALLTYPE Present(UINT sync_interval, UINT flags) override
   {
      EndInitWindow();
      return DXGI::PresentForGame(real, sync_interval, flags, nullptr, false, std::exchange(prologue_done, std::nullopt));
   }

   // An overlay that hooked the proxy (Steam does) calls these from its Present, before the proxy's, and gets the real buffers composed
   // first; the return address is its own. ReShade is answered first: its calls are the game's except inside its windows.
   HRESULT STDMETHODCALLTYPE GetBuffer(UINT index, REFIID riid, void** buffer) override
   {
      const void* caller = _ReturnAddress();
      if (const std::optional<HRESULT> result = AnswerReShadeCall(caller, [&] { return real->GetBuffer(index, riid, buffer); }))
         return *result;
      if (ThirdPartyModules::FindBase(caller))
      {
         DXGI::OnOverlayBufferRequest(real);
         return real->GetBuffer(index, riid, buffer);
      }
      return ForwardKeepingInitWindow([&] { return real->GetBuffer(index, riid, buffer); });
   }

   HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen, IDXGIOutput* output) override
   {
      return ForwardEndingInitWindow([&] { return real->SetFullscreenState(fullscreen, output); });
   }

   HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fullscreen, IDXGIOutput** output) override
   {
      return ForwardKeepingInitWindow([&] { return real->GetFullscreenState(fullscreen, output); });
   }

   HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* desc) override
   {
      if (const std::optional<HRESULT> result = AnswerReShadeCall(_ReturnAddress(), [&] { return real->GetDesc(desc); }))
         return *result;
      return ForwardKeepingInitWindow([&] { return real->GetDesc(desc); });
   }

   HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) override
   {
      const void* caller = _ReturnAddress();
      HRESULT result = E_FAIL;
      if (std::exchange(is_luma_own_call, false))
      {
         EndInitWindow();
         // Luma's call through ReShade's wrapper came with the game's values
         const std::optional<LumaResizeThroughReShade> through_reshade = std::exchange(pending_luma_resize, std::nullopt);
         if (through_reshade)
         {
            count = through_reshade->real.count;
            width = through_reshade->real.width;
            height = through_reshade->real.height;
            format = through_reshade->real.format;
            flags = through_reshade->real.flags;
         }
         {
            Scope::Guard scope;
            result = real->ResizeBuffers(count, width, height, format, flags);
         }
         if (through_reshade && SUCCEEDED(result))
            color_space_answer = ColorSpaceAnswer{real, through_reshade->color_space};
      }
      else
      {
         result = ForwardEndingInitWindow([&] { return real->ResizeBuffers(count, width, height, format, flags); });
      }
      OpenInitWindow(caller);
      return result;
   }

   HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* mode) override
   {
      return ForwardEndingInitWindow([&] { return real->ResizeTarget(mode); });
   }

   HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** output) override
   {
      return ForwardEndingInitWindow([&] { return real->GetContainingOutput(output); });
   }

   HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* statistics) override
   {
      return ForwardEndingInitWindow([&] { return real->GetFrameStatistics(statistics); });
   }

   HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* count) override
   {
      return ForwardEndingInitWindow([&] { return real->GetLastPresentCount(count); });
   }

   // IDXGISwapChain1
   HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* desc) override
   {
      if (const std::optional<HRESULT> result = AnswerReShadeCall(_ReturnAddress(), [&] { return real1->GetDesc1(desc); }))
         return *result;
      return ForwardKeepingInitWindow([&] { return real1->GetDesc1(desc); });
   }

   HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* desc) override
   {
      return ForwardEndingInitWindow([&] { return real1->GetFullscreenDesc(desc); });
   }

   HRESULT STDMETHODCALLTYPE GetHwnd(HWND* window) override
   {
      return ForwardKeepingInitWindow([&] { return real1->GetHwnd(window); });
   }

   HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID riid, void** window) override
   {
      return ForwardKeepingInitWindow([&] { return real1->GetCoreWindow(riid, window); });
   }

   HRESULT STDMETHODCALLTYPE Present1(UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters) override
   {
      EndInitWindow();
      return DXGI::PresentForGame(real, sync_interval, flags, parameters, true, std::exchange(prologue_done, std::nullopt));
   }

   BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override
   {
      return ForwardEndingInitWindow([&] { return real1->IsTemporaryMonoSupported(); });
   }

   HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** output) override
   {
      return ForwardEndingInitWindow([&] { return real1->GetRestrictToOutput(output); });
   }

   HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* color) override
   {
      return ForwardEndingInitWindow([&] { return real1->SetBackgroundColor(color); });
   }

   HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* color) override
   {
      return ForwardEndingInitWindow([&] { return real1->GetBackgroundColor(color); });
   }

   HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation) override
   {
      return ForwardEndingInitWindow([&] { return real1->SetRotation(rotation); });
   }

   HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* rotation) override
   {
      return ForwardEndingInitWindow([&] { return real1->GetRotation(rotation); });
   }

   // IDXGISwapChain2
   HRESULT STDMETHODCALLTYPE SetSourceSize(UINT width, UINT height) override
   {
      return ForwardEndingInitWindow([&] { return real2->SetSourceSize(width, height); });
   }

   HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* width, UINT* height) override
   {
      return ForwardEndingInitWindow([&] { return real2->GetSourceSize(width, height); });
   }

   HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT latency) override
   {
      return ForwardEndingInitWindow([&] { return real2->SetMaximumFrameLatency(latency); });
   }

   HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* latency) override
   {
      return ForwardEndingInitWindow([&] { return real2->GetMaximumFrameLatency(latency); });
   }

   HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override
   {
      return ForwardEndingInitWindow([&] { return real2->GetFrameLatencyWaitableObject(); });
   }

   HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* matrix) override
   {
      return ForwardEndingInitWindow([&] { return real2->SetMatrixTransform(matrix); });
   }

   HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* matrix) override
   {
      return ForwardEndingInitWindow([&] { return real2->GetMatrixTransform(matrix); });
   }

   // IDXGISwapChain3
   UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override
   {
      const void* caller = _ReturnAddress();
      if (const std::optional<UINT> index = AnswerReShadeCall(caller, [&] { return real3->GetCurrentBackBufferIndex(); }))
         return *index;
      if (ThirdPartyModules::FindBase(caller))
      {
         DXGI::OnOverlayBufferRequest(real);
         return real3->GetCurrentBackBufferIndex();
      }
      return ForwardEndingInitWindow([&] { return real3->GetCurrentBackBufferIndex(); });
   }

   HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space, UINT* support) override
   {
      return ForwardKeepingInitWindow([&] { return real3->CheckColorSpaceSupport(color_space, support); });
   }

   // ReShade's wrapper resets its runtime here when the value changes, and reads the swapchain again once this returns
   HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE color_space) override
   {
      const void* caller = _ReturnAddress();
      HRESULT result = E_FAIL;
      if (std::exchange(is_luma_own_call, false))
      {
         EndInitWindow();
         Scope::Guard scope;
         result = real3->SetColorSpace1(color_space);
      }
      else
      {
         result = ForwardEndingInitWindow([&] { return real3->SetColorSpace1(color_space); });
      }
      OpenInitWindow(caller);
      return result;
   }

   HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags, const UINT* node_masks, IUnknown* const* queues) override
   {
      const void* caller = _ReturnAddress();
      const HRESULT result = ForwardEndingInitWindow([&] { return real3->ResizeBuffers1(count, width, height, format, flags, node_masks, queues); });
      OpenInitWindow(caller);
      return result;
   }

   // IDXGISwapChain4
   HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE type, UINT size, void* metadata) override
   {
      if (std::exchange(is_luma_own_call, false))
      {
         EndInitWindow();
         Scope::Guard scope;
         return real4->SetHDRMetaData(type, size, metadata);
      }
      return ForwardEndingInitWindow([&] { return real4->SetHDRMetaData(type, size, metadata); });
   }

   IDXGISwapChain* GetReal() const { return real; }

   // True the first time: the missing ReShade wrapper is logged once per swapchain
   bool IsFirstMissingWrapperReport() { return !std::exchange(missing_wrapper_logged, true); }

   // The proxy's creation returned: ReShade wraps it and reads it next
   void OpenInitWindowAtCreation()
   {
      if (DXGI::GetReShadeSwapchainView(real) == ReShadeSwapchainView::RealInsideWindows)
         init_window_swapchain = real;
   }

   void EndInitWindow()
   {
      if (init_window_swapchain == real)
         init_window_swapchain = nullptr;
   }

private:
   using PresentFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain4* self, UINT sync_interval, UINT flags);
   using Present1Function = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain4* self, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters);

   template <size_t Copy>
   static HRESULT STDMETHODCALLTYPE OuterPresent(IDXGISwapChain4* self, UINT sync_interval, UINT flags)
   {
      DXGI::CloseThirdPartyWorkWindowAtPresent();
      ProxiedSwapchain* proxy = static_cast<ProxiedSwapchain*>(self);
      proxy->EndInitWindow();
      // Reached again through a tool that called what it hooked (an outer Present): the proxy's own Present
      if (in_outer_present)
         return reinterpret_cast<PresentFunction>(proxy->inner_present)(self, sync_interval, flags);
      outer_copy = uint32_t(Copy);
      proxy->Reclaim(LumaSlot_IDXGISwapChain_Present, GetOuters().present, GetOuters().present_signatures, proxy->next_present);
      in_outer_present = true;
      prologue_done = DXGI::PresentPrologue(proxy->real, sync_interval, flags);
      const HRESULT result = reinterpret_cast<PresentFunction>(proxy->next_present)(self, sync_interval, flags);
      prologue_done.reset();
      in_outer_present = false;
      return result;
   }

   template <size_t Copy>
   static HRESULT STDMETHODCALLTYPE OuterPresent1(IDXGISwapChain4* self, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters)
   {
      DXGI::CloseThirdPartyWorkWindowAtPresent();
      ProxiedSwapchain* proxy = static_cast<ProxiedSwapchain*>(self);
      proxy->EndInitWindow();
      if (in_outer_present)
         return reinterpret_cast<Present1Function>(proxy->inner_present1)(self, sync_interval, flags, parameters);
      outer_copy = uint32_t(Copy) + 100;
      proxy->Reclaim(LumaSlot_IDXGISwapChain1_Present1, GetOuters().present1, GetOuters().present1_signatures, proxy->next_present1);
      in_outer_present = true;
      prologue_done = DXGI::PresentPrologue(proxy->real, sync_interval, flags);
      const HRESULT result = reinterpret_cast<Present1Function>(proxy->next_present1)(self, sync_interval, flags, parameters);
      prologue_done.reset();
      in_outer_present = false;
      return result;
   }

   struct Outers
   {
      std::array<void*, OUTER_COPIES> present;
      std::array<void*, OUTER_COPIES> present1;
      std::array<Signature, OUTER_COPIES> present_signatures;
      std::array<Signature, OUTER_COPIES> present1_signatures;
   };

   template <size_t... Copies>
   static Outers MakeOuters(std::index_sequence<Copies...>)
   {
      Outers outers = {{reinterpret_cast<void*>(&OuterPresent<Copies>)...}, {reinterpret_cast<void*>(&OuterPresent1<Copies>)...}};
      for (size_t i = 0; i < OUTER_COPIES; i++)
      {
         outers.present_signatures[i] = ReadSignature(outers.present[i]);
         outers.present1_signatures[i] = ReadSignature(outers.present1[i]);
      }
      return outers;
   }

   // Made with the first proxy, before any tool saw one
   static const Outers& GetOuters()
   {
      static const Outers outers = MakeOuters(std::make_index_sequence<OUTER_COPIES>());
      return outers;
   }

   // Luma's outer Present in the slot, untouched: a tool that replaced the slot or patched the outer Present there comes after it now (it
   // calls what it hooked, an outer Present, which then goes on to the proxy's own)
   void Reclaim(uint32_t slot, const std::array<void*, OUTER_COPIES>& copies, const std::array<Signature, OUTER_COPIES>& signatures, void*& next)
   {
      const auto is_untouched = [&](size_t i) { return ReadSignature(copies[i]) == signatures[i]; };
      const auto current = std::ranges::find(copies, vtable[slot]);
      if (current != copies.end() && is_untouched(size_t(current - copies.begin())))
         return;
      for (size_t i = 0; i < OUTER_COPIES; i++)
      {
         if (copies[i] != vtable[slot] && copies[i] != next && is_untouched(i))
         {
            next = vtable[slot];
            vtable[slot] = copies[i];
            return;
         }
      }
   }

   std::vector<void*> vtable;
   void* inner_present = nullptr; // The compiler's Present (a tool may have patched its code: it still runs after Luma's outer one)
   void* inner_present1 = nullptr;
   void* next_present = nullptr; // What the outer Present calls: a tool's function, or the inner Present
   void* next_present1 = nullptr;

   // The game's call. A method outside what ReShade's runtime reads when it initialises ends this swapchain's init window.
   template <typename Call>
   auto ForwardEndingInitWindow(Call call)
   {
      EndInitWindow();
      ForwardingDepthGuard forwarding;
      return call();
   }

   template <typename Call>
   static auto ForwardKeepingInitWindow(Call call)
   {
      ForwardingDepthGuard forwarding;
      return call();
   }

   bool IsInReShadeWindow(const void* caller) const
   {
      return DXGI::IsThirdPartyWorkWindowOpenFor(real) || (init_window_swapchain == real && IsCalledFromReShade(caller));
   }

   // A read from ReShade's code (SwapchainProxy::ReShadeSwapchainView): from the real swapchain inside its window, the game's view otherwise,
   // even while Luma calls ReShade's wrapper inside its own scope. Empty when ReShade isn't the caller, or is an overlay here.
   template <typename Call>
   std::optional<std::invoke_result_t<Call>> AnswerReShadeCall(const void* caller, Call call)
   {
      if (!IsReShadeCode(caller))
         return std::nullopt;
      const ReShadeSwapchainView view = DXGI::GetReShadeSwapchainView(real);
      if (view == ReShadeSwapchainView::SameAsOverlay)
         return std::nullopt;
      if (view == ReShadeSwapchainView::RealInsideWindows && IsInReShadeWindow(caller))
      {
         Scope::Guard scope;
         return call();
      }
      Scope::Suspension suspension;
      ForwardingDepthGuard forwarding;
      return call();
   }

   // A resize or color space change returned to ReShade's wrapper, which reads the swapchain again next
   void OpenInitWindow(const void* caller)
   {
      if (IsReShadeCode(caller) && DXGI::GetReShadeSwapchainView(real) == ReShadeSwapchainView::RealInsideWindows)
         init_window_swapchain = real;
   }

   bool missing_wrapper_logged = false;
   std::atomic<ULONG> references = 1;
   IDXGISwapChain* real; // Owned (the reference the creation returned)
   // Weak: the same object
   IDXGISwapChain1* real1 = nullptr;
   IDXGISwapChain2* real2 = nullptr;
   IDXGISwapChain3* real3 = nullptr;
   IDXGISwapChain4* real4 = nullptr;
};

ProxiedSwapchain* FindProxy(IDXGISwapChain* swapchain)
{
   ProxiedSwapchain* proxy = nullptr;
   UINT size = sizeof(proxy);
   if (!swapchain || FAILED(swapchain->GetPrivateData(PROXY_GUID, &size, &proxy)) || size != sizeof(proxy))
      return nullptr;
   return proxy;
}
} // namespace

void Wrap(IDXGISwapChain** swapchain)
{
   if (!swapchain || !*swapchain || FindProxy(*swapchain))
      return;
   ProxiedSwapchain* proxy = new ProxiedSwapchain(*swapchain);
   *swapchain = proxy;
   proxy->OpenInitWindowAtCreation();
}

void Wrap(IDXGISwapChain1** swapchain)
{
   if (!swapchain || !*swapchain || FindProxy(*swapchain))
      return;
   // The real reference moves into the proxy, through its base interface
   IDXGISwapChain* base = nullptr;
   (*swapchain)->QueryInterface(IID_PPV_ARGS(&base));
   (*swapchain)->Release();
   ProxiedSwapchain* proxy = new ProxiedSwapchain(base);
   *swapchain = proxy;
   proxy->OpenInitWindowAtCreation();
}

IDXGISwapChain* Unwrap(IDXGISwapChain* swapchain)
{
   // A proxy's private data is the real swapchain's, which names the proxy: the pointer asked about is the proxy itself, or the real one
   ProxiedSwapchain* proxy = FindProxy(swapchain);
   return proxy && static_cast<IDXGISwapChain*>(static_cast<IDXGISwapChain4*>(proxy)) == swapchain ? proxy->GetReal() : swapchain;
}

bool HasProxy(IDXGISwapChain* swapchain)
{
   return FindProxy(swapchain) != nullptr;
}

bool IsForwarding()
{
   return forwarding_depth != 0;
}

HRESULT ResizeBuffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
   ProxiedSwapchain* proxy = FindProxy(swapchain);
   if (!proxy)
      return swapchain->ResizeBuffers(count, width, height, format, flags);
   is_luma_own_call = true;
   // Through its vtable: a tool's hook on it runs first
   IDXGISwapChain* game_facing = proxy;
   const HRESULT result = game_facing->ResizeBuffers(count, width, height, format, flags);
   is_luma_own_call = false;
   return result;
}

HRESULT SetColorSpace1(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space)
{
   ProxiedSwapchain* proxy = FindProxy(swapchain);
   if (!proxy)
      return swapchain->SetColorSpace1(color_space);
   is_luma_own_call = true;
   IDXGISwapChain4* game_facing = proxy;
   const HRESULT result = game_facing->SetColorSpace1(color_space);
   is_luma_own_call = false;
   return result;
}

HRESULT SetHDRMetaData(IDXGISwapChain4* swapchain, DXGI_HDR_METADATA_TYPE type, UINT size, void* metadata)
{
   ProxiedSwapchain* proxy = FindProxy(swapchain);
   if (!proxy)
      return swapchain->SetHDRMetaData(type, size, metadata);
   is_luma_own_call = true;
   IDXGISwapChain4* game_facing = proxy;
   const HRESULT result = game_facing->SetHDRMetaData(type, size, metadata);
   is_luma_own_call = false;
   return result;
}

void SetReShadeModule(const void* base, size_t size)
{
   reshade_end.store(reinterpret_cast<uintptr_t>(base) + size, std::memory_order_relaxed);
   reshade_begin.store(reinterpret_cast<uintptr_t>(base), std::memory_order_relaxed);
}

void SetReShadeBridged(bool bridged)
{
   reshade_bridged.store(bridged, std::memory_order_relaxed);
}

bool IsReShadeBridged()
{
   return reshade_bridged.load(std::memory_order_relaxed);
}

void CloseReShadeInitWindow()
{
   init_window_swapchain = nullptr;
}

std::optional<HRESULT> ResizeBuffersThroughReShade(IDXGISwapChain* swapchain, const ResizeArguments& game, const ResizeArguments& real,
   DXGI_COLOR_SPACE_TYPE color_space)
{
   ProxiedSwapchain* proxy = FindProxy(swapchain);
   IDXGISwapChain* wrapper = proxy ? FindReShadeWrapper(proxy->GetReal()) : nullptr;
   if (!wrapper)
   {
      if (proxy && proxy->IsFirstMissingWrapperReport())
         Utils::Log::Warning("ReShade: swapchain {} has no ReShade wrapper in its private data (ReShade didn't wrap it, or a ReShade newer than 6.8 keeps it elsewhere): "
                             "Luma doesn't switch its output mode while ReShade is loaded, it keeps presenting the current one",
            static_cast<void*>(proxy->GetReal()));
      return std::nullopt;
   }
   is_luma_own_call = true;
   pending_luma_resize = LumaResizeThroughReShade{real, color_space};
   const HRESULT result = wrapper->ResizeBuffers(game.count, game.width, game.height, game.format, game.flags);
   is_luma_own_call = false;
   pending_luma_resize.reset();
   color_space_answer.reset();
   return result;
}

std::optional<HRESULT> SetColorSpace1ThroughReShade(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE color_space)
{
   ProxiedSwapchain* proxy = FindProxy(swapchain);
   IDXGISwapChain* wrapper = proxy ? FindReShadeWrapper(proxy->GetReal()) : nullptr;
   ComPtr<IDXGISwapChain3> wrapper3;
   if (!wrapper || FAILED(wrapper->QueryInterface(IID_PPV_ARGS(&wrapper3))))
      return std::nullopt;
   // Its reads come once the real swapchain has it
   color_space_answer = ColorSpaceAnswer{proxy->GetReal(), color_space};
   is_luma_own_call = true;
   const HRESULT result = wrapper3->SetColorSpace1(color_space);
   // ReShade answers without calling the proxy when an add-on changed the format: the real swapchain didn't get it
   const bool reached_proxy = !std::exchange(is_luma_own_call, false);
   color_space_answer.reset();
   return reached_proxy ? std::optional(result) : std::nullopt;
}
} // namespace SwapchainProxy
} // namespace Hooks
} // namespace Luma
