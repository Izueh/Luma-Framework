#include "Handlers/DXGI/DXGIHandlers.hpp"
#include "Handlers/D3D11/D3D11Handlers.hpp"
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
#include "Effects/SR/D3D11/D3D11SuperResolution.hpp"
#include "Effects/SR/SuperResolution.hpp"
#include "ReShade/ReShade.hpp"
#include "Game/GameRegistry.hpp"
#include "Hooks/HookManager.hpp"
#include "Hooks/HookScope.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D11/D3D11ResourceDatabase.hpp"
#include "Resources/D3D11/D3D11Samplers.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Display/DXGI/DXGISwapchain.hpp"
#include "Display/DisplayMode.hpp"
#include "Display/DXGI/DXGISpoofing.hpp"
#include "FrameState/FrameState.hpp"
#include "Shaders/ShaderPatching.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/Shaders.hpp"
#include "System/JobQueue.hpp"
#if DEVELOPMENT
#include "Trace/DebugDraw.hpp"
#include "Trace/FrameCapture.hpp"
#include "Trace/D3D11/D3D11Capture.hpp"
#include "Trace/D3D11/D3D11Readback.hpp"
#include "Trace/D3D12/D3D12Capture.hpp"
#include "Trace/D3D12/D3D12Readback.hpp"
#include "Trace/ResourceReadback.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Trace/D3D12/D3D12Stats.hpp"
#endif
#include "Trace/RuntimeStatus.hpp"
#include "Mcp/McpServer.hpp"
#include "UI/DXGI/DXGIOverlay.hpp"
#include "Log/Log.hpp"
#include "Utils/Strings.hpp"
#include "Validation/D3D11/D3D11Validation.hpp"
#include "Validation/D3D12/D3D12Validation.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Handlers {
namespace {
using Display::Swapchain::DXGI::GetApi;
using Display::Swapchain::DXGI::Presenter;

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
      Log::Info("DXGI: overlays loaded: {}", loaded.empty() ? "none" : loaded);
      logged = loaded;
   }
}

DXGI_SWAP_CHAIN_DESC Describe(IDXGISwapChain* swapchain, const Presenter& presenter, const char* event)
{
   DXGI_SWAP_CHAIN_DESC desc = {};
   swapchain->GetDesc(&desc);
   const std::string_view api = GetName(GetApi(presenter));
   Log::Info("DXGI: {} swapchain {} {} ({}x{}, format {}, {} buffers, flags 0x{:X}, window {})", api, static_cast<void*>(swapchain), event,
      desc.BufferDesc.Width, desc.BufferDesc.Height, uint32_t(desc.BufferDesc.Format), desc.BufferCount, desc.Flags, static_cast<void*>(desc.OutputWindow));
   Trace::SetSwapchain({reinterpret_cast<uint64_t>(swapchain), std::string(api), desc.BufferDesc.Width, desc.BufferDesc.Height, uint32_t(desc.BufferDesc.Format),
      desc.BufferCount});
   return desc;
}

// Empty for a creator that isn't a device or queue of an API the game uses: its swapchain is left alone
std::optional<Api> GetCreatorApi(IUnknown* creator)
{
   const std::optional<Presenter> presenter = creator ? GetPresenter(creator) : std::nullopt;
   if (!presenter || !Game::Registry::GetSupportedApis().Has(GetApi(*presenter)))
      return std::nullopt;
   return GetApi(*presenter);
}

bool HasCreateSwapchainEvent(Api api)
{
   switch (api)
   {
   case Api::D3D11: return Game::Registry::GetD3D11Events().on_create_swapchain != nullptr;
   case Api::D3D12: return Game::Registry::GetD3D12Events().on_create_swapchain != nullptr;
   }
   return false;
}

