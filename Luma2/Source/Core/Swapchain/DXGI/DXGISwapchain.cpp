#include "Swapchain/DXGI/DXGISwapchain.hpp"

#include <algorithm>
#include <mutex>
#include <variant>
#include <vector>

#include <d3d11.h>
#include <d3d12.h>

#include "Display/Composition.hpp"
#include "Display/D3D12/D3D12Composition.hpp"
#include "Display/Levels.hpp"
#include "Frame/Frame.hpp"
#include "Hooks/Lifetime.hpp"
#include "Swapchain/D3D11/D3D11Swapchain.hpp"
#include "Swapchain/D3D12/D3D12Swapchain.hpp"
#include "System/Display.hpp"
#include "System/Window.hpp"
#include "Utils/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Swapchain {
namespace DXGI {
namespace {
// What Luma made of the game's request at creation. A resize keeps all of it but the format.
struct Real
{
   UINT count = 2;
   UINT flags = 0;
   bool flip = true;
   bool tearing = false;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   OutputMode output = OutputMode::Sdr;
   // The real buffers' size (0 is the window's, as the game's); known once created
   UINT width = 0;
   UINT height = 0;
};

// A creation between its Pre and Post handlers, on the creating thread
struct Pending
{
   Mode mode = Mode::Mirror;
   BufferDesc game;
   Real real;
   HWND window = nullptr;
   bool game_fullscreen = false;
   System::Display::Info display;
};

thread_local std::optional<Pending> pending;

struct State
{
   Mode mode = Mode::Mirror;
   Presenter presenter;
   HWND window = nullptr;
   BufferDesc game;
   Real real;
   bool game_fullscreen = false; // What the game asked for, borderless
   // The game's back buffers and which one it draws next (Mirror). The game asks for them from any thread.
   std::mutex mutex;
   std::vector<ComPtr<IUnknown>> buffers;
   UINT index = 0;
   std::optional<DXGI_COLOR_SPACE_TYPE> game_color_space;
   DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
   System::Display::Info display;
   uint32_t presents_since_query = 0;
   D3D11::Composition d3d11;
   Display::Composition::D3D12::RendererPtr d3d12;
   bool composed = false;
   bool switch_failed_logged = false;
   // Windows HDR was turned on for the user's HDR choice: once per choice
   bool hdr_requested = false;
};

// The display is asked again this often, for Windows' HDR switch: a few seconds
constexpr uint32_t PRESENTS_PER_QUERY = 300;

std::mutex status_mutex;
Status status;

State* Find(IDXGISwapChain* swapchain)
{
   return swapchain ? Hooks::Lifetime::GetState<State>(swapchain) : nullptr;
}

bool IsD3D11(const State& state)
{
   return std::holds_alternative<ID3D11Device*>(state.presenter);
}

bool IsLinear(DXGI_FORMAT format)
{
   return format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}

// The real buffers' format for an output mode. SDR keeps the game's 10 bits or BGRA order, without sRGB (flip model has none).
DXGI_FORMAT GetFormat(OutputMode output, DXGI_FORMAT game)
{
   switch (output)
   {
   case OutputMode::Hdr10: return DXGI_FORMAT_R10G10B10A2_UNORM;
   case OutputMode::ScRgb: return DXGI_FORMAT_R16G16B16A16_FLOAT;
   default:
      if (game == DXGI_FORMAT_R10G10B10A2_UNORM)
         return game;
      if (game == DXGI_FORMAT_B8G8R8A8_UNORM || game == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
         return DXGI_FORMAT_B8G8R8A8_UNORM;
      return DXGI_FORMAT_R8G8B8A8_UNORM;
   }
}

DXGI_COLOR_SPACE_TYPE GetColorSpace(OutputMode output, DXGI_FORMAT format)
{
   if (output == OutputMode::Hdr10)
      return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
   // Windows encodes scRGB with sRGB in SDR
   return format == DXGI_FORMAT_R16G16B16A16_FLOAT ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
}

Display::Composition::Encoding GetEncoding(OutputMode output, DXGI_FORMAT format)
{
   if (output == OutputMode::Hdr10)
      return Display::Composition::Encoding::Pq;
   return format == DXGI_FORMAT_R16G16B16A16_FLOAT ? Display::Composition::Encoding::Linear : Display::Composition::Encoding::Srgb;
}

// Luma's changes to the game's swapchain flags, the same at creation and every resize (a resize can't change tearing)
UINT GetFlags(UINT game_flags, bool flip, bool tearing)
{
   UINT flags = game_flags;
   if (IsBorderless())
      flags &= ~UINT(DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
   if (flip)
      flags &= ~UINT(DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE); // Not with a float format, and the game's buffers carry it
   if (tearing)
      flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
   return flags;
}

// The real buffers' size for the game's: the same, but scaled by Luma.Development.SwapchainScale in Mirror (the game's zero size then
// resolved to the window's, which a scaled one isn't)
void ScaleSize(Mode mode, bool flip, HWND window, BufferDesc& game, UINT& width, UINT& height)
{
   const float scale = GetScale();
   width = game.width;
   height = game.height;
   if (mode != Mode::Mirror || !flip || scale == 1.f)
      return;
   if (game.width == 0 || game.height == 0)
   {
      RECT client = {};
      GetClientRect(window, &client);
      game.width = UINT(std::max(client.right - client.left, LONG(1)));
      game.height = UINT(std::max(client.bottom - client.top, LONG(1)));
   }
   width = std::max(1u, UINT(float(game.width) * scale + 0.5f));
   height = std::max(1u, UINT(float(game.height) * scale + 0.5f));
}

// What Luma creates for `game`, the swap effect and windowed state edited in place
Pending Rewrite(const BufferDesc& game, DXGI_SWAP_EFFECT& swap_effect, BOOL& windowed, HWND window)
{
   Pending request = {.mode = GetMode(), .game = game, .window = window, .game_fullscreen = !windowed};
   request.display = System::Display::Query(window);
   Real& real = request.real;
   real.flip = UsesFlipModel();
   // The game's own flags may already ask for it
   real.tearing = (game.flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) || (real.flip && AllowsTearing());
   real.output = GetOutputMode(request.display.hdr_enabled);
   real.format = real.flip ? GetFormat(real.output, game.format) : game.format;
   real.count = real.flip ? std::max(2u, game.count) : game.count;
   real.flags = GetFlags(game.flags, real.flip, real.tearing);
   ScaleSize(request.mode, real.flip, window, request.game, real.width, real.height);
   if (real.flip)
      swap_effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
   if (IsBorderless())
      windowed = TRUE;
   return request;
}

void ApplyColorSpace(IDXGISwapChain* swapchain, State& state)
{
   if (!state.real.flip)
      return;
   const DXGI_COLOR_SPACE_TYPE color_space = GetColorSpace(state.real.output, state.real.format);
   ComPtr<IDXGISwapChain3> swapchain3;
   UINT support = 0;
   if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3))) || FAILED(swapchain3->CheckColorSpaceSupport(color_space, &support)) ||
      !(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) || FAILED(swapchain3->SetColorSpace1(color_space)))
   {
      Utils::Log::Warning("Swapchain: {} can't present color space {} (format {})", static_cast<void*>(swapchain), GetName(color_space), uint32_t(state.real.format));
      return;
   }
   state.color_space = color_space;
   ComPtr<IDXGISwapChain4> swapchain4;
   if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4))))
      return;
   if (state.real.output == OutputMode::Hdr10 && SetsHdrMetadata())
   {
      // BT.2020 primaries and a D65 white point, in 0.00002 units; the display's luminance, the minimum in 0.0001 nits
      constexpr auto CHROMATICITY = [](float value) { return UINT16(value * 50000.f + 0.5f); };
      const DXGI_HDR_METADATA_HDR10 metadata = {
         .RedPrimary = {CHROMATICITY(0.708f), CHROMATICITY(0.292f)},
         .GreenPrimary = {CHROMATICITY(0.170f), CHROMATICITY(0.797f)},
         .BluePrimary = {CHROMATICITY(0.131f), CHROMATICITY(0.046f)},
         .WhitePoint = {CHROMATICITY(0.3127f), CHROMATICITY(0.3290f)},
         .MaxMasteringLuminance = UINT(state.display.peak_nits + 0.5f),
         .MinMasteringLuminance = UINT(state.display.min_nits * 10000.f + 0.5f),
         .MaxContentLightLevel = UINT16(std::min(state.display.peak_nits, 65535.f)),
         .MaxFrameAverageLightLevel = UINT16(std::min(state.display.max_full_frame_nits, 65535.f)),
      };
      swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), const_cast<DXGI_HDR_METADATA_HDR10*>(&metadata));
   }
   else
   {
      swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
   }
}

