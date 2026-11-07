#include <doctest/doctest.h>

#include <chrono>
#include <functional>
#include <thread>
#include <variant>
#include <vector>

#include <d3d11.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "game/game.hpp"
#include "hooks/hooks.hpp"
#include "hooks/lifetime.hpp"
#include "hooks/device_proxy.hpp"
#include "hooks/scope.hpp"
#include "settings/registry.hpp"
#include "trace/info.hpp"
#include "ui/input.hpp"
#include "ui/overlay.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;
using namespace Luma;

namespace {
void InstallHooks()
{
   static const bool installed = [] {
      LoadLibraryW(L"dxgi.dll");
      LoadLibraryW(L"d3d11.dll");
      LoadLibraryW(L"d3d12.dll");
      Hooks::InstallDXGI();
      Hooks::InstallD3D11();
      Hooks::InstallD3D12();
      return true;
   }();
   static_cast<void>(installed);
}

// Destroy flags of the test's objects, set by their lifetime trackers
bool destroyed[4] = {};

// Swapchains Luma knows already have a tracker
template <size_t Index>
void TrackDestruction(IUnknown* object)
{
   destroyed[Index] = false;
   REQUIRE(Hooks::Lifetime::Track(object, [](const void*) { destroyed[Index] = true; }));
}

// Swapchains need a window, but the tests don't need to show one
struct HiddenWindow
{
   HWND hwnd = nullptr;

   HiddenWindow()
   {
      static const ATOM window_class = [] {
         const WNDCLASSW desc = {.lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(nullptr), .lpszClassName = L"LumaTests"};
         return RegisterClassW(&desc);
      }();
      hwnd = CreateWindowW(MAKEINTATOM(window_class), L"Luma tests", WS_OVERLAPPEDWINDOW, 0, 0, 640, 360, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
      REQUIRE(hwnd);
   }
   ~HiddenWindow() { DestroyWindow(hwnd); }
};

// The D3D11 hooks go on the vtables of the process's first device, so every test uses the same kind (with the debug
// layer when it's installed, Windows "Graphics Tools")
UINT GetD3D11Flags()
{
   static const UINT flags = [] {
      if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, nullptr, nullptr, nullptr)))
         return UINT(D3D11_CREATE_DEVICE_DEBUG);
      MESSAGE("D3D11 debug layer not installed, running without it");
      return UINT(0);
   }();
   return flags;
}

void CreateD3D11Device(ComPtr<ID3D11Device>& device, ComPtr<ID3D11DeviceContext>& immediate_context)
{
   REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, GetD3D11Flags(), nullptr, 0, D3D11_SDK_VERSION, &device, nullptr,
      &immediate_context)));
}

// D3D12 devices are shared per adapter, and the debug layer can only be enabled before the process's first one
ComPtr<ID3D12Device> CreateD3D12Device()
{
   static const bool debug_layer = [] {
      ComPtr<ID3D12Debug> debug;
      if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
      {
         MESSAGE("D3D12 debug layer not installed, running without it");
         return false;
      }
      debug->EnableDebugLayer();
      return true;
   }();
   static_cast<void>(debug_layer);
   ComPtr<IDXGIFactory4> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   ComPtr<IDXGIAdapter> adapter;
   REQUIRE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
   ComPtr<ID3D12Device> device;
   REQUIRE(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
   return device;
}

template <typename T>
T* Real(const ComPtr<T>& proxy)
{
   return static_cast<T*>(Hooks::DeviceProxy::Unwrap(proxy.Get()));
}

bool WaitFor(const std::function<bool()>& condition)
{
   const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
   while (!condition())
   {
      if (std::chrono::steady_clock::now() > end)
         return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   return true;
}

ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device)
{
   const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_UPLOAD};
   const D3D12_RESOURCE_DESC desc = {
      .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Width = 256,
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .SampleDesc = {1, 0},
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
   };
   ComPtr<ID3D12Resource> buffer;
   REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer))));
   return buffer;
}

ComPtr<ID3D11Buffer> CreateBuffer(ID3D11Device* device)
{
   const D3D11_BUFFER_DESC desc = {.ByteWidth = 256, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
   ComPtr<ID3D11Buffer> buffer;
   REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &buffer)));
   return buffer;
}