void ReportCreateSwapchain(Api api, Game::SwapchainDesc& desc)
{
   switch (api)
   {
   case Api::D3D11: Game::Registry::Call(Game::Registry::GetD3D11Events().on_create_swapchain, desc); break;
   case Api::D3D12: Game::Registry::Call(Game::Registry::GetD3D12Events().on_create_swapchain, desc); break;
   }
}

void ReportInitSwapchain(IDXGISwapChain* swapchain, const Presenter& presenter, bool resize)
{
   std::visit(
      Overloaded{
         [&](ID3D11Device* device) { Game::Registry::Call(Game::Registry::GetD3D11Events().on_init_swapchain, swapchain, device, resize); },
         [&](ID3D12CommandQueue* queue) {
#if DEVELOPMENT
            DXGI_SWAP_CHAIN_DESC desc = {};
            swapchain->GetDesc(&desc);
            for (UINT i = 0; i < desc.BufferCount; i++)
            {
               ComPtr<ID3D12Resource> buffer;
               if (SUCCEEDED(swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer))))
                  Resources::Descriptors::D3D12::TrackResource(buffer.Get());
            }
#endif
            Game::Registry::Call(Game::Registry::GetD3D12Events().on_init_swapchain, swapchain, queue, resize);
         },
      },
      presenter);
}
} // namespace

// Swapchain creation and destruction
void PreCreateSwapChain(IUnknown* creator, DXGI_SWAP_CHAIN_DESC& desc, std::optional<Api> known_api)
{
   // D3D11CreateDeviceAndSwapChain creates the device and swapchain together so `creator` is null,
   // but the caller already knows it is D3D11 and passes `known_api`.
   const std::optional<Api> api =
      (known_api && Game::Registry::GetSupportedApis().Has(*known_api)) ? known_api : GetCreatorApi(creator);
   if (!api)
      return;
   if (HasCreateSwapchainEvent(*api))
   {
      Game::SwapchainDesc reported = {desc.BufferDesc.Format, desc.BufferCount, desc.Flags, desc.SwapEffect, desc.SampleDesc.Count, desc.Windowed != FALSE};
      ReportCreateSwapchain(*api, reported);
      desc.BufferDesc.Format = reported.format;
      desc.BufferCount = reported.buffer_count;
      desc.Flags = reported.flags;
      desc.SwapEffect = reported.swap_effect;
      desc.SampleDesc.Count = reported.sample_count;
      desc.Windowed = reported.windowed;
   }
   Display::Swapchain::DXGI::PrepareCreation(desc);
}

void PreCreateSwapChainForHwnd(IUnknown* creator, DXGI_SWAP_CHAIN_DESC1& desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen_desc, HWND hwnd)
{
   const std::optional<Api> api = GetCreatorApi(creator);
   if (!api)
      return;
   if (HasCreateSwapchainEvent(*api))
   {
      Game::SwapchainDesc reported = {desc.Format, desc.BufferCount, desc.Flags, desc.SwapEffect, desc.SampleDesc.Count, fullscreen_desc.Windowed != FALSE};
      ReportCreateSwapchain(*api, reported);
      desc.Format = reported.format;
      desc.BufferCount = reported.buffer_count;
      desc.Flags = reported.flags;
      desc.SwapEffect = reported.swap_effect;
      desc.SampleDesc.Count = reported.sample_count;
      fullscreen_desc.Windowed = reported.windowed;
   }
   Display::Swapchain::DXGI::PrepareCreation(desc, fullscreen_desc, hwnd);
}