// Mirror: the game's buffers, in the size and format it asked for
void CreateBuffers(State& state)
{
   std::vector<ComPtr<IUnknown>> buffers;
   if (auto* const* device = std::get_if<ID3D11Device*>(&state.presenter))
   {
      if (ComPtr<ID3D11Texture2D> buffer = D3D11::CreateBuffer(*device, state.game))
         buffers.push_back(std::move(buffer));
   }
   else
   {
      ComPtr<ID3D12Device> d3d12_device;
      if (SUCCEEDED(std::get<ID3D12CommandQueue*>(state.presenter)->GetDevice(IID_PPV_ARGS(&d3d12_device))))
      {
         for (UINT i = 0; i < std::max(1u, state.game.count); i++)
         {
            if (ComPtr<ID3D12Resource> buffer = D3D12::CreateBuffer(d3d12_device.Get(), state.game, i))
               buffers.push_back(std::move(buffer));
         }
      }
   }
   std::scoped_lock lock(state.mutex);
   state.buffers = std::move(buffers);
   state.index = 0;
}

// A zero size is the window's: unscaled real buffers have it. Then the device's four resolutions.
void ResolveSizes(IDXGISwapChain* swapchain, State& state)
{
   DXGI_SWAP_CHAIN_DESC real = {};
   swapchain->GetDesc(&real);
   state.real.width = real.BufferDesc.Width;
   state.real.height = real.BufferDesc.Height;
   if (state.game.width == 0 || state.game.height == 0)
   {
      state.game.width = real.BufferDesc.Width;
      state.game.height = real.BufferDesc.Height;
   }
   ComPtr<IUnknown> device;
   if (auto* const* d3d11_device = std::get_if<ID3D11Device*>(&state.presenter))
      device = *d3d11_device;
   else if (ComPtr<ID3D12Device> d3d12_device; SUCCEEDED(std::get<ID3D12CommandQueue*>(state.presenter)->GetDevice(IID_PPV_ARGS(&d3d12_device))))
      device = d3d12_device;
   MONITORINFO monitor = {.cbSize = sizeof(monitor)};
   GetMonitorInfoW(MonitorFromWindow(state.window, MONITOR_DEFAULTTONEAREST), &monitor);
   // Before the game's callbacks, which read them, and the game's buffers, whose upgrade rules match them
   Frame::SetOutputResolution(device.Get(), {state.game.width, state.game.height});
   Frame::SetSwapchainResolution(device.Get(), {state.real.width, state.real.height},
      {uint32_t(monitor.rcMonitor.right - monitor.rcMonitor.left), uint32_t(monitor.rcMonitor.bottom - monitor.rcMonitor.top)});
}

