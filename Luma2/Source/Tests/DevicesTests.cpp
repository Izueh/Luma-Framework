#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <thread>
#include <variant>
#include <vector>

#include <d3d11.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "FrameState/FrameState.hpp"
#include "Game/GameRegistry.hpp"
#include "Hooks/HookManager.hpp"
#include "Hooks/InputHooks.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "Hooks/HookScope.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Shaders/ShaderStore.hpp"
#include "Trace/RuntimeStatus.hpp"
#include "UI/OverlayInput.hpp"
#include "UI/DXGI/DXGIOverlay.hpp"
#include "UI/Overlay.hpp"
#include "Validation/D3D11/D3D11Validation.hpp"
#include "Validation/D3D12/D3D12Validation.hpp"
#include "Validation/Validation.hpp"
#include "Handlers/DXGI/DXGIHandlers.hpp"
#include "Display/DXGI/DXGISwapchain.hpp"
#include "Display/Swapchain.hpp"
#include "ReShade/AddonEvents.hpp"
#include "ReShade/ReShade.hpp"
#include "ReShade/ReShadeABI.hpp"
#include "Draw/D3D11/D3D11Draw.hpp"
#include "Draw/Draw.hpp"
#include "Hooks/DXGI/DXGISwapchainProxy.hpp"
#include "Hooks/ThirdPartyModules.hpp"
#include "System/Modules.hpp"
#include "Log/Log.hpp"
#include "Hooks/DXGI/DXGIHooks.hpp"

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
      // What Core requests for itself
      Hooks::D3D11::Functions functions;
      // The bound-state cache's viewports, for the ReShade tests
      functions.set(size_t(Hooks::D3D11::Function::RSSetViewports));
      // The in-place spoof (a no-op while no rules ask for it)
      functions.set(size_t(Hooks::D3D11::Function::GetTextureDesc));
      Hooks::InstallDXGI(Hooks::DXGI::Functions().set());
      Hooks::InstallD3D11(functions);
      // The in-place spoof (a no-op while no rules ask for it)
      Hooks::D3D12::Functions d3d12_functions;
      d3d12_functions.set(size_t(Hooks::D3D12::Function::GetResourceDesc));
      Hooks::InstallD3D12(d3d12_functions);
      // The overlay sees the game's input on its message queue
      Hooks::Input::Install();
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
   REQUIRE(Hooks::Lifetime::Track(object, nullptr, [](IUnknown*, const IUnknown*) { destroyed[Index] = true; }));
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
uint32_t swapchain_pre_create_calls = 0;
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

void OnCreateSwapchain(Game::SwapchainDesc&)
{
   swapchain_pre_create_calls++;
}