template <typename Message, typename InfoQueue>
uint64_t CountErrors(InfoQueue* info_queue, int error_severity)
{
   uint64_t errors = 0;
   for (uint64_t i = 0; info_queue && i < info_queue->GetNumStoredMessages(); i++)
   {
      SIZE_T size = 0;
      info_queue->GetMessage(i, nullptr, &size);
      std::vector<uint8_t> storage(size);
      auto* message = reinterpret_cast<Message*>(storage.data());
      info_queue->GetMessage(i, message, &size);
      if (int(message->Severity) <= error_severity)
      {
         MESSAGE(message->pDescription);
         errors++;
      }
   }
   return errors;
}

// A game whose callbacks create an object on the device and release it when the device is destroyed
uint32_t created_calls = 0;
uint32_t destroyed_calls = 0;
uint32_t swapchain_created_calls = 0;
uint32_t devices_known_at_swapchain = 0;
uint32_t swapchain_resized_calls = 0;
uint32_t present_calls = 0;
const void* callback_device = nullptr;
const void* callback_presenter = nullptr;
bool device_usable_when_destroyed = false;
ComPtr<IUnknown> game_object;

void OnDeviceCreated(ID3D12Device* device)
{
   created_calls++;
   callback_device = device;
   ComPtr<ID3D12Fence> fence;
   device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
   game_object = fence;
}

void OnDeviceCreated(ID3D11Device* device)
{
   created_calls++;
   callback_device = device;
   game_object = CreateBuffer(device);
}

void OnDeviceDestroyed(ID3D12Device* device)
{
   destroyed_calls++;
   device_usable_when_destroyed = device == callback_device && device->GetNodeCount() == 1;
   game_object.Reset();
}

void OnDeviceDestroyed(ID3D11Device* device)
{
   destroyed_calls++;
   device_usable_when_destroyed = device == callback_device && device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_9_1;
   game_object.Reset();
}

template <typename Presenter>
void OnSwapchainCreated(IDXGISwapChain*, Presenter* presenter)
{
   swapchain_created_calls++;
   devices_known_at_swapchain = created_calls;
   callback_presenter = presenter;
}

template <typename Presenter>
void OnSwapchainResized(IDXGISwapChain*, Presenter* presenter)
{
   swapchain_resized_calls++;
   callback_presenter = presenter;
}

template <typename Presenter>
void OnPresent(IDXGISwapChain*, Presenter* presenter)
{
   present_calls++;
   callback_presenter = presenter;
}

struct GameCallbacks
{
   GameCallbacks()
   {
      Game::Reset();
      Game::Callbacks::SetOnDeviceCreated(static_cast<void (*)(ID3D11Device*)>(&OnDeviceCreated));
      Game::Callbacks::SetOnDeviceCreated(static_cast<void (*)(ID3D12Device*)>(&OnDeviceCreated));
      Game::Callbacks::SetOnDeviceDestroyed(static_cast<void (*)(ID3D11Device*)>(&OnDeviceDestroyed));
      Game::Callbacks::SetOnDeviceDestroyed(static_cast<void (*)(ID3D12Device*)>(&OnDeviceDestroyed));
      Game::Callbacks::SetOnSwapchainCreated(&OnSwapchainCreated<ID3D11Device>);
      Game::Callbacks::SetOnSwapchainCreated(&OnSwapchainCreated<ID3D12CommandQueue>);
      Game::Callbacks::SetOnSwapchainResized(&OnSwapchainResized<ID3D11Device>);
      Game::Callbacks::SetOnSwapchainResized(&OnSwapchainResized<ID3D12CommandQueue>);
      Game::Callbacks::SetOnPresent(&OnPresent<ID3D11Device>);
      Game::Callbacks::SetOnPresent(&OnPresent<ID3D12CommandQueue>);
      created_calls = 0;
      destroyed_calls = 0;
      swapchain_created_calls = 0;
      devices_known_at_swapchain = 0;
      swapchain_resized_calls = 0;
      present_calls = 0;
      callback_device = nullptr;
      callback_presenter = nullptr;
      device_usable_when_destroyed = false;
   }
   ~GameCallbacks()
   {
      game_object.Reset();
      Game::Reset();
   }
};
} // namespace

