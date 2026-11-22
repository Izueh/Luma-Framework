#include <cstdint>
#include <vector>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {
constexpr uint32_t EVENT_COUNT = 256;

void* registered_module = reinterpret_cast<void*>(1);
uint32_t registered_api_version = 0;
void* registered_events[EVENT_COUNT] = {};
void* registered_event_modules[EVENT_COUNT] = {};
uint32_t registered_event_count = 0;

// ReShade 6.8.0's numbers and GUIDs
constexpr uint32_t INIT_EFFECT_RUNTIME_EVENT = 9;
constexpr uint32_t PRESENT_EVENT = 74;
constexpr uint32_t FINISH_PRESENT_EVENT = 100;
constexpr GUID WRAPPER_GUID = {0x1f445f9f, 0x9887, 0x4c4c, {0x90, 0x55, 0x4e, 0x3b, 0xad, 0xaf, 0xcc, 0xa8}};
constexpr GUID COLOR_SPACE_GUID = {0x18b57e4, 0x1493, 0x4953, {0xad, 0xf2, 0xde, 0x6d, 0x99, 0xcc, 0x5, 0xe5}};
constexpr int32_t D3D11_API = 0xb000;
constexpr int32_t D3D12_API = 0xc000;

// The effect runtime switched off in its config ([GENERAL] Disable=1): the wrapper reads nothing and fires no init_effect_runtime
bool runtime_enabled = true;

// ReShade's api::device and api::swapchain, as its present event hands them out
struct ApiDevice
{
   virtual uint64_t GetNative() const { return 0; }
   virtual void GetPrivateData(const uint8_t[16], uint64_t* data) const { *data = 0; }
   virtual void SetPrivateData(const uint8_t[16], const uint64_t) {}
   virtual int32_t GetApi() const { return api; }

   int32_t api = D3D11_API;
};

struct ApiSwapchain
{
   virtual uint64_t GetNative() const { return reinterpret_cast<uint64_t>(native); }
   virtual void GetPrivateData(const uint8_t[16], uint64_t* data) const { *data = 0; }
   virtual void SetPrivateData(const uint8_t[16], const uint64_t) {}
   virtual ApiDevice* GetDevice() { return device; }

   ApiDevice* device = nullptr;
   IDXGISwapChain* native = nullptr;
};

// ReShade's DXGISwapChain as far as Luma meets it: every call forwarded from this dll to what it wraps (the game's proxy), its runtime's
// reads when it initialises (in functions of their own, as ReShade's swapchain_impl), the effects' back buffer views, its reset in
// ResizeBuffers and SetColorSpace1, its present event and D3D12's index read at present
struct Wrapper final : IDXGISwapChain4
{
   ULONG references = 1;
   IDXGISwapChain4* orig = nullptr;
   bool d3d12 = false;
   DXGI_FORMAT orig_format = DXGI_FORMAT_UNKNOWN; // The game's format, as ReShade's _orig_desc
   ApiDevice api_device;
   ApiSwapchain api_swapchain;
   bool initialized = false;
   // What its runtime holds and read
   std::vector<ComPtr<IUnknown>> buffers;
   ComPtr<ID3D11RenderTargetView> view;
   DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RESERVED;
   uint32_t inits = 0;
   UINT present_index = UINT_MAX;
   IUnknown* present_buffer = nullptr;
   UINT resize_count = 0;
   DXGI_FORMAT resize_format = DXGI_FORMAT_UNKNOWN;
   uint32_t game_get_buffer_calls = 0;
   uint32_t probe_reads = 0;

   __declspec(noinline) void ReadD3D11()
   {
      ComPtr<ID3D11Texture2D> buffer;
      if (FAILED(orig->GetBuffer(0, IID_PPV_ARGS(&buffer))))
         return;
      ComPtr<ID3D11Device> device;
      buffer->GetDevice(&device);
      device->CreateRenderTargetView(buffer.Get(), nullptr, &view);
      buffers.push_back(buffer);
      DXGI_SWAP_CHAIN_DESC desc = {};
      orig->GetDesc(&desc);
   }

