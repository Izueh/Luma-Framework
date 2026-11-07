#include "handlers/dxgi_handlers.hpp"

#include <algorithm>
#include <array>
#include <format>
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

#include "common/api.hpp"
#include "game/game.hpp"
#include "hooks/lifetime.hpp"
#include "hooks/scope.hpp"
#include "trace/info.hpp"
#include "ui/overlay.hpp"
#include "utils/log.hpp"
#include "utils/strings.hpp"
#include "validation/d3d11_validation.hpp"
#include "validation/d3d12_validation.hpp"

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

// Logs the swapchain's description, and reports it to Trace
void Describe(IDXGISwapChain* swapchain, const Presenter& presenter, const char* event)
{
   DXGI_SWAP_CHAIN_DESC desc = {};
   swapchain->GetDesc(&desc);
   const std::string_view api = GetName(std::holds_alternative<ID3D11Device*>(presenter) ? Api::D3D11 : Api::D3D12);
   Utils::Log::Info("DXGI: {} swapchain {} {} ({}x{}, format {}, {} buffers, flags 0x{:X}, window {})", api, static_cast<void*>(swapchain), event,
      desc.BufferDesc.Width, desc.BufferDesc.Height, uint32_t(desc.BufferDesc.Format), desc.BufferCount, desc.Flags, static_cast<void*>(desc.OutputWindow));
   Trace::SetSwapchain({reinterpret_cast<uint64_t>(swapchain), std::string(api), desc.BufferDesc.Width, desc.BufferDesc.Height, uint32_t(desc.BufferDesc.Format), desc.BufferCount});
}

void OnSwapchainDestroyed(const void* swapchain)
{
   UI::OnSwapchainDestroyed(swapchain);
   Trace::RemoveSwapchain(swapchain);
   {
      std::scoped_lock lock(known_mutex);
      std::erase_if(known, [swapchain](const KnownSwapchain& entry) { return entry.swapchain == swapchain; });
   }
   Utils::Log::Info("DXGI: swapchain {} destroyed", swapchain);
}
} // namespace

void OnFactoryCreated(IDXGIFactory* factory)
{
   Utils::Log::Verbose("DXGI: factory created at {}", static_cast<void*>(factory));
}

void OnSwapchainCreated(IDXGISwapChain* swapchain, IUnknown* creator)
{
   Presenter presenter;
   const void* device = nullptr;
   if (ComPtr<ID3D12CommandQueue> queue; SUCCEEDED(creator->QueryInterface(IID_PPV_ARGS(&queue))))
   {
      presenter = queue.Get();
      // The real device, not the game's proxy
      Hooks::Scope::Guard scope;
      ComPtr<ID3D12Device> queue_device;
      if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&queue_device))))
         device = queue_device.Get();
   }
   else if (ComPtr<ID3D11Device> d3d11_device; SUCCEEDED(creator->QueryInterface(IID_PPV_ARGS(&d3d11_device))))
   {
      presenter = d3d11_device.Get();
      device = d3d11_device.Get();
   }
   else
   {
      Utils::Log::Warning("DXGI: swapchain {} was created with {}, which is neither a D3D11 device nor a D3D12 queue", static_cast<void*>(swapchain),
         static_cast<void*>(creator));
      return;
   }
   if (!Hooks::Lifetime::Track(swapchain, &OnSwapchainDestroyed))
      return;
   {
      std::scoped_lock lock(known_mutex);
      known.push_back({swapchain, presenter, device});
   }
   Describe(swapchain, presenter, "created");
   LogOverlays();
   UI::OnSwapchainCreated(swapchain, presenter, device);
   Game::CallSwapchainCreated(swapchain, presenter);
}

void OnPresent(IDXGISwapChain* swapchain)
{
   Trace::OnPresent();
   if (Game::HasPresentCallbacks())
   {
      if (const std::optional<Presenter> presenter = FindPresenter(swapchain))
         Game::CallPresent(swapchain, *presenter);
   }
   // Last, on top of everything the game and Luma drew
   UI::OnPresent(swapchain);
   Validation::D3D11::ReadMessages();
   Validation::D3D12::ReadMessages();
}

void OnSwapchainResized(IDXGISwapChain* swapchain)
{
   const std::optional<Presenter> presenter = FindPresenter(swapchain);
   if (!presenter)
      return;
   Describe(swapchain, *presenter, "resized");
   LogOverlays();
   Game::CallSwapchainResized(swapchain, *presenter);
}

void OnFullscreenStateChanged(IDXGISwapChain* swapchain, BOOL fullscreen)
{
   Utils::Log::Info("DXGI: swapchain {} {} fullscreen", static_cast<void*>(swapchain), fullscreen ? "entered" : "left");
}

void ForgetSwapchains(const void* device)
{
   std::scoped_lock lock(known_mutex);
   std::erase_if(known, [device](const KnownSwapchain& entry) { return entry.device == device; });
}
} // namespace Handlers
} // namespace Luma
