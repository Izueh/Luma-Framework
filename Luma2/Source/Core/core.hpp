#pragma once

#include <optional>
#include <span>
#include <string_view>

#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>

#include "common/api.hpp"
#include "settings/registry.hpp"
#include "utils/log.hpp"

namespace Luma {
namespace Core {
// Only registers a loader notification. Luma initializes when d3d11.dll or d3d12.dll loads, still under the loader lock:
// no dll loads, no waiting on other threads.
BOOL DllMain(HMODULE module, DWORD reason, LPVOID reserved);
} // namespace Core

namespace Game {
struct Link
{
   std::string_view label;
   std::string_view url;
};

struct Info
{
   // Shown in the overlay's footer. Must outlive Luma (string literals, static arrays).
   std::string_view name;
   std::string_view version;
   std::span<const std::string_view> credits;
   std::span<const Link> links;
   // The only API the game uses: the other one's hooks are never installed. Unset, Luma handles both.
   std::optional<Api> api;
};

// Implemented by the game. Runs once, under the loader lock: declarations only (no dll loads, threads or API objects).
void Init();

void SetInfo(const Info& info);

// One callback per event and API, null by default. Must be set in Init: hooks are chosen right after it, and they're read
// without a lock. Setting one twice replaces the first. They run on the thread of the API call that triggered them, inside
// a Luma scope, and receive the real API objects, never Luma's device proxies.
namespace Callbacks {
template <typename Device>
using DeviceCallback = void (*)(Device* device);
template <typename Presenter>
using SwapchainCallback = void (*)(IDXGISwapChain* swapchain, Presenter* presenter);

void SetOnDeviceCreated(DeviceCallback<ID3D11Device> callback);
void SetOnDeviceCreated(DeviceCallback<ID3D12Device> callback);
// The game released its last reference: release everything created on the device, which is still valid during the call
void SetOnDeviceDestroyed(DeviceCallback<ID3D11Device> callback);
void SetOnDeviceDestroyed(DeviceCallback<ID3D12Device> callback);
void SetOnSwapchainCreated(SwapchainCallback<ID3D11Device> callback);
void SetOnSwapchainCreated(SwapchainCallback<ID3D12CommandQueue> callback);
void SetOnSwapchainResized(SwapchainCallback<ID3D11Device> callback);
void SetOnSwapchainResized(SwapchainCallback<ID3D12CommandQueue> callback);
// Before every present of a swapchain Luma knows
void SetOnPresent(SwapchainCallback<ID3D11Device> callback);
void SetOnPresent(SwapchainCallback<ID3D12CommandQueue> callback);
} // namespace Callbacks
} // namespace Game
} // namespace Luma