void PostCreateSwapChain(IDXGISwapChain* swapchain, IUnknown* creator)
{
   ReShade::FindReShadeLoadedAfterLuma();
   const std::optional<Presenter> presenter = GetPresenter(creator);
   if (!presenter || !GetCreatorApi(creator))
   {
      static std::atomic<bool> logged = false;
      if (!logged.exchange(true))
         Log::Warning("DXGI: swapchain {} was created with {}, not a device or queue of an API the game uses: left alone", static_cast<void*>(swapchain),
            static_cast<void*>(creator));
      return;
   }
   const void* device = std::visit(
      Overloaded{
         [](ID3D11Device* d3d11) -> const void* { return d3d11; },
         [](ID3D12CommandQueue* queue) -> const void* {
            ComPtr<ID3D12Device> queue_device;
            return SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&queue_device))) ? queue_device.Get() : nullptr;
         },
      },
      *presenter);
   {
      std::scoped_lock lock(known_mutex);
      known.push_back({swapchain, *presenter, device});
   }
   const DXGI_SWAP_CHAIN_DESC desc = Describe(swapchain, *presenter, "created");
   // Before the game's callbacks, which read it
   FrameState::SetOutputResolution(static_cast<IUnknown*>(const_cast<void*>(device)), {desc.BufferDesc.Width, desc.BufferDesc.Height});
   // Its state and the game's buffers (whose upgrade rules match the output resolution), before anything asks for them
   Display::Swapchain::DXGI::TrackCreated(swapchain, *presenter);
   LogOverlays();
   ReportInitSwapchain(swapchain, *presenter, false);
}

void OnSwapChainDestroyed(const IDXGISwapChain* swapchain)
{
   UI::DXGI::DetachFromSwapchain(swapchain);
   Trace::RemoveSwapchain(swapchain);
   {
      std::scoped_lock lock(known_mutex);
      std::erase_if(known, [swapchain](const KnownSwapchain& entry) { return entry.swapchain == swapchain; });
   }
   Log::Info("DXGI: swapchain {} destroyed", static_cast<const void*>(swapchain));
}

void ForgetSwapchains(const void* device)
{
   std::scoped_lock lock(known_mutex);
   std::erase_if(known, [device](const KnownSwapchain& entry) { return entry.device == device; });
}

// Proxy and overlay routing
bool ShouldWrapSwapChain(IDXGISwapChain* swapchain)
{
   return Display::Swapchain::DXGI::Wraps(swapchain);
}

Hooks::SwapchainProxy::ReShadeSwapchainView GetReShadeSwapchainView(IDXGISwapChain* swapchain)
{
   return Display::Swapchain::DXGI::GetReShadeSwapchainView(swapchain);
}

void OnOverlayBufferRequest(IDXGISwapChain* swapchain)
{
   Display::Swapchain::DXGI::ComposeForOverlay(swapchain);
}

// Resize and fullscreen
void PreResizeBuffers(IDXGISwapChain* swapchain, UINT& count, UINT& width, UINT& height, DXGI_FORMAT& format, UINT& flags, const UINT*, IUnknown* const*)
{
   const std::optional<Presenter> presenter = FindPresenter(swapchain);
   if (!presenter)
      return;
   if (const Api api = GetApi(*presenter); HasCreateSwapchainEvent(api))
   {
      DXGI_SWAP_CHAIN_DESC current = {};
      swapchain->GetDesc(&current);
      const DXGI_FORMAT resolved_format = format == DXGI_FORMAT_UNKNOWN ? current.BufferDesc.Format : format;
      const UINT resolved_count = count == 0 ? current.BufferCount : count;
      Game::SwapchainDesc reported = {resolved_format, resolved_count, flags, current.SwapEffect, current.SampleDesc.Count, current.Windowed != FALSE, true};
      ReportCreateSwapchain(api, reported);
      if (reported.format != resolved_format)
         format = reported.format;
      if (reported.buffer_count != resolved_count)
         count = reported.buffer_count;
      flags = reported.flags;
   }
   // After the game's event: what it asked for becomes its buffers, the real ones Luma's
   Display::Swapchain::DXGI::PrepareResize(swapchain, count, width, height, format, flags);
}