TEST_CASE("Device proxy D3D12: the game holds a proxy, and its last release lets everything on the device go")
{
   InstallHooks();
   GameCallbacks game;
   ComPtr<ID3D12Device> device = CreateD3D12Device();
   CHECK(Hooks::DeviceProxy::IsProxy(device.Get()));
   ID3D12Device* real = Real(device);
   REQUIRE(real != device.Get());
   CHECK(created_calls == 1);
   CHECK(callback_device == real);
   TrackDestruction<0>(real);

   // COM identity, and every device version is the proxy
   ComPtr<IUnknown> identity;
   REQUIRE(SUCCEEDED(device.As(&identity)));
   CHECK(identity.Get() == device.Get());
   identity.Reset();
   ComPtr<ID3D12Device1> device1;
   REQUIRE(SUCCEEDED(device.As(&device1)));
   CHECK(device1.Get() == device.Get());
   device1.Reset();

   // Calls go through to the device, where the hooks see the real one
   const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   ComPtr<ID3D12CommandQueue> queue;
   REQUIRE(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));
   {
      Hooks::Scope::Guard scope;
      ComPtr<ID3D12Device> queue_device;
      REQUIRE(SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&queue_device))));
      CHECK(queue_device.Get() == real);
   }

   // Children hand out the proxy, whatever their GetDevice function
   ComPtr<ID3D12CommandAllocator> allocator;
   REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
   ComPtr<ID3D12GraphicsCommandList> list;
   REQUIRE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
   ComPtr<ID3D12Resource> buffer = CreateBuffer(device.Get());
   for (ID3D12DeviceChild* child : {static_cast<ID3D12DeviceChild*>(queue.Get()), static_cast<ID3D12DeviceChild*>(allocator.Get()),
           static_cast<ID3D12DeviceChild*>(list.Get()), static_cast<ID3D12DeviceChild*>(buffer.Get())})
   {
      ComPtr<ID3D12Device> from_child;
      REQUIRE(SUCCEEDED(child->GetDevice(IID_PPV_ARGS(&from_child))));
      CHECK(from_child.Get() == device.Get());
      // Luma's own code gets the real one
      Hooks::Scope::Guard scope;
      REQUIRE(SUCCEEDED(child->GetDevice(IID_PPV_ARGS(&from_child))));
      CHECK(from_child.Get() == real);
   }

   // The game callback's fence doesn't keep the device from the game's release
   list->Close();
   list.Reset();
   allocator.Reset();
   buffer.Reset();
   queue.Reset();
   CHECK(device.Reset() == 0);
   CHECK(destroyed_calls == 1);
   CHECK(device_usable_when_destroyed);
   CHECK(Hooks::DeviceProxy::GetCount() == 0);
   CHECK(destroyed[0]);
}

TEST_CASE("Device proxy D3D12: a device released before its children is released for the game at once")
{
   InstallHooks();
   GameCallbacks game;
   ComPtr<ID3D12Device> device = CreateD3D12Device();
   ID3D12Device* real = Real(device);
   TrackDestruction<0>(real);
   const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   ComPtr<ID3D12CommandQueue> queue;
   REQUIRE(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));

   device.Reset();
   CHECK(destroyed_calls == 1);
   // The game's queue still holds the device
   CHECK_FALSE(destroyed[0]);
   queue.Reset();
   CHECK(destroyed[0]);
}

TEST_CASE("Device proxy D3D12: creating the device of an adapter again returns its proxy")
{
   InstallHooks();
   GameCallbacks game;
   ComPtr<ID3D12Device> first = CreateD3D12Device();
   ComPtr<ID3D12Device> second = CreateD3D12Device();
   CHECK(first.Get() == second.Get());
   CHECK(created_calls == 1);
   first.Reset();
   CHECK(destroyed_calls == 0);
   second.Reset();
   CHECK(destroyed_calls == 1);

   // After the release, the next device is a new one to Luma
   ComPtr<ID3D12Device> third = CreateD3D12Device();
   CHECK(created_calls == 2);
   CHECK(Hooks::DeviceProxy::GetCount() == 1);
   third.Reset();
   CHECK(destroyed_calls == 2);
}

