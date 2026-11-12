#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <Windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>

#include "Common/Api.hpp"
#include "Settings/Registry.hpp"
#include "UI/Icons.hpp"
#include "UI/Widgets.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Log.hpp"

namespace Luma {
namespace Core {
// Only registers a loader notification. Luma initializes when d3d11.dll or d3d12.dll loads, still under the loader lock:
// no dll loads, no waiting on other threads.
BOOL DllMain(HMODULE module, DWORD reason, LPVOID reserved);
} // namespace Core

// Everything Luma does to the image, at once. Tracking keeps running while it's off, so switching back needs no catch-up.
// Every feature's draw-path entry checks it. Lock-free, any thread.
inline constexpr std::string_view ENABLED_SETTING = "Luma.Enabled";

inline bool IsEnabled()
{
   return Settings::Get<bool>(ENABLED_SETTING);
}

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

// The native callbacks, one set per API, null by default. Setting one is what installs the hook of its native function(s),
// so they must be set in Init. They run on the thread of the API call, inside a Luma scope (the game's own API calls made
// from a callback pass through Luma's hooks untouched), and receive the real API objects, never Luma's device proxies.
// "Pre" callbacks run first and can edit the arguments; those returning bool skip the call (and Luma's own handling) with
// true. "Post" ones run after a successful call, "On...Destroyed" ones get an object that can't be used anymore.
// Fields are declared in this order, which designated initializers must follow.
namespace D3D11 {
struct Callbacks
{
   // Device
   void (*post_create_device)(ID3D11Device* device) = nullptr;
   // The game released its last reference: release everything created on the device, which is still valid during the call
   void (*on_device_destroyed)(ID3D11Device* device) = nullptr;

   // Swapchain
   void (*pre_create_swapchain)(DXGI_SWAP_CHAIN_DESC& desc) = nullptr;
   void (*pre_create_swapchain_for_hwnd)(DXGI_SWAP_CHAIN_DESC1& desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC& fullscreen_desc, HWND hwnd) = nullptr;
   void (*post_create_swapchain)(IDXGISwapChain* swapchain, ID3D11Device* device) = nullptr;
   void (*on_swapchain_destroyed)(ID3D11Device* device, const IDXGISwapChain* swapchain) = nullptr;
   // `node_masks` and `present_queues` are only set by ResizeBuffers1
   void (*pre_resize_buffers)(IDXGISwapChain* swapchain, UINT& count, UINT width, UINT height, DXGI_FORMAT& format, UINT& flags,
      const UINT* node_masks, IUnknown* const* present_queues) = nullptr;
   void (*post_resize_buffers)(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) = nullptr;
   // 1.0's Game::OnPresent
   void (*pre_present)(IDXGISwapChain* swapchain, ID3D11Device* device) = nullptr;
   void (*post_present)(IDXGISwapChain* swapchain, ID3D11Device* device) = nullptr;

   // Resources
   void (*pre_create_buffer)(ID3D11Device* device, D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA*& initial_data) = nullptr;
   void (*post_create_buffer)(ID3D11Device* device, const D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Buffer* buffer) = nullptr;
   void (*pre_create_texture_2d)(ID3D11Device* device, D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data) = nullptr;
   void (*post_create_texture_2d)(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture) = nullptr;
   // Buffers and textures of every dimension
   void (*on_resource_destroyed)(ID3D11Device* device, const ID3D11Resource* resource) = nullptr;

   // Views: a null desc is the game's null one
   void (*post_create_shader_resource_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ID3D11ShaderResourceView* view) = nullptr;
   void (*post_create_render_target_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view) = nullptr;
   void (*post_create_unordered_access_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ID3D11UnorderedAccessView* view) = nullptr;
   void (*post_create_depth_stencil_view)(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view) = nullptr;
   void (*on_view_destroyed)(ID3D11Device* device, const ID3D11View* view) = nullptr;

   // States. CreateBlendState is reported as a DESC1 with logic ops disabled.
   void (*pre_create_sampler_state)(ID3D11Device* device, D3D11_SAMPLER_DESC& desc) = nullptr;
   void (*pre_create_blend_state)(ID3D11Device* device, D3D11_BLEND_DESC1& desc) = nullptr;

   // Shaders of every stage (geometry shaders with stream output too)
   void (*pre_create_shader)(ID3D11Device* device, ShaderStage stage, const void*& bytecode, SIZE_T& length) = nullptr;
   void (*post_create_shader)(ID3D11Device* device, ShaderStage stage, const void* bytecode, SIZE_T length, ID3D11DeviceChild* shader) = nullptr;