// Nothing of Luma's refers to the real buffers (a resize fails otherwise)
void ReleaseRealBuffers(State& state)
{
   if (auto* const* device = std::get_if<ID3D11Device*>(&state.presenter))
      D3D11::PrepareResize(*device, state.d3d11);
   else if (state.d3d12)
      Display::Composition::D3D12::WaitIdle(*state.d3d12);
}

void RefreshDisplay(IDXGISwapChain* swapchain, State& state, bool force)
{
   const HMONITOR monitor = MonitorFromWindow(state.window, MONITOR_DEFAULTTONEAREST);
   if (!force && monitor == state.display.monitor && ++state.presents_since_query < PRESENTS_PER_QUERY)
      return;
   state.presents_since_query = 0;
   ComPtr<IDXGIOutput> output;
   swapchain->GetContainingOutput(&output);
   const System::Display::Info display = System::Display::Query(state.window, output.Get());
   if (display.hdr_enabled != state.display.hdr_enabled || display.monitor != state.display.monitor || force)
   {
      Utils::Log::Info("Swapchain: display {} HDR {}{}, peak {:.0f} nits, SDR white {:.0f} nits", static_cast<void*>(display.monitor),
         display.hdr_enabled ? "on" : "off", display.hdr_supported ? "" : " (unsupported)", display.peak_nits, display.sdr_white_nits);
   }
   state.display = display;
}