TEST_CASE("Device proxy D3D11: contexts, children and IDXGIDevice hand out the proxy, and IDXGIDevice keeps it alive")
{
   InstallHooks();
   GameCallbacks game;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   CreateD3D11Device(device, immediate_context);
   CHECK(Hooks::DeviceProxy::IsProxy(device.Get()));
   ID3D11Device* real = Real(device);
   CHECK(created_calls == 1);
   CHECK(callback_device == real);

   ComPtr<ID3D11Device> from_context;
   immediate_context->GetDevice(&from_context);
   CHECK(from_context.Get() == device.Get());
   ComPtr<ID3D11Buffer> buffer = CreateBuffer(device.Get());
   ComPtr<ID3D11Device> from_buffer;
   buffer->GetDevice(&from_buffer);
   CHECK(from_buffer.Get() == device.Get());
   {
      Hooks::Scope::Guard scope;
      ComPtr<ID3D11Device> real_from_buffer;
      buffer->GetDevice(&real_from_buffer);
      CHECK(real_from_buffer.Get() == real);
   }

   // IDXGIDevice is another interface of the same object, with the same count
   ComPtr<IDXGIDevice> dxgi_device;
   REQUIRE(SUCCEEDED(device.As(&dxgi_device)));
   CHECK(Hooks::DeviceProxy::IsProxy(dxgi_device.Get()));
   CHECK(static_cast<IUnknown*>(dxgi_device.Get()) != static_cast<IUnknown*>(device.Get()));
   ComPtr<IDXGIAdapter> adapter;
   CHECK(SUCCEEDED(dxgi_device->GetAdapter(&adapter)));
   adapter.Reset();
   ComPtr<ID3D11Device> from_dxgi;
   REQUIRE(SUCCEEDED(dxgi_device.As(&from_dxgi)));
   CHECK(from_dxgi.Get() == device.Get());

   from_dxgi.Reset();
   from_buffer.Reset();
   from_context.Reset();
   buffer.Reset();
   immediate_context.Reset();
   device.Reset();
   CHECK(destroyed_calls == 0);
   dxgi_device.Reset();
   CHECK(destroyed_calls == 1);
   CHECK(device_usable_when_destroyed);
   CHECK(Hooks::DeviceProxy::GetCount() == 0);
}

TEST_CASE("D3D11: work still reads objects released right after it was recorded (so D3D11 never needs deferred release)")
{
   InstallHooks();
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   CreateD3D11Device(device, immediate_context);

   const auto create_filled = [&](uint32_t value) {
      std::vector<uint32_t> data(64, value);
      const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data.data()};
      const D3D11_BUFFER_DESC desc = {.ByteWidth = 256, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
      ComPtr<ID3D11Buffer> buffer;
      REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &initial, &buffer)));
      return buffer;
   };
   const auto read_first = [&](ID3D11Buffer* buffer) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = 256, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
      ComPtr<ID3D11Buffer> staging;
      REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &staging)));
      immediate_context->CopyResource(staging.Get(), buffer);
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      REQUIRE(SUCCEEDED(immediate_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
      const uint32_t value = *static_cast<const uint32_t*>(mapped.pData);
      immediate_context->Unmap(staging.Get(), 0);
      return value;
   };
   ComPtr<ID3D11Buffer> destination = create_filled(0);

   // Work on the immediate context, its source released before it was flushed
   immediate_context->CopyResource(destination.Get(), create_filled(1).Get());
   CHECK(read_first(destination.Get()) == 1);

   // Work recorded in a command list, its source released before it was executed
   ComPtr<ID3D11DeviceContext> deferred_context;
   REQUIRE(SUCCEEDED(device->CreateDeferredContext(0, &deferred_context)));
   deferred_context->CopyResource(destination.Get(), create_filled(2).Get());
   ComPtr<ID3D11CommandList> command_list;
   REQUIRE(SUCCEEDED(deferred_context->FinishCommandList(FALSE, &command_list)));
   immediate_context->ExecuteCommandList(command_list.Get(), FALSE);
   command_list.Reset();
   CHECK(read_first(destination.Get()) == 2);

   ComPtr<ID3D11InfoQueue> info_queue;
   if (SUCCEEDED(device.As(&info_queue)))
      CHECK(CountErrors<D3D11_MESSAGE>(info_queue.Get(), D3D11_MESSAGE_SEVERITY_ERROR) == 0);
}