   __declspec(noinline) void ReadD3D12()
   {
      DXGI_SWAP_CHAIN_DESC desc = {};
      orig->GetDesc(&desc);
      for (UINT i = 0; i < desc.BufferCount; i++)
      {
         ComPtr<ID3D12Resource> buffer;
         if (SUCCEEDED(orig->GetBuffer(i, IID_PPV_ARGS(&buffer))))
            buffers.push_back(buffer);
      }
      HWND window = nullptr;
      orig->GetHwnd(&window);
   }

   // The first back buffer, read from this dll as its runtime reads it
   __declspec(noinline) IUnknown* ReadFirstBuffer()
   {
      ComPtr<IUnknown> buffer;
      if (d3d12)
      {
         ComPtr<ID3D12Resource> resource;
         orig->GetBuffer(0, IID_PPV_ARGS(&resource));
         buffer = resource;
      }
      else
      {
         ComPtr<ID3D11Texture2D> texture;
         orig->GetBuffer(0, IID_PPV_ARGS(&texture));
         buffer = texture;
      }
      return buffer.Get();
   }

   // What a read of ReShade's runtime gets now. The count after the read keeps it from being a tail call.
   __declspec(noinline) IUnknown* ProbeRead()
   {
      IUnknown* buffer = ReadFirstBuffer();
      probe_reads++;
      return buffer;
   }

   __declspec(noinline) void ReadColorSpace()
   {
      DXGI_COLOR_SPACE_TYPE read = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
      UINT size = sizeof(read);
      orig->GetPrivateData(COLOR_SPACE_GUID, &size, &read);
      color_space = read;
   }

   __declspec(noinline) void OnInit()
   {
      BOOL fullscreen = FALSE;
      orig->GetFullscreenState(&fullscreen, nullptr);
      if (!runtime_enabled)
         return;
      if (d3d12)
         ReadD3D12();
      else
         ReadD3D11();
      ReadColorSpace();
      initialized = true;
      inits++;
      if (void* callback = registered_events[INIT_EFFECT_RUNTIME_EVENT])
         reinterpret_cast<void (*)(void*)>(callback)(this);
   }

   void OnReset()
   {
      view.Reset();
      buffers.clear();
      present_buffer = nullptr;
      initialized = false;
   }

   __declspec(noinline) void OnPresent()
   {
      if (void* callback = registered_events[PRESENT_EVENT])
         reinterpret_cast<void (*)(void*, ApiSwapchain*, const void*, const void*, uint32_t, const void*)>(callback)(nullptr, &api_swapchain, nullptr, nullptr, 0, nullptr);
      if (!initialized || !d3d12)
         return;
      present_index = orig->GetCurrentBackBufferIndex();
      present_buffer = present_index < buffers.size() ? buffers[present_index].Get() : nullptr;
   }

