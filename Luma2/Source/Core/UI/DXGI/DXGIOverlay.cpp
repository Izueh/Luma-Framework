#include "UI/DXGI/DXGIOverlay.hpp"

#include <optional>
#include <variant>

#include <dxgi.h>
#include <imgui.h>

#include "Handlers/DXGI/DXGIHandlers.hpp"
#include "Hooks/HookScope.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "UI/D3D11/D3D11Renderer.hpp"
#include "UI/D3D12/D3D12Renderer.hpp"
#include "UI/Overlay.hpp"
#include "UI/OverlayEncoding.hpp"
#include "UI/OverlayInput.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace UI {
namespace DXGI {
namespace {
// A launcher's or a capability probe's swapchain is a few pixels
constexpr UINT MIN_SIZE = 64;
// The overlay stays on a swapchain that presented this recently
constexpr ULONGLONG HANDOFF_MS = 1000;

// Weak: the overlay detaches when the swapchain or its device goes
struct Attachment
{
   IDXGISwapChain* swapchain = nullptr;
   Swapchain::DXGI::Presenter presenter;
   const void* device = nullptr;
   ULONGLONG last_present = 0;
};

// Under UI::Lock
std::optional<Attachment> attached;

bool IsEligible(const DXGI_SWAP_CHAIN_DESC& desc)
{
   return desc.OutputWindow && IsWindow(desc.OutputWindow) && desc.BufferDesc.Width >= MIN_SIZE && desc.BufferDesc.Height >= MIN_SIZE;
}

// With the lock held
bool Attach(IDXGISwapChain* swapchain, Swapchain::DXGI::Presenter presenter, const void* device)
{
   DXGI_SWAP_CHAIN_DESC desc = {};
   if (FAILED(swapchain->GetDesc(&desc)) || !AttachWindow(desc.OutputWindow))
      return false;
   attached = Attachment{swapchain, presenter, device, GetTickCount64()};
   Log::Info("UI: overlay attached to swapchain {} (window {})", static_cast<void*>(swapchain), static_cast<void*>(desc.OutputWindow));
   return true;
}

// With the lock held. ImGui's docking branch destroys the platform windows when a renderer shuts down, so a renderer is only
// recreated with a new context: detached, then attached again.
void Detach()
{
   if (!attached)
      return;
   UI::D3D11::Shutdown();
   UI::D3D12::Shutdown();
   DetachWindow();
   Log::Info("UI: overlay detached from swapchain {}", static_cast<void*>(attached->swapchain));
   attached.reset();
}

// Each API's renderer, by what presents (Presenter's types)
template <typename T>
struct Renderer;

template <>
struct Renderer<ID3D11Device>
{
   static constexpr auto NeedsRecreation = &UI::D3D11::NeedsRecreation;
   static constexpr auto NewFrame = &UI::D3D11::NewFrame;
   static constexpr auto Render = &UI::D3D11::Render;
};

template <>
struct Renderer<ID3D12CommandQueue>
{
   static constexpr auto NeedsRecreation = &UI::D3D12::NeedsRecreation;
   static constexpr auto NewFrame = &UI::D3D12::NewFrame;
   static constexpr auto Render = &UI::D3D12::Render;
};

// How the overlay is written to the swapchain's buffer
Encoding::Output GetOutput(IDXGISwapChain* swapchain, const DXGI_SWAP_CHAIN_DESC& desc)
{
   return {Encoding::GetKind(desc.BufferDesc.Format, Handlers::GetColorSpace(swapchain)), Settings::Get(Settings::Core::OVERLAY_HDR_PAPER_WHITE)};
}

// Whether a frame was drawn
template <typename T>
bool Draw(IDXGISwapChain* swapchain, T* presenter, const DXGI_SWAP_CHAIN_DESC& desc)
{
   using Traits = Renderer<T>;
   // A new format or buffer count needs new renderer objects, and with them a new context (see Detach)
   if (Traits::NeedsRecreation(desc))
   {
      const void* device = attached->device;
      Detach();
      if (!Attach(swapchain, presenter, device))
         return false;
   }
   if (!Traits::NewFrame(presenter, desc))
      return false;
   BuildFrame({desc.BufferDesc.Width, desc.BufferDesc.Height});
   Traits::Render(presenter, swapchain, ImGui::GetDrawData(), GetOutput(swapchain, desc));
   return true;
}
} // namespace

void AttachOverlay(IDXGISwapChain* swapchain, const Swapchain::DXGI::Presenter& presenter, const void* device)
{
   Hooks::Scope::Guard scope;
   const auto lock = Lock();
   const ULONGLONG now = GetTickCount64();
   if (attached && attached->swapchain == swapchain)
   {
      attached->last_present = now;
      return;
   }
   if (attached && now - attached->last_present < HANDOFF_MS)
      return;
   DXGI_SWAP_CHAIN_DESC desc = {};
   if (FAILED(swapchain->GetDesc(&desc)) || !IsEligible(desc))
      return;
   Detach();
   Attach(swapchain, presenter, device);
}

void DrawOverlay(IDXGISwapChain* swapchain)
{
   Hooks::Scope::Guard scope;
   const auto lock = Lock();
   if (!attached || attached->swapchain != swapchain || !BeginPresent())
      return;
   Input::ApplyCapturedKey();
   DXGI_SWAP_CHAIN_DESC desc = {};
   if (FAILED(swapchain->GetDesc(&desc)))
      return;
   // A copy: drawing may attach again
   if (std::visit([&](auto* presenter) { return Draw(swapchain, presenter, desc); }, Swapchain::DXGI::Presenter(attached->presenter)))
      EndFrame();
}

void DetachFromSwapchain(const void* swapchain)
{
   const auto lock = Lock();
   if (attached && swapchain == attached->swapchain)
      Detach();
}

void DetachFromDevice(const void* device)
{
   Hooks::Scope::Guard scope;
   const auto lock = Lock();
   if (attached && device == attached->device)
      Detach();
}

const void* GetSwapchain()
{
   const auto lock = Lock();
   return attached ? attached->swapchain : nullptr;
}
} // namespace DXGI
} // namespace UI
} // namespace Luma