// Mirror, at present: the real buffers in the output mode's format, then its color space
void SwitchOutput(IDXGISwapChain* swapchain, State& state, OutputMode output)
{
   const DXGI_FORMAT format = GetFormat(output, state.game.format);
   ReleaseRealBuffers(state);
   DXGI_SWAP_CHAIN_DESC real = {};
   swapchain->GetDesc(&real);
   // Through the vtable, so an overlay's ResizeBuffers hook sees it too
   const HRESULT result = swapchain->ResizeBuffers(real.BufferCount, real.BufferDesc.Width, real.BufferDesc.Height, format, real.Flags);
   if (FAILED(result))
   {
      if (!state.switch_failed_logged)
         Utils::Log::Error("Swapchain: {} couldn't switch to {} (0x{:X})", static_cast<void*>(swapchain), GetName(output), uint32_t(result));
      state.switch_failed_logged = true;
      return;
   }
   Utils::Log::Info("Swapchain: {} now presents {} (format {})", static_cast<void*>(swapchain), GetName(output), uint32_t(format));
   state.real.output = output;
   state.real.format = format;
   state.switch_failed_logged = false;
   ApplyColorSpace(swapchain, state);
   Display::Levels::ApplyOutputMode(output != OutputMode::Sdr, state.display);
}

void UpdateStatus(IDXGISwapChain* swapchain, const State& state)
{
   DXGI_SWAP_CHAIN_DESC real = {};
   swapchain->GetDesc(&real);
   std::scoped_lock lock(status_mutex);
   status = {
      .known = true,
      .mode = std::string(GetName(state.mode)),
      .output_mode = std::string(GetName(state.real.output)),
      .game_format = state.game.format,
      .real_format = real.BufferDesc.Format,
      .color_space = state.color_space,
      .game_color_space = state.game_color_space,
      .game_width = state.game.width,
      .game_height = state.game.height,
      .real_width = real.BufferDesc.Width,
      .real_height = real.BufferDesc.Height,
      .game_buffers = state.game.count,
      .real_buffers = real.BufferCount,
      .flip_model = state.real.flip,
      .tearing = state.real.tearing,
      .borderless = IsBorderless(),
      .game_fullscreen = state.game_fullscreen,
      .composed = state.composed,
      .hdr_supported = state.display.hdr_supported,
      .hdr_enabled = state.display.hdr_enabled,
      .peak_nits = state.display.peak_nits,
      .sdr_white_nits = state.display.sdr_white_nits,
   };
}
} // namespace