TEST_CASE("Swapchain D3D11: created with the device, presented and resized")
{
   InstallHooks();
   GameCallbacks game;
   HiddenWindow window;
   const DXGI_SWAP_CHAIN_DESC desc = {
      .BufferDesc = {.Width = 320, .Height = 180, .Format = DXGI_FORMAT_R8G8B8A8_UNORM},
      .SampleDesc = {1, 0},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 2,
      .OutputWindow = window.hwnd,
      .Windowed = TRUE,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
   };
   ComPtr<IDXGISwapChain> swapchain;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   // D3D11CreateDeviceAndSwapChain creates the swapchain before Luma knows the device: the game hears of the device first,
   // and once of the swapchain
   REQUIRE(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, GetD3D11Flags(), nullptr, 0, D3D11_SDK_VERSION, &desc, &swapchain, &device,
      nullptr, &immediate_context)));
   CHECK(created_calls == 1);
   CHECK(swapchain_created_calls == 1);
   CHECK(devices_known_at_swapchain == 1);
   CHECK(callback_presenter == Real(device));

   // The swapchain hands out the proxy too
   ComPtr<ID3D11Device> from_swapchain;
   REQUIRE(SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&from_swapchain))));
   CHECK(from_swapchain.Get() == device.Get());
   from_swapchain.Reset();

   // DXGI_PRESENT_TEST doesn't count
   swapchain->Present(0, DXGI_PRESENT_TEST);
   CHECK(present_calls == 0);
   swapchain->Present(0, 0);
   CHECK(present_calls == 1);

   REQUIRE(SUCCEEDED(swapchain->ResizeBuffers(0, 400, 200, DXGI_FORMAT_UNKNOWN, 0)));
   CHECK(swapchain_resized_calls == 1);

   swapchain.Reset();
   immediate_context.Reset();
   device.Reset();
   CHECK(destroyed_calls == 1);
}

TEST_CASE("Swapchain D3D11: created by a factory with the device's proxy")
{
   InstallHooks();
   GameCallbacks game;
   HiddenWindow window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   CreateD3D11Device(device, immediate_context);
   ID3D11Device* real = Real(device);
   ComPtr<IDXGIFactory2> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   const DXGI_SWAP_CHAIN_DESC1 desc = {
      .Width = 320,
      .Height = 180,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {1, 0},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 2,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
   };
   ComPtr<IDXGISwapChain1> swapchain;
   REQUIRE(SUCCEEDED(factory->CreateSwapChainForHwnd(device.Get(), window.hwnd, &desc, nullptr, nullptr, &swapchain)));
   CHECK(swapchain_created_calls == 1);
   CHECK(callback_presenter == real);
   swapchain->Present(0, 0);
   CHECK(present_calls == 1);

   // The game releases the device first: the swapchain still holds the real one, but the game no longer hears of it
   immediate_context.Reset();
   device.Reset();
   CHECK(destroyed_calls == 1);
   swapchain->Present(0, 0);
   CHECK(present_calls == 1);
}

// Run these two alone too ("-tc=..."): in a process of their own, the factory device creation made internally is the only
// one that can resolve the factory's vtable
namespace {
ComPtr<IDXGIFactory> GetFactoryOfDevice(const ComPtr<ID3D11Device>& device)
{
   ComPtr<IDXGIDevice> dxgi_device;
   REQUIRE(SUCCEEDED(device.As(&dxgi_device)));
   ComPtr<IDXGIAdapter> adapter;
   REQUIRE(SUCCEEDED(dxgi_device->GetAdapter(&adapter)));
   ComPtr<IDXGIFactory> factory;
   REQUIRE(SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))));
   return factory;
}

DXGI_SWAP_CHAIN_DESC GetWindowedDesc(HWND hwnd)
{
   return {
      .BufferDesc = {.Width = 320, .Height = 180, .Format = DXGI_FORMAT_R8G8B8A8_UNORM},
      .SampleDesc = {1, 0},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 2,
      .OutputWindow = hwnd,
      .Windowed = TRUE,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
   };
}
} // namespace

TEST_CASE("Swapchain D3D11: created by the factory of D3D11CreateDevice's device")
{
   InstallHooks();
   GameCallbacks game;
   HiddenWindow window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   CreateD3D11Device(device, immediate_context);
   DXGI_SWAP_CHAIN_DESC desc = GetWindowedDesc(window.hwnd);
   ComPtr<IDXGISwapChain> swapchain;
   REQUIRE(SUCCEEDED(GetFactoryOfDevice(device)->CreateSwapChain(device.Get(), &desc, &swapchain)));
   CHECK(swapchain_created_calls == 1);
   CHECK(callback_presenter == Real(device));
}

TEST_CASE("Swapchain D3D11: a second swapchain, by the factory of D3D11CreateDeviceAndSwapChain's device")
{
   InstallHooks();
   GameCallbacks game;
   HiddenWindow window;
   HiddenWindow second_window;
   const DXGI_SWAP_CHAIN_DESC first_desc = GetWindowedDesc(window.hwnd);
   ComPtr<IDXGISwapChain> first;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   REQUIRE(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, GetD3D11Flags(), nullptr, 0, D3D11_SDK_VERSION, &first_desc, &first, &device,
      nullptr, &immediate_context)));
   CHECK(swapchain_created_calls == 1);
   DXGI_SWAP_CHAIN_DESC second_desc = GetWindowedDesc(second_window.hwnd);
   ComPtr<IDXGISwapChain> second;
   REQUIRE(SUCCEEDED(GetFactoryOfDevice(device)->CreateSwapChain(device.Get(), &second_desc, &second)));
   CHECK(swapchain_created_calls == 2);
}

