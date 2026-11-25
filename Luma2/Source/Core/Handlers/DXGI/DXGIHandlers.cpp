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
#include "ReShade/ReShade.hpp"
#include "Game/GameRegistry.hpp"
#include "Hooks/HookManager.hpp"
#include "Hooks/HookScope.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D11/D3D11ResourceDatabase.hpp"
#include "Resources/D3D11/D3D11Samplers.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Swapchain/DXGI/DXGISwapchain.hpp"
#include "Display/DisplayMode.hpp"
#include "Display/DXGI/DXGISpoofing.hpp"
#include "Frame/FrameState.hpp"
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
#include "Trace/McpServer.hpp"
#include "UI/Overlay.hpp"
#include "Log/Log.hpp"
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

// Where ReShade and Special K keep a swapchain's color space, so they agree on it
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

Api GetCreatorApi(IUnknown* creator)
{
   if (!creator)
      return Api::D3D11;
   const std::optional<Presenter> presenter = GetPresenter(creator);
   return presenter ? GetApi(*presenter) : Api::D3D12;
}

bool HasCreateSwapchainEvent(Api api)
{
   return api == Api::D3D11 ? Game::Registry::GetD3D11Events().on_create_swapchain != nullptr
                            : Game::Registry::GetD3D12Events().on_create_swapchain != nullptr;
}

void ReportCreateSwapchain(Api api, Game::SwapchainDesc& desc)
{
   if (api == Api::D3D11)
      Game::Registry::Call(Game::Registry::GetD3D11Events().on_create_swapchain, desc);
   else
      Game::Registry::Call(Game::Registry::GetD3D12Events().on_create_swapchain, desc);
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

void PreCreateSwapChain(IUnknown* creator, DXGI_SWAP_CHAIN_DESC& desc)
{
   if (const Api api = GetCreatorApi(creator); HasCreateSwapchainEvent(api))
   {
      Game::SwapchainDesc reported = {desc.BufferDesc.Format, desc.BufferCount, desc.Flags, desc.SwapEffect, desc.SampleDesc.Count, desc.Windowed != FALSE};
      ReportCreateSwapchain(api, reported);
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
   if (const Api api = GetCreatorApi(creator); HasCreateSwapchainEvent(api))
   {
      Game::SwapchainDesc reported = {desc.Format, desc.BufferCount, desc.Flags, desc.SwapEffect, desc.SampleDesc.Count, fullscreen_desc.Windowed != FALSE};
      ReportCreateSwapchain(api, reported);
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
   ReShade::FindReShadeLoadedAfterLuma();
   const std::optional<Presenter> presenter = GetPresenter(creator);
   if (!presenter)
   {
      Log::Warning("DXGI: swapchain {} was created with {}, which is neither a D3D11 device nor a D3D12 queue", static_cast<void*>(swapchain),
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

Hooks::SwapchainProxy::ReShadeSwapchainView GetReShadeSwapchainView(IDXGISwapChain* swapchain)
{
   return Swapchain::DXGI::GetReShadeSwapchainView(swapchain);
}

void OnSwapChainDestroyed(const IDXGISwapChain* swapchain)
{
   UI::DetachFromSwapchain(swapchain);
   Trace::RemoveSwapchain(swapchain);
   {
      std::scoped_lock lock(known_mutex);
      std::erase_if(known, [swapchain](const KnownSwapchain& entry) { return entry.swapchain == swapchain; });
   }
   Log::Info("DXGI: swapchain {} destroyed", static_cast<const void*>(swapchain));
}

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
      Log::Info("DXGI: swapchain {} color space forgotten, its format changed", static_cast<void*>(swapchain));
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
   Log::Info("DXGI: swapchain {} {} fullscreen{}", static_cast<void*>(swapchain), fullscreen ? "enters" : "leaves", borderless ? " (borderless)" : "");
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
   Log::Info("DXGI: swapchain {} color space {} ({}), format {}", static_cast<void*>(swapchain), GetColorSpaceName(color_space), uint32_t(color_space),
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

void PrePresentWork(IDXGISwapChain* swapchain)
{
   Trace::RecordPresent();
   Hooks::VerifyOnPresent();
   // After the hooks were verified, whose counts it logs
   ReShade::CheckHookOrderAtFirstPresent();
#if DEVELOPMENT
   if (const std::optional<Presenter> presenting = FindPresenter(swapchain))
   {
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
         *presenting);
   }
   else
   {
      Trace::Capture::D3D11::ReadBackTracked();
   }
   Trace::Readback::OnPresent();
#endif
   Settings::ApplyQueued();
#if DEVELOPMENT
   Trace::Mcp::RunPendingJobs();
#endif
   System::Jobs::RunFinished();
#if DEVELOPMENT || TEST
   if (Shaders::IsNeeded())
      Shaders::Database::QueueDumps();
#endif
   Shaders::Patching::OnPresent();
   Shaders::CountFrame();
   if (const std::optional<Presenter> presenter = FindPresenter(swapchain))
   {
      std::visit(
         Overloaded{
            [&](ID3D11Device* device) {
               Game::Registry::Call(Game::Registry::GetD3D11Events().on_present, swapchain, device);
               if (Shaders::IsNeeded())
                  Shaders::Variants::D3D11::PublishVariants(device);
               if (Resources::Samplers::IsNeeded())
                  Resources::Samplers::D3D11::RefreshMirrors();
               if (Resources::Mirrors::IsNeeded())
                  Resources::Mirrors::D3D11::OnPresent(device);
               Resources::Database::D3D11::UpdateShaderObjects(device);
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
   }
   // A switch of the game's own HDR option this frame, then the game's image into the real buffer, once the game's frame is done (its
   // on_present included)
   Display::DisplayMode::Update();
   Swapchain::DXGI::Compose(swapchain);
}

void PrePresent(IDXGISwapChain* swapchain, UINT sync_interval, UINT& flags, bool present_work_done)
{
   flags = Swapchain::DXGI::GetPresentFlags(swapchain, sync_interval, flags);
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
   // Last, on top of everything the game, Luma and ReShade drew
   UI::DrawOverlay(swapchain);
#if DEVELOPMENT
   // Start any capture requested by the UI or MCP now, so the next frame starts cleanly at its first draw
   Trace::Capture::StartRequestedFrame();
   Trace::DebugDraw::OnPresent();
#endif
   // After the game's on_present and the overlay: what they changed (its settings object, the dev sliders) uploads from
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