void PrepareCreation(DXGI_SWAP_CHAIN_DESC& desc)
{
   const BufferDesc game = {desc.BufferDesc.Width, desc.BufferDesc.Height, desc.BufferDesc.Format, desc.BufferCount, desc.SampleDesc.Count, desc.BufferUsage, desc.Flags};
   pending = Rewrite(game, desc.SwapEffect, desc.Windowed, desc.OutputWindow);
   const Real& real = pending->real;
   desc.BufferDesc.Format = real.format;
   desc.BufferDesc.Width = real.width;
   desc.BufferDesc.Height = real.height;
   desc.BufferCount = real.count;
   desc.Flags = real.flags;
   if (real.flip)
      desc.SampleDesc = {1, 0};
   if (pending->mode == Mode::Mirror)
      desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
   else
      desc.BufferUsage |= DXGI_USAGE_RENDER_TARGET_OUTPUT;
}

void PrepareCreation(DXGI_SWAP_CHAIN_DESC1& desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen_desc, HWND window)
{
   const BufferDesc game = {desc.Width, desc.Height, desc.Format, desc.BufferCount, desc.SampleDesc.Count, desc.BufferUsage, desc.Flags};
   pending = Rewrite(game, desc.SwapEffect, fullscreen_desc.Windowed, window);
   const Real& real = pending->real;
   desc.Format = real.format;
   desc.Width = real.width;
   desc.Height = real.height;
   desc.BufferCount = real.count;
   desc.Flags = real.flags;
   if (real.flip)
   {
      desc.SampleDesc = {1, 0};
      // Only composition swapchains have it
      if (desc.Scaling == DXGI_SCALING_ASPECT_RATIO_STRETCH)
         desc.Scaling = DXGI_SCALING_STRETCH;
   }
   if (pending->mode == Mode::Mirror)
      desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
   else
      desc.BufferUsage |= DXGI_USAGE_RENDER_TARGET_OUTPUT;
}

void OnCreated(IDXGISwapChain* swapchain, const Presenter& presenter)
{
   if (!pending)
      return;
   Pending request = std::move(*pending);
   pending.reset();
   State* state = Hooks::Lifetime::SetState<State>(swapchain);
   if (!state)
      return;
   state->mode = request.mode;
   state->presenter = presenter;
   state->window = request.window;
   state->game = request.game;
   state->real = request.real;
   state->game_fullscreen = request.game_fullscreen;
   state->display = request.display;
   ResolveSizes(swapchain, *state);
   RefreshDisplay(swapchain, *state, true);
   if (state->mode == Mode::Mirror)
      CreateBuffers(*state);
   if (auto* const* queue = std::get_if<ID3D12CommandQueue*>(&presenter))
   {
      state->d3d12 = Display::Composition::D3D12::CreateRenderer(*queue);
      if (!state->d3d12)
         Utils::Log::Error("Swapchain: display composition couldn't be set up on queue {}", static_cast<void*>(*queue));
   }
   ApplyColorSpace(swapchain, *state);
   Display::Levels::ApplyOutputMode(state->real.output != OutputMode::Sdr, state->display);
   if (IsBorderless())
   {
      // DXGI's own Alt+Enter would go to exclusive fullscreen without calling the hooked SetFullscreenState
      ComPtr<IDXGIFactory> factory;
      if (SUCCEEDED(swapchain->GetParent(IID_PPV_ARGS(&factory))))
         factory->MakeWindowAssociation(state->window, DXGI_MWA_NO_ALT_ENTER);
      if (state->game_fullscreen)
         System::Window::MakeBorderless(state->window);
   }
   Utils::Log::Info("Swapchain: {} is {}, {} ({}x{} format {} x{} for the game, format {} x{}{}{})", static_cast<void*>(swapchain), GetName(state->mode),
      GetName(state->real.output), state->game.width, state->game.height, uint32_t(state->game.format), state->game.count, uint32_t(state->real.format),
      state->real.count, state->real.flip ? ", flip" : ", the game's swap effect", state->real.tearing ? ", tearing" : "");
   UpdateStatus(swapchain, *state);
}

