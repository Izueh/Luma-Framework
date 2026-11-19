#include "Handlers/DXGI/DXGIHandlers.hpp"
#include "Handlers/D3D12/D3D12Handlers.hpp"

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
#include "Game/Registry.hpp"
#include "Hooks/Hooks.hpp"
#include "Hooks/Scope.hpp"
#include "Settings/Registry.hpp"
#include "Resources/D3D11/D3D11ResourceDatabase.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Swapchain/DXGI/DXGISwapchain.hpp"
#include "Display/Levels.hpp"
#include "Display/DXGI/DXGISpoofing.hpp"
#include "Frame/Frame.hpp"
#include "Shaders/Patching.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Shaders/Database.hpp"
#include "Shaders/Shaders.hpp"
#include "System/Jobs.hpp"
#if DEVELOPMENT
#include "Trace/Capture.hpp"
#include "Trace/D3D11/D3D11Capture.hpp"
#include "Trace/D3D11/D3D11Readback.hpp"
#include "Trace/D3D12/D3D12Capture.hpp"
#include "Trace/D3D12/D3D12Readback.hpp"
#include "Trace/Readback.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Trace/D3D12/D3D12Stats.hpp"
#endif
#include "Trace/Info.hpp"
#include "Trace/Mcp.hpp"
#include "UI/Overlay.hpp"
#include "Utils/Log.hpp"
#include "Utils/Strings.hpp"
#include "Validation/D3D11/D3D11Validation.hpp"
#include "Validation/D3D12/D3D12Validation.hpp"

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

// The real device the swapchain presents for, null if Luma doesn't know it
IUnknown* FindDevice(const void* swapchain)
{
   std::shared_lock lock(known_mutex);
   const auto it = std::ranges::find(known, swapchain, &KnownSwapchain::swapchain);
   return it != known.end() ? static_cast<IUnknown*>(const_cast<void*>(it->device)) : nullptr;
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
DXGI_SWAP_CHAIN_DESC Describe(IDXGISwapChain* swapchain, const Presenter& presenter, const char* event)
{
   DXGI_SWAP_CHAIN_DESC desc = {};
   swapchain->GetDesc(&desc);
   const std::string_view api = GetName(std::holds_alternative<ID3D11Device*>(presenter) ? Api::D3D11 : Api::D3D12);
   Utils::Log::Info("DXGI: {} swapchain {} {} ({}x{}, format {}, {} buffers, flags 0x{:X}, window {})", api, static_cast<void*>(swapchain), event,
      desc.BufferDesc.Width, desc.BufferDesc.Height, uint32_t(desc.BufferDesc.Format), desc.BufferCount, desc.Flags, static_cast<void*>(desc.OutputWindow));
   Trace::SetSwapchain({reinterpret_cast<uint64_t>(swapchain), std::string(api), desc.BufferDesc.Width, desc.BufferDesc.Height, uint32_t(desc.BufferDesc.Format),
      desc.BufferCount});
   return desc;
}

// Where 1.0, ReShade and Special K keep a swapchain's color space, so they agree on it
constexpr GUID COLOR_SPACE_GUID = {0x18b57e4, 0x1493, 0x4953, {0xad, 0xf2, 0xde, 0x6d, 0x99, 0xcc, 0x5, 0xe5}};
// Luma's own: the format the color space was set for, which a resize to another format ends
constexpr GUID COLOR_SPACE_FORMAT_GUID = {0x6f1c2a7e, 0x3d4b, 0x4c8a, {0x9b, 0x52, 0x1e, 0x7a, 0x60, 0x2d, 0x84, 0x31}};

std::string_view GetColorSpaceName(DXGI_COLOR_SPACE_TYPE color_space)
{
   switch (color_space)
   {
   case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return "sRGB (G22 P709)";
   case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: return "scRGB (G10 P709)";
   case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return "HDR10 (G2084 P2020)";
   case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020: return "G22 P2020";
   default: return "other";
   }
}

// Null for D3D11CreateDeviceAndSwapChain, before the device exists
bool IsD3D11(IUnknown* creator)
{
   if (!creator)
      return true;
   const std::optional<Presenter> presenter = GetPresenter(creator);
   return presenter && std::holds_alternative<ID3D11Device*>(*presenter);
}

bool HasCreateSwapchainEvent(bool d3d11)
{
   return d3d11 ? Game::Registry::GetD3D11Events().on_create_swapchain != nullptr : Game::Registry::GetD3D12Events().on_create_swapchain != nullptr;
}

// The game's on_create_swapchain for the creator's API, before Core's own changes (1.0's order)
void ReportCreateSwapchain(bool d3d11, Game::SwapchainDesc& desc)
{
   if (d3d11)
      Game::Registry::Call(Game::Registry::GetD3D11Events().on_create_swapchain, desc);
   else
      Game::Registry::Call(Game::Registry::GetD3D12Events().on_create_swapchain, desc);
}

// At the end of Core's handling of a creation or resize
void ReportInitSwapchain(IDXGISwapChain* swapchain, const Presenter& presenter, bool resize)
{
   if (auto* const* device = std::get_if<ID3D11Device*>(&presenter))
   {
      Game::Registry::Call(Game::Registry::GetD3D11Events().on_init_swapchain, swapchain, *device, resize);
      return;
   }
#if DEVELOPMENT
   // DXGI creates the buffers itself: trace knows them from here
   DXGI_SWAP_CHAIN_DESC desc = {};
   swapchain->GetDesc(&desc);
   for (UINT i = 0; i < desc.BufferCount; i++)
   {
      Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
      if (SUCCEEDED(swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer))))
         Resources::Descriptors::D3D12::TrackResource(buffer.Get());
   }