   // IUnknown
   HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
   {
      if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) || riid == __uuidof(IDXGISwapChain) ||
         riid == __uuidof(IDXGISwapChain1) || riid == __uuidof(IDXGISwapChain2) || riid == __uuidof(IDXGISwapChain3) || riid == __uuidof(IDXGISwapChain4))
      {
         AddRef();
         *object = static_cast<IDXGISwapChain4*>(this);
         return S_OK;
      }
      return orig->QueryInterface(riid, object);
   }

   ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }

   ULONG STDMETHODCALLTYPE Release() override
   {
      const ULONG count = --references;
      if (count == 0)
      {
         OnReset();
         orig->SetPrivateData(WRAPPER_GUID, 0, nullptr);
         orig->Release();
         delete this;
      }
      return count;
   }

   // IDXGIObject
   HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name, UINT size, const void* data) override { return orig->SetPrivateData(name, size, data); }
   HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name, const IUnknown* unknown) override { return orig->SetPrivateDataInterface(name, unknown); }
   HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name, UINT* size, void* data) override { return orig->GetPrivateData(name, size, data); }
   HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** parent) override { return orig->GetParent(riid, parent); }
   HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** device) override { return orig->GetDevice(riid, device); }

   // IDXGISwapChain
   HRESULT STDMETHODCALLTYPE Present(UINT sync_interval, UINT flags) override
   {
      OnPresent();
      const HRESULT result = orig->Present(sync_interval, flags);
      if (void* callback = registered_events[FINISH_PRESENT_EVENT])
         reinterpret_cast<void (*)(void*, ApiSwapchain*)>(callback)(nullptr, &api_swapchain);
      return result;
   }

   // The game's call, forwarded: work after it keeps it from being a tail call, so the return address is this dll's
   HRESULT STDMETHODCALLTYPE GetBuffer(UINT index, REFIID riid, void** buffer) override
   {
      const HRESULT result = orig->GetBuffer(index, riid, buffer);
      game_get_buffer_calls++;
      return result;
   }

   HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen, IDXGIOutput* output) override { return orig->SetFullscreenState(fullscreen, output); }
   HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fullscreen, IDXGIOutput** output) override { return orig->GetFullscreenState(fullscreen, output); }

   HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* desc) override { return orig->GetDesc(desc); }

   HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) override
   {
      OnReset();
      DXGI_SWAP_CHAIN_DESC desc = {};
      orig->GetDesc(&desc);
      if (format == DXGI_FORMAT_UNKNOWN)
         format = orig_format;
      orig_format = format;
      resize_count = count;
      resize_format = format;
      const HRESULT result = orig->ResizeBuffers(count, width, height, format, flags);
      if (SUCCEEDED(result) || result == DXGI_ERROR_INVALID_CALL)
         OnInit();
      return result;
   }

   HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* mode) override { return orig->ResizeTarget(mode); }
   HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** output) override { return orig->GetContainingOutput(output); }
   HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* statistics) override { return orig->GetFrameStatistics(statistics); }
   HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* count) override { return orig->GetLastPresentCount(count); }

   // IDXGISwapChain1
   HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* desc) override { return orig->GetDesc1(desc); }
   HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* desc) override { return orig->GetFullscreenDesc(desc); }
   HRESULT STDMETHODCALLTYPE GetHwnd(HWND* window) override { return orig->GetHwnd(window); }
   HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID riid, void** window) override { return orig->GetCoreWindow(riid, window); }

   HRESULT STDMETHODCALLTYPE Present1(UINT sync_interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters) override
   {
      OnPresent();
      return orig->Present1(sync_interval, flags, parameters);
   }

   BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return orig->IsTemporaryMonoSupported(); }
   HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** output) override { return orig->GetRestrictToOutput(output); }
   HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* color) override { return orig->SetBackgroundColor(color); }
   HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* color) override { return orig->GetBackgroundColor(color); }
   HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation) override { return orig->SetRotation(rotation); }
   HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* rotation) override { return orig->GetRotation(rotation); }

   // IDXGISwapChain2
   HRESULT STDMETHODCALLTYPE SetSourceSize(UINT width, UINT height) override { return orig->SetSourceSize(width, height); }
   HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* width, UINT* height) override { return orig->GetSourceSize(width, height); }
   HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT latency) override { return orig->SetMaximumFrameLatency(latency); }
   HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* latency) override { return orig->GetMaximumFrameLatency(latency); }
   HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return orig->GetFrameLatencyWaitableObject(); }
   HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* matrix) override { return orig->SetMatrixTransform(matrix); }
   HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* matrix) override { return orig->GetMatrixTransform(matrix); }

   // IDXGISwapChain3
   UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return orig->GetCurrentBackBufferIndex(); }

   HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE color_space_asked, UINT* support) override
   {
      return orig->CheckColorSpaceSupport(color_space_asked, support);
   }

   // As ReShade's: skipped when the format isn't the game's, a reset when the value changes
   HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE color_space_set) override
   {
      DXGI_SWAP_CHAIN_DESC desc = {};
      orig->GetDesc(&desc);
      if (desc.BufferDesc.Format != orig_format)
         return S_OK;
      DXGI_COLOR_SPACE_TYPE previous = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
      UINT size = sizeof(previous);
      orig->GetPrivateData(COLOR_SPACE_GUID, &size, &previous);
      if (color_space_set != previous)
         OnReset();
      const HRESULT result = orig->SetColorSpace1(color_space_set);
      if (SUCCEEDED(result))
         orig->SetPrivateData(COLOR_SPACE_GUID, sizeof(color_space_set), &color_space_set);
      if (color_space_set != previous)
         OnInit();
      return result;
   }

   HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags, const UINT* node_masks, IUnknown* const* queues) override
   {
      OnReset();
      const HRESULT result = orig->ResizeBuffers1(count, width, height, format, flags, node_masks, queues);
      if (SUCCEEDED(result) || result == DXGI_ERROR_INVALID_CALL)
         OnInit();
      return result;
   }

   // IDXGISwapChain4
   HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE, UINT, void*) override { return S_OK; }
};
} // namespace

