#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include <d3d11_4.h>

#include "Common/ShaderStage.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceUpgrades.hpp"

// Resources::Mirrors on D3D11. A texture the rules match gets a mirror at its creation, held in the texture's
// private data and released with it; each view the game creates of it gets the same view of the mirror, held in the view's.
// Chain and seed mirrors are created immediately on the immediate context, or queued for the next present on deferred contexts.
// Everything here runs inside Luma's scope (the handlers').
namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D11 {
struct MirrorSummary
{
   Upgrade::TextureInfo original_info;
   Upgrade::TextureInfo mirror_info;
   Chains::Origin origin = {};
   bool is_scaled = false;
   bool pending = false;
};

struct ContextScalingSnapshot
{
   bool viewports_scaled = false;
   bool scissors_scaled = false;
   UINT game_viewport_count = 0;
   std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> game_viewports = {};
   UINT game_scissor_count = 0;
   std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> game_scissors = {};
   bool chain_seed = false;
   bool chain_propagated = false;
   bool cross_thread_overlap = false;
};

// At the game's texture creation: a mirror when the rules match it
void CreateMirror(ID3D11Device* device, const D3D11_TEXTURE1D_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture1D* texture);
void CreateMirror(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture);
void CreateMirror(ID3D11Device* device, const D3D11_TEXTURE3D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture3D* texture);

// At the game's view creation, for a mirrored texture: the same view of the mirror. A null desc is the game's null one.
void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ID3D11ShaderResourceView* view);
void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view);
void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ID3D11UnorderedAccessView* view);
void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view);

// Binds, while swapping: each view of a mirrored texture in the detour's copy of the array replaced by its mirror's
void SwapViews(ID3D11ShaderResourceView** views, UINT count);
void SwapViews(ID3D11DeviceContext* context, ShaderStage stage, UINT start, ID3D11ShaderResourceView** views, UINT count);
void SwapViews(ID3D11RenderTargetView** views, UINT count);
void SwapViews(ID3D11UnorderedAccessView** views, UINT count);
void SwapViews(ID3D11DeviceContext* context, ShaderStage stage, UINT start, ID3D11UnorderedAccessView** views, UINT count);
void SwapView(ID3D11DepthStencilView*& view);

// Viewport/scissor hooks for scaling (resets scaled flags and applies ViewportsOnly expansion when active)
void OnBindViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports);
void OnBindScissorRects(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects);

// Draw/dispatch hooks for chains (seeds + DirectAndIndirect) and scaling (proportional & ViewportsOnly viewport/scissor rescaling)
void PrepareDrawOrDispatch(ID3D11DeviceContext* context, bool is_dispatch);
void FinishDrawOrDispatch(ID3D11DeviceContext* context, bool is_dispatch);

// Context and device lifecycle
void ForgetContextState(ID3D11DeviceContext* context);
void OnPresent(ID3D11Device* device);
void InvalidateScaledMirrors(ID3D11Device* device);
void ReleaseDeviceObjects(ID3D11Device* device);

// Queries
ID3D11Resource* FindMirror(ID3D11Resource* resource);
ID3D11Resource* FindOriginal(ID3D11Resource* mirror);
std::optional<MirrorSummary> FindMirrorSummary(ID3D11Resource* resource);
std::optional<ContextScalingSnapshot> GetContextScalingSnapshot(ID3D11DeviceContext* context);

// Copies, resolves and clears of a mirrored texture or view, issued on the mirrors while swapping. True: Luma issued the call, the
// game's doesn't run.
bool RedirectCopy(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source, bool* out_scaled = nullptr);
bool RedirectCopyRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z, ID3D11Resource* source,
   UINT source_subresource, const D3D11_BOX* box, UINT copy_flags, bool* out_scaled = nullptr, std::optional<D3D11_BOX>* out_rescaled_box = nullptr);
bool RedirectResolve(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source, UINT source_subresource,
   DXGI_FORMAT format);
bool RedirectClear(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4]);
bool RedirectClear(ID3D11DeviceContext* context, ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil);
bool RedirectClear(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const UINT values[4]);
bool RedirectClear(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const FLOAT values[4]);
bool RedirectClearView(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count);

// UpdateSubresource of a mirrored texture: the data converted into its mirror as well (the game's call still writes the original)
void WriteToMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT row_pitch, UINT depth_pitch);
// Map/Unmap of a mirrored texture: asserts on CPU reads and converts CPU writes into the mirror at Unmap
void TrackMappedMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, const D3D11_MAPPED_SUBRESOURCE& mapped);
void CommitMappedMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource);
// Resolves between a mirror and a texture of another family, which run on the game's textures
uint32_t GetUnhandledResolves();
} // namespace D3D11
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