void PrepareResize(IDXGISwapChain* swapchain, UINT& count, UINT& width, UINT& height, DXGI_FORMAT& format, UINT& flags)
{
   State* state = Find(swapchain);
   if (!state)
      return;
   state->game = {width, height, format, count, state->game.sample_count, state->game.usage, flags};
   ScaleSize(state->mode, state->real.flip, state->window, state->game, width, height);
   ReleaseRealBuffers(*state);
   // In place, a resize is where a new output mode can apply: the game lets go of the buffers here
   state->real.output = state->real.flip ? GetOutputMode(state->display.hdr_enabled) : OutputMode::Sdr;
   state->real.format = state->real.flip ? GetFormat(state->real.output, format) : format;
   state->real.count = state->real.flip ? std::max(2u, count) : count;
   state->real.flags = GetFlags(flags, state->real.flip, state->real.tearing);
   count = state->real.count;
   format = state->real.format;
   flags = state->real.flags;
}

void OnResized(IDXGISwapChain* swapchain, bool succeeded)
{
   State* state = Find(swapchain);
   if (!state)
      return;
   DXGI_SWAP_CHAIN_DESC real = {};
   swapchain->GetDesc(&real);
   // A failed resize left the real buffers as they were
   state->real.format = real.BufferDesc.Format;
   if (!succeeded)
      return;
   ResolveSizes(swapchain, *state);
   if (state->mode == Mode::Mirror)
      CreateBuffers(*state);
   RefreshDisplay(swapchain, *state, true);
   ApplyColorSpace(swapchain, *state);
   Display::Levels::ApplyOutputMode(state->real.output != OutputMode::Sdr, state->display);
   UpdateStatus(swapchain, *state);
}

std::optional<HRESULT> GetBuffer(IDXGISwapChain* swapchain, UINT index, REFIID riid, void** buffer)
{
   State* state = Find(swapchain);
   if (!state || state->mode != Mode::Mirror)
      return std::nullopt;
   std::scoped_lock lock(state->mutex);
   if (state->buffers.empty())
      return std::nullopt;
   if (!buffer)
      return E_INVALIDARG;
   // D3D11's buffer 0 is always the one the game draws
   if (IsD3D11(*state))
      index = 0;
   if (index >= state->buffers.size())
      return DXGI_ERROR_INVALID_CALL;
   return state->buffers[index]->QueryInterface(riid, buffer);
}

std::optional<UINT> GetCurrentBackBufferIndex(IDXGISwapChain* swapchain)
{
   State* state = Find(swapchain);
   if (!state || state->mode != Mode::Mirror)
      return std::nullopt;
   std::scoped_lock lock(state->mutex);
   return state->index;
}

bool SetColorSpace(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE color_space)
{
   State* state = Find(swapchain);
   if (!state || !state->real.flip)
      return false;
   if (state->game_color_space != color_space)
      Utils::Log::Info("Swapchain: the game set color space {} (kept as its image's, Luma presents {})", GetName(color_space), GetName(state->color_space));
   state->game_color_space = color_space;
   return true;
}

bool SetHdrMetadata(IDXGISwapChain* swapchain, DXGI_HDR_METADATA_TYPE, UINT, const void*)
{
   State* state = Find(swapchain);
   return state && state->real.flip;
}

bool SetFullscreenState(IDXGISwapChain* swapchain, BOOL fullscreen)
{
   State* state = Find(swapchain);
   if (!state || !IsBorderless())
      return false;
   state->game_fullscreen = fullscreen;
   if (fullscreen)
      System::Window::MakeBorderless(state->window);
   return true;
}

std::optional<BOOL> GetFullscreenState(IDXGISwapChain* swapchain)
{
   State* state = Find(swapchain);
   if (!state || !IsBorderless())
      return std::nullopt;
   return BOOL(state->game_fullscreen);
}

UINT GetPresentFlags(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags)
{
   if (sync_interval != 0 || (flags & DXGI_PRESENT_ALLOW_TEARING))
      return flags;
   State* state = Find(swapchain);
   if (!state || !state->real.tearing)
      return flags;
   // Not in exclusive fullscreen, where DXGI refuses it
   if (!IsBorderless())
   {
      BOOL fullscreen = FALSE;
      if (FAILED(swapchain->GetFullscreenState(&fullscreen, nullptr)) || fullscreen)
         return flags;
   }
   return flags | DXGI_PRESENT_ALLOW_TEARING;
}

