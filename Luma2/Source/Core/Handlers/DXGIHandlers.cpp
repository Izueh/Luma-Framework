#include "Handlers/DXGIHandlers.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "Common/Api.hpp"
#include "Game/Game.hpp"
#include "Hooks/Scope.hpp"
#include "Settings/Registry.hpp"
#include "Trace/Info.hpp"
#include "Trace/Mcp.hpp"
#include "UI/Overlay.hpp"
#include "Utils/Log.hpp"
#include "Utils/Strings.hpp"
#include "Validation/D3D11Validation.hpp"
#include "Validation/D3D12Validation.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Handlers {
namespace {
struct KnownSwapchain
{
   const void* swapchain;
   Presenter presenter;
   const void* device;
};

// DXGI can't be asked what a swapchain was created with. Never held with another lock.
std::shared_mutex known_mutex;
std::vector<KnownSwapchain> known;

std::optional<Presenter> FindPresenter(const void* swapchain)
{
   std::shared_lock lock(known_mutex);
   const auto it = std::ranges::find(known, swapchain, &KnownSwapchain::swapchain);
   return it != known.end() ? std::optional(it->presenter) : std::nullopt;
}

std::optional<Presenter> GetPresenter(IUnknown* creator)
{
   if (ComPtr<ID3D12CommandQueue> queue; SUCCEEDED(creator->QueryInterface(IID_PPV_ARGS(&queue))))
      return queue.Get();
   if (ComPtr<ID3D11Device> device; SUCCEEDED(creator->QueryInterface(IID_PPV_ARGS(&device))))
      return device.Get();
   return std::nullopt;
}

// Overlays hooking DXGI too explain most present and resize issues
constexpr std::array<const wchar_t*, 4> OVERLAY_MODULES = {
   L"RTSSHooks64.dll",
   L"GameOverlayRenderer64.dll", // Steam
   L"DiscordHook64.dll",
   L"nvspcap64.dll", // NVIDIA overlay
};

// When the set changes: overlays are often injected after the first swapchain
void LogOverlays()
{
   std::string loaded;
   for (const wchar_t* name : OVERLAY_MODULES)
   {
      if (GetModuleHandleW(name))
         loaded += (loaded.empty() ? "" : ", ") + Utils::Strings::ToUtf8(name);
   }
   static std::mutex mutex;
   static std::string logged;
   std::scoped_lock lock(mutex);
   if (loaded != logged)
   {
      Utils::Log::Info("DXGI: overlays loaded: {}", loaded.empty() ? "none" : loaded);
      logged = loaded;
   }
}

// Logs the swapchain's real description (inside the scope), and reports it to Trace
void Describe(IDXGISwapChain* swapchain, const Presenter& presenter, const char* event)
{
   DXGI_SWAP_CHAIN_DESC desc = {};
   swapchain->GetDesc(&desc);
   const std::string_view api = GetName(std::holds_alternative<ID3D11Device*>(presenter) ? Api::D3D11 : Api::D3D12);
   Utils::Log::Info("DXGI: {} swapchain {} {} ({}x{}, format {}, {} buffers, flags 0x{:X}, window {})", api, static_cast<void*>(swapchain), event,
      desc.BufferDesc.Width, desc.BufferDesc.Height, uint32_t(desc.BufferDesc.Format), desc.BufferCount, desc.Flags, static_cast<void*>(desc.OutputWindow));
   Trace::SetSwapchain({reinterpret_cast<uint64_t>(swapchain), std::string(api), desc.BufferDesc.Width, desc.BufferDesc.Height, uint32_t(desc.BufferDesc.Format),
      desc.BufferCount});
}

// Null for D3D11CreateDeviceAndSwapChain, before the device exists
bool IsD3D11(IUnknown* creator)
{
   if (!creator)
      return true;
   const std::optional<Presenter> presenter = GetPresenter(creator);
   return presenter && std::holds_alternative<ID3D11Device*>(*presenter);
}
} // namespace

void PreCreateSwapChain(IUnknown* creator, DXGI_SWAP_CHAIN_DESC& desc)
{
   if (IsD3D11(creator))
      Game::Call(Game::GetD3D11().pre_create_swapchain, desc);
}

void PreCreateSwapChainForHwnd(IUnknown* creator, DXGI_SWAP_CHAIN_DESC1& desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen_desc, HWND hwnd)
{
   if (IsD3D11(creator))
      Game::Call(Game::GetD3D11().pre_create_swapchain_for_hwnd, desc, fullscreen_desc, hwnd);
}