#endif
   Game::Registry::Call(Game::Registry::GetD3D12Events().on_init_swapchain, swapchain, std::get<ID3D12CommandQueue*>(presenter), resize);
}
} // namespace

void PreCreateSwapChain(IUnknown* creator, DXGI_SWAP_CHAIN_DESC& desc)
{
   // The game's event first, then Luma's changes on what it made of the desc (1.0's order)
   if (const bool d3d11 = IsD3D11(creator); HasCreateSwapchainEvent(d3d11))
   {
      Game::SwapchainDesc reported = {desc.BufferDesc.Format, desc.BufferCount, desc.Flags, desc.SwapEffect, desc.SampleDesc.Count, desc.Windowed != FALSE};
      ReportCreateSwapchain(d3d11, reported);
      desc.BufferDesc.Format = reported.format;
      desc.BufferCount = reported.buffer_count;
      desc.Flags = reported.flags;
      desc.SwapEffect = reported.swap_effect;
      desc.SampleDesc.Count = reported.sample_count;
      desc.Windowed = reported.windowed;
   }
   Swapchain::DXGI::PrepareCreation(desc);
}

void PreCreateSwapChainForHwnd(IUnknown* creator, DXGI_SWAP_CHAIN_DESC1& desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen_desc, HWND hwnd)
{
   if (const bool d3d11 = IsD3D11(creator); HasCreateSwapchainEvent(d3d11))
   {
      Game::SwapchainDesc reported = {desc.Format, desc.BufferCount, desc.Flags, desc.SwapEffect, desc.SampleDesc.Count, fullscreen_desc.Windowed != FALSE};
      ReportCreateSwapchain(d3d11, reported);
      desc.Format = reported.format;
      desc.BufferCount = reported.buffer_count;
      desc.Flags = reported.flags;
      desc.SwapEffect = reported.swap_effect;
      desc.SampleDesc.Count = reported.sample_count;
      fullscreen_desc.Windowed = reported.windowed;
   }
   Swapchain::DXGI::PrepareCreation(desc, fullscreen_desc, hwnd);
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
   const DXGI_SWAP_CHAIN_DESC desc = Describe(swapchain, *presenter, "created");
   // Before the game's callbacks, which read it
   Frame::SetOutputResolution(static_cast<IUnknown*>(const_cast<void*>(device)), {desc.BufferDesc.Width, desc.BufferDesc.Height});
   // Its state and the game's buffers (whose upgrade rules match the output resolution), before anything asks for them
   Swapchain::DXGI::OnCreated(swapchain, *presenter);
   LogOverlays();
   UI::AttachOverlay(swapchain, *presenter, device);
   ReportInitSwapchain(swapchain, *presenter, false);
}

void OnOverlayBufferRequest(IDXGISwapChain* swapchain)
{
   Swapchain::DXGI::ComposeForOverlay(swapchain);
}

bool ShouldWrapSwapChain(IDXGISwapChain* swapchain)
{
   return Swapchain::DXGI::Wraps(swapchain);
}

void OnSwapChainDestroyed(const IDXGISwapChain* swapchain)
{
   UI::DetachFromSwapchain(swapchain);
   Trace::RemoveSwapchain(swapchain);
   {
      std::scoped_lock lock(known_mutex);
      std::erase_if(known, [swapchain](const KnownSwapchain& entry) { return entry.swapchain == swapchain; });
   }
   Utils::Log::Info("DXGI: swapchain {} destroyed", static_cast<const void*>(swapchain));
}