   // Binds: the arrays are copies of the game's, which can be edited
   bool (*pre_set_constant_buffers)(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11Buffer** buffers,
      const UINT* first_constant, const UINT* num_constants) = nullptr;
   // Null arrays when the game keeps the bound ones (D3D11_KEEP_*)
   bool (*pre_om_set_render_targets)(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView*& depth_stencil) = nullptr;
   bool (*pre_om_set_render_targets_and_unordered_access_views)(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView** views,
      ID3D11DepthStencilView*& depth_stencil, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView** uavs, const UINT* initial_counts) = nullptr;
   void (*post_rs_set_viewports)(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports) = nullptr;
   void (*post_rs_set_scissor_rects)(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects) = nullptr;
   void (*post_om_set_blend_state)(ID3D11DeviceContext* context, ID3D11BlendState* state, const FLOAT blend_factor[4], UINT sample_mask) = nullptr;
   void (*post_om_set_depth_stencil_state)(ID3D11DeviceContext* context, ID3D11DepthStencilState* state, UINT stencil_ref) = nullptr;
   void (*post_ia_set_vertex_buffers)(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets) = nullptr;
   void (*post_ia_set_index_buffer)(ID3D11DeviceContext* context, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset) = nullptr;
   void (*post_clear_state)(ID3D11DeviceContext* context) = nullptr;

   // Data
   void (*post_map)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE& mapped) = nullptr;
   bool (*pre_unmap)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource) = nullptr;
   // After the original returned (the mapped memory is gone)
   void (*post_unmap)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource) = nullptr;
   // `copy_flags` is only set by UpdateSubresource1
   bool (*pre_update_subresource)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data,
      UINT row_pitch, UINT depth_pitch, UINT copy_flags) = nullptr;
   bool (*pre_copy_resource)(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source) = nullptr;
   // A whole-subresource copy has no box, as the game passed it
   bool (*pre_copy_subresource_region)(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
      ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, UINT copy_flags) = nullptr;
   bool (*pre_resolve_subresource)(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source,
      UINT source_subresource, DXGI_FORMAT format) = nullptr;
   bool (*pre_clear_render_target_view)(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4]) = nullptr;
   bool (*pre_copy_structure_count)(ID3D11DeviceContext* context, ID3D11Buffer* destination, UINT destination_offset, ID3D11UnorderedAccessView* source) = nullptr;
   // Not reported as a render target clear, even on a render target view
   bool (*pre_clear_view)(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count) = nullptr;

   // Deferred contexts
   void (*post_create_deferred_context)(ID3D11Device* device, ID3D11DeviceContext* context) = nullptr;
   void (*on_deferred_context_destroyed)(ID3D11Device* device, const ID3D11DeviceContext* context) = nullptr;
   void (*post_finish_command_list)(ID3D11DeviceContext* context, BOOL restore, ID3D11CommandList* list) = nullptr;
   bool (*pre_execute_command_list)(ID3D11DeviceContext* context, ID3D11CommandList* list, BOOL restore) = nullptr;
};
} // namespace D3D11

// Grows with Step 2 (pipelines, command lists)
namespace D3D12 {
struct Callbacks
{
   void (*post_create_device)(ID3D12Device* device) = nullptr;
   void (*on_device_destroyed)(ID3D12Device* device) = nullptr;
   void (*post_create_swapchain)(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue) = nullptr;
   void (*post_resize_buffers)(IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) = nullptr;
   void (*pre_present)(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue) = nullptr;
   void (*post_present)(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue) = nullptr;
};
} // namespace D3D12

// Replaces the callbacks set before for that API
void SetCallbacks(const D3D11::Callbacks& callbacks);
void SetCallbacks(const D3D12::Callbacks& callbacks);

// 1.0's DrawOrDispatchOverrideType
enum class DrawResult : uint8_t
{
   None,
   Skip,
   Replaced,
};
using D3D11DrawCallback = DrawResult (*)(ID3D11DeviceContext* context);
// A callback for the draws and dispatches of any of these shaders (CRC32 hashes). Step 1: LUMA_UNIMPLEMENTED until then.
void OnDraw(std::span<const uint32_t> hashes, D3D11DrawCallback callback);

// The game's own content in the overlay (1.0's DrawImGuiSettings, PrintImGuiInfo, PrintImGuiAbout, DrawImGuiDevSettings).
// Drawn with UI::Widgets. Plain settings go through the registry instead.
struct Panels
{
   void (*settings)() = nullptr; // Below the registry's settings
   void (*info)() = nullptr; // In the Info tab (Development) or the settings view
   void (*about)() = nullptr; // Above the credits
   void (*development)() = nullptr; // Development builds only
};

void SetPanels(const Panels& panels);

// Per-device game data: created with each device the game creates (before post_create_device), destroyed with it
struct DeviceDataType
{
   void* (*create)() = nullptr;
   void (*destroy)(void* data) = nullptr;
};

void SetDeviceDataType(const DeviceDataType& type);
// Null for a device Luma doesn't know (or before SetDeviceData)
void* GetDeviceDataPointer(IUnknown* device);

template <typename T>
void SetDeviceData()
{
   SetDeviceDataType({[]() -> void* { return new T(); }, [](void* data) { delete static_cast<T*>(data); }});
}

template <typename T>
T* GetDeviceData(IUnknown* device)
{
   return static_cast<T*>(GetDeviceDataPointer(device));
}
} // namespace Game
} // namespace Luma
