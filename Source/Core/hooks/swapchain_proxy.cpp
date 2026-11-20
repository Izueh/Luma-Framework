#include "hooks/swapchain_proxy.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>
#include <atomic>
#include <cstdint>

#include <intrin.h>

#include "hooks/scope.hpp"
#include "hooks/slots.h"

namespace Luma {
namespace Hooks {
namespace SwapchainProxy {
namespace {
constexpr GUID PROXY_GUID = {0x4c554d41, 0x5357, 0x5052, {0x4f, 0x58, 0x59, 0x00, 0x00, 0x00, 0x00, 0x01}};

thread_local uint32_t forwarding_depth = 0;
thread_local bool luma_call = false;

// Copies of the outer Present: when a tool patches the one in the slot, the slot moves to an untouched copy
constexpr size_t OUTER_COPIES = 4;
constexpr size_t SIGNATURE_SIZE = 16;
using Signature = std::array<uint8_t, SIGNATURE_SIZE>;

Signature ReadSignature(const void* function)
{
   Signature signature;
   std::memcpy(signature.data(), function, SIGNATURE_SIZE);
   return signature;
}

// The outer Present ran the prologue on this thread
thread_local std::optional<DXGI::Prologue> prologue_done;
thread_local bool in_outer_present = false;
// Each copy writes its own number, so the linker can't fold them
thread_local uint32_t outer_copy = 0;

struct Forwarding
{
   Forwarding() { forwarding_depth++; }

   ~Forwarding() { forwarding_depth--; }

   Forwarding(const Forwarding&) = delete;
   Forwarding& operator=(const Forwarding&) = delete;
};

bool IsProxied(REFIID riid)
{
   return riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) || riid == __uuidof(IDXGISwapChain) ||
      riid == __uuidof(IDXGISwapChain1) || riid == __uuidof(IDXGISwapChain2) || riid == __uuidof(IDXGISwapChain3) || riid == __uuidof(IDXGISwapChain4);
}

// Every method forwards to the real swapchain through its vtable, marked as the game's
class Proxy final : public IDXGISwapChain4
{
public:
   explicit Proxy(IDXGISwapChain* real) : real(real)
   {
      real->QueryInterface(IID_PPV_ARGS(&real1));
      real->QueryInterface(IID_PPV_ARGS(&real2));
      real->QueryInterface(IID_PPV_ARGS(&real3));
      real->QueryInterface(IID_PPV_ARGS(&real4));
      const std::array<IUnknown*, 4> versions = {real1, real2, real3, real4};
      for (IUnknown* version : versions)
      {
         if (version)
            version->Release();
      }
      Proxy* self = this;
      real->SetPrivateData(PROXY_GUID, sizeof(self), &self);
      // Its own vtable, with the outer Present in the Present slots
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
      Forwarding forwarding;
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
      return Forward([&] { return real->SetPrivateData(name, size, data); });
   }

   HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name, const IUnknown* unknown) override
   {
      return Forward([&] { return real->SetPrivateDataInterface(name, unknown); });
   }

   HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name, UINT* size, void* data) override
   {
      return Forward([&] { return real->GetPrivateData(name, size, data); });
   }

   HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** parent) override
   {
      return Forward([&] { return real->GetParent(riid, parent); });
   }