TEST_CASE("Swapchain D3D12: created on a queue, presented and resized")
{
   InstallHooks();
   GameCallbacks game;
   HiddenWindow window;
   ComPtr<ID3D12Device> device = CreateD3D12Device();
   ComPtr<IDXGIFactory4> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   ComPtr<ID3D12CommandQueue> queue;
   REQUIRE(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));

   const DXGI_SWAP_CHAIN_DESC1 desc = {
      .Width = 320,
      .Height = 180,
      .Format = DXGI_FORMAT_R10G10B10A2_UNORM,
      .SampleDesc = {1, 0},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 3,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
   };
   ComPtr<IDXGISwapChain1> swapchain1;
   REQUIRE(SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(), window.hwnd, &desc, nullptr, nullptr, &swapchain1)));
   ComPtr<IDXGISwapChain3> swapchain;
   REQUIRE(SUCCEEDED(swapchain1.As(&swapchain)));
   swapchain1.Reset();
   CHECK(swapchain_created_calls == 1);
   CHECK(callback_presenter == queue.Get());

   swapchain->Present(0, 0);
   const DXGI_PRESENT_PARAMETERS parameters = {};
   swapchain->Present1(0, 0, &parameters);
   CHECK(present_calls == 2);

   // Back buffers must be idle before a resize
   ComPtr<ID3D12Fence> fence;
   REQUIRE(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
   queue->Signal(fence.Get(), 1);
   CHECK(WaitFor([&] { return fence->GetCompletedValue() >= 1; }));
   REQUIRE(SUCCEEDED(swapchain->ResizeBuffers(2, 640, 360, DXGI_FORMAT_UNKNOWN, 0)));
   CHECK(swapchain_resized_calls == 1);

   if constexpr (Trace::ENABLED)
   {
      // Trace has its own copy, updated by the resize
      const Trace::Status status = Trace::GetStatus();
      CHECK(status.api == "D3D12");
      REQUIRE(status.swapchains.size() == 1);
      CHECK(status.swapchains[0].width == 640);
      CHECK(status.swapchains[0].buffers == 2);
      CHECK(status.swapchains[0].format == uint32_t(DXGI_FORMAT_R10G10B10A2_UNORM));
      CHECK(status.frame_count >= 2);
      CHECK(status.ms_since_present >= 0);
   }

   queue->Signal(fence.Get(), 2);
   CHECK(WaitFor([&] { return fence->GetCompletedValue() >= 2; }));
   swapchain.Reset();
   if constexpr (Trace::ENABLED)
      CHECK(Trace::GetStatus().swapchains.empty());
   fence.Reset();
   queue.Reset();
   device.Reset();
   CHECK(destroyed_calls == 1);
}

namespace {
// A game window whose procedure counts the input it receives
uint32_t game_keys = 0;
uint32_t game_mouse = 0;

LRESULT CALLBACK GameWindowProcedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
   if (message == WM_KEYDOWN)
      game_keys++;
   if (message == WM_MOUSEMOVE)
      game_mouse++;
   return DefWindowProcW(hwnd, message, wparam, lparam);
}

struct GameWindow
{
   HWND hwnd = nullptr;

   GameWindow()
   {
      static const ATOM window_class = [] {
         const WNDCLASSW desc = {.lpfnWndProc = GameWindowProcedure, .hInstance = GetModuleHandleW(nullptr), .lpszClassName = L"LumaGameTests"};
         return RegisterClassW(&desc);
      }();
      hwnd = CreateWindowW(MAKEINTATOM(window_class), L"Luma game tests", WS_OVERLAPPEDWINDOW, 0, 0, 640, 360, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
      REQUIRE(hwnd);
      game_keys = 0;
      game_mouse = 0;
   }
   ~GameWindow() { DestroyWindow(hwnd); }

   void Key(WPARAM key) { SendMessageW(hwnd, WM_KEYDOWN, key, 0); }
   void Mouse() { SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10)); }
   // ImGui captures the mouse on the press and releases it on the release, which sends WM_CAPTURECHANGED back into the
   // window procedure on the same thread, while the overlay handles the release
   void Click()
   {
      SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
      SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
   }
   bool IsSubclassed() const { return GetWindowLongPtrW(hwnd, GWLP_WNDPROC) != reinterpret_cast<LONG_PTR>(&GameWindowProcedure); }
};