void PreResizeBuffers(IDXGISwapChain* swapchain, UINT& count, UINT& width, UINT& height, DXGI_FORMAT& format, UINT& flags, const UINT*, IUnknown* const*)
{
   const std::optional<Presenter> presenter = FindPresenter(swapchain);
   if (!presenter)
      return;
   if (const bool d3d11 = std::holds_alternative<ID3D11Device*>(*presenter); HasCreateSwapchainEvent(d3d11))
   {
      // What the game kept (zero, unknown) is reported as the swapchain's current value, and stays kept unless the game changed it
      DXGI_SWAP_CHAIN_DESC current = {};
      swapchain->GetDesc(&current);
      const DXGI_FORMAT resolved_format = format == DXGI_FORMAT_UNKNOWN ? current.BufferDesc.Format : format;
      const UINT resolved_count = count == 0 ? current.BufferCount : count;
      Game::SwapchainDesc reported = {resolved_format, resolved_count, flags, current.SwapEffect, current.SampleDesc.Count, current.Windowed != FALSE, true};
      ReportCreateSwapchain(d3d11, reported);
      if (reported.format != resolved_format)
         format = reported.format;
      if (reported.buffer_count != resolved_count)
         count = reported.buffer_count;
      flags = reported.flags;
   }
   // After the game's event: what it asked for becomes its buffers, the real ones Luma's
   Swapchain::DXGI::PrepareResize(swapchain, count, width, height, format, flags);
}

void PostResizeBuffers(IDXGISwapChain* swapchain, UINT, UINT, UINT, DXGI_FORMAT, UINT, bool succeeded)
{
   const std::optional<Presenter> presenter = FindPresenter(swapchain);
   if (!presenter)
      return;
   if (!succeeded)
   {
      Swapchain::DXGI::OnResized(swapchain, false);
      return;
   }
   const DXGI_SWAP_CHAIN_DESC desc = Describe(swapchain, *presenter, "resized");
   // A color space set for another format no longer describes the buffers: forgotten until the game sets one again
   DXGI_FORMAT color_space_format = DXGI_FORMAT_UNKNOWN;
   UINT size = sizeof(color_space_format);
   if (SUCCEEDED(swapchain->GetPrivateData(COLOR_SPACE_FORMAT_GUID, &size, &color_space_format)) && color_space_format != desc.BufferDesc.Format)
   {
      swapchain->SetPrivateData(COLOR_SPACE_GUID, 0, nullptr);
      swapchain->SetPrivateData(COLOR_SPACE_FORMAT_GUID, 0, nullptr);
      Utils::Log::Info("DXGI: swapchain {} color space forgotten, its format changed", static_cast<void*>(swapchain));
   }
   Frame::SetOutputResolution(FindDevice(swapchain), {desc.BufferDesc.Width, desc.BufferDesc.Height});
   // The game's buffers again, and the real color space (Luma's, set after the check above)
   Swapchain::DXGI::OnResized(swapchain, true);
   LogOverlays();
   ReportInitSwapchain(swapchain, *presenter, true);
}

bool PreSetFullscreenState(IDXGISwapChain* swapchain, BOOL fullscreen, IDXGIOutput* output)
{
   (void)output;
   const bool borderless = Swapchain::DXGI::SetFullscreenState(swapchain, fullscreen);
   Utils::Log::Info("DXGI: swapchain {} {} fullscreen{}", static_cast<void*>(swapchain), fullscreen ? "enters" : "leaves", borderless ? " (borderless)" : "");
   return borderless;
}

std::optional<HRESULT> GetBuffer(IDXGISwapChain* swapchain, UINT index, REFIID riid, void** buffer, bool third_party)
{
   return Swapchain::DXGI::GetBuffer(swapchain, index, riid, buffer, third_party);
}

std::optional<UINT> GetCurrentBackBufferIndex(IDXGISwapChain* swapchain, bool third_party)
{
   return Swapchain::DXGI::GetCurrentBackBufferIndex(swapchain, third_party);
}

std::optional<BOOL> GetFullscreenState(IDXGISwapChain* swapchain)
{
   return Swapchain::DXGI::GetFullscreenState(swapchain);
}

bool PreSetColorSpace(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE color_space)
{
   return Swapchain::DXGI::SetColorSpace(swapchain, color_space);
}

void PostCheckColorSpaceSupport(IDXGISwapChain*, DXGI_COLOR_SPACE_TYPE color_space, UINT& support)
{
   Display::Spoofing::DXGI::OnColorSpaceSupport(color_space, support);
}

void PostGetOutputDesc1(IDXGIOutput6*, DXGI_OUTPUT_DESC1& desc)
{
   Display::Spoofing::DXGI::OnOutputDesc(desc);
}

bool PreSetHdrMetadata(IDXGISwapChain* swapchain, DXGI_HDR_METADATA_TYPE type, UINT size, const void* metadata)
{
   return Swapchain::DXGI::SetHdrMetadata(swapchain, type, size, metadata);
}