bool ComposesWholeBuffer(IDXGISwapChain* swapchain)
{
   const State* state = Find(swapchain);
   return state && state->mode == Mode::Mirror;
}

void Compose(IDXGISwapChain* swapchain)
{
   State* state = Find(swapchain);
   if (!state)
      return;
   RefreshDisplay(swapchain, *state, false);
   const bool requests_hdr = RequestsHdr();
   if (requests_hdr && !state->hdr_requested && state->display.hdr_supported && !state->display.hdr_enabled)
   {
      Utils::Log::Info("Swapchain: turning Windows HDR on for the HDR output picked: {}", System::Display::SetHdrEnabled(state->window, true) ? "done" : "failed");
      RefreshDisplay(swapchain, *state, true);
   }
   state->hdr_requested = requests_hdr;
   const bool mirror = state->mode == Mode::Mirror;
   if (mirror && state->real.flip)
   {
      if (const OutputMode output = GetOutputMode(state->display.hdr_enabled); output != state->real.output)
         SwitchOutput(swapchain, *state, output);
   }
   const Display::Composition::Inputs inputs = {
      .encoding = GetEncoding(state->real.output, state->real.format),
      .game_linear = IsLinear(state->game.format),
      .always = mirror,
      // The scale stage: the game's image over the real buffer (Mirror only)
      .source_scale = {mirror ? float(state->game.width) / float(std::max(state->real.width, 1u)) : 1.f,
         mirror ? float(state->game.height) / float(std::max(state->real.height, 1u)) : 1.f},
   };
   state->composed = false;
   if (const std::optional<Display::Composition::Pass> pass = Display::Composition::Plan(inputs))
   {
      ComPtr<IUnknown> game_buffer = mirror ? GetGameBuffer(swapchain) : nullptr;
      if (auto* const* device = std::get_if<ID3D11Device*>(&state->presenter))
      {
         ComPtr<ID3D11Texture2D> texture;
         if (!mirror || (game_buffer && SUCCEEDED(game_buffer.As(&texture))))
            state->composed = D3D11::Compose(swapchain, *device, texture.Get(), state->d3d11, *pass);
      }
      else if (state->d3d12)
      {
         ComPtr<ID3D12Resource> resource;
         if (!mirror || (game_buffer && SUCCEEDED(game_buffer.As(&resource))))
            state->composed = D3D12::Compose(swapchain, *state->d3d12, resource.Get(), *pass);
      }
   }
   UpdateStatus(swapchain, *state);
}

void OnPresented(IDXGISwapChain* swapchain)
{
   State* state = Find(swapchain);
   if (!state || state->mode != Mode::Mirror)
      return;
   std::scoped_lock lock(state->mutex);
   if (!state->buffers.empty())
      state->index = (state->index + 1) % UINT(state->buffers.size());
}

ComPtr<IUnknown> GetGameBuffer(IDXGISwapChain* swapchain)
{
   State* state = Find(swapchain);
   if (!state || state->mode != Mode::Mirror)
      return nullptr;
   std::scoped_lock lock(state->mutex);
   if (state->buffers.empty())
      return nullptr;
   return state->buffers[IsD3D11(*state) ? 0 : state->index % state->buffers.size()];
}

Status GetStatus()
{
   std::scoped_lock lock(status_mutex);
   return status;
}

std::string_view GetName(DXGI_COLOR_SPACE_TYPE color_space)
{
   switch (color_space)
   {
   case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return "sRGB (G22 P709)";
   case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: return "scRGB (G10 P709)";
   case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return "HDR10 (G2084 P2020)";
   case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020: return "HDR10 studio (G2084 P2020)";
   case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020: return "G22 P2020";
   default: return "other";
   }
}
} // namespace DXGI
} // namespace Swapchain
} // namespace Luma
