#include "Hooks/DXGI/DXGISwapchainProxy.hpp"

#include <array>
#include <atomic>
#include <cstdint>

#include <intrin.h>

#include "Hooks/DeviceProxy.hpp"
#include "Hooks/ThirdPartyModules.hpp"

namespace Luma {
namespace Hooks {
namespace SwapchainProxy {
namespace {
// On the real swapchain: the proxy the game holds (weak), cleared when the proxy goes
constexpr GUID PROXY_GUID = {0x4c554d41, 0x5357, 0x5052, {0x4f, 0x58, 0x59, 0x00, 0x00, 0x00, 0x00, 0x01}};

thread_local uint32_t forwarding_depth = 0;
// Every proxy's, set by the first
std::atomic<const void*> proxy_vtable = nullptr;

struct Forwarding
{
   Forwarding() { forwarding_depth++; }

   ~Forwarding() { forwarding_depth--; }

   Forwarding(const Forwarding&) = delete;
   Forwarding& operator=(const Forwarding&) = delete;
};

// Only the interfaces the real swapchain has are the proxy's: a game asking for IDXGISwapChain4 on an older DXGI must fail
bool IsProxied(REFIID riid)
{
   return riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) || riid == __uuidof(IDXGISwapChain) ||
      riid == __uuidof(IDXGISwapChain1) || riid == __uuidof(IDXGISwapChain2) || riid == __uuidof(IDXGISwapChain3) || riid == __uuidof(IDXGISwapChain4);
}

// A COM interface needs its virtuals. Every method forwards the game's call to the real swapchain through its vtable, so Luma's hooks and
// every other tool's see it, marked as the game's.
class Proxy final : public IDXGISwapChain4
{
public:
   explicit Proxy(IDXGISwapChain* real) : real(real)
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
      Proxy* self = this;
      real->SetPrivateData(PROXY_GUID, sizeof(self), &self);
      proxy_vtable = *reinterpret_cast<void* const*>(static_cast<IDXGISwapChain4*>(this));
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
   HRESULT STDMETHODCALLTYPE Present(UINT sync_interval, UINT flags) override { return DXGI::PresentForGame(real, sync_interval, flags, nullptr, false); }

   // An overlay that hooked the proxy (Steam hooks the swapchain the game holds) calls these from its Present, before the proxy's: it gets
   // the real buffers, composed first. Nothing is hooked between it and the proxy, so the return address is its own.
   HRESULT STDMETHODCALLTYPE GetBuffer(UINT index, REFIID riid, void** buffer) override
   {
      if (ThirdPartyModules::FindBase(_ReturnAddress()))
      {
         DXGI::OnOverlayBufferRequest(real);
         return real->GetBuffer(index, riid, buffer);
      }
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
      return DXGI::PresentForGame(real, sync_interval, flags, parameters, true);
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
      if (ThirdPartyModules::FindBase(_ReturnAddress()))
      {
         DXGI::OnOverlayBufferRequest(real);
         return real3->GetCurrentBackBufferIndex();
      }
      return Forward([&] { return real3->GetCurrentBackBufferIndex(); });
   }

   HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space, UINT* support) override
   {
      return Forward([&] { return real3->CheckColorSpaceSupport(color_space, support); });
   }

   HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE color_space) override
   {
      return Forward([&] { return real3->SetColorSpace1(color_space); });
   }

   HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags, const UINT* node_masks, IUnknown* const* queues) override
   {
      return Forward([&] { return real3->ResizeBuffers1(count, width, height, format, flags, node_masks, queues); });
   }

   // IDXGISwapChain4
   HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE type, UINT size, void* metadata) override
   {
      return Forward([&] { return real4->SetHDRMetaData(type, size, metadata); });
   }

   IDXGISwapChain* GetReal() const { return real; }

private:
   template <typename Call>
   static auto Forward(Call call)
   {
      Forwarding forwarding;
      return call();
   }

   std::atomic<ULONG> references = 1;
   IDXGISwapChain* real; // Owned (the reference the creation returned)
   // Weak: the same object
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
   // The real reference moves into the proxy, through its base interface
   IDXGISwapChain* base = nullptr;
   (*swapchain)->QueryInterface(IID_PPV_ARGS(&base));
   (*swapchain)->Release();
   *swapchain = new Proxy(base);
}

IDXGISwapChain* Unwrap(IDXGISwapChain* swapchain)
{
   // A proxy's private data is the real swapchain's: its vtable tells it apart. Every interface of it is the same pointer.
   if (swapchain && proxy_vtable && *reinterpret_cast<void* const*>(swapchain) == proxy_vtable)
      return static_cast<Proxy*>(static_cast<IDXGISwapChain4*>(swapchain))->GetReal();
   return swapchain;
}

bool HasProxy(IDXGISwapChain* swapchain)
{
   return FindProxy(swapchain) != nullptr;
}

bool IsForwarding()
{
   return forwarding_depth != 0;
}
} // namespace SwapchainProxy
} // namespace Hooks
} // namespace Luma