template <typename Presenter>
void OnInitSwapchain(IDXGISwapChain*, Presenter* presenter, bool resize)
{
   if (resize)
   {
      swapchain_resized_calls++;
      return;
   }
   swapchain_created_calls++;
   devices_known_at_swapchain = created_calls;
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
      Game::Registry::Reset();
      Game::SetEvents(Game::D3D11::Events{
         .on_init_device = static_cast<void (*)(ID3D11Device*)>(&OnDeviceCreated),
         .on_destroy_device = static_cast<void (*)(ID3D11Device*)>(&OnDeviceDestroyed),
         .on_create_swapchain = &OnCreateSwapchain,
         .on_init_swapchain = &OnInitSwapchain<ID3D11Device>,
         .on_present = &OnPresent<ID3D11Device>,
      });
      Game::SetEvents(Game::D3D12::Events{
         .on_init_device = static_cast<void (*)(ID3D12Device*)>(&OnDeviceCreated),
         .on_destroy_device = static_cast<void (*)(ID3D12Device*)>(&OnDeviceDestroyed),
         .on_create_swapchain = &OnCreateSwapchain,
         .on_init_swapchain = &OnInitSwapchain<ID3D12CommandQueue>,
         .on_present = &OnPresent<ID3D12CommandQueue>,
      });
      created_calls = 0;
      destroyed_calls = 0;
      swapchain_pre_create_calls = 0;
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
      Game::Registry::Reset();
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

TEST_CASE("Hooks D3D11: GetDesc of a texture upgraded in place gives the game the format it asked for, and Luma the real one")
{
   InstallHooks();
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   CreateD3D11Device(device, immediate_context);
   // The spoof is on by default
   Resources::Upgrade::SetRules({.method = Resources::Upgrade::Method::InPlace, .formats = {DXGI_FORMAT_R11G11B10_FLOAT}});

   struct Cleanup
   {
      ~Cleanup() { Resources::Upgrade::SetRules({}); }
   } cleanup;

   const D3D11_TEXTURE2D_DESC desc = {.Width = 8, .Height = 8, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
   ComPtr<ID3D11Texture2D> texture;
   ComPtr<ID3D11Texture2D> plain;
   {
      Hooks::Scope::Guard scope;
      REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)));
      REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &plain)));
   }
   // As if the rules had also doubled its size: the game asked for 4x4
   const Resources::Upgrade::TextureInfo game = {.width = 4, .height = 4, .format = DXGI_FORMAT_R11G11B10_FLOAT, .render_target = true};
   Resources::Upgrade::TextureInfo upgraded = game;
   upgraded.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
   Resources::Mirrors::AttachInPlace(static_cast<ID3D11Resource*>(texture.Get()), game, upgraded);

   D3D11_TEXTURE2D_DESC seen = {};
   texture->GetDesc(&seen);
   CHECK(seen.Format == DXGI_FORMAT_R11G11B10_FLOAT);
   CHECK(seen.Width == 4);
   CHECK(seen.Height == 4);
   ComPtr<ID3D11Texture2D1> texture1;
   if (SUCCEEDED(texture.As(&texture1)))
   {
      D3D11_TEXTURE2D_DESC1 seen1 = {};
      texture1->GetDesc1(&seen1);
      CHECK(seen1.Format == DXGI_FORMAT_R11G11B10_FLOAT);
   }
   {
      Hooks::Scope::Guard scope;
      texture->GetDesc(&seen);
      CHECK(seen.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
      CHECK(seen.Width == 8);
   }
   // Other textures, and the in-place one without the spoof, report their own
   plain->GetDesc(&seen);
   CHECK(seen.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
   Resources::Upgrade::SetRules({.method = Resources::Upgrade::Method::InPlace, .spoof_description = false, .formats = {DXGI_FORMAT_R11G11B10_FLOAT}});
   texture->GetDesc(&seen);
   CHECK(seen.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
}

TEST_CASE("Hooks D3D12: GetDesc and GetDesc1 of a resource upgraded in place give the game what it asked for, and Luma the real one")
{
   InstallHooks();
   const ComPtr<ID3D12Device> device = CreateD3D12Device();
   Resources::Upgrade::SetRules({.method = Resources::Upgrade::Method::InPlace, .formats = {DXGI_FORMAT_R11G11B10_FLOAT}});

   struct Cleanup
   {
      ~Cleanup() { Resources::Upgrade::SetRules({}); }
   } cleanup;

   const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
   const D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 8, 8, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
   ComPtr<ID3D12Resource> texture;
   {
      Hooks::Scope::Guard scope;
      REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture))));
   }
   // As if the rules had also doubled its size: the game asked for 4x4
   const Resources::Upgrade::TextureInfo game = {.width = 4, .height = 4, .format = DXGI_FORMAT_R11G11B10_FLOAT, .render_target = true};
   Resources::Upgrade::TextureInfo upgraded = game;
   upgraded.width = 8;
   upgraded.height = 8;
   upgraded.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
   Resources::Mirrors::AttachInPlace(texture.Get(), game, upgraded);

   const D3D12_RESOURCE_DESC seen = texture->GetDesc();
   CHECK(seen.Format == DXGI_FORMAT_R11G11B10_FLOAT);
   CHECK(seen.Width == 4);
   CHECK(seen.Height == 4);
   CHECK(seen.Flags == D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
   ComPtr<ID3D12Resource2> texture2;
   if (SUCCEEDED(texture.As(&texture2)))
      CHECK(texture2->GetDesc1().Format == DXGI_FORMAT_R11G11B10_FLOAT);
   {
      Hooks::Scope::Guard scope;
      CHECK(texture->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
      CHECK(texture->GetDesc().Width == 8);
   }
   Resources::Upgrade::SetRules({.method = Resources::Upgrade::Method::InPlace, .spoof_description = false, .formats = {DXGI_FORMAT_R11G11B10_FLOAT}});
   CHECK(texture->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
}

TEST_CASE("Frame D3D11: a device's resolutions change together, and move LumaSettings' stamp")
{
   InstallHooks();
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   CreateD3D11Device(device, immediate_context);
   if (!FrameState::Find(device.Get()))
      FrameState::CreateDeviceState(device.Get());
   const FrameState::DeviceState* state = FrameState::Find(device.Get());
   REQUIRE(state);

   uint64_t stamp = Resources::LumaCBuffers::GetSettingsStamp();
   FrameState::SetOutputResolution(device.Get(), {320, 180});
   CHECK(Resources::LumaCBuffers::GetSettingsStamp() != stamp);
   FrameState::Resolutions resolutions = FrameState::Read(state);
   CHECK(resolutions.output.x == 320);
   CHECK(resolutions.render.y == 180);
   CHECK(resolutions.previous_render.x == 320);

   stamp = Resources::LumaCBuffers::GetSettingsStamp();
   FrameState::SetRenderResolution(device.Get(), {160, 90});
   CHECK(Resources::LumaCBuffers::GetSettingsStamp() != stamp);
   resolutions = FrameState::Read(state);
   CHECK(resolutions.render.x == 160);
   CHECK(resolutions.previous_render.x == 320);
   CHECK(resolutions.output.x == 320);
   // The cached state reads as the device does
   CHECK(FrameState::Get(device.Get()).render.y == 90);
}

TEST_CASE("Shaders D3D11: each device has its own store, built for its API, and the store goes with the device")
{
   InstallHooks();
   ComPtr<ID3D11Device> first;
   ComPtr<ID3D11DeviceContext> first_context;
   CreateD3D11Device(first, first_context);
   ComPtr<ID3D11Device> second;
   ComPtr<ID3D11DeviceContext> second_context;
   CreateD3D11Device(second, second_context);
   const std::shared_ptr<Shaders::Store> second_store = Shaders::GetStore(second.Get());
   std::weak_ptr<Shaders::Store> first_store = Shaders::GetStore(first.Get());
   REQUIRE(second_store);
   REQUIRE_FALSE(first_store.expired());
   CHECK(first_store.lock() != second_store);
   CHECK(second_store->api == Api::D3D11);
   CHECK(std::ranges::contains(Shaders::GetStores(), second_store));

   first_context.Reset();
   first.Reset();
   CHECK(first_store.expired());
   CHECK(std::ranges::contains(Shaders::GetStores(), second_store));
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
   CHECK(swapchain_pre_create_calls == 1);
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

   // Through the message queue, as a game gets its input: Luma sees it there, and never subclasses the window
   void Post(UINT message, WPARAM wparam, LPARAM lparam)
   {
      PostMessageW(hwnd, message, wparam, lparam);
      MSG queued;
      while (PeekMessageW(&queued, hwnd, 0, 0, PM_REMOVE))
         DispatchMessageW(&queued);
   }

   void Key(WPARAM key) { Post(WM_KEYDOWN, key, 0); }

   void Mouse() { Post(WM_MOUSEMOVE, 0, MAKELPARAM(10, 10)); }

   // ImGui captures the mouse on the press and releases it on the release, which sends WM_CAPTURECHANGED into the window
   // procedure on the same thread, while the overlay handles the release
   void Click()
   {
      Post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
      Post(WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
   }

   bool IsSubclassed() const { return GetWindowLongPtrW(hwnd, GWLP_WNDPROC) != reinterpret_cast<LONG_PTR>(&GameWindowProcedure); }
};

// Other tests clear the settings registry
void InitializeOverlay()
{
   if (!Settings::Find(Settings::Core::OVERLAY_KEY.key))
      UI::Input::Initialize();
   Settings::Reset(Settings::Core::OVERLAY_KEY.key);
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

   // Attached at its first present, closed
   swapchain->Present(0, 0);
   CHECK(UI::DXGI::GetSwapchain() == swapchain.Get());
   CHECK_FALSE(window.IsSubclassed());
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
   CHECK(Settings::Get(Settings::Core::OVERLAY_KEY).code == VK_F2);
   window.Key(VK_F2);
   CHECK_FALSE(UI::IsOpen());
   window.Key(VK_INSERT);
   CHECK(game_keys == 2);
   CHECK_FALSE(UI::IsOpen());

   if (info_queue)
      CHECK(CountErrors<D3D11_MESSAGE>(info_queue.Get(), D3D11_MESSAGE_SEVERITY_ERROR) == 0);
   info_queue.Reset();

   // The game releases its device before the swapchain (which holds the real device on D3D11): the overlay goes with the device
   still_bound.Reset();
   constants.Reset();
   immediate_context.Reset();
   device.Reset();
   CHECK_FALSE(UI::DXGI::GetSwapchain());
   CHECK_FALSE(window.IsSubclassed());
   swapchain->Present(0, 0);
   CHECK_FALSE(UI::DXGI::GetSwapchain());
   swapchain.Reset();
   Settings::Reset(Settings::Core::OVERLAY_KEY.key);
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

   swapchain->Present(0, 0);
   CHECK(UI::DXGI::GetSwapchain() == swapchain.Get());
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
   CHECK_FALSE(UI::DXGI::GetSwapchain());
   CHECK_FALSE(window.IsSubclassed());
   fence.Reset();
   queue.Reset();
   device.Reset();
   CHECK(Hooks::DeviceProxy::GetCount() == 0);
}

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

// The bypass Luma sets before it unhooks: every detour of the device, swapchain, present and resize path passes the game's call
// through, so no handler and no game event runs (Hooks::Scope::IsActive)
TEST_CASE("Hooks: bypassed, the game's devices, swapchains and presents reach no handler and no game event")
{
   InstallHooks();
   GameCallbacks game;
   HiddenWindow window;
   const uint32_t proxies = Hooks::DeviceProxy::GetCount();
   const size_t swapchains = Trace::GetStatus().swapchains.size();
   Hooks::Scope::SetBypass(true);
   {
      const DXGI_SWAP_CHAIN_DESC desc = GetWindowedDesc(window.hwnd);
      ComPtr<IDXGISwapChain> swapchain;
      ComPtr<ID3D11Device> device;
      ComPtr<ID3D11DeviceContext> immediate_context;
      REQUIRE(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, GetD3D11Flags(), nullptr, 0, D3D11_SDK_VERSION, &desc, &swapchain,
         &device, nullptr, &immediate_context)));
      CHECK_FALSE(Hooks::DeviceProxy::IsProxy(device.Get()));
      ComPtr<ID3D11Device> from_swapchain;
      REQUIRE(SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&from_swapchain))));
      CHECK_FALSE(Hooks::DeviceProxy::IsProxy(from_swapchain.Get()));
      swapchain->Present(0, 0);
      REQUIRE(SUCCEEDED(swapchain->ResizeBuffers(0, 400, 200, DXGI_FORMAT_UNKNOWN, 0)));
      swapchain->Present(0, 0);
      CHECK(UI::DXGI::GetSwapchain() == nullptr);

      // A factory's swapchain, and D3D12's device and submissions
      ComPtr<IDXGIFactory2> factory;
      REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
      ComPtr<IDXGIAdapter> adapter;
      ComPtr<IDXGIFactory4> factory4;
      REQUIRE(SUCCEEDED(factory.As(&factory4)));
      REQUIRE(SUCCEEDED(factory4->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
      ComPtr<ID3D12Device> d3d12_device;
      REQUIRE(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12_device))));
      CHECK_FALSE(Hooks::DeviceProxy::IsProxy(d3d12_device.Get()));
      const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
      ComPtr<ID3D12CommandQueue> queue;
      REQUIRE(SUCCEEDED(d3d12_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));
      queue->ExecuteCommandLists(0, nullptr);
   }
   Hooks::Scope::SetBypass(false);
   CHECK(created_calls == 0);
   CHECK(destroyed_calls == 0);
   CHECK(swapchain_created_calls == 0);
   CHECK(swapchain_resized_calls == 0);
   CHECK(present_calls == 0);
   CHECK(Hooks::DeviceProxy::GetCount() == proxies);
   CHECK(Trace::GetStatus().swapchains.size() == swapchains);
}