void PostResizeBuffers(IDXGISwapChain* swapchain, UINT, UINT, UINT, DXGI_FORMAT, UINT, bool succeeded)
{
   const std::optional<Presenter> presenter = FindPresenter(swapchain);
   if (!presenter)
      return;
   if (!succeeded)
   {
      Display::Swapchain::DXGI::TrackResized(swapchain, false);
      return;
   }
   const DXGI_SWAP_CHAIN_DESC desc = Describe(swapchain, *presenter, "resized");
   Display::Swapchain::DXGI::ClearColorSpaceOnFormatChange(swapchain, desc.BufferDesc.Format);
   IUnknown* const device = FindDevice(swapchain);
   FrameState::SetOutputResolution(device, {desc.BufferDesc.Width, desc.BufferDesc.Height});
   std::visit(
      Overloaded{
         [](ID3D11Device* d3d11) { Resources::Database::D3D11::Invalidate(d3d11, Resources::Database::Invalidation::SwapchainResize); },
         [device](ID3D12CommandQueue*) {
            if (device)
               Resources::Database::D3D12::Invalidate(static_cast<ID3D12Device*>(device), Resources::Database::Invalidation::SwapchainResize);
         },
      },
      *presenter);
   // The game's buffers again, and the real color space (Luma's, set after the check above)
   Display::Swapchain::DXGI::TrackResized(swapchain, true);
   LogOverlays();
   ReportInitSwapchain(swapchain, *presenter, true);
}

bool PreSetFullscreenState(IDXGISwapChain* swapchain, BOOL fullscreen, IDXGIOutput* output)
{
   (void)output;
   const bool borderless = Display::Swapchain::DXGI::SetFullscreenState(swapchain, fullscreen);
   Log::Info("DXGI: swapchain {} {} fullscreen{}", static_cast<void*>(swapchain), fullscreen ? "enters" : "leaves", borderless ? " (borderless)" : "");
   return borderless;
}

std::optional<BOOL> GetFullscreenState(IDXGISwapChain* swapchain)
{
   return Display::Swapchain::DXGI::GetFullscreenState(swapchain);
}

// Buffer and index queries
std::optional<HRESULT> GetBuffer(IDXGISwapChain* swapchain, UINT index, REFIID riid, void** buffer, bool third_party)
{
   return Display::Swapchain::DXGI::GetBuffer(swapchain, index, riid, buffer, third_party);
}

std::optional<UINT> GetCurrentBackBufferIndex(IDXGISwapChain* swapchain, bool third_party)
{
   return Display::Swapchain::DXGI::GetCurrentBackBufferIndex(swapchain, third_party);
}

// Color space, HDR and display spoofing
bool PreSetColorSpace(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE color_space)
{
   return Display::Swapchain::DXGI::SetColorSpace(swapchain, color_space);
}

void PostSetColorSpace(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space)
{
   Display::Swapchain::DXGI::RecordColorSpace(swapchain, color_space);
}

bool PreSetHdrMetadata(IDXGISwapChain* swapchain, DXGI_HDR_METADATA_TYPE type, UINT size, const void* metadata)
{
   return Display::Swapchain::DXGI::SetHdrMetadata(swapchain, type, size, metadata);
}

void PostCheckColorSpaceSupport(IDXGISwapChain*, DXGI_COLOR_SPACE_TYPE color_space, UINT& support)
{
   Display::Spoofing::DXGI::SpoofColorSpaceSupport(color_space, support);
}

void PostGetOutputDesc1(IDXGIOutput6*, DXGI_OUTPUT_DESC1& desc)
{
   Display::Spoofing::DXGI::SpoofOutputDesc(desc);
}

