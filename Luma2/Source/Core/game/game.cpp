#include "game/game.hpp"

#include <format>
#include <variant>

#include "hooks/scope.hpp"
#include "utils/log.hpp"

namespace Luma {
namespace Game {
namespace {
template <typename Device, typename Presenter>
struct Events
{
   Callbacks::DeviceCallback<Device> device_created = nullptr;
   Callbacks::DeviceCallback<Device> device_destroyed = nullptr;
   Callbacks::SwapchainCallback<Presenter> swapchain_created = nullptr;
   Callbacks::SwapchainCallback<Presenter> swapchain_resized = nullptr;
   Callbacks::SwapchainCallback<Presenter> present = nullptr;

   size_t Count() const
   {
      return size_t(device_created != nullptr) + (device_destroyed != nullptr) + (swapchain_created != nullptr) + (swapchain_resized != nullptr) + (present != nullptr);
   }
};

// Only written while Init runs, so no lock
struct Declarations
{
   Info info;
   Events<ID3D11Device, ID3D11Device> d3d11;
   Events<ID3D12Device, ID3D12CommandQueue> d3d12;
};

Declarations declarations;

auto& GetEvents(ID3D11Device*)
{
   return declarations.d3d11;
}

auto& GetEvents(ID3D12CommandQueue*)
{
   return declarations.d3d12;
}

template <typename T>
void Set(T& slot, T callback, const char* event)
{
   if (slot && callback != slot)
      Utils::Log::Warning("Game: the {} callback was set twice, the second one replaces the first", event);
   slot = callback;
}

template <typename T, typename... Arguments>
void Call(T callback, Arguments... arguments)
{
   if (!callback)
      return;
   Hooks::Scope::Guard scope;
   callback(arguments...);
}
} // namespace

void SetInfo(const Info& info)
{
   declarations.info = info;
}

namespace Callbacks {
void SetOnDeviceCreated(DeviceCallback<ID3D11Device> callback)
{
   Set(declarations.d3d11.device_created, callback, "OnDeviceCreated");
}

void SetOnDeviceCreated(DeviceCallback<ID3D12Device> callback)
{
   Set(declarations.d3d12.device_created, callback, "OnDeviceCreated");
}

void SetOnDeviceDestroyed(DeviceCallback<ID3D11Device> callback)
{
   Set(declarations.d3d11.device_destroyed, callback, "OnDeviceDestroyed");
}

void SetOnDeviceDestroyed(DeviceCallback<ID3D12Device> callback)
{
   Set(declarations.d3d12.device_destroyed, callback, "OnDeviceDestroyed");
}

void SetOnSwapchainCreated(SwapchainCallback<ID3D11Device> callback)
{
   Set(declarations.d3d11.swapchain_created, callback, "OnSwapchainCreated");
}

void SetOnSwapchainCreated(SwapchainCallback<ID3D12CommandQueue> callback)
{
   Set(declarations.d3d12.swapchain_created, callback, "OnSwapchainCreated");
}

void SetOnSwapchainResized(SwapchainCallback<ID3D11Device> callback)
{
   Set(declarations.d3d11.swapchain_resized, callback, "OnSwapchainResized");
}

void SetOnSwapchainResized(SwapchainCallback<ID3D12CommandQueue> callback)
{
   Set(declarations.d3d12.swapchain_resized, callback, "OnSwapchainResized");
}

void SetOnPresent(SwapchainCallback<ID3D11Device> callback)
{
   Set(declarations.d3d11.present, callback, "OnPresent");
}

void SetOnPresent(SwapchainCallback<ID3D12CommandQueue> callback)
{
   Set(declarations.d3d12.present, callback, "OnPresent");
}
} // namespace Callbacks

const Info& GetInfo()
{
   return declarations.info;
}

void LogDeclarations()
{
   const Info& info = declarations.info;
   Utils::Log::Info("Game: {} {} set {} D3D11 and {} D3D12 callbacks{}", info.name.empty() ? "(unnamed game)" : info.name, info.version, declarations.d3d11.Count(),
      declarations.d3d12.Count(), info.api ? std::format(", and uses {} only", GetName(*info.api)) : "");
}

void Reset()
{
   declarations = {};
}

void CallDeviceCreated(ID3D11Device* device)
{
   Call(declarations.d3d11.device_created, device);
}

void CallDeviceCreated(ID3D12Device* device)
{
   Call(declarations.d3d12.device_created, device);
}

void CallDeviceDestroyed(ID3D11Device* device)
{
   Call(declarations.d3d11.device_destroyed, device);
}

void CallDeviceDestroyed(ID3D12Device* device)
{
   Call(declarations.d3d12.device_destroyed, device);
}

void CallSwapchainCreated(IDXGISwapChain* swapchain, const Presenter& presenter)
{
   std::visit([&](auto* native) { Call(GetEvents(native).swapchain_created, swapchain, native); }, presenter);
}

void CallSwapchainResized(IDXGISwapChain* swapchain, const Presenter& presenter)
{
   std::visit([&](auto* native) { Call(GetEvents(native).swapchain_resized, swapchain, native); }, presenter);
}

void CallPresent(IDXGISwapChain* swapchain, const Presenter& presenter)
{
   std::visit([&](auto* native) { Call(GetEvents(native).present, swapchain, native); }, presenter);
}

bool HasPresentCallbacks()
{
   return declarations.d3d11.present || declarations.d3d12.present;
}
} // namespace Game
} // namespace Luma
