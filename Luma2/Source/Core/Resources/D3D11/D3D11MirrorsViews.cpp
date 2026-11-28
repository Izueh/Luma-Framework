#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D11/D3D11MirrorsInternal.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "Hooks/ObjectLifetime.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Utils/Format.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D11 {
namespace {
using Internal::ContextChainState;
using Internal::OwnedResource;
using Internal::ResourceMirrorState;

struct ViewState
{
   ComPtr<ID3D11View> mirror;
   ID3D11Resource* mirror_resource = nullptr;
   uint64_t generation = 0;

   ~ViewState() { CountView(-1); }
};

struct OriginalViewState
{
   ID3D11View* original = nullptr;
};

struct ResourceViewList
{
   std::mutex mutex; // Never held with another
   ID3D11Resource* resource = nullptr;
   std::vector<ID3D11View*> views;
};

struct ResourceViewsState
{
   std::shared_ptr<ResourceViewList> list;

   ~ResourceViewsState()
   {
      if (!list)
         return;
      std::scoped_lock lock(list->mutex);
      list->resource = nullptr;
      list->views.clear();
   }
};

struct ViewResourceState
{
   std::shared_ptr<ResourceViewList> list;
   ID3D11View* view = nullptr;

   ~ViewResourceState()
   {
      if (!list || !view)
         return;
      std::scoped_lock lock(list->mutex);
      std::erase(list->views, view);
   }
};

void RegisterViewResource(ID3D11Resource* resource, ID3D11View* view)
{
   if (!resource || !view)
      return;
   ResourceViewsState* res_state = Hooks::Lifetime::GetState<ResourceViewsState>(resource);
   if (!res_state)
   {
      auto list = std::make_shared<ResourceViewList>();
      list->resource = resource;
      res_state = Hooks::Lifetime::SetState<ResourceViewsState>(resource, std::move(list));
   }
   if (!res_state || !res_state->list)
      return;
   {
      std::scoped_lock lock(res_state->list->mutex);
      if (std::ranges::find(res_state->list->views, view) == res_state->list->views.end())
         res_state->list->views.push_back(view);
   }
   Hooks::Lifetime::SetState<ViewResourceState>(view, res_state->list, view);
}

template <typename Desc>
Desc ToBaseDesc(const auto& desc1)
{
   Desc desc;
   std::memcpy(&desc, &desc1, sizeof(desc));
   return desc;
}

Upgrade::ViewKind GetViewKind(const D3D11_SHADER_RESOURCE_VIEW_DESC1*) { return Upgrade::ViewKind::ShaderResource; }

Upgrade::ViewKind GetViewKind(const D3D11_SHADER_RESOURCE_VIEW_DESC*) { return Upgrade::ViewKind::ShaderResource; }

Upgrade::ViewKind GetViewKind(const D3D11_RENDER_TARGET_VIEW_DESC1*) { return Upgrade::ViewKind::RenderTarget; }

Upgrade::ViewKind GetViewKind(const D3D11_RENDER_TARGET_VIEW_DESC*) { return Upgrade::ViewKind::RenderTarget; }

Upgrade::ViewKind GetViewKind(const D3D11_UNORDERED_ACCESS_VIEW_DESC1*) { return Upgrade::ViewKind::UnorderedAccess; }

Upgrade::ViewKind GetViewKind(const D3D11_UNORDERED_ACCESS_VIEW_DESC*) { return Upgrade::ViewKind::UnorderedAccess; }

Upgrade::ViewKind GetViewKind(const D3D11_DEPTH_STENCIL_VIEW_DESC*) { return Upgrade::ViewKind::DepthStencil; }

HRESULT CreateView(ID3D11Device* device, ID3D11Resource* mirror, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ComPtr<ID3D11View>& view)
{
   const D3D11_SHADER_RESOURCE_VIEW_DESC base = desc ? ToBaseDesc<D3D11_SHADER_RESOURCE_VIEW_DESC>(*desc) : D3D11_SHADER_RESOURCE_VIEW_DESC{};
   return device->CreateShaderResourceView(mirror, desc ? &base : nullptr, reinterpret_cast<ID3D11ShaderResourceView**>(view.GetAddressOf()));
}

HRESULT CreateView(ID3D11Device* device, ID3D11Resource* mirror, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc, ComPtr<ID3D11View>& view)
{
   return device->CreateShaderResourceView(mirror, desc, reinterpret_cast<ID3D11ShaderResourceView**>(view.GetAddressOf()));
}

HRESULT CreateView(ID3D11Device* device, ID3D11Resource* mirror, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ComPtr<ID3D11View>& view)
{
   const D3D11_RENDER_TARGET_VIEW_DESC base = desc ? ToBaseDesc<D3D11_RENDER_TARGET_VIEW_DESC>(*desc) : D3D11_RENDER_TARGET_VIEW_DESC{};
   return device->CreateRenderTargetView(mirror, desc ? &base : nullptr, reinterpret_cast<ID3D11RenderTargetView**>(view.GetAddressOf()));
}

HRESULT CreateView(ID3D11Device* device, ID3D11Resource* mirror, const D3D11_RENDER_TARGET_VIEW_DESC* desc, ComPtr<ID3D11View>& view)
{
   return device->CreateRenderTargetView(mirror, desc, reinterpret_cast<ID3D11RenderTargetView**>(view.GetAddressOf()));
}

HRESULT CreateView(ID3D11Device* device, ID3D11Resource* mirror, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ComPtr<ID3D11View>& view)
{
   const D3D11_UNORDERED_ACCESS_VIEW_DESC base = desc ? ToBaseDesc<D3D11_UNORDERED_ACCESS_VIEW_DESC>(*desc) : D3D11_UNORDERED_ACCESS_VIEW_DESC{};
   return device->CreateUnorderedAccessView(mirror, desc ? &base : nullptr, reinterpret_cast<ID3D11UnorderedAccessView**>(view.GetAddressOf()));
}

HRESULT CreateView(ID3D11Device* device, ID3D11Resource* mirror, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc, ComPtr<ID3D11View>& view)
{
   return device->CreateUnorderedAccessView(mirror, desc, reinterpret_cast<ID3D11UnorderedAccessView**>(view.GetAddressOf()));
}

HRESULT CreateView(ID3D11Device* device, ID3D11Resource* mirror, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ComPtr<ID3D11View>& view)
{
   return device->CreateDepthStencilView(mirror, desc, reinterpret_cast<ID3D11DepthStencilView**>(view.GetAddressOf()));
}

// The game's view format of a texture upgraded in place, between the Pre and the Post of its view creation (same thread)
thread_local DXGI_FORMAT game_view_format = DXGI_FORMAT_UNKNOWN;

template <typename Desc>
DXGI_FORMAT MapInPlaceViewFormat(ID3D11Resource* resource, std::optional<Desc>& desc)
{
   const InPlaceState* state = FindInPlace(resource);
   if (!state || !desc)
      return DXGI_FORMAT_UNKNOWN;
   const DXGI_FORMAT game_format = desc->Format;
   const DXGI_FORMAT format = Upgrade::GetBestViewFormat(game_format, GetViewKind(&*desc), state->original_info.format, state->info.format);
   if (format == DXGI_FORMAT_UNKNOWN || format == game_format)
      return DXGI_FORMAT_UNKNOWN;
   desc->Format = format;
   return game_format;
}

template <typename Desc, typename View>
View* CreateMirroredView(ID3D11Device* device, ID3D11Resource* resource, const Desc* desc, View* view)
{
   const ResourceMirrorState* state = FindMirrorState<OwnedResource>(resource);
   if (!state || !view)
      return nullptr;
   std::optional<Desc> mirror_desc;
   if (desc)
   {
      mirror_desc = *desc;
      const DXGI_FORMAT format = Upgrade::GetBestViewFormat(desc->Format, GetViewKind(desc), state->original_info.format, state->info.format);
      mirror_desc->Format = format != DXGI_FORMAT_UNKNOWN ? format : desc->Format;
   }
   ComPtr<ID3D11View> mirror_view;
   const HRESULT result = CreateView(device, state->mirror.Get(), mirror_desc ? &*mirror_desc : nullptr, mirror_view);
   if (FAILED(result) || !mirror_view)
   {
      LogCreationFailure(std::format("a view of a {} mirror", Utils::Format::GetName(state->info.format)), result);
      return nullptr;
   }
   View* raw = static_cast<View*>(mirror_view.Get());
   Hooks::Lifetime::SetState<OriginalViewState>(mirror_view.Get(), static_cast<ID3D11View*>(view));
   RegisterViewResource(state->mirror.Get(), mirror_view.Get());
   CountView(1);
   Hooks::Lifetime::SetState<ViewState>(view, std::move(mirror_view), state->mirror.Get(), state->generation);
   return raw;
}

template <typename View>
View* CreateMirrorViewLazily(View* view)
{
   if (!view)
      return nullptr;
   ID3D11Resource* resource = GetViewResource(view);
   const ResourceMirrorState* state = FindMirrorState<OwnedResource>(resource);
   if (!state)
      return nullptr;
   ComPtr<ID3D11Device> device;
   view->GetDevice(&device);
   if (!device)
      return nullptr;
   if constexpr (std::is_same_v<View, ID3D11ShaderResourceView>)
   {
      D3D11_SHADER_RESOURCE_VIEW_DESC desc;
      view->GetDesc(&desc);
      return CreateMirroredView(device.Get(), resource, &desc, view);
   }
   else if constexpr (std::is_same_v<View, ID3D11RenderTargetView>)
   {
      D3D11_RENDER_TARGET_VIEW_DESC desc;
      view->GetDesc(&desc);
      return CreateMirroredView(device.Get(), resource, &desc, view);
   }
   else if constexpr (std::is_same_v<View, ID3D11UnorderedAccessView>)
   {
      D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
      view->GetDesc(&desc);
      return CreateMirroredView(device.Get(), resource, &desc, view);
   }
   else if constexpr (std::is_same_v<View, ID3D11DepthStencilView>)
   {
      D3D11_DEPTH_STENCIL_VIEW_DESC desc;
      view->GetDesc(&desc);
      return CreateMirroredView(device.Get(), resource, &desc, view);
   }
   else
   {
      return nullptr;
   }
}

template <typename View>
View* FindMirrorViewImpl(View* view)
{
   if (!view)
      return nullptr;
   const ViewState* state = Hooks::Lifetime::GetState<ViewState>(view);
   if (state)
   {
      // Verify the underlying mirror wasn't invalidated/recreated (only possible when scaling is active)
      if (!Scaling::IsNeeded())
         return static_cast<View*>(state->mirror.Get());
      ID3D11Resource* resource = GetViewResource(view);
      const ResourceMirrorState* mirror = FindMirrorState<OwnedResource>(resource);
      if (mirror && mirror->mirror.Get() == state->mirror_resource && mirror->generation == state->generation)
         return static_cast<View*>(state->mirror.Get());
      if (!mirror)
         return nullptr;
      return CreateMirrorViewLazily(view);
   }
   if (Chains::IsNeeded() || Scaling::IsNeeded())
      return CreateMirrorViewLazily(view);
   return nullptr;
}

template <typename View>
bool Swap(View** views, UINT count)
{
   if (!views || !HasAnyMirrors())
      return false;
   bool any = false;
   for (UINT i = 0; i < count; i++)
   {
      if (View* mirror = FindMirrorViewImpl(views[i]))
      {
         views[i] = mirror;
         any = true;
      }
   }
   return any;
}

template <typename Desc, typename View>
const Desc* UpgradeCreatedView(ID3D11Device* device, ID3D11Resource* resource, const Desc* desc, View* view, Desc& game_desc)
{
   RegisterViewResource(resource, view);
   CreateMirroredView(device, resource, desc, view);
   const DXGI_FORMAT format = std::exchange(game_view_format, DXGI_FORMAT_UNKNOWN);
   if (!desc || format == DXGI_FORMAT_UNKNOWN)
      return desc;
   game_desc = *desc;
   game_desc.Format = format;
   return &game_desc;
}
} // namespace