// Other tests clear the settings registry
void InitializeOverlay()
{
   if (!Settings::Find(UI::Input::TOGGLE_KEY_SETTING))
      UI::Input::Initialize();
   Settings::Reset(UI::Input::TOGGLE_KEY_SETTING);
   UI::SetOpen(false);
}
} // namespace

TEST_CASE("Overlay D3D11: attaches to the game's swapchain, toggles, blocks input while open and draws")
{
   InstallHooks();
   InitializeOverlay();
   GameWindow window;
   const DXGI_SWAP_CHAIN_DESC desc = {
      .BufferDesc = {.Width = 320, .Height = 180, .Format = DXGI_FORMAT_R8G8B8A8_UNORM},
      .SampleDesc = {1, 0},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 2,
      .OutputWindow = window.hwnd,
      .Windowed = TRUE,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
   };
   ComPtr<IDXGISwapChain> swapchain;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   REQUIRE(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, GetD3D11Flags(), nullptr, 0, D3D11_SDK_VERSION, &desc, &swapchain, &device,
      nullptr, &immediate_context)));
   ComPtr<ID3D11InfoQueue> info_queue;
   device.As(&info_queue);

   // Attached when the swapchain is created, closed
   CHECK(UI::GetSwapchain() == swapchain.Get());
   swapchain->Present(0, 0);
   CHECK(window.IsSubclassed());
   window.Key('A');
   window.Mouse();
   CHECK(game_keys == 1);
   CHECK(game_mouse == 1);

   // The toggle key opens it, and the game doesn't see it or anything else while it's open
   window.Key(VK_INSERT);
   CHECK(UI::IsOpen());
   window.Key('A');
   window.Mouse();
   window.Click();
   CHECK(game_keys == 1);
   CHECK(game_mouse == 1);
   for (int i = 0; i < 3; i++)
      CHECK(SUCCEEDED(swapchain->Present(0, 0)));

   // The game's state is restored after drawing
   ComPtr<ID3D11Buffer> constants = CreateBuffer(device.Get());
   ID3D11Buffer* bound[] = {constants.Get()};
   immediate_context->VSSetConstantBuffers(0, 1, bound);
   swapchain->Present(0, 0);
   ComPtr<ID3D11Buffer> still_bound;
   immediate_context->VSGetConstantBuffers(0, 1, &still_bound);
   CHECK(still_bound.Get() == constants.Get());
   still_bound.Reset();
   immediate_context->ClearState();

   // The overlay holds no back buffer reference, so the game can resize while it's open
   CHECK(SUCCEEDED(swapchain->ResizeBuffers(0, 400, 200, DXGI_FORMAT_UNKNOWN, 0)));
   CHECK(SUCCEEDED(swapchain->Present(0, 0)));

   // A captured key becomes the toggle key (applied at the next present), Insert is then just a key for the game
   UI::Input::CaptureToggleKey();
   window.Key(VK_F2);
   swapchain->Present(0, 0);
   CHECK(Settings::Get<int32_t>(UI::Input::TOGGLE_KEY_SETTING) == VK_F2);
   window.Key(VK_F2);
   CHECK_FALSE(UI::IsOpen());
   window.Key(VK_INSERT);
   CHECK(game_keys == 2);
   CHECK_FALSE(UI::IsOpen());

   if (info_queue)
      CHECK(CountErrors<D3D11_MESSAGE>(info_queue.Get(), D3D11_MESSAGE_SEVERITY_ERROR) == 0);
   info_queue.Reset();

   // The game releases its device before the swapchain (which holds the real device on D3D11): the overlay goes with the
   // device, and the window gets its procedure back
   still_bound.Reset();
   constants.Reset();
   immediate_context.Reset();
   device.Reset();
   CHECK_FALSE(UI::GetSwapchain());
   CHECK_FALSE(window.IsSubclassed());
   swapchain->Present(0, 0);
   CHECK_FALSE(UI::GetSwapchain());
   swapchain.Reset();
   Settings::Reset(UI::Input::TOGGLE_KEY_SETTING);
}