// Present
void PrePresentWork(IDXGISwapChain* swapchain)
{
   const std::optional<Presenter> presenter = FindPresenter(swapchain);
   if (!presenter)
      return;
   Trace::RecordPresent();
   Hooks::VerifyOnPresent();
   // After the hooks were verified, whose counts it logs
   ReShade::CheckHookOrderAtFirstPresent();
#if DEVELOPMENT
   std::visit(
      Overloaded{
         [&](ID3D11Device* device) {
            Trace::Capture::D3D11::ReadBackTracked();
            Trace::Readback::D3D11::ReadBack(swapchain, device);
         },
         [&](ID3D12CommandQueue* queue) {
            Trace::Capture::D3D12::ReadBackTracked();
            Trace::Readback::D3D12::ReadBack(swapchain, queue);
         },
      },
      *presenter);
   Trace::Readback::CompletePending();
#endif
   Settings::ApplyQueued();
#if DEVELOPMENT
   Mcp::RunPendingJobs();
#endif
   System::Jobs::RunFinished();
#if DEVELOPMENT || TEST
   if (Shaders::IsNeeded())
      Shaders::Database::QueueDumps();
#endif
   Shaders::Patching::PublishCompleted();
   Shaders::CountFrame(FindDevice(swapchain));
   std::visit(
      Overloaded{
         [&](ID3D11Device* device) {
            Game::Registry::Call(Game::Registry::GetD3D11Events().on_present, swapchain, device);
            PresentD3D11(device);
         },
         [&](ID3D12CommandQueue* queue) {
            Game::Registry::Call(Game::Registry::GetD3D12Events().on_present, swapchain, queue);
            PresentD3D12(queue);
#if DEVELOPMENT
            Trace::Stats::D3D12::CountFrame();
#endif
         },
      },
      *presenter);
   // A switch of the game's own HDR option this frame, then the game's image into the real buffer, once the game's frame is done (its
   // on_present included)
   Display::DisplayMode::Update();
   Display::Swapchain::DXGI::Compose(swapchain);
}

void PrePresent(IDXGISwapChain* swapchain, UINT sync_interval, UINT& flags, bool present_work_done)
{
   const std::optional<Presenter> presenter = FindPresenter(swapchain);
   if (!presenter)
      return;
   flags = Display::Swapchain::DXGI::GetPresentFlags(swapchain, sync_interval, flags);
   if (!present_work_done)
      PrePresentWork(swapchain);
   ReShade::RecordWherePresentWorkRan(present_work_done);
#if DEVELOPMENT
   // What ran since the event passed through untraced: one entry stands for it
   if (present_work_done && Trace::Capture::IsCapturing())
      Trace::Capture::Add({.type = Trace::Capture::EventType::ThirdPartyWork, .name = "ReShade and add-on work at the last present (effects, overlay, present handlers)"});
   // Finish the captured frame after on_present, Display Composition, and ThirdPartyWork so they appear at the end of the trace
   if (Trace::Capture::IsCapturing())
   {
      Trace::Capture::D3D11::ReadBackConstantBuffers();
      Trace::Capture::FinishFrame();
   }
#endif
   UI::DXGI::AttachOverlay(swapchain, *presenter, FindDevice(swapchain));
   // Last, on top of everything the game, Luma and ReShade drew
   UI::DXGI::DrawOverlay(swapchain);
#if DEVELOPMENT
   // Start any capture requested by the UI or MCP now, so the next frame starts cleanly at its first draw
   Trace::Capture::StartRequestedFrame();
   Trace::DebugDraw::ResetFrame();
#endif
   // After the game's on_present and the overlay: what they changed (its settings object, the dev sliders) uploads from
   // the next draw
   Resources::LumaCBuffers::MarkSettingsDirty();
   Validation::D3D11::ReadMessages();
   Validation::D3D12::ReadMessages();
}

bool ComposesWholeBuffer(IDXGISwapChain* swapchain)
{
   return Display::Swapchain::DXGI::ComposesWholeBuffer(swapchain);
}

// The game's part of a present is on_present, inside Core's handling
void PostPresent(IDXGISwapChain* swapchain)
{
   Display::Swapchain::DXGI::AdvanceBuffer(swapchain);
}
} // namespace Handlers
} // namespace Luma