// ReShade's exports, with its names and signatures
extern "C" __declspec(dllexport) const char* ReShadeVersion = "6.8.0";

extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void* module, uint32_t api_version)
{
   registered_module = module;
   registered_api_version = api_version;
   return true;
}

// Not through the exports: Luma hooks those
static void StoreEvent(uint32_t event, void* callback)
{
   if (event < EVENT_COUNT)
      registered_events[event] = callback;
   registered_event_count++;
}

extern "C" __declspec(dllexport) void ReShadeRegisterEvent(uint32_t event, void* callback)
{
   StoreEvent(event, callback);
}

// The by-add-on forms: the fake keeps one handler per event, so the module is only recorded
extern "C" __declspec(dllexport) void ReShadeRegisterEventForAddon(void* module, uint32_t event, void* callback)
{
   registered_event_modules[event < EVENT_COUNT ? event : 0] = module;
   StoreEvent(event, callback);
}

static void RemoveEvent(uint32_t event, void* callback)
{
   if (event < EVENT_COUNT && registered_events[event] == callback)
      registered_events[event] = nullptr;
}

extern "C" __declspec(dllexport) void ReShadeUnregisterEvent(uint32_t event, void* callback)
{
   RemoveEvent(event, callback);
}

extern "C" __declspec(dllexport) void ReShadeUnregisterEventForAddon(void*, uint32_t event, void* callback)
{
   RemoveEvent(event, callback);
}

extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void* module)
{
   for (uint32_t event = 0; event < EVENT_COUNT; event++)
   {
      if (registered_event_modules[event] == module)
      {
         registered_events[event] = nullptr;
         registered_event_modules[event] = nullptr;
      }
   }
}

// An add-on's handler for the tests (this dll isn't Luma's): runs `FakeReShadeSetHandlerBody`'s function with the arguments it got, and returns its result
using HandlerBody = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
HandlerBody handler_body = nullptr;