void PrepareView(ID3D11Resource* resource, std::optional<D3D11_SHADER_RESOURCE_VIEW_DESC1>& desc)
{
   game_view_format = MapInPlaceViewFormat(resource, desc);
}

void PrepareView(ID3D11Resource* resource, std::optional<D3D11_RENDER_TARGET_VIEW_DESC1>& desc)
{
   game_view_format = MapInPlaceViewFormat(resource, desc);
}

void PrepareView(ID3D11Resource* resource, std::optional<D3D11_UNORDERED_ACCESS_VIEW_DESC1>& desc)
{
   game_view_format = MapInPlaceViewFormat(resource, desc);
}

void PrepareView(ID3D11Resource* resource, std::optional<D3D11_DEPTH_STENCIL_VIEW_DESC>& desc)
{
   game_view_format = MapInPlaceViewFormat(resource, desc);
}

const D3D11_SHADER_RESOURCE_VIEW_DESC1* UpgradeView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc,
   ID3D11ShaderResourceView* view, D3D11_SHADER_RESOURCE_VIEW_DESC1& game_desc)
{
   return UpgradeCreatedView(device, resource, desc, view, game_desc);
}

const D3D11_RENDER_TARGET_VIEW_DESC1* UpgradeView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc,
   ID3D11RenderTargetView* view, D3D11_RENDER_TARGET_VIEW_DESC1& game_desc)
{
   return UpgradeCreatedView(device, resource, desc, view, game_desc);
}