namespace {
// The game picks Native, with the swapchain settings at their defaults (flip model, tearing, borderless); all gone after
struct NativeMode
{
   NativeMode()
   {
      if (!Settings::Find(Settings::Core::SWAPCHAIN_MODE.key))
         Display::Swapchain::Register();
      // The game holds the real swapchain, as the other tests here
      Settings::Set(Settings::Core::SWAPCHAIN_WRAP, false);
      Display::Swapchain::SetOptions({.mode = Display::Swapchain::Mode::Native});
   }

   ~NativeMode()
   {
      Display::Swapchain::SetOptions({});
      Settings::Clear();
   }
};

// Inside Luma's scope: the real swapchain's description
DXGI_SWAP_CHAIN_DESC1 GetRealDesc(IDXGISwapChain1* swapchain)
{
   Hooks::Scope::Guard scope;
   DXGI_SWAP_CHAIN_DESC1 desc = {};
   REQUIRE(SUCCEEDED(swapchain->GetDesc1(&desc)));
   return desc;
}

DXGI_SWAP_CHAIN_DESC GetRealDesc(IDXGISwapChain* swapchain)
{
   Hooks::Scope::Guard scope;
   DXGI_SWAP_CHAIN_DESC desc = {};
   REQUIRE(SUCCEEDED(swapchain->GetDesc(&desc)));
   return desc;
}

bool SupportsColorSpace(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space)
{
   UINT support = 0;
   return SUCCEEDED(swapchain->CheckColorSpaceSupport(color_space, &support)) && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT);
}
} // namespace