void PostCreateSwapChain(IDXGISwapChain* swapchain, IUnknown* creator)
{
   const std::optional<Presenter> presenter = GetPresenter(creator);
   if (!presenter)
   {
      Utils::Log::Warning("DXGI: swapchain {} was created with {}, which is neither a D3D11 device nor a D3D12 queue", static_cast<void*>(swapchain),
         static_cast<void*>(creator));
      return;
   }
   // The real device, not the game's proxy
   const void* device = nullptr;
   if (auto* const* queue = std::get_if<ID3D12CommandQueue*>(&*presenter))
   {
      ComPtr<ID3D12Device> queue_device;
      if (SUCCEEDED((*queue)->GetDevice(IID_PPV_ARGS(&queue_device))))
         device = queue_device.Get();
   }
   else
   {
      device = std::get<ID3D11Device*>(*presenter);
   }
   {
      std::scoped_lock lock(known_mutex);
      known.push_back({swapchain, *presenter, device});
   }
   Describe(swapchain, *presenter, "created");
   LogOverlays();
   UI::OnSwapchainCreated(swapchain, *presenter, device);
   if (auto* const* d3d11_device = std::get_if<ID3D11Device*>(&*presenter))
      Game::Call(Game::GetD3D11().post_create_swapchain, swapchain, *d3d11_device);
   else
      Game::Call(Game::GetD3D12().post_create_swapchain, swapchain, std::get<ID3D12CommandQueue*>(*presenter));
}

void OnSwapChainDestroyed(const IDXGISwapChain* swapchain)
{
   if (const std::optional<Presenter> presenter = FindPresenter(swapchain))
   {
      if (auto* const* device = std::get_if<ID3D11Device*>(&*presenter))
         Game::Call(Game::GetD3D11().on_swapchain_destroyed, *device, swapchain);
   }
   UI::OnSwapchainDestroyed(swapchain);
   Trace::RemoveSwapchain(swapchain);
   {
      std::scoped_lock lock(known_mutex);
      std::erase_if(known, [swapchain](const KnownSwapchain& entry) { return entry.swapchain == swapchain; });
   }
   Utils::Log::Info("DXGI: swapchain {} destroyed", static_cast<const void*>(swapchain));
}

void PreResizeBuffers(IDXGISwapChain* swapchain, UINT& count, UINT width, UINT height, DXGI_FORMAT& format, UINT& flags, const UINT* node_masks,
   IUnknown* const* present_queues)
{
   const std::optional<Presenter> presenter = FindPresenter(swapchain);
   if (presenter && std::holds_alternative<ID3D11Device*>(*presenter))
      Game::Call(Game::GetD3D11().pre_resize_buffers, swapchain, count, width, height, format, flags, node_masks, present_queues);
}

void PostResizeBuffers(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
   const std::optional<Presenter> presenter = FindPresenter(swapchain);
   if (!presenter)
      return;
   Describe(swapchain, *presenter, "resized");
   LogOverlays();
   if (std::holds_alternative<ID3D11Device*>(*presenter))
      Game::Call(Game::GetD3D11().post_resize_buffers, swapchain, count, width, height, format, flags);
   else
      Game::Call(Game::GetD3D12().post_resize_buffers, swapchain, count, width, height, format, flags);
}

bool PreSetFullscreenState(IDXGISwapChain* swapchain, BOOL fullscreen, IDXGIOutput* output)
{
   (void)output;
   Utils::Log::Info("DXGI: swapchain {} {} fullscreen", static_cast<void*>(swapchain), fullscreen ? "enters" : "leaves");
   return false;
}

void PrePresent(IDXGISwapChain* swapchain)
{
   Trace::OnPresent();
   // The one writer of the settings: changes from other threads land here
   Settings::ApplyQueued();
#if DEVELOPMENT
   Trace::Mcp::RunPendingJobs();
#endif
   if (const std::optional<Presenter> presenter = FindPresenter(swapchain))
   {
      if (auto* const* device = std::get_if<ID3D11Device*>(&*presenter))
         Game::Call(Game::GetD3D11().pre_present, swapchain, *device);
      else
         Game::Call(Game::GetD3D12().pre_present, swapchain, std::get<ID3D12CommandQueue*>(*presenter));
   }
   // Last, on top of everything the game and Luma drew
   UI::OnPresent(swapchain);
   Validation::D3D11::ReadMessages();
   Validation::D3D12::ReadMessages();
}

void PostPresent(IDXGISwapChain* swapchain)
{
   if (const std::optional<Presenter> presenter = FindPresenter(swapchain))
   {
      if (auto* const* device = std::get_if<ID3D11Device*>(&*presenter))
         Game::Call(Game::GetD3D11().post_present, swapchain, *device);
      else
         Game::Call(Game::GetD3D12().post_present, swapchain, std::get<ID3D12CommandQueue*>(*presenter));
   }
}

void ForgetSwapchains(const void* device)
{
   std::scoped_lock lock(known_mutex);
   std::erase_if(known, [device](const KnownSwapchain& entry) { return entry.device == device; });
}
} // namespace Handlers
} // namespace Luma