void PostSetColorSpace(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space)
{
   // Logged when it changes: games set it again after every resize
   if (GetColorSpace(swapchain) == color_space)
      return;
   swapchain->SetPrivateData(COLOR_SPACE_GUID, sizeof(color_space), &color_space);
   DXGI_SWAP_CHAIN_DESC desc = {};
   swapchain->GetDesc(&desc);
   swapchain->SetPrivateData(COLOR_SPACE_FORMAT_GUID, sizeof(desc.BufferDesc.Format), &desc.BufferDesc.Format);
   Utils::Log::Info("DXGI: swapchain {} color space {} ({}), format {}", static_cast<void*>(swapchain), GetColorSpaceName(color_space), uint32_t(color_space),
      uint32_t(desc.BufferDesc.Format));
}

std::optional<DXGI_COLOR_SPACE_TYPE> GetColorSpace(IDXGISwapChain* swapchain)
{
   DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RESERVED;
   UINT size = sizeof(color_space);
   if (FAILED(swapchain->GetPrivateData(COLOR_SPACE_GUID, &size, &color_space)) || size != sizeof(color_space))
      return std::nullopt;
   return color_space;
}

void PrePresent(IDXGISwapChain* swapchain, UINT sync_interval, UINT& flags)
{
   flags = Swapchain::DXGI::GetPresentFlags(swapchain, sync_interval, flags);
   Trace::RecordPresent();
   Hooks::VerifyOnPresent();
#if DEVELOPMENT
   // A capture is one frame, present to present: what's drawn next (the game's present callback, the overlay) isn't in it. D3D12's
   // contents were copied at the calls; its events reached the frame when their lists were executed.
   if (const std::optional<Presenter> presenting = FindPresenter(swapchain); presenting && std::holds_alternative<ID3D12CommandQueue*>(*presenting))
   {
      Trace::Capture::D3D12::ReadBackTracked();
      // Before the overlay: a swapchain readback is the game's image
      Trace::Readback::D3D12::ReadBack(swapchain, std::get<ID3D12CommandQueue*>(*presenting));
   }
   else
   {
      Trace::Capture::D3D11::ReadBackConstantBuffers();
      Trace::Capture::D3D11::ReadBackTracked();
      if (presenting)
         Trace::Readback::D3D11::ReadBack(swapchain, std::get<ID3D11Device*>(*presenting));
   }
   Trace::Readback::OnPresent();
   Trace::Capture::AdvanceFrame();
#endif
   // The one writer of the settings: changes from other threads land here
   Settings::ApplyQueued();
#if DEVELOPMENT
   Trace::Mcp::RunPendingJobs();
#endif
   // Finished jobs (a shader build) are published before the device's objects follow them, then the frame counts (retirement is by frame)
   System::Jobs::RunFinished();
#if DEVELOPMENT || TEST
   if (Shaders::IsNeeded())
      Shaders::Database::QueueDumps();
#endif
   // The shaders the game created since, or all of them after a recipe change, patched on a job
   Shaders::Patching::OnPresent();
   Shaders::CountFrame();
   if (const std::optional<Presenter> presenter = FindPresenter(swapchain))
   {
      if (auto* const* device = std::get_if<ID3D11Device*>(&*presenter))
      {
         Game::Registry::Call(Game::Registry::GetD3D11Events().on_present, swapchain, *device);
         // After the game's callback: what its passes bound this frame is retired from now on
         if (Shaders::IsNeeded())
            Shaders::Variants::D3D11::PublishVariants(*device);
         Resources::Database::D3D11::UpdateShaderObjects(*device);
      }
      else
      {
         Game::Registry::Call(Game::Registry::GetD3D12Events().on_present, swapchain, std::get<ID3D12CommandQueue*>(*presenter));
         // After the game's callback, as D3D11's variants: pipelines whose choice changed are made again
         PresentD3D12();
#if DEVELOPMENT
         Trace::Stats::D3D12::CountFrame();
#endif
      }
   }
   // A switch of the game's own HDR option this frame, then the game's image into the real buffer, once the game's frame is done (its
   // on_present included)
   Display::Levels::Update();
   Swapchain::DXGI::Compose(swapchain);
   // Last, on top of everything the game and Luma drew
   UI::DrawOverlay(swapchain);
   // After the game's on_present and the overlay, as 1.0: what they changed (its settings object, the dev sliders) uploads from
   // the next draw
   Resources::LumaCBuffers::MarkSettingsDirty();
   Validation::D3D11::ReadMessages();
   Validation::D3D12::ReadMessages();
}

bool ComposesWholeBuffer(IDXGISwapChain* swapchain)
{
   return Swapchain::DXGI::ComposesWholeBuffer(swapchain);
}

// The game's part of a present is on_present, inside Core's handling
void PostPresent(IDXGISwapChain* swapchain)
{
   Swapchain::DXGI::OnPresented(swapchain);
}

void ForgetSwapchains(const void* device)
{
   std::scoped_lock lock(known_mutex);
   std::erase_if(known, [device](const KnownSwapchain& entry) { return entry.device == device; });
}
} // namespace Handlers
} // namespace Luma