const D3D11_UNORDERED_ACCESS_VIEW_DESC1* UpgradeView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc,
   ID3D11UnorderedAccessView* view, D3D11_UNORDERED_ACCESS_VIEW_DESC1& game_desc)
{
   return UpgradeCreatedView(device, resource, desc, view, game_desc);
}

const D3D11_DEPTH_STENCIL_VIEW_DESC* UpgradeView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view,
   D3D11_DEPTH_STENCIL_VIEW_DESC& game_desc)
{
   return UpgradeCreatedView(device, resource, desc, view, game_desc);
}

void SwapViews(ID3D11ShaderResourceView** views, UINT count)
{
   Swap(views, count);
}

void SwapViews(ID3D11DeviceContext* context, ShaderStage stage, UINT start, ID3D11ShaderResourceView** views, UINT count)
{
   if (!views)
      return;
   const bool track_indirect = Chains::GetMode() >= Chains::Mode::DirectAndIndirect && (stage == ShaderStage::Pixel || stage == ShaderStage::Compute);
   ContextChainState* state = track_indirect ? Hooks::Lifetime::GetOrCreateStateCached<ContextChainState>(context) : nullptr;
   for (UINT i = 0; i < count; i++)
   {
      bool upgraded = false;
      if (HasAnyMirrors())
      {
         if (ID3D11ShaderResourceView* mirror = FindMirrorView(views[i]))
         {
            views[i] = mirror;
            upgraded = true;
         }
      }
      if (state && start + i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
      {
         if (stage == ShaderStage::Pixel)
            state->ps_srvs_upgraded[start + i] = upgraded;
         else
            state->cs_srvs_upgraded[start + i] = upgraded;
      }
   }
   if (state)
   {
      if (stage == ShaderStage::Pixel)
         state->any_ps_srvs_upgraded = std::ranges::any_of(state->ps_srvs_upgraded, [](bool v) { return v; });
      else
         state->any_cs_srvs_upgraded = std::ranges::any_of(state->cs_srvs_upgraded, [](bool v) { return v; });
   }
}

void SwapViews(ID3D11RenderTargetView** views, UINT count)
{
   Swap(views, count);
}

void SwapViews(ID3D11UnorderedAccessView** views, UINT count)
{
   Swap(views, count);
}

void SwapViews(ID3D11DeviceContext* context, ShaderStage stage, UINT start, ID3D11UnorderedAccessView** views, UINT count)
{
   if (!views)
      return;
   const bool track_indirect = Chains::GetMode() >= Chains::Mode::DirectAndIndirect && (stage == ShaderStage::Pixel || stage == ShaderStage::Compute);
   ContextChainState* state = track_indirect ? Hooks::Lifetime::GetOrCreateStateCached<ContextChainState>(context) : nullptr;
   for (UINT i = 0; i < count; i++)
   {
      bool upgraded = false;
      if (HasAnyMirrors())
      {
         if (ID3D11UnorderedAccessView* mirror = FindMirrorView(views[i]))
         {
            views[i] = mirror;
            upgraded = true;
         }
      }
      if (state && start + i < D3D11_1_UAV_SLOT_COUNT)
      {
         if (stage == ShaderStage::Pixel)
            state->ps_uavs_upgraded[start + i] = upgraded;
         else
            state->cs_uavs_upgraded[start + i] = upgraded;
      }
   }
   if (state)
   {
      if (stage == ShaderStage::Pixel)
         state->any_ps_uavs_upgraded = std::ranges::any_of(state->ps_uavs_upgraded, [](bool v) { return v; });
      else
         state->any_cs_uavs_upgraded = std::ranges::any_of(state->cs_uavs_upgraded, [](bool v) { return v; });
   }
}

void SwapView(ID3D11DepthStencilView*& view)
{
   if (!HasAnyMirrors())
      return;
   if (ID3D11DepthStencilView* mirror = FindMirrorView(view))
      view = mirror;
}

ID3D11ShaderResourceView* FindMirrorView(ID3D11ShaderResourceView* view)
{
   return FindMirrorViewImpl(view);
}

ID3D11RenderTargetView* FindMirrorView(ID3D11RenderTargetView* view)
{
   return FindMirrorViewImpl(view);
}

ID3D11UnorderedAccessView* FindMirrorView(ID3D11UnorderedAccessView* view)
{
   return FindMirrorViewImpl(view);
}

ID3D11DepthStencilView* FindMirrorView(ID3D11DepthStencilView* view)
{
   return FindMirrorViewImpl(view);
}

ID3D11View* FindMirrorView(ID3D11View* view)
{
   if (!view)
      return nullptr;
   if (!Scaling::IsNeeded())
   {
      if (const ViewState* state = Hooks::Lifetime::GetState<ViewState>(view))
         return state->mirror.Get();
      if (!Chains::IsNeeded())
         return nullptr;
   }
   ComPtr<ID3D11ShaderResourceView> srv;
   if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&srv))))
      return FindMirrorViewImpl(srv.Get());
   ComPtr<ID3D11RenderTargetView> rtv;
   if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&rtv))))
      return FindMirrorViewImpl(rtv.Get());
   ComPtr<ID3D11UnorderedAccessView> uav;
   if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&uav))))
      return FindMirrorViewImpl(uav.Get());
   ComPtr<ID3D11DepthStencilView> dsv;
   if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&dsv))))
      return FindMirrorViewImpl(dsv.Get());
   return nullptr;
}

ID3D11View* FindOriginalView(ID3D11View* mirror_view)
{
   if (!mirror_view || !HasAnyMirrors())
      return nullptr;
   const OriginalViewState* state = Hooks::Lifetime::GetState<OriginalViewState>(mirror_view);
   return state ? state->original : nullptr;
}

void TrackView(ID3D11Resource* resource, ID3D11View* view)
{
   RegisterViewResource(resource, view);
}

ID3D11Resource* GetViewResource(ID3D11View* view)
{
   if (!view)
      return nullptr;
   if (const ViewResourceState* state = Hooks::Lifetime::GetState<ViewResourceState>(view); state && state->list)
   {
      std::scoped_lock lock(state->list->mutex);
      if (state->list->resource)
         return state->list->resource;
   }
   ComPtr<ID3D11Resource> resource;
   view->GetResource(&resource);
   return resource.Get();
}

std::vector<ID3D11View*> GetResourceViews(ID3D11Resource* resource)
{
   if (!resource)
      return {};
   const ResourceViewsState* state = Hooks::Lifetime::GetState<ResourceViewsState>(resource);
   if (!state || !state->list)
      return {};
   std::scoped_lock lock(state->list->mutex);
   return state->list->views;
}
} // namespace D3D11
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