extern "C" __declspec(dllexport) uint64_t FakeReShadeAddonHandler(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
{
   return handler_body ? handler_body(a0, a1, a2, a3, a4, a5, a6, a7, a8) : 0;
}

extern "C" __declspec(dllexport) void FakeReShadeSetHandlerBody(void* body)
{
   handler_body = reinterpret_cast<HandlerBody>(body);
}

extern "C" __declspec(dllexport) void* FakeReShadeGetEventModule(uint32_t event)
{
   return registered_event_modules[event < EVENT_COUNT ? event : 0];
}

// What the tests read back
extern "C" __declspec(dllexport) void* FakeReShadeGetModule()
{
   return registered_module;
}

extern "C" __declspec(dllexport) uint32_t FakeReShadeGetApiVersion()
{
   return registered_api_version;
}

extern "C" __declspec(dllexport) void* FakeReShadeGetEvent(uint32_t event)
{
   return event < EVENT_COUNT ? registered_events[event] : nullptr;
}

extern "C" __declspec(dllexport) uint32_t FakeReShadeGetEventCount()
{
   return registered_event_count;
}

extern "C" __declspec(dllexport) void FakeReShadeSetRuntimeEnabled(bool enabled)
{
   runtime_enabled = enabled;
}

// ReShade's swapchain creation hook: the wrapper of what the creation returned (the game's proxy), in its private data, then its runtime
// initialises. The game gets the wrapper.
extern "C" __declspec(dllexport) IDXGISwapChain* FakeReShadeWrap(IDXGISwapChain* swapchain, bool d3d12)
{
   Wrapper* wrapper = new Wrapper();
   if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&wrapper->orig))))
   {
      delete wrapper;
      return nullptr;
   }
   wrapper->d3d12 = d3d12;
   wrapper->api_device.api = d3d12 ? D3D12_API : D3D11_API;
   wrapper->api_swapchain.device = &wrapper->api_device;
   wrapper->api_swapchain.native = wrapper->orig;
   DXGI_SWAP_CHAIN_DESC desc = {};
   wrapper->orig->GetDesc(&desc);
   wrapper->orig_format = desc.BufferDesc.Format;
   Wrapper* self = wrapper;
   wrapper->orig->SetPrivateData(WRAPPER_GUID, sizeof(self), &self);
   wrapper->OnInit();
   return wrapper;
}

extern "C" __declspec(dllexport) IUnknown* FakeReShadeGetHeldBuffer(IDXGISwapChain* wrapper, uint32_t index)
{
   const Wrapper* fake = static_cast<Wrapper*>(static_cast<IDXGISwapChain4*>(wrapper));
   return index < fake->buffers.size() ? fake->buffers[index].Get() : nullptr;
}

extern "C" __declspec(dllexport) uint32_t FakeReShadeGetHeldCount(IDXGISwapChain* wrapper)
{
   return uint32_t(static_cast<Wrapper*>(static_cast<IDXGISwapChain4*>(wrapper))->buffers.size());
}

// What its runtime read: the color space, how many times it initialised, the index and buffer at the last present, what its
// ResizeBuffers passed on, the game's GetBuffer calls through it
extern "C" __declspec(dllexport) uint32_t FakeReShadeGetColorSpace(IDXGISwapChain* wrapper)
{
   return uint32_t(static_cast<Wrapper*>(static_cast<IDXGISwapChain4*>(wrapper))->color_space);
}

extern "C" __declspec(dllexport) uint32_t FakeReShadeGetInits(IDXGISwapChain* wrapper)
{
   return static_cast<Wrapper*>(static_cast<IDXGISwapChain4*>(wrapper))->inits;
}

extern "C" __declspec(dllexport) uint32_t FakeReShadeGetPresentIndex(IDXGISwapChain* wrapper)
{
   return static_cast<Wrapper*>(static_cast<IDXGISwapChain4*>(wrapper))->present_index;
}

extern "C" __declspec(dllexport) IUnknown* FakeReShadeGetPresentBuffer(IDXGISwapChain* wrapper)
{
   return static_cast<Wrapper*>(static_cast<IDXGISwapChain4*>(wrapper))->present_buffer;
}

extern "C" __declspec(dllexport) uint32_t FakeReShadeGetResizeFormat(IDXGISwapChain* wrapper)
{
   return uint32_t(static_cast<Wrapper*>(static_cast<IDXGISwapChain4*>(wrapper))->resize_format);
}

// The first back buffer a read from this dll gets now, as its runtime would read it (not owned)
extern "C" __declspec(dllexport) IUnknown* FakeReShadeProbeRead(IDXGISwapChain* wrapper)
{
   return static_cast<Wrapper*>(static_cast<IDXGISwapChain4*>(wrapper))->ProbeRead();
}

extern "C" __declspec(dllexport) uint32_t FakeReShadeGetGameBufferCalls(IDXGISwapChain* wrapper)
{
   return static_cast<Wrapper*>(static_cast<IDXGISwapChain4*>(wrapper))->game_get_buffer_calls;
}