   // IDXGIDeviceSubObject
   HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** device) override
   {
      return Forward([&] { return real->GetDevice(riid, device); });
   }

   // IDXGISwapChain
   HRESULT STDMETHODCALLTYPE Present(UINT sync_interval, UINT flags) override
   {
      return DXGI::PresentForGame(real, sync_interval, flags, nullptr, false, std::exchange(prologue_done, std::nullopt));
   }

   HRESULT STDMETHODCALLTYPE GetBuffer(UINT index, REFIID riid, void** buffer) override
   {
      return Forward([&] { return real->GetBuffer(index, riid, buffer); });
   }

   HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen, IDXGIOutput* output) override
   {
      return Forward([&] { return real->SetFullscreenState(fullscreen, output); });
   }

   HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fullscreen, IDXGIOutput** output) override
   {
      return Forward([&] { return real->GetFullscreenState(fullscreen, output); });
   }

   HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* desc) override
   {
      return Forward([&] { return real->GetDesc(desc); });
   }

   HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) override
   {
      if (luma_call)
      {
         Scope::Guard scope;
         return real->ResizeBuffers(count, width, height, format, flags);
      }
      return Forward([&] { return real->ResizeBuffers(count, width, height, format, flags); });
   }

   HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* mode) override
   {
      return Forward([&] { return real->ResizeTarget(mode); });
   }

   HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** output) override
   {
      return Forward([&] { return real->GetContainingOutput(output); });
   }

   HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* statistics) override
   {
      return Forward([&] { return real->GetFrameStatistics(statistics); });
   }

   HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* count) override
   {
      return Forward([&] { return real->GetLastPresentCount(count); });
   }

   // IDXGISwapChain1
   HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* desc) override
   {
      return Forward([&] { return real1->GetDesc1(desc); });
   }

   HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* desc) override
   {
      return Forward([&] { return real1->GetFullscreenDesc(desc); });
   }

   HRESULT STDMETHODCALLTYPE GetHwnd(HWND* window) override
   {
      return Forward([&] { return real1->GetHwnd(window); });
   }

   HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID riid, void** window) override
   {
      return Forward([&] { return real1->GetCoreWindow(riid, window); });
   }

   HRESULT STDMETHODCALLTYPE Present1(UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters) override
   {
      return DXGI::PresentForGame(real, sync_interval, flags, parameters, true, std::exchange(prologue_done, std::nullopt));
   }

   BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override
   {
      return Forward([&] { return real1->IsTemporaryMonoSupported(); });
   }

   HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** output) override
   {
      return Forward([&] { return real1->GetRestrictToOutput(output); });
   }

   HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* color) override
   {
      return Forward([&] { return real1->SetBackgroundColor(color); });
   }

   HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* color) override
   {
      return Forward([&] { return real1->GetBackgroundColor(color); });
   }

   HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation) override
   {
      return Forward([&] { return real1->SetRotation(rotation); });
   }

   HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* rotation) override
   {
      return Forward([&] { return real1->GetRotation(rotation); });
   }

   // IDXGISwapChain2
   HRESULT STDMETHODCALLTYPE SetSourceSize(UINT width, UINT height) override
   {
      return Forward([&] { return real2->SetSourceSize(width, height); });
   }

   HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* width, UINT* height) override
   {
      return Forward([&] { return real2->GetSourceSize(width, height); });
   }

   HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT latency) override
   {
      return Forward([&] { return real2->SetMaximumFrameLatency(latency); });
   }

   HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* latency) override
   {
      return Forward([&] { return real2->GetMaximumFrameLatency(latency); });
   }

   HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override
   {
      return Forward([&] { return real2->GetFrameLatencyWaitableObject(); });
   }

   HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* matrix) override
   {
      return Forward([&] { return real2->SetMatrixTransform(matrix); });
   }

   HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* matrix) override
   {
      return Forward([&] { return real2->GetMatrixTransform(matrix); });
   }

   // IDXGISwapChain3
   UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override
   {
      return Forward([&] { return real3->GetCurrentBackBufferIndex(); });
   }

   HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space, UINT* support) override
   {
      return Forward([&] { return real3->CheckColorSpaceSupport(color_space, support); });
   }

   HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE color_space) override
   {
      if (luma_call)
      {
         Scope::Guard scope;
         return real3->SetColorSpace1(color_space);
      }
      return Forward([&] { return real3->SetColorSpace1(color_space); });
   }

   HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags, const UINT* node_masks, IUnknown* const* queues) override
   {
      return Forward([&] { return real3->ResizeBuffers1(count, width, height, format, flags, node_masks, queues); });
   }

   // IDXGISwapChain4
   HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE type, UINT size, void* metadata) override
   {
      if (luma_call)
      {
         Scope::Guard scope;
         return real4->SetHDRMetaData(type, size, metadata);
      }
      return Forward([&] { return real4->SetHDRMetaData(type, size, metadata); });
   }

   IDXGISwapChain* GetReal() const { return real; }