TEST_CASE("Swapchain D3D11 Native: presented as in place (flip model, tearing, borderless), in the game's format and color space, never composed")
{
   InstallHooks();
   GameCallbacks game;
   NativeMode native;
   HiddenWindow window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   CreateD3D11Device(device, immediate_context);
   ComPtr<IDXGIFactory2> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   // A blit swapchain of one buffer that may switch display modes, in a float format
   const DXGI_SWAP_CHAIN_DESC1 desc = {
      .Width = 320,
      .Height = 180,
      .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
      .SampleDesc = {1, 0},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT,
      .BufferCount = 1,
      .Scaling = DXGI_SCALING_STRETCH,
      .SwapEffect = DXGI_SWAP_EFFECT_DISCARD,
      .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
      .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH,
   };
   ComPtr<IDXGISwapChain1> swapchain;
   REQUIRE(SUCCEEDED(factory->CreateSwapChainForHwnd(device.Get(), window.hwnd, &desc, nullptr, nullptr, &swapchain)));
   CHECK(swapchain_created_calls == 1);
   // The presentation is rewritten, the format is the game's
   const DXGI_SWAP_CHAIN_DESC1 real = GetRealDesc(swapchain.Get());
   CHECK(real.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD);
   CHECK(real.BufferCount == 2);
   CHECK(real.Flags == UINT(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));
   CHECK(real.Format == desc.Format);
   CHECK(real.Width == desc.Width);
   CHECK(real.Height == desc.Height);
   CHECK(real.SampleDesc.Count == 1);
   CHECK((real.BufferUsage & ~DXGI_USAGE(DXGI_USAGE_BACK_BUFFER)) == desc.BufferUsage);
   CHECK(real.Scaling == desc.Scaling);

   // The game's buffer is the real one
   {
      ComPtr<ID3D11Texture2D> game_buffer;
      REQUIRE(SUCCEEDED(swapchain->GetBuffer(0, IID_PPV_ARGS(&game_buffer))));
      ComPtr<ID3D11Texture2D> real_buffer;
      {
         Hooks::Scope::Guard scope;
         REQUIRE(SUCCEEDED(swapchain->GetBuffer(0, IID_PPV_ARGS(&real_buffer))));
      }
      CHECK(game_buffer.Get() == real_buffer.Get());
   }

   REQUIRE(SUCCEEDED(swapchain->Present(0, 0)));
   CHECK(present_calls == 1);
   Display::Swapchain::DXGI::Status status = Display::Swapchain::DXGI::GetStatus();
   CHECK(status.mode == "Native");
   CHECK_FALSE(status.composed);
   CHECK(status.flip_model);
   CHECK(status.tearing);
   CHECK(status.borderless);
   CHECK(status.real_format == desc.Format);
   CHECK(status.game_format == desc.Format);
   CHECK(status.output_mode == "scRGB");
   CHECK(status.color_space == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
   CHECK_FALSE(status.game_color_space);
   // The output is the real size: what upscaling targets
   FrameState::Resolutions resolutions = FrameState::Get(Real(device));
   CHECK(resolutions.output.x == 320);
   CHECK(resolutions.swapchain.y == 180);

   // The game's color space reaches the real swapchain (recorded there, what the overlay encodes for)
   ComPtr<IDXGISwapChain3> swapchain3;
   REQUIRE(SUCCEEDED(swapchain.As(&swapchain3)));
   if (SupportsColorSpace(swapchain3.Get(), DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709))
   {
      REQUIRE(SUCCEEDED(swapchain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)));
      CHECK(Display::Swapchain::DXGI::GetColorSpace(swapchain.Get()) == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
      REQUIRE(SUCCEEDED(swapchain->Present(0, 0)));
      CHECK(Display::Swapchain::DXGI::GetStatus().game_color_space == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
   }
   else
   {
      MESSAGE("scRGB isn't presentable here: the game's color space isn't checked");
   }

   // A resize keeps the game's new format, with the presentation rewritten as at creation
   REQUIRE(SUCCEEDED(swapchain->ResizeBuffers(1, 400, 200, DXGI_FORMAT_R10G10B10A2_UNORM, 0)));
   CHECK(swapchain_resized_calls == 1);
   const DXGI_SWAP_CHAIN_DESC1 resized = GetRealDesc(swapchain.Get());
   CHECK(resized.Width == 400);
   CHECK(resized.Height == 200);
   CHECK(resized.Format == DXGI_FORMAT_R10G10B10A2_UNORM);
   CHECK(resized.BufferCount == 2);
   CHECK(resized.Flags == UINT(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));
   REQUIRE(SUCCEEDED(swapchain->Present(0, 0)));
   status = Display::Swapchain::DXGI::GetStatus();
   CHECK_FALSE(status.composed);
   CHECK(status.real_width == 400);
   CHECK(status.real_format == DXGI_FORMAT_R10G10B10A2_UNORM);
   // The float format's color space no longer describes the buffers
   CHECK_FALSE(status.game_color_space);
   CHECK(status.output_mode == "SDR");
   resolutions = FrameState::Get(Real(device));
   CHECK(resolutions.output.x == 400);
   CHECK(resolutions.swapchain.y == 200);

   if (SupportsColorSpace(swapchain3.Get(), DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020))
   {
      REQUIRE(SUCCEEDED(swapchain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)));
      REQUIRE(SUCCEEDED(swapchain->Present(0, 0)));
      status = Display::Swapchain::DXGI::GetStatus();
      CHECK(status.output_mode == "HDR10");
      CHECK(status.color_space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
   }
   else
   {
      MESSAGE("HDR10 isn't presentable here: the game's HDR color space isn't checked");
   }

   ComPtr<ID3D11InfoQueue> info_queue;
   if (SUCCEEDED(device.As(&info_queue)))
      CHECK(CountErrors<D3D11_MESSAGE>(info_queue.Get(), D3D11_MESSAGE_SEVERITY_ERROR) == 0);
}

TEST_CASE("Swapchain D3D11 Native: an sRGB format, which the flip model can't present, keeps the game's swap effect and format")
{
   InstallHooks();
   GameCallbacks game;
   NativeMode native;
   HiddenWindow window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   CreateD3D11Device(device, immediate_context);
   ComPtr<IDXGIFactory> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   DXGI_SWAP_CHAIN_DESC desc = {
      .BufferDesc = {.Width = 320, .Height = 180, .Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
      .SampleDesc = {1, 0},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = 1,
      .OutputWindow = window.hwnd,
      .Windowed = TRUE,
      .SwapEffect = DXGI_SWAP_EFFECT_DISCARD,
      .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH,
   };
   ComPtr<IDXGISwapChain> swapchain;
   REQUIRE(SUCCEEDED(factory->CreateSwapChain(device.Get(), &desc, &swapchain)));
   CHECK(swapchain_created_calls == 1);
   // Still the blit model: no tearing, the buffer count the game's, but borderless drops the mode switch
   const DXGI_SWAP_CHAIN_DESC real = GetRealDesc(swapchain.Get());
   CHECK(real.SwapEffect == DXGI_SWAP_EFFECT_DISCARD);
   CHECK(real.BufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
   CHECK(real.BufferCount == 1);
   CHECK(real.Flags == 0);
   CHECK(real.BufferDesc.Width == 320);

   REQUIRE(SUCCEEDED(swapchain->Present(0, 0)));
   const Display::Swapchain::DXGI::Status status = Display::Swapchain::DXGI::GetStatus();
   CHECK(status.mode == "Native");
   CHECK_FALSE(status.flip_model);
   CHECK_FALSE(status.tearing);
   CHECK_FALSE(status.composed);
   CHECK(status.real_format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
   CHECK(status.output_mode == "SDR");
   CHECK(status.color_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);

   ComPtr<ID3D11InfoQueue> info_queue;
   if (SUCCEEDED(device.As(&info_queue)))
      CHECK(CountErrors<D3D11_MESSAGE>(info_queue.Get(), D3D11_MESSAGE_SEVERITY_ERROR) == 0);
}

TEST_CASE("Overlay D3D11 Native: a swapchain asked without render target usage gets it, and the overlay draws on it")
{
   InstallHooks();
   InitializeOverlay();
   NativeMode native;
   GameWindow window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> immediate_context;
   CreateD3D11Device(device, immediate_context);
   ComPtr<IDXGIFactory2> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   const DXGI_SWAP_CHAIN_DESC1 desc = {
      .Width = 320,
      .Height = 180,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {1, 0},
      .BufferUsage = DXGI_USAGE_UNORDERED_ACCESS,
      .BufferCount = 2,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
   };
   ComPtr<IDXGISwapChain1> swapchain;
   REQUIRE(SUCCEEDED(factory->CreateSwapChainForHwnd(device.Get(), window.hwnd, &desc, nullptr, nullptr, &swapchain)));
   const DXGI_USAGE usage = GetRealDesc(swapchain.Get()).BufferUsage;
   CHECK((usage & DXGI_USAGE_RENDER_TARGET_OUTPUT) != 0);
   CHECK((usage & DXGI_USAGE_UNORDERED_ACCESS) != 0);
   UI::SetOpen(true);
   for (int i = 0; i < 2; i++)
      CHECK(SUCCEEDED(swapchain->Present(0, 0)));
   CHECK(UI::DXGI::GetSwapchain() == swapchain.Get());
   UI::SetOpen(false);
   ComPtr<ID3D11InfoQueue> info_queue;
   if (SUCCEEDED(device.As(&info_queue)))
      CHECK(CountErrors<D3D11_MESSAGE>(info_queue.Get(), D3D11_MESSAGE_SEVERITY_ERROR) == 0);
}

namespace {
namespace ReShadeABI = ReShade::ABI;

// A stand-in for ReShade's dll (FakeReShade.dll, next to Tests.exe): it exports what Luma looks for and calls, and what it recorded
HMODULE fake_reshade = nullptr;

template <typename Function>
Function GetFakeExport(const char* name)
{
   const auto function = reinterpret_cast<Function>(GetProcAddress(fake_reshade, name));
   REQUIRE(function);
   return function;
}

// ReShade's api::device and api::swapchain, as far as Luma reads them
struct FakeDevice : ReShadeABI::Device
{
   ReShadeABI::DeviceApi api = ReShadeABI::DeviceApi::D3D11;

   uint64_t GetNative() const override { return 0; }

   void GetPrivateData(const uint8_t[16], uint64_t* data) const override { *data = 0; }

   void SetPrivateData(const uint8_t[16], const uint64_t) override {}

   ReShadeABI::DeviceApi GetApi() const override { return api; }
};

struct FakeSwapchain : ReShadeABI::DeviceObject
{
   FakeDevice* device = nullptr;
   IDXGISwapChain* native = nullptr; // What ReShade's wrapper holds: the game's proxy

   uint64_t GetNative() const override { return reinterpret_cast<uint64_t>(native); }

   void GetPrivateData(const uint8_t[16], uint64_t* data) const override { *data = 0; }

   void SetPrivateData(const uint8_t[16], const uint64_t) override {}

   ReShadeABI::Device* GetDevice() override { return device; }
};

void* FindRegistered(ReShadeABI::Event event)
{
   return GetFakeExport<void* (*)(uint32_t)>("FakeReShadeGetEvent")(uint32_t(event));
}

// ReShade firing its events
void FirePresent(FakeSwapchain& swapchain)
{
   using Callback = void (*)(void*, ReShadeABI::DeviceObject*, const void*, const void*, uint32_t, const void*);
   reinterpret_cast<Callback>(FindRegistered(ReShadeABI::Event::Present))(nullptr, &swapchain, nullptr, nullptr, 0, nullptr);
}

void FireFinishPresent(FakeSwapchain& swapchain)
{
   using Callback = void (*)(void*, ReShadeABI::DeviceObject*);
   reinterpret_cast<Callback>(FindRegistered(ReShadeABI::Event::FinishPresent))(nullptr, &swapchain);
}

bool FireOpenOverlay(bool open)
{
   using Callback = bool (*)(void*, bool, int32_t);
   return reinterpret_cast<Callback>(FindRegistered(ReShadeABI::Event::ReShadeOpenOverlay))(nullptr, open, 0);
}

// The game holds a proxy of its swapchain, as with ReShade above Luma; the settings are gone after
struct WrappedSwapchains
{
   WrappedSwapchains()
   {
      if (!Settings::Find(Settings::Core::SWAPCHAIN_WRAP.key))
         Display::Swapchain::Register();
      Settings::Set(Settings::Core::SWAPCHAIN_WRAP, true);
   }

   ~WrappedSwapchains() { Settings::Clear(); }
};

uint32_t viewport_calls = 0;

void OnBindViewports(ID3D11DeviceContext*, UINT, const D3D11_VIEWPORT*)
{
   viewport_calls++;
}

// The game's present and viewport events, and the viewports in the bound-state cache
struct ViewportEvents
{
   ViewportEvents()
   {
      Game::SetEvents(Game::D3D11::Events{.on_present = &OnPresent<ID3D11Device>, .on_bind_viewports = &OnBindViewports});
      Draw::SetOptions({.viewports = true});
      viewport_calls = 0;
   }

   ~ViewportEvents() { Draw::SetOptions({}); }
};

ComPtr<IDXGISwapChain1> CreateSwapchain(IUnknown* presenter, HWND hwnd)
{
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
   REQUIRE(SUCCEEDED(factory->CreateSwapChainForHwnd(presenter, hwnd, &desc, nullptr, nullptr, &swapchain)));
   return swapchain;
}
} // namespace

TEST_CASE("ReShade D3D11: Luma's present work runs at the present event, once, and the window to Luma's Present passes calls through")
{
   InstallHooks();
   GameCallbacks game;
   ViewportEvents events;
   WrappedSwapchains wrapped;
   HiddenWindow window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   CreateD3D11Device(device, context);
   ComPtr<IDXGISwapChain1> swapchain = CreateSwapchain(device.Get(), window.hwnd);
   IDXGISwapChain* real = Hooks::SwapchainProxy::Unwrap(swapchain.Get());
   REQUIRE(real != swapchain.Get());

   const D3D11_VIEWPORT game_viewport = {0.f, 0.f, 320.f, 180.f, 0.f, 1.f};
   const D3D11_VIEWPORT reshade_viewport = {0.f, 0.f, 16.f, 16.f, 0.f, 1.f};
   context->RSSetViewports(1, &game_viewport);
   CHECK(viewport_calls == 1);

   // ReShade's present event: the game's frame is done (its on_present ran), then the window opens
   CHECK(Hooks::DXGI::RunPresentWorkAtEvent(real));
   CHECK(present_calls == 1);
   CHECK(Hooks::Scope::IsThirdPartyWorkOpen());
   // ReShade's effects bind their state and restore the game's: no game event, and the cache keeps the game's viewport
   context->RSSetViewports(1, &reshade_viewport);
   const Draw::D3D11::Bound* bound = Draw::D3D11::GetBound(context.Get());
   REQUIRE(bound);
   CHECK(bound->viewports[0].Width == 320.f);
   context->RSSetViewports(1, &game_viewport);
   CHECK(viewport_calls == 1);

   // Luma's Present closes it and doesn't run the present work again
   swapchain->Present(0, 0);
   CHECK_FALSE(Hooks::Scope::IsThirdPartyWorkOpen());
   CHECK(present_calls == 1);
   context->RSSetViewports(1, &game_viewport);
   CHECK(viewport_calls == 2);

   // Without the event (a DO_NOT_WAIT retry, the bridge off) Present runs both parts
   swapchain->Present(0, 0);
   CHECK(present_calls == 2);
   // DXGI_PRESENT_TEST runs neither
   swapchain->Present(0, DXGI_PRESENT_TEST);
   CHECK(present_calls == 2);

   // Present1 the same: once
   const DXGI_PRESENT_PARAMETERS parameters = {};
   CHECK(Hooks::DXGI::RunPresentWorkAtEvent(real));
   swapchain->Present1(0, 0, &parameters);
   CHECK_FALSE(Hooks::Scope::IsThirdPartyWorkOpen());
   CHECK(present_calls == 3);

   // An event no Luma Present followed: the next Present doesn't skip its present work
   CHECK(Hooks::DXGI::RunPresentWorkAtEvent(real));
   CHECK(Hooks::DXGI::CloseThirdPartyWorkWindowAtFinishPresent());
   CHECK_FALSE(Hooks::Scope::IsThirdPartyWorkOpen());
   swapchain->Present(0, 0);
   CHECK(present_calls == 5);

   // Inside Luma's own work the event does nothing
   {
      Hooks::Scope::Guard scope;
      CHECK_FALSE(Hooks::DXGI::RunPresentWorkAtEvent(real));
   }
   CHECK(present_calls == 5);
}

TEST_CASE("ReShade D3D11: without a proxy, Luma's Present hook closes the window and skips the present work the event ran")
{
   InstallHooks();
   GameCallbacks game;
   HiddenWindow window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   CreateD3D11Device(device, context);
   ComPtr<IDXGISwapChain1> swapchain = CreateSwapchain(device.Get(), window.hwnd);
   REQUIRE_FALSE(Hooks::SwapchainProxy::HasProxy(swapchain.Get()));
   CHECK(Hooks::DXGI::RunPresentWorkAtEvent(swapchain.Get()));
   CHECK(Hooks::Scope::IsThirdPartyWorkOpen());
   swapchain->Present(0, 0);
   CHECK_FALSE(Hooks::Scope::IsThirdPartyWorkOpen());
   CHECK(present_calls == 1);
   swapchain->Present(0, 0);
   CHECK(present_calls == 2);
}

// A ReShade already in the process (as dxgi.dll, or injected by something else): Luma doesn't load another, and registers its bridge with it
TEST_CASE("ReShade: another module exporting ReShadeVersion isn't loaded again, the bridge registers with it, and its events drive the present")
{
   InstallHooks();
   GameCallbacks game;
   WrappedSwapchains wrapped;
   HiddenWindow window;
   HiddenWindow d3d12_window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   CreateD3D11Device(device, context);
   // Created while the executable is still the game's code
   ComPtr<IDXGISwapChain1> swapchain = CreateSwapchain(device.Get(), window.hwnd);
   REQUIRE(Hooks::SwapchainProxy::HasProxy(Hooks::SwapchainProxy::Unwrap(swapchain.Get())));
   ComPtr<ID3D12Device> d3d12_device = CreateD3D12Device();
   const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   ComPtr<ID3D12CommandQueue> queue;
   REQUIRE(SUCCEEDED(d3d12_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));
   ComPtr<IDXGISwapChain1> d3d12_swapchain = CreateSwapchain(queue.Get(), d3d12_window.hwnd);
   REQUIRE(Hooks::SwapchainProxy::HasProxy(Hooks::SwapchainProxy::Unwrap(d3d12_swapchain.Get())));

   // Loaded after the first present's check, as ReShade would be before Luma: Load finds it
   CHECK(ReShade::GetLoadState() == ReShade::LoadState::NotLoaded);
   fake_reshade = LoadLibraryW((System::Modules::GetPath(GetModuleHandleW(nullptr)).parent_path() / L"FakeReShade.dll").c_str());
   REQUIRE(fake_reshade);
   ReShade::Load();
   CHECK(ReShade::GetLoadState() == ReShade::LoadState::LoadedByAnotherModule);
   // Registered as an overlay: a call from its code is its own
   CHECK(Hooks::ThirdPartyModules::FindBase(GetProcAddress(fake_reshade, "ReShadeRegisterEvent")) == reinterpret_cast<uintptr_t>(fake_reshade));
   CHECK(GetFakeExport<void* (*)()>("FakeReShadeGetModule")() == System::Modules::GetSelf());
   CHECK(GetFakeExport<uint32_t (*)()>("FakeReShadeGetApiVersion")() == ReShadeABI::API_VERSION);
   for (const ReShadeABI::Event event : {ReShadeABI::Event::Present, ReShadeABI::Event::FinishPresent, ReShadeABI::Event::ReShadeBeginEffects, ReShadeABI::Event::ReShadeFinishEffects,
           ReShadeABI::Event::InitEffectRuntime, ReShadeABI::Event::DestroyEffectRuntime, ReShadeABI::Event::ReShadeOpenOverlay})
      CHECK(FindRegistered(event) != nullptr);
   CHECK(GetFakeExport<uint32_t (*)()>("FakeReShadeGetEventCount")() == 7);
   CHECK(ReShade::DescribeBridge().starts_with("on"));

   // D3D11: the event runs the present work once, Luma's Present closes the window
   FakeDevice d3d11 = {};
   FakeSwapchain reshade_swapchain = {};
   reshade_swapchain.device = &d3d11;
   reshade_swapchain.native = swapchain.Get();
   FirePresent(reshade_swapchain);
   CHECK(present_calls == 1);
   CHECK(Hooks::Scope::IsThirdPartyWorkOpen());
   swapchain->Present(0, 0);
   FireFinishPresent(reshade_swapchain);
   CHECK_FALSE(Hooks::Scope::IsThirdPartyWorkOpen());
   CHECK(present_calls == 1);
   // ReShade presents without reaching Luma's Present: finish_present closes the window
   FirePresent(reshade_swapchain);
   FireFinishPresent(reshade_swapchain);
   CHECK_FALSE(Hooks::Scope::IsThirdPartyWorkOpen());
   CHECK(present_calls == 2);

   // D3D12 the same
   FakeDevice d3d12 = {};
   d3d12.api = ReShadeABI::DeviceApi::D3D12;
   FakeSwapchain reshade_d3d12_swapchain = {};
   reshade_d3d12_swapchain.device = &d3d12;
   reshade_d3d12_swapchain.native = d3d12_swapchain.Get();
   FirePresent(reshade_d3d12_swapchain);
   CHECK(present_calls == 3);
   CHECK(callback_presenter == queue.Get());
   CHECK(Hooks::Scope::IsThirdPartyWorkOpen());
   d3d12_swapchain->Present(0, 0);
   CHECK_FALSE(Hooks::Scope::IsThirdPartyWorkOpen());
   CHECK(present_calls == 3);

   // Another API's swapchain (ReShade's OpenGL): nothing
   FakeDevice opengl = {};
   opengl.api = ReShadeABI::DeviceApi(0x10000);
   reshade_swapchain.device = &opengl;
   FirePresent(reshade_swapchain);
   CHECK_FALSE(Hooks::Scope::IsThirdPartyWorkOpen());
   CHECK(present_calls == 3);

   // One overlay open at a time
   UI::SetOpen(false);
   CHECK_FALSE(FireOpenOverlay(true));
   CHECK(ReShade::IsOverlayOpen());
   UI::SetOpen(true);
   CHECK_FALSE(UI::IsOpen());
   CHECK_FALSE(FireOpenOverlay(false));
   UI::SetOpen(true);
   CHECK(UI::IsOpen());
   CHECK(FireOpenOverlay(true));
   CHECK_FALSE(ReShade::IsOverlayOpen());
   UI::SetOpen(false);

   // The presents may still be running on the GPU
   ComPtr<ID3D12Fence> fence;
   REQUIRE(SUCCEEDED(d3d12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
   queue->Signal(fence.Get(), 1);
   CHECK(WaitFor([&] { return fence->GetCompletedValue() >= 1; }));
}

namespace {
void LoadFakeReShade();
bool addon_handler_in_scope = false;
uint64_t addon_handler_args[9] = {};

// What an add-on's handler does when ReShade calls it: reads its arguments and whether Luma's hooks would let the call through
uint64_t AddonHandlerBody(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
{
   addon_handler_in_scope = Hooks::Scope::IsActive();
   const uint64_t args[9] = {a0, a1, a2, a3, a4, a5, a6, a7, a8};
   std::copy(std::begin(args), std::end(args), std::begin(addon_handler_args));
   return a0 + a8;
}
} // namespace

// An add-on's handlers (here the fake's, in a dll that isn't Luma's) run in a Luma scope, whatever the registration function it used
TEST_CASE("ReShade: add-on event handlers are wrapped in a Luma scope, unwrapped and unregistered through their own pointer")
{
   InstallHooks();
   LoadFakeReShade();
   constexpr uint32_t EVENT = 40;
   void* const handler = reinterpret_cast<void*>(GetProcAddress(fake_reshade, "FakeReShadeAddonHandler"));
   REQUIRE(handler);
   GetFakeExport<void (*)(void*)>("FakeReShadeSetHandlerBody")(reinterpret_cast<void*>(&AddonHandlerBody));
   using Handler = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
   const auto find_event = [] { return GetFakeExport<void* (*)(uint32_t)>("FakeReShadeGetEvent")(EVENT); };

   // The plain registration: ReShade holds a wrapper, registered for the handler's module
   GetFakeExport<void (*)(uint32_t, void*)>("ReShadeRegisterEvent")(EVENT, handler);
   void* wrapper = find_event();
   REQUIRE(wrapper);
   CHECK(wrapper != handler);
   CHECK(GetFakeExport<void* (*)(uint32_t)>("FakeReShadeGetEventModule")(EVENT) == fake_reshade);
   CHECK(ReShade::DescribeBridge().find("add-on handlers kept out of Luma's hooks") != std::string::npos);

   // Its arguments (the ones on the stack too) and result pass through, the call is in a scope that ends with it
   CHECK_FALSE(Hooks::Scope::IsActive());
   CHECK(reinterpret_cast<Handler>(wrapper)(1, 2, 3, 4, 5, 6, 7, 8, 9) == 10);
   CHECK(addon_handler_in_scope);
   CHECK_FALSE(Hooks::Scope::IsActive());
   for (uint64_t index = 0; index < 9; index++)
      CHECK(addon_handler_args[index] == index + 1);

   // Unregistered by the handler's own pointer: ReShade's wrapper goes
   GetFakeExport<void (*)(uint32_t, void*)>("ReShadeUnregisterEvent")(EVENT, handler);
   CHECK(find_event() == nullptr);

   // By add-on: the module given is kept
   GetFakeExport<void (*)(void*, uint32_t, void*)>("ReShadeRegisterEventForAddon")(fake_reshade, EVENT, handler);
   wrapper = find_event();
   REQUIRE(wrapper);
   CHECK(wrapper != handler);
   CHECK(GetFakeExport<void* (*)(uint32_t)>("FakeReShadeGetEventModule")(EVENT) == fake_reshade);
   addon_handler_in_scope = false;
   CHECK(reinterpret_cast<Handler>(wrapper)(5, 0, 0, 0, 0, 0, 0, 0, 6) == 11);
   CHECK(addon_handler_in_scope);
   GetFakeExport<void (*)(void*, uint32_t, void*)>("ReShadeUnregisterEventForAddon")(fake_reshade, EVENT, handler);
   CHECK(find_event() == nullptr);

   // Reload cleanup: ReShadeUnregisterAddon clears all wrapped slots for that module so they are recycled on reload
   GetFakeExport<void (*)(void*, uint32_t, void*)>("ReShadeRegisterEventForAddon")(fake_reshade, EVENT, handler);
   GetFakeExport<void (*)(void*, uint32_t, void*)>("ReShadeRegisterEventForAddon")(fake_reshade, EVENT + 1, handler);
   CHECK(ReShade::AddonEvents::Describe().find("2 add-on handlers") != std::string::npos);
   GetFakeExport<void (*)(void*)>("ReShadeUnregisterAddon")(fake_reshade);
   CHECK(find_event() == nullptr);
   CHECK(ReShade::AddonEvents::Describe().find("0 add-on handlers") != std::string::npos);
   GetFakeExport<void (*)(void*)>("FakeReShadeSetHandlerBody")(nullptr);

   // Native caller tracking for .addon64/.addon32/.addon modules (excluding regular .dlls, Luma, and ReShade itself)
   alignas(64) uint8_t fake_addon64[64] = {};
   alignas(64) uint8_t fake_addon32[64] = {};
   alignas(64) uint8_t fake_addon[64] = {};
   alignas(64) uint8_t fake_regular_dll[64] = {};
   ReShade::AddonEvents::OnModuleLoaded(L"renodx.addon64", fake_addon64, sizeof(fake_addon64));
   ReShade::AddonEvents::OnModuleLoaded(L"legacy.addon32", fake_addon32, sizeof(fake_addon32));
   ReShade::AddonEvents::OnModuleLoaded(L"generic.addon", fake_addon, sizeof(fake_addon));
   ReShade::AddonEvents::OnModuleLoaded(L"game_helper.dll", fake_regular_dll, sizeof(fake_regular_dll));
   CHECK(ReShade::AddonEvents::IsAddonAddress(fake_addon64 + 8));
   CHECK(ReShade::AddonEvents::IsAddonAddress(fake_addon32 + 8));
   CHECK(ReShade::AddonEvents::IsAddonAddress(fake_addon + 8));
   CHECK_FALSE(ReShade::AddonEvents::IsAddonAddress(fake_regular_dll + 8));
   CHECK_FALSE(ReShade::AddonEvents::IsAddonAddress(handler)); // ReShade itself must never be treated as an add-on
   ReShade::AddonEvents::OnModuleUnloaded(fake_addon64);
   ReShade::AddonEvents::OnModuleUnloaded(fake_addon32);
   ReShade::AddonEvents::OnModuleUnloaded(fake_addon);
   CHECK_FALSE(ReShade::AddonEvents::IsAddonAddress(fake_addon64 + 8));
   CHECK_FALSE(ReShade::AddonEvents::IsAddonAddress(fake_addon32 + 8));
   CHECK_FALSE(ReShade::AddonEvents::IsAddonAddress(fake_addon + 8));
}

namespace {
constexpr GUID COLOR_SPACE_GUID = {0x18b57e4, 0x1493, 0x4953, {0xad, 0xf2, 0xde, 0x6d, 0x99, 0xcc, 0x5, 0xe5}};

// The fake ReShade in the process with its bridge on, whether the test above ran first or not
void LoadFakeReShade()
{
   if (!fake_reshade)
   {
      fake_reshade = GetModuleHandleW(L"FakeReShade.dll");
      if (!fake_reshade)
         fake_reshade = LoadLibraryW((System::Modules::GetPath(GetModuleHandleW(nullptr)).parent_path() / L"FakeReShade.dll").c_str());
   }
   REQUIRE(fake_reshade);
   ReShade::Load();
   REQUIRE(ReShade::DescribeBridge().starts_with("on"));
}

// The fake's swapchain creation hook: the game gets ReShade's wrapper of its proxy
ComPtr<IDXGISwapChain> WrapInFakeReShade(IDXGISwapChain* proxy, bool d3d12)
{
   ComPtr<IDXGISwapChain> wrapper;
   wrapper.Attach(GetFakeExport<IDXGISwapChain* (*)(IDXGISwapChain*, bool)>("FakeReShadeWrap")(proxy, d3d12));
   REQUIRE(wrapper);
   return wrapper;
}

IUnknown* GetHeldBuffer(IDXGISwapChain* wrapper, uint32_t index)
{
   return GetFakeExport<IUnknown* (*)(IDXGISwapChain*, uint32_t)>("FakeReShadeGetHeldBuffer")(wrapper, index);
}

uint32_t ReadFake(const char* name, IDXGISwapChain* wrapper)
{
   return GetFakeExport<uint32_t (*)(IDXGISwapChain*)>(name)(wrapper);
}

// The first buffer a read from the fake's code gets now, as ReShade's runtime reads it: the real one inside a window, the game's outside
IUnknown* ProbeRead(IDXGISwapChain* wrapper)
{
   return GetFakeExport<IUnknown* (*)(IDXGISwapChain*)>("FakeReShadeProbeRead")(wrapper);
}

// What Luma itself sees (its scope): the real swapchain's
template <typename Interface>
ComPtr<Interface> GetRealBuffer(IDXGISwapChain* real, UINT index)
{
   Hooks::Scope::Guard scope;
   ComPtr<Interface> buffer;
   REQUIRE(SUCCEEDED(real->GetBuffer(index, IID_PPV_ARGS(&buffer))));
   return buffer;
}

DXGI_COLOR_SPACE_TYPE GetRecordedColorSpace(IDXGISwapChain* real)
{
   Hooks::Scope::Guard scope;
   DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RESERVED;
   UINT size = sizeof(color_space);
   real->GetPrivateData(COLOR_SPACE_GUID, &size, &color_space);
   return color_space;
}

// The fake's runtime switched off ([GENERAL] Disable=1), back on after
struct FakeRuntimeDisabled
{
   FakeRuntimeDisabled() { GetFakeExport<void (*)(bool)>("FakeReShadeSetRuntimeEnabled")(false); }

   ~FakeRuntimeDisabled() { GetFakeExport<void (*)(bool)>("FakeReShadeSetRuntimeEnabled")(true); }
};
} // namespace

TEST_CASE("ReShade D3D11 Mirror: ReShade's runtime reads the real buffer and Luma's color space in its window, the game's calls through its wrapper get the mirror")
{
   InstallHooks();
   GameCallbacks game;
   WrappedSwapchains wrapped;
   HiddenWindow window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   CreateD3D11Device(device, context);
   LoadFakeReShade();
   ComPtr<IDXGISwapChain1> swapchain = CreateSwapchain(device.Get(), window.hwnd);
   IDXGISwapChain* real = Hooks::SwapchainProxy::Unwrap(swapchain.Get());
   REQUIRE(real != swapchain.Get());
   REQUIRE(Hooks::DXGI::GetReShadeSwapchainView(real) == Hooks::SwapchainProxy::ReShadeSwapchainView::RealInsideWindows);

   ComPtr<IDXGISwapChain> reshade = WrapInFakeReShade(swapchain.Get(), false);
   CHECK(ReadFake("FakeReShadeGetInits", reshade.Get()) == 1);
   const ComPtr<ID3D11Texture2D> real_buffer = GetRealBuffer<ID3D11Texture2D>(real, 0);
   CHECK(GetHeldBuffer(reshade.Get(), 0) == real_buffer.Get());
   CHECK(ReadFake("FakeReShadeGetColorSpace", reshade.Get()) == uint32_t(GetRecordedColorSpace(real)));

   // The game, through ReShade's wrapper and on the proxy: its mirror
   ComPtr<ID3D11Texture2D> mirror;
   REQUIRE(SUCCEEDED(Display::Swapchain::DXGI::GetGameBuffer(real).As(&mirror)));
   CHECK(mirror.Get() != real_buffer.Get());
   ComPtr<ID3D11Texture2D> through_reshade;
   REQUIRE(SUCCEEDED(reshade->GetBuffer(0, IID_PPV_ARGS(&through_reshade))));
   CHECK(ReadFake("FakeReShadeGetGameBufferCalls", reshade.Get()) == 1);
   CHECK(through_reshade.Get() == mirror.Get());
   ComPtr<ID3D11Texture2D> from_proxy;
   REQUIRE(SUCCEEDED(swapchain->GetBuffer(0, IID_PPV_ARGS(&from_proxy))));
   CHECK(from_proxy.Get() == mirror.Get());
   // Its runtime initialised: the window closed at its init_effect_runtime event, so its own read is the game's now
   CHECK(ProbeRead(reshade.Get()) == static_cast<IUnknown*>(mirror.Get()));
   DXGI_SWAP_CHAIN_DESC game_desc = {};
   REQUIRE(SUCCEEDED(reshade->GetDesc(&game_desc)));
   CHECK(game_desc.BufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
   CHECK(game_desc.BufferCount == 2);

   // The game's color space through the wrapper: ReShade resets and reads again once the proxy's SetColorSpace1 returned to it, the real
   // buffer and Luma's color space, which the color space ReShade writes doesn't replace
   const DXGI_COLOR_SPACE_TYPE luma_color_space = GetRecordedColorSpace(real);
   const DXGI_COLOR_SPACE_TYPE game_color_space =
      luma_color_space == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 ? DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
   ComPtr<IDXGISwapChain3> reshade3;
   REQUIRE(SUCCEEDED(reshade.As(&reshade3)));
   CHECK(SUCCEEDED(reshade3->SetColorSpace1(game_color_space)));
   CHECK(ReadFake("FakeReShadeGetInits", reshade.Get()) == 2);
   // Its runtime initialised again, so the window closed again
   CHECK(ProbeRead(reshade.Get()) == static_cast<IUnknown*>(mirror.Get()));
   CHECK(GetHeldBuffer(reshade.Get(), 0) == real_buffer.Get());
   CHECK(GetRecordedColorSpace(real) == luma_color_space);
   CHECK(ReadFake("FakeReShadeGetColorSpace", reshade.Get()) == uint32_t(luma_color_space));

   // A present through the wrapper: Luma's present work at its event, once
   const uint32_t presents = present_calls;
   CHECK(SUCCEEDED(reshade->Present(0, 0)));
   CHECK(present_calls == presents + 1);
   CHECK_FALSE(Hooks::Scope::IsThirdPartyWorkOpen());
}

TEST_CASE("ReShade D3D11 Mirror: an init window no init_effect_runtime closes stays shut to the game, and closes at the next present")
{
   InstallHooks();
   GameCallbacks game;
   WrappedSwapchains wrapped;
   HiddenWindow window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   CreateD3D11Device(device, context);
   LoadFakeReShade();
   FakeRuntimeDisabled disabled;
   ComPtr<IDXGISwapChain1> swapchain = CreateSwapchain(device.Get(), window.hwnd);
   IDXGISwapChain* real = Hooks::SwapchainProxy::Unwrap(swapchain.Get());
   ComPtr<IDXGISwapChain> reshade = WrapInFakeReShade(swapchain.Get(), false);
   CHECK(ReadFake("FakeReShadeGetInits", reshade.Get()) == 0);
   // The window is still open: its read gets the real buffer
   CHECK(ProbeRead(reshade.Get()) == static_cast<IUnknown*>(GetRealBuffer<ID3D11Texture2D>(real, 0).Get()));

   // The game's call through the wrapper inside it: ReShade's module, but the game's code called it
   ComPtr<ID3D11Texture2D> mirror;
   REQUIRE(SUCCEEDED(Display::Swapchain::DXGI::GetGameBuffer(real).As(&mirror)));
   ComPtr<ID3D11Texture2D> through_reshade;
   REQUIRE(SUCCEEDED(reshade->GetBuffer(0, IID_PPV_ARGS(&through_reshade))));
   CHECK(through_reshade.Get() == mirror.Get());
   CHECK(through_reshade.Get() != GetRealBuffer<ID3D11Texture2D>(real, 0).Get());

   CHECK(SUCCEEDED(reshade->Present(0, 0)));
   CHECK(ProbeRead(reshade.Get()) == static_cast<IUnknown*>(mirror.Get()));
   through_reshade.Reset();
   REQUIRE(SUCCEEDED(reshade->GetBuffer(0, IID_PPV_ARGS(&through_reshade))));
   CHECK(through_reshade.Get() == mirror.Get());
}

TEST_CASE("ReShade D3D11 Mirror: Luma's resize goes through ReShade's wrapper, which lets go of the real buffer a resize under it can't")
{
   InstallHooks();
   GameCallbacks game;
   WrappedSwapchains wrapped;
   HiddenWindow window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   CreateD3D11Device(device, context);
   LoadFakeReShade();
   ComPtr<IDXGISwapChain1> swapchain = CreateSwapchain(device.Get(), window.hwnd);
   IDXGISwapChain* real = Hooks::SwapchainProxy::Unwrap(swapchain.Get());
   ComPtr<IDXGISwapChain> reshade = WrapInFakeReShade(swapchain.Get(), false);
   REQUIRE(GetHeldBuffer(reshade.Get(), 0) != nullptr);
   ComPtr<ID3D11Texture2D> mirror;
   REQUIRE(SUCCEEDED(Display::Swapchain::DXGI::GetGameBuffer(real).As(&mirror)));
   const DXGI_SWAP_CHAIN_DESC before = GetRealDesc(real);
   const Hooks::SwapchainProxy::ResizeArguments game_arguments = {2, 320, 180, DXGI_FORMAT_R8G8B8A8_UNORM, 0};
   const auto real_arguments = [&](DXGI_FORMAT format) {
      return Hooks::SwapchainProxy::ResizeArguments{before.BufferCount, before.BufferDesc.Width, before.BufferDesc.Height, format, before.Flags};
   };

   // Under ReShade, whose runtime holds a view on the real buffer, DXGI refuses it
   {
      Hooks::Scope::Guard scope;
      CHECK(Hooks::SwapchainProxy::ResizeBuffers(real, before.BufferCount, before.BufferDesc.Width, before.BufferDesc.Height, DXGI_FORMAT_R10G10B10A2_UNORM,
               before.Flags) == DXGI_ERROR_INVALID_CALL);
   }
   CHECK(GetRealDesc(real).BufferDesc.Format == before.BufferDesc.Format);

   // Through its wrapper, as Luma's output switch at the present event: ReShade sees the game's values, the real swapchain gets Luma's, and
   // ReShade reads the new buffer and the color space Luma is about to set
   std::optional<HRESULT> result;
   {
      Hooks::Scope::Guard scope;
      result = Hooks::SwapchainProxy::ResizeBuffersThroughReShade(real, game_arguments, real_arguments(DXGI_FORMAT_R10G10B10A2_UNORM), DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
   }
   REQUIRE(result);
   CHECK(SUCCEEDED(*result));
   CHECK(GetRealDesc(real).BufferDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM);
   CHECK(ReadFake("FakeReShadeGetResizeFormat", reshade.Get()) == uint32_t(DXGI_FORMAT_R8G8B8A8_UNORM));
   CHECK(GetHeldBuffer(reshade.Get(), 0) == GetRealBuffer<ID3D11Texture2D>(real, 0).Get());
   CHECK(ReadFake("FakeReShadeGetColorSpace", reshade.Get()) == uint32_t(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020));
   CHECK(ProbeRead(reshade.Get()) == static_cast<IUnknown*>(Display::Swapchain::DXGI::GetGameBuffer(real).Get()));
   // The game's view didn't move
   DXGI_SWAP_CHAIN_DESC game_desc = {};
   REQUIRE(SUCCEEDED(swapchain->GetDesc(&game_desc)));
   CHECK(game_desc.BufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
   CHECK(Display::Swapchain::DXGI::GetGameBuffer(real).Get() == static_cast<IUnknown*>(mirror.Get()));

   // And back, so the swapchain is as Luma set it
   {
      Hooks::Scope::Guard scope;
      result = Hooks::SwapchainProxy::ResizeBuffersThroughReShade(real, game_arguments, real_arguments(before.BufferDesc.Format), GetRecordedColorSpace(real));
   }
   REQUIRE(result);
   CHECK(SUCCEEDED(*result));
   CHECK(GetRealDesc(real).BufferDesc.Format == before.BufferDesc.Format);
}

TEST_CASE("ReShade D3D11 Mirror: without ReShade's wrapper in the private data nothing is resized, and that's logged once")
{
   InstallHooks();
   GameCallbacks game;
   WrappedSwapchains wrapped;
   HiddenWindow window;
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   CreateD3D11Device(device, context);
   LoadFakeReShade();
   ComPtr<IDXGISwapChain1> swapchain = CreateSwapchain(device.Get(), window.hwnd);
   IDXGISwapChain* real = Hooks::SwapchainProxy::Unwrap(swapchain.Get());
   const DXGI_SWAP_CHAIN_DESC before = GetRealDesc(real);
   const std::filesystem::path log = std::filesystem::temp_directory_path() / "LumaTestsReShadeWrapper.log";
   Log::Open(log);
   for (int i = 0; i < 2; i++)
   {
      Hooks::Scope::Guard scope;
      CHECK_FALSE(Hooks::SwapchainProxy::ResizeBuffersThroughReShade(real, {2, 320, 180, DXGI_FORMAT_R8G8B8A8_UNORM, 0},
         {before.BufferCount, before.BufferDesc.Width, before.BufferDesc.Height, DXGI_FORMAT_R10G10B10A2_UNORM, before.Flags}, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020));
      CHECK_FALSE(Hooks::SwapchainProxy::SetColorSpace1ThroughReShade(real, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020));
   }
   Log::Close();
   CHECK(GetRealDesc(real).BufferDesc.Format == before.BufferDesc.Format);
   std::stringstream text;
   text << std::ifstream(log).rdbuf();
   std::error_code error;
   std::filesystem::remove(log, error);
   const std::string contents = text.str();
   const std::string line = std::format("swapchain {} has no ReShade wrapper", static_cast<void*>(real));
   size_t count = 0;
   for (size_t at = contents.find(line); at != std::string::npos; at = contents.find(line, at + 1))
      count++;
   CHECK(count == 1);
}

TEST_CASE("ReShade D3D12 Mirror: ReShade's runtime reads every real buffer, and at present the real index, while the game's calls get its mirrors")
{
   InstallHooks();
   GameCallbacks game;
   WrappedSwapchains wrapped;
   HiddenWindow window;
   LoadFakeReShade();
   ComPtr<ID3D12Device> device = CreateD3D12Device();
   const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   ComPtr<ID3D12CommandQueue> queue;
   REQUIRE(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));
   ComPtr<IDXGISwapChain1> swapchain = CreateSwapchain(queue.Get(), window.hwnd);
   IDXGISwapChain* real = Hooks::SwapchainProxy::Unwrap(swapchain.Get());
   REQUIRE(Hooks::DXGI::GetReShadeSwapchainView(real) == Hooks::SwapchainProxy::ReShadeSwapchainView::RealInsideWindows);
   ComPtr<IDXGISwapChain> reshade = WrapInFakeReShade(swapchain.Get(), true);
   const DXGI_SWAP_CHAIN_DESC real_desc = GetRealDesc(real);
   REQUIRE(ReadFake("FakeReShadeGetHeldCount", reshade.Get()) == real_desc.BufferCount);
   for (UINT i = 0; i < real_desc.BufferCount; i++)
   {
      const ComPtr<ID3D12Resource> real_buffer = GetRealBuffer<ID3D12Resource>(real, i);
      CHECK(GetHeldBuffer(reshade.Get(), i) == real_buffer.Get());
      ComPtr<ID3D12Resource> through_reshade;
      REQUIRE(SUCCEEDED(reshade->GetBuffer(i, IID_PPV_ARGS(&through_reshade))));
      CHECK(through_reshade.Get() != real_buffer.Get());
   }

   for (int frame = 0; frame < 3; frame++)
   {
      UINT real_index = 0;
      {
         Hooks::Scope::Guard scope;
         ComPtr<IDXGISwapChain3> real3;
         REQUIRE(SUCCEEDED(real->QueryInterface(IID_PPV_ARGS(&real3))));
         real_index = real3->GetCurrentBackBufferIndex();
      }
      CHECK(SUCCEEDED(reshade->Present(0, 0)));
      CHECK(ReadFake("FakeReShadeGetPresentIndex", reshade.Get()) == real_index);
      CHECK(GetFakeExport<IUnknown* (*)(IDXGISwapChain*)>("FakeReShadeGetPresentBuffer")(reshade.Get()) == GetRealBuffer<ID3D12Resource>(real, real_index).Get());
      CHECK_FALSE(Hooks::Scope::IsThirdPartyWorkOpen());
   }

   ComPtr<ID3D12Fence> fence;
   REQUIRE(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
   queue->Signal(fence.Get(), 1);
   CHECK(WaitFor([&] { return fence->GetCompletedValue() >= 1; }));
}