TEST_CASE("Overlay D3D12: draws on the swapchain's queue, survives resizes, and goes with the swapchain")
{
   InstallHooks();
   InitializeOverlay();
   GameWindow window;
   ComPtr<ID3D12Device> device = CreateD3D12Device();
   ComPtr<ID3D12InfoQueue> info_queue;
   device.As(&info_queue);
   ComPtr<IDXGIFactory4> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   ComPtr<ID3D12CommandQueue> queue;
   REQUIRE(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));
   const DXGI_SWAP_CHAIN_DESC1 desc = {
      .Width = 320,
      .Height = 180,
      .Format = DXGI_FORMAT_R10G10B10A2_UNORM,
      .SampleDesc = {1, 0},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 3,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
   };
   ComPtr<IDXGISwapChain1> swapchain;
   REQUIRE(SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(), window.hwnd, &desc, nullptr, nullptr, &swapchain)));

   CHECK(UI::GetSwapchain() == swapchain.Get());
   swapchain->Present(0, 0);
   window.Key(VK_INSERT);
   REQUIRE(UI::IsOpen());
   for (int i = 0; i < 5; i++)
      CHECK(SUCCEEDED(swapchain->Present(0, 0)));

   // The game waits for its queue before resizing: the overlay's work on it is done too
   ComPtr<ID3D12Fence> fence;
   REQUIRE(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
   queue->Signal(fence.Get(), 1);
   CHECK(WaitFor([&] { return fence->GetCompletedValue() >= 1; }));
   CHECK(SUCCEEDED(swapchain->ResizeBuffers(2, 640, 360, DXGI_FORMAT_R16G16B16A16_FLOAT, 0)));
   for (int i = 0; i < 3; i++)
      CHECK(SUCCEEDED(swapchain->Present(0, 0)));
   window.Key(VK_INSERT);
   CHECK_FALSE(UI::IsOpen());

   if (info_queue)
      CHECK(CountErrors<D3D12_MESSAGE>(info_queue.Get(), D3D12_MESSAGE_SEVERITY_ERROR) == 0);
   info_queue.Reset();

   // Detached with the swapchain. (A D3D12 swapchain holds its device: DXGI asks the queue for it, and gets the proxy.)
   queue->Signal(fence.Get(), 2);
   CHECK(WaitFor([&] { return fence->GetCompletedValue() >= 2; }));
   swapchain.Reset();
   CHECK_FALSE(UI::GetSwapchain());
   CHECK_FALSE(window.IsSubclassed());
   fence.Reset();
   queue.Reset();
   device.Reset();
   CHECK(Hooks::DeviceProxy::GetCount() == 0);
}

#include "validation/d3d11_validation.hpp"
#include "validation/d3d12_validation.hpp"
#include "validation/validation.hpp"

TEST_CASE("Validation: the debug layers' messages of the game's devices are read and counted")
{
   InstallHooks();
   Validation::Reset();

   ComPtr<ID3D11Device> d3d11_device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   CreateD3D11Device(d3d11_device, immediate_context);
   if (GetD3D11Flags() & D3D11_CREATE_DEVICE_DEBUG)
   {
      // A buffer of 0 bytes is an error for the debug layer
      const D3D11_BUFFER_DESC desc = {.ByteWidth = 0, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      ComPtr<ID3D11Buffer> buffer;
      CHECK(FAILED(d3d11_device->CreateBuffer(&desc, nullptr, &buffer)));
      Validation::D3D11::ReadMessages();
      CHECK(Validation::GetCounts().errors >= 1);
      // Read once
      const uint64_t errors = Validation::GetCounts().errors;
      Validation::D3D11::ReadMessages();
      CHECK(Validation::GetCounts().errors == errors);
   }
   immediate_context.Reset();
   d3d11_device.Reset();

   Validation::Reset();
   ComPtr<ID3D12Device> d3d12_device = CreateD3D12Device();
   ComPtr<ID3D12InfoQueue> info_queue;
   if (SUCCEEDED(d3d12_device.As(&info_queue)))
   {
      // A committed resource without size is an error for the debug layer
      const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
      const D3D12_RESOURCE_DESC desc = {
         .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
         .Width = 0,
         .Height = 1,
         .DepthOrArraySize = 1,
         .MipLevels = 1,
         .SampleDesc = {1, 0},
         .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
      };
      ComPtr<ID3D12Resource> resource;
      CHECK(FAILED(d3d12_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource))));
      // Read when the game releases the device, at the latest
      info_queue.Reset();
      d3d12_device.Reset();
      CHECK(Validation::GetCounts().errors >= 1);
   }
   Validation::Reset();
}