private:
   using PresentFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain4* self, UINT sync_interval, UINT flags);
   using Present1Function = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain4* self, UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters);

   template <size_t Copy>
   static HRESULT STDMETHODCALLTYPE OuterPresent(IDXGISwapChain4* self, UINT sync_interval, UINT flags)
   {
      DXGI::EnterPresent();
      Proxy* proxy = static_cast<Proxy*>(self);
      // Reached again through a tool's hook: the proxy's own Present
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
      DXGI::EnterPresent();
      Proxy* proxy = static_cast<Proxy*>(self);
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

   static const Outers& GetOuters()
   {
      static const Outers outers = MakeOuters(std::make_index_sequence<OUTER_COPIES>());
      return outers;
   }

   // Puts an untouched outer Present back in the slot when a tool replaced or patched it, so the tool comes after Luma
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
   void* inner_present = nullptr; // The proxy's own Present
   void* inner_present1 = nullptr;
   void* next_present = nullptr; // What the outer Present calls: a tool's function, or the proxy's own
   void* next_present1 = nullptr;

   template <typename Call>
   static auto Forward(Call call)
   {
      Forwarding forwarding;
      return call();
   }

   std::atomic<ULONG> references = 1;
   IDXGISwapChain* real; // Owned
   IDXGISwapChain1* real1 = nullptr;
   IDXGISwapChain2* real2 = nullptr;
   IDXGISwapChain3* real3 = nullptr;
   IDXGISwapChain4* real4 = nullptr;
};

Proxy* FindProxy(IDXGISwapChain* swapchain)
{
   Proxy* proxy = nullptr;
   UINT size = sizeof(proxy);
   if (!swapchain || FAILED(swapchain->GetPrivateData(PROXY_GUID, &size, &proxy)) || size != sizeof(proxy))
      return nullptr;
   return proxy;
}
} // namespace

void Wrap(IDXGISwapChain** swapchain)
{
   if (swapchain && *swapchain && !FindProxy(*swapchain))
      *swapchain = new Proxy(*swapchain);
}

void Wrap(IDXGISwapChain1** swapchain)
{
   if (!swapchain || !*swapchain || FindProxy(*swapchain))
      return;
   IDXGISwapChain* base = nullptr;
   (*swapchain)->QueryInterface(IID_PPV_ARGS(&base));
   (*swapchain)->Release();
   *swapchain = new Proxy(base);
}

IDXGISwapChain* Unwrap(IDXGISwapChain* swapchain)
{
   Proxy* proxy = FindProxy(swapchain);
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
   Proxy* proxy = FindProxy(swapchain);
   if (!proxy)
      return swapchain->ResizeBuffers(count, width, height, format, flags);
   luma_call = true;
   IDXGISwapChain* game_facing = proxy;
   const HRESULT result = game_facing->ResizeBuffers(count, width, height, format, flags);
   luma_call = false;
   return result;
}

HRESULT SetColorSpace1(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space)
{
   Proxy* proxy = FindProxy(swapchain);
   if (!proxy)
      return swapchain->SetColorSpace1(color_space);
   luma_call = true;
   IDXGISwapChain4* game_facing = proxy;
   const HRESULT result = game_facing->SetColorSpace1(color_space);
   luma_call = false;
   return result;
}

HRESULT SetHDRMetaData(IDXGISwapChain4* swapchain, DXGI_HDR_METADATA_TYPE type, UINT size, void* metadata)
{
   Proxy* proxy = FindProxy(swapchain);
   if (!proxy)
      return swapchain->SetHDRMetaData(type, size, metadata);
   luma_call = true;
   IDXGISwapChain4* game_facing = proxy;
   const HRESULT result = game_facing->SetHDRMetaData(type, size, metadata);
   luma_call = false;
   return result;
}
} // namespace SwapchainProxy
} // namespace Hooks
} // namespace Luma
