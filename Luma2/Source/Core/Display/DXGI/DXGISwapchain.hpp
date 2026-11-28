#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include "Display/DXGI/DXGIPresenter.hpp"
#include "Hooks/DXGI/DXGISwapchainProxy.hpp"
#include "Display/Swapchain.hpp"

// The swapchain on DXGI, for D3D11 and D3D12. Everything here runs inside Luma's scope (the handlers'), except where it says it
// answers the game.
//
// A swapchain created while the hooks run gets a state in its private data: what the game asked for, what Luma made of it, and in
// Mirror mode the game's back buffers. A swapchain without one (made inside Luma's scope) is left alone.
namespace Luma {
namespace Display {
namespace Swapchain {
namespace DXGI {
// What the game's back buffers are (as it asked, after its own on_create_swapchain)
struct BufferDesc
{
   UINT width = 0;
   UINT height = 0;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   UINT count = 1;
   UINT sample_count = 1;
   DXGI_USAGE usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
   UINT flags = 0; // The game's swapchain flags (DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE)
};

// Creation: the game's request rewritten for Luma (flip model, the output mode's format, tearing, windowed), remembered on this thread
// for OnCreated (Native keeps the game's format). The game's on_create_swapchain already ran.
void PrepareCreation(DXGI_SWAP_CHAIN_DESC& desc);
void PrepareCreation(DXGI_SWAP_CHAIN_DESC1& desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen_desc, HWND window);
// After the creation succeeded: its state, the game's buffers, color space and HDR metadata
void TrackCreated(IDXGISwapChain* swapchain, const Presenter& presenter);
// The game's resize rewritten (its values resolved: a zero count or unknown format already are the previous ones), Native's format kept
void PrepareResize(IDXGISwapChain* swapchain, UINT& count, UINT& width, UINT& height, DXGI_FORMAT& format, UINT& flags);
void TrackResized(IDXGISwapChain* swapchain, bool succeeded);

// Answers to the game (outside Luma's scope). Empty or false: the call goes to DXGI.
// `third_party`: an overlay's code asks (Hooks::ThirdPartyModules). Overlays always see the real buffers and index: one that caches the
// buffers and picks one by the index each frame needs both from the same swapchain.
std::optional<HRESULT> GetBuffer(IDXGISwapChain* swapchain, UINT index, REFIID riid, void** buffer, bool third_party);
std::optional<UINT> GetCurrentBackBufferIndex(IDXGISwapChain* swapchain, bool third_party);
// An overlay's code asked for a buffer or the index on the presenting thread (from the proxy: one that hooked the swapchain the game holds,
// as Steam does; without it, one whose Present runs before Luma's): it's about to draw, so Luma composes now (once a frame) and the overlay
// draws on the final image, in the real color space. The present after doesn't compose again. (Binds and submissions aren't a sign: an
// overlay hooking them above Luma makes the game's look like its own.)
void ComposeForOverlay(IDXGISwapChain* swapchain);
// After creation: the game gets a proxy of this swapchain (Luma owns it, Swapchain::Wraps). Overlays then present after Luma by
// construction, and the early composition above is never needed.
bool Wraps(IDXGISwapChain* swapchain);
// What ReShade's calls on the game's proxy of it get (Hooks::SwapchainProxy::ReShadeSwapchainView)
Hooks::SwapchainProxy::ReShadeSwapchainView GetReShadeSwapchainView(IDXGISwapChain* swapchain);
// The game's color space is what its image is in: kept, not applied (Luma sets the real one). Native: recorded, and applied.
bool SetColorSpace(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE color_space);
// Records the color space actually set on the real swapchain (in its private data, shared with ReShade/Special K)
void RecordColorSpace(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE color_space);
// Clears the recorded color space if a resize changed the buffer format
void ClearColorSpaceOnFormatChange(IDXGISwapChain* swapchain, DXGI_FORMAT format);
// What was last set on the real swapchain, empty if never set or cleared on format change
std::optional<DXGI_COLOR_SPACE_TYPE> GetColorSpace(IDXGISwapChain* swapchain);
bool SetHdrMetadata(IDXGISwapChain* swapchain, DXGI_HDR_METADATA_TYPE type, UINT size, const void* metadata);
// True skips the call (borderless in place of exclusive fullscreen)
bool SetFullscreenState(IDXGISwapChain* swapchain, BOOL fullscreen);
std::optional<BOOL> GetFullscreenState(IDXGISwapChain* swapchain);

// Present: tearing where the real swapchain allows it and the game doesn't wait for V-Sync
UINT GetPresentFlags(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags);
// Mirror: composition writes the whole buffer, so the game's dirty rects and scrolling don't apply
bool ComposesWholeBuffer(IDXGISwapChain* swapchain);
// Before the real present: the output mode the user and the display want, then composition
void Compose(IDXGISwapChain* swapchain);
// After the real present: the game's next buffer
void AdvanceBuffer(IDXGISwapChain* swapchain);

// The game's current back buffer (Mirror), null for a swapchain whose buffers are the real ones
Microsoft::WRL::ComPtr<IUnknown> GetGameBuffer(IDXGISwapChain* swapchain);

// What luma_status and the Info tab show, of the swapchain presented last
struct Status
{
   bool known = false;
   std::string mode;
   std::string output_mode;
   DXGI_FORMAT game_format = DXGI_FORMAT_UNKNOWN;
   DXGI_FORMAT real_format = DXGI_FORMAT_UNKNOWN;
   DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
   std::optional<DXGI_COLOR_SPACE_TYPE> game_color_space;
   UINT game_width = 0;
   UINT game_height = 0;
   UINT real_width = 0;
   UINT real_height = 0;
   UINT game_buffers = 0;
   UINT real_buffers = 0;
   bool flip_model = false;
   bool tearing = false;
   bool borderless = false;
   bool game_fullscreen = false;
   bool composed = false; // The last present ran composition
   bool wrapped = false; // The game holds a proxy
   bool reshade_reads_real_swapchain = false; // ReShade's own reads get the real swapchain (SwapchainProxy::ReShadeSwapchainView::RealInsideWindows)
   uint32_t reshade_switches = 0; // Output switches through ReShade's wrapper
   bool reshade_switch_stopped = false; // One couldn't go through it: no output switch while ReShade is loaded
   bool hdr_supported = false;
   bool hdr_enabled = false;
   float peak_nits = 0.f;
   float sdr_white_nits = 0.f;
};

Status GetStatus();
// "HDR10 (G2084 P2020)", for logs and status
std::string_view GetName(DXGI_COLOR_SPACE_TYPE color_space);
} // namespace DXGI
} // namespace Swapchain
} // namespace Display
} // namespace Luma
