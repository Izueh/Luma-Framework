#include "Resources/D3D11/D3D11Mirrors.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include <wrl/client.h>

#include "Draw/D3D11/D3D11Draw.hpp"
#include "Frame/FrameState.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Log/Log.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Shaders/Shaders.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Format.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D11 {
namespace {
using namespace Utils::Format::DXGI;
using OwnedResource = ComPtr<ID3D11Resource>;
using ResourceMirrorState = MirrorState<ID3D11Resource, OwnedResource>;

constexpr uint64_t RETIRE_PRESENTS = 2;
constexpr uint64_t POOL_TTL_FRAMES = 120;

std::atomic<uint32_t> unhandled_resolves = 0;

struct PooledTexture
{
   ComPtr<ID3D11Texture2D> texture;
   D3D11_TEXTURE2D_DESC desc = {};
   uint64_t stored_frame = 0;
};

struct RetiredMirror
{
   ComPtr<ID3D11Resource> mirror;
   D3D11_TEXTURE2D_DESC desc = {};
   uint64_t retire_frame = 0;
   bool poolable = false;
};

struct PendingChainMirror
{
   ComPtr<ID3D11Resource> original;
   Upgrade::TextureInfo original_info;
   Upgrade::TextureInfo mirror_info;
   Chains::Origin origin = {};
};

// Per-device mirror registry, recycle pool and deferred-context pending chain queue. Never held with another lock.
struct DeviceMirrorRegistry
{
   std::mutex mutex;
   uint64_t scale_generation = 0;
   std::unordered_set<ID3D11Resource*> scaled_originals;
   std::unordered_set<const ID3D11Resource*> pending_set;
   std::vector<PendingChainMirror> pending_queue;
   std::vector<RetiredMirror> retired;
   std::vector<PooledTexture> pool;
   bool released = false;
};

struct DeviceMirrorState
{
   std::shared_ptr<DeviceMirrorRegistry> registry = std::make_shared<DeviceMirrorRegistry>();
};

DeviceMirrorState* GetOrCreateDeviceState(ID3D11Device* device)
{
   if (!device)
      return nullptr;
   if (DeviceMirrorState* existing = Hooks::Lifetime::GetState<DeviceMirrorState>(device))
      return existing;
   return Hooks::Lifetime::SetState<DeviceMirrorState>(device);
}

// Attached to an original resource whose mirror is scaled; removes the pointer on resource destruction
struct ScaledTracker
{
   std::shared_ptr<DeviceMirrorRegistry> registry;
   ID3D11Resource* original = nullptr;

   ~ScaledTracker()
   {
      if (!registry || !original)
         return;
      std::scoped_lock lock(registry->mutex);
      registry->scaled_originals.erase(original);
   }
};

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

struct ContextChainState
{
   std::array<bool, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> ps_srvs_upgraded = {};
   std::array<bool, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> cs_srvs_upgraded = {};
   std::array<bool, D3D11_1_UAV_SLOT_COUNT> ps_uavs_upgraded = {};
   std::array<bool, D3D11_1_UAV_SLOT_COUNT> cs_uavs_upgraded = {};
   bool any_ps_srvs_upgraded = false;
   bool any_cs_srvs_upgraded = false;
   bool any_ps_uavs_upgraded = false;
   bool any_cs_uavs_upgraded = false;
   ContextScalingSnapshot scaling = {};
};

bool DescMatches(const D3D11_TEXTURE2D_DESC& a, const D3D11_TEXTURE2D_DESC& b)
{
   return a.Width == b.Width && a.Height == b.Height && a.MipLevels == b.MipLevels && a.ArraySize == b.ArraySize && a.Format == b.Format &&
      a.SampleDesc.Count == b.SampleDesc.Count && a.SampleDesc.Quality == b.SampleDesc.Quality && a.Usage == b.Usage && a.BindFlags == b.BindFlags &&
      a.CPUAccessFlags == b.CPUAccessFlags && a.MiscFlags == b.MiscFlags;
}

ComPtr<ID3D11Texture2D> AcquireFromPool(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& desc)
{
   DeviceMirrorState* dev = GetOrCreateDeviceState(device);
   if (!dev)
      return nullptr;
   std::scoped_lock lock(dev->registry->mutex);
   for (size_t i = 0; i < dev->registry->pool.size(); i++)
   {
      if (DescMatches(dev->registry->pool[i].desc, desc))
      {
         ComPtr<ID3D11Texture2D> texture = std::move(dev->registry->pool[i].texture);
         dev->registry->pool.erase(dev->registry->pool.begin() + ptrdiff_t(i));
         Scaling::CountPoolSize(-1);
         return texture;
      }
   }
   return nullptr;
}

template <typename Desc>
Upgrade::TextureInfo Describe(const Desc& desc)
{
   Upgrade::TextureInfo described = {.width = desc.Width, .levels = desc.MipLevels, .format = desc.Format};
   if constexpr (std::is_same_v<Desc, D3D11_TEXTURE1D_DESC>)
   {
      described.dimension = Upgrade::Dimension::Texture1D;
      described.depth_or_layers = desc.ArraySize;
   }
   else if constexpr (std::is_same_v<Desc, D3D11_TEXTURE2D_DESC1> || std::is_same_v<Desc, D3D11_TEXTURE2D_DESC>)
   {
      described.height = desc.Height;
      described.depth_or_layers = desc.ArraySize;
      described.samples = desc.SampleDesc.Count;
      described.cube = (desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0 && desc.ArraySize % 6 == 0;
   }
   else
   {
      described.dimension = Upgrade::Dimension::Texture3D;
      described.height = desc.Height;
      described.depth_or_layers = desc.Depth;
   }
   described.render_target = (desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
   described.unordered_access = (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
   described.depth_stencil = (desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0;
   described.shader_resource = (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0;
   described.gpu_only = desc.Usage == D3D11_USAGE_DEFAULT || desc.Usage == D3D11_USAGE_IMMUTABLE;
   return described;
}

std::optional<Upgrade::TextureInfo> DescribeResource(ID3D11Resource* resource)
{
   if (!resource)
      return std::nullopt;
   D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   resource->GetType(&dimension);
   switch (dimension)
   {
   case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
   {
      D3D11_TEXTURE1D_DESC desc;
      static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
      return Describe(desc);
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
   {
      D3D11_TEXTURE2D_DESC desc;
      static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
      return Describe(desc);
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
   {
      D3D11_TEXTURE3D_DESC desc;
      static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
      return Describe(desc);
   }
   default: return std::nullopt;
   }
}

Upgrade::FrameState GetFrameState(ID3D11Device* device)
{
   const Frame::Resolutions resolutions = Frame::Read(Frame::Find(device));
   return {.render = resolutions.render, .output = resolutions.output, .display = resolutions.display};
}

uint32_t MipSize(uint32_t size, uint32_t level)
{
   return std::max(size >> level, 1u);
}

struct ConvertedData
{
   std::vector<Converted> subresources;
   std::vector<D3D11_SUBRESOURCE_DATA> data;
};

std::optional<ConvertedData> ConvertInitialData(const Upgrade::TextureInfo& desc, DXGI_FORMAT mirror_format, const D3D11_SUBRESOURCE_DATA* initial_data)
{
   if (!initial_data || mirror_format != DXGI_FORMAT_R16G16B16A16_FLOAT)
      return std::nullopt;
   const bool volume = desc.dimension == Upgrade::Dimension::Texture3D;
   const uint32_t layers = volume ? 1 : desc.depth_or_layers;
   ConvertedData converted;
   for (uint32_t layer = 0; layer < layers; layer++)
   {
      for (uint32_t level = 0; level < desc.levels; level++)
      {
         const D3D11_SUBRESOURCE_DATA& source = initial_data[layer * desc.levels + level];
         std::optional<Converted> subresource = ConvertToRgba16Float(source.pSysMem, source.SysMemPitch, source.SysMemSlicePitch, desc.format, MipSize(desc.width, level),
            MipSize(desc.height, level), volume ? MipSize(desc.depth_or_layers, level) : 1);
         if (!subresource)
            return std::nullopt;
         converted.subresources.push_back(std::move(*subresource));
      }
   }
   for (const Converted& subresource : converted.subresources)
      converted.data.push_back({subresource.bytes.data(), subresource.row_pitch, subresource.depth_pitch});
   return converted;
}

template <typename Desc>
Desc MakeMirrorDesc(Desc desc, const Upgrade::TextureInfo& mirror_info)
{
   desc.Format = mirror_info.format;
   desc.Width = mirror_info.width;
   desc.MipLevels = mirror_info.levels;
   if constexpr (std::is_same_v<Desc, D3D11_TEXTURE1D_DESC>)
   {
      desc.ArraySize = mirror_info.depth_or_layers;
   }
   else if constexpr (std::is_same_v<Desc, D3D11_TEXTURE2D_DESC>)
   {
      desc.Height = mirror_info.height;
      desc.ArraySize = mirror_info.depth_or_layers;
      desc.SampleDesc.Count = mirror_info.samples;
      if (desc.SampleDesc.Count == 1)
         desc.SampleDesc.Quality = 0;
   }
   else
   {
      desc.Height = mirror_info.height;
      desc.Depth = mirror_info.depth_or_layers;
   }
   desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
   desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_RESOURCE_CLAMP;
   desc.CPUAccessFlags = 0;
   desc.Usage = D3D11_USAGE_DEFAULT;
   return desc;
}

void RegisterScaledOriginal(ID3D11Device* device, ID3D11Resource* original)
{
   DeviceMirrorState* dev = GetOrCreateDeviceState(device);
   if (!dev)
      return;
   {
      std::scoped_lock lock(dev->registry->mutex);
      dev->registry->scaled_originals.insert(original);
   }
   Hooks::Lifetime::SetState<ScaledTracker>(original, dev->registry, original);
}

uint64_t GetDeviceScaleGeneration(ID3D11Device* device)
{
   if (DeviceMirrorState* dev = GetOrCreateDeviceState(device))
   {
      std::scoped_lock lock(dev->registry->mutex);
      return dev->registry->scale_generation;
   }
   return 0;
}

bool CreateMirror2D(ID3D11Device* device, ID3D11Texture2D* texture, const D3D11_TEXTURE2D_DESC& desc, const Upgrade::TextureInfo& described,
   const Upgrade::TextureInfo& mirrored, const D3D11_SUBRESOURCE_DATA* initial_data, Chains::Origin origin)
{
   const std::optional<ConvertedData> data = ConvertInitialData(described, mirrored.format, initial_data);
   ASSERT_ONCE_MSG(!initial_data || data, "A texture matching the upgrade rules has initial data that can't be converted to its mirror's format");
   const D3D11_TEXTURE2D_DESC mirror_desc = MakeMirrorDesc(desc, mirrored);
   const bool scaled = mirrored.width != described.width || mirrored.height != described.height;
   ComPtr<ID3D11Texture2D> mirror;
   if (!initial_data && scaled)
      mirror = AcquireFromPool(device, mirror_desc);
   if (!mirror)
   {
      const HRESULT result = device->CreateTexture2D(&mirror_desc, data ? data->data.data() : nullptr, &mirror);
      if (FAILED(result) || !mirror)
      {
         LogCreationFailure(std::format("a {} mirror of a {} texture", Utils::Format::GetName(mirrored.format), Utils::Format::GetName(desc.Format)), result);
         return false;
      }
   }
   ComPtr<ID3D11Resource> mirror_res = mirror;
   const uint64_t generation = scaled ? GetDeviceScaleGeneration(device) : 0;
   AttachMirror<ID3D11Resource>(texture, std::move(mirror_res), described, mirrored, origin, generation);
   if (scaled)
      RegisterScaledOriginal(device, texture);
   return true;
}

template <typename Desc, typename Texture>
void Create(ID3D11Device* device, const Desc& desc, const D3D11_SUBRESOURCE_DATA* initial_data, Texture* texture)
{
   const Upgrade::TextureInfo described = Describe(desc);
   bool resized = false;
   const bool allow_writable_sr = Chains::GetMode() >= Chains::Mode::Direct;
   const std::optional<Upgrade::TextureInfo> mirrored = Upgrade::Match(described, GetFrameState(device), initial_data != nullptr, allow_writable_sr, false, &resized);
   if (!mirrored)
      return;
   const Chains::Origin origin = resized ? Chains::Origin{Chains::OriginKind::CustomDimensions} : Chains::Origin{Chains::OriginKind::Rule};
   if constexpr (std::is_same_v<Desc, D3D11_TEXTURE2D_DESC1>)
   {
      D3D11_TEXTURE2D_DESC base_desc;
      std::memcpy(&base_desc, &desc, sizeof(base_desc));
      CreateMirror2D(device, texture, base_desc, described, *mirrored, initial_data, origin);
   }
   else
   {
      const std::optional<ConvertedData> data = ConvertInitialData(described, mirrored->format, initial_data);
      ASSERT_ONCE_MSG(!initial_data || data, "A texture matching the upgrade rules has initial data that can't be converted to its mirror's format");
      ComPtr<ID3D11Resource> mirror;
      HRESULT result = E_FAIL;
      if constexpr (std::is_same_v<Desc, D3D11_TEXTURE1D_DESC>)
      {
         const D3D11_TEXTURE1D_DESC mirror_desc = MakeMirrorDesc(desc, *mirrored);
         result = device->CreateTexture1D(&mirror_desc, data ? data->data.data() : nullptr, reinterpret_cast<ID3D11Texture1D**>(mirror.GetAddressOf()));
      }
      else
      {
         D3D11_TEXTURE3D_DESC mirror_desc;
         std::memcpy(&mirror_desc, &desc, sizeof(mirror_desc));
         mirror_desc = MakeMirrorDesc(mirror_desc, *mirrored);
         result = device->CreateTexture3D(&mirror_desc, data ? data->data.data() : nullptr, reinterpret_cast<ID3D11Texture3D**>(mirror.GetAddressOf()));
      }
      if (FAILED(result) || !mirror)
         return LogCreationFailure(std::format("a {} mirror of a {} texture", Utils::Format::GetName(mirrored->format), Utils::Format::GetName(desc.Format)), result);
      AttachMirror<ID3D11Resource>(texture, std::move(mirror), described, *mirrored, origin);
   }
}

// Creates a chain/seed mirror for an existing 2D texture (immediate context or Present boundary)
bool MaterializeChainMirror(ID3D11Device* device, ID3D11Resource* resource, const Upgrade::TextureInfo& original_info, const Upgrade::TextureInfo& mirror_info,
   Chains::Origin origin)
{
   if (!device || !resource || FindMirrorState<OwnedResource>(resource))
      return FindMirrorState<OwnedResource>(resource) != nullptr;
   ComPtr<ID3D11Texture2D> texture;
   if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture))))
      return false;
   D3D11_TEXTURE2D_DESC desc;
   texture->GetDesc(&desc);
   return CreateMirror2D(device, texture.Get(), desc, original_info, mirror_info, nullptr, origin);
}

void QueueDeferredChainMirror(ID3D11Device* device, ID3D11Resource* resource, const Upgrade::TextureInfo& original_info, const Upgrade::TextureInfo& mirror_info,
   Chains::Origin origin)
{
   DeviceMirrorState* dev = GetOrCreateDeviceState(device);
   if (!dev || !resource)
      return;
   std::scoped_lock lock(dev->registry->mutex);
   if (dev->registry->released || !dev->registry->pending_set.insert(resource).second)
      return;
   dev->registry->pending_queue.push_back({resource, original_info, mirror_info, origin});
   Chains::CountDeferredQueued(1);
}

bool IsPendingMirror(ID3D11Device* device, const ID3D11Resource* resource)
{
   if (!device || !resource)
      return false;
   DeviceMirrorState* dev = Hooks::Lifetime::GetState<DeviceMirrorState>(device);
   if (!dev)
      return false;
   std::scoped_lock lock(dev->registry->mutex);
   return dev->registry->pending_set.contains(resource);
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

// A native CopyResource/CopySubresourceRegion reinterprets raw bits within a compatible typeless format family,
// whereas a shader blit converts values or scales pixels. We use non-sRGB views here so the shader performs a
// raw-value copy without applying hardware sRGB decode/encode.
DXGI_FORMAT GetRawFormat(DXGI_FORMAT format)
{
   switch (format)
   {
   case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
   case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
   case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
   default: return IsTypeless(format) ? GetDefaultTyped(format) : format;
   }
}

ComPtr<ID3D11Texture2D> CreateProxy(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& desc, UINT bind, UINT width = 0, UINT height = 0)
{
   D3D11_TEXTURE2D_DESC proxy = desc;
   if (width > 0)
      proxy.Width = width;
   if (height > 0)
      proxy.Height = height;
   proxy.MipLevels = 1;
   proxy.ArraySize = 1;
   proxy.SampleDesc = {1, 0};
   proxy.Usage = D3D11_USAGE_DEFAULT;
   proxy.BindFlags = bind;
   proxy.CPUAccessFlags = 0;
   proxy.MiscFlags = 0;
   ComPtr<ID3D11Texture2D> texture;
   device->CreateTexture2D(&proxy, nullptr, &texture);
   return texture;
}

// Converts and/or scales a 2D texture subresource (or partial region) through a shader blit.
// Resolves MSAA sources first into a single-sample temporary because a standard 2D SRV blit cannot sample MSAA directly.
bool ConvertingCopySubresourceRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
   ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, bool* out_scaled = nullptr, DXGI_FORMAT resolve_format = DXGI_FORMAT_UNKNOWN)
{
   if (z != 0)
      return false;
   ComPtr<ID3D11Texture2D> source_texture;
   ComPtr<ID3D11Texture2D> destination_texture;
   if (FAILED(source->QueryInterface(IID_PPV_ARGS(&source_texture))) || FAILED(destination->QueryInterface(IID_PPV_ARGS(&destination_texture))))
      return false;
   D3D11_TEXTURE2D_DESC source_desc;
   D3D11_TEXTURE2D_DESC destination_desc;
   source_texture->GetDesc(&source_desc);
   destination_texture->GetDesc(&destination_desc);
   if (destination_desc.SampleDesc.Count != 1)
      return false;
   const UINT src_mips = std::max(source_desc.MipLevels, 1u);
   const UINT dst_mips = std::max(destination_desc.MipLevels, 1u);
   const UINT src_mip = source_subresource % src_mips;
   const UINT src_slice = source_subresource / src_mips;
   const UINT dst_mip = destination_subresource % dst_mips;
   const UINT dst_slice = destination_subresource / dst_mips;
   if (src_slice >= source_desc.ArraySize || dst_slice >= destination_desc.ArraySize)
      return false;
   const UINT src_w = MipSize(source_desc.Width, src_mip);
   const UINT src_h = MipSize(source_desc.Height, src_mip);
   const UINT dst_w = MipSize(destination_desc.Width, dst_mip);
   const UINT dst_h = MipSize(destination_desc.Height, dst_mip);
   D3D11_BOX src_box = {0, 0, 0, src_w, src_h, 1};
   if (box)
   {
      if (box->left >= box->right || box->top >= box->bottom || box->front >= box->back || box->right > src_w || box->bottom > src_h || box->back > 1)
         return false;
      src_box = *box;
   }
   const UINT copy_w = src_box.right - src_box.left;
   const UINT copy_h = src_box.bottom - src_box.top;
   const bool full_src_box = src_box.left == 0 && src_box.top == 0 && src_box.front == 0 && copy_w == src_w && copy_h == src_h;
   const bool full_dst = x == 0 && y == 0 && full_src_box;
   if (!full_dst && (x + copy_w > dst_w || y + copy_h > dst_h))
      return false;

   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   ComPtr<ID3D11Texture2D> read = source_texture;
   UINT read_mip = src_mip;
   UINT read_slice = src_slice;
   UINT read_array_size = source_desc.ArraySize;
   if (source_desc.SampleDesc.Count > 1)
   {
      // Resolve MSAA source first (must be a non-typeless format compatible with the source texture's family)
      D3D11_TEXTURE2D_DESC resolved_desc = source_desc;
      resolved_desc.Format = resolve_format != DXGI_FORMAT_UNKNOWN ? resolve_format : GetRawFormat(source_desc.Format);
      read = CreateProxy(device.Get(), resolved_desc, D3D11_BIND_SHADER_RESOURCE, src_w, src_h);
      if (!read)
         return false;
      context->ResolveSubresource(read.Get(), 0, source_texture.Get(), source_subresource, resolved_desc.Format);
      read_mip = 0;
      read_slice = 0;
      read_array_size = 1;
      if (!full_src_box)
      {
         ComPtr<ID3D11Texture2D> cropped = CreateProxy(device.Get(), resolved_desc, D3D11_BIND_SHADER_RESOURCE, copy_w, copy_h);
         if (!cropped)
            return false;
         context->CopySubresourceRegion(cropped.Get(), 0, 0, 0, 0, read.Get(), 0, &src_box);
         read = std::move(cropped);
      }
   }
   else if (!full_src_box || !(source_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
   {
      read = CreateProxy(device.Get(), source_desc, D3D11_BIND_SHADER_RESOURCE, copy_w, copy_h);
      if (!read)
         return false;
      context->CopySubresourceRegion(read.Get(), 0, 0, 0, 0, source_texture.Get(), source_subresource, &src_box);
      read_mip = 0;
      read_slice = 0;
      read_array_size = 1;
   }

   const UINT target_w = full_dst ? dst_w : copy_w;
   const UINT target_h = full_dst ? dst_h : copy_h;
   const bool direct_rtv = full_dst && (destination_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
   ComPtr<ID3D11Texture2D> write = destination_texture;
   UINT write_mip = dst_mip;
   UINT write_slice = dst_slice;
   UINT write_array_size = destination_desc.ArraySize;
   if (!direct_rtv)
   {
      write = CreateProxy(device.Get(), destination_desc, D3D11_BIND_RENDER_TARGET, target_w, target_h);
      if (!write)
         return false;
      write_mip = 0;
      write_slice = 0;
      write_array_size = 1;
   }

   D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = GetRawFormat(source_desc.Format)};
   if (read_array_size > 1)
   {
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
      srv_desc.Texture2DArray = {read_mip, 1, read_slice, 1};
   }
   else
   {
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D = {read_mip, 1};
   }

   D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {.Format = GetRawFormat(destination_desc.Format)};
   if (write_array_size > 1)
   {
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
      rtv_desc.Texture2DArray = {write_mip, write_slice, 1};
   }
   else
   {
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      rtv_desc.Texture2D = {write_mip};
   }

   ComPtr<ID3D11ShaderResourceView> srv;
   ComPtr<ID3D11RenderTargetView> rtv;
   if (FAILED(device->CreateShaderResourceView(read.Get(), &srv_desc, &srv)) || FAILED(device->CreateRenderTargetView(write.Get(), &rtv_desc, &rtv)))
      return false;

   const bool same_size = copy_w == target_w && copy_h == target_h;
   if (!(same_size ? Draw::D3D11::Copy(context, srv.Get(), rtv.Get(), Draw::FULL_GRAPHICS) : Draw::D3D11::Scale(context, srv.Get(), rtv.Get(), Draw::FULL_GRAPHICS)))
      return false;
   if (!same_size)
   {
      Scaling::CountScaledBlit();
      if (out_scaled)
         *out_scaled = true;
   }
   if (write != destination_texture)
      context->CopySubresourceRegion(destination_texture.Get(), destination_subresource, x, y, 0, write.Get(), 0, nullptr);
   return true;
}

bool ConvertingCopy(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source, bool* out_scaled = nullptr)
{
   return ConvertingCopySubresourceRegion(context, destination, 0, 0, 0, 0, source, 0, nullptr, out_scaled);
}

DXGI_FORMAT GetFormat(ID3D11Resource* resource)
{
   D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   resource->GetType(&dimension);
   switch (dimension)
   {
   case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
   {
      D3D11_TEXTURE1D_DESC desc;
      static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
      return desc.Format;
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
   {
      D3D11_TEXTURE2D_DESC desc;
      static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
      return desc.Format;
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
   {
      D3D11_TEXTURE3D_DESC desc;
      static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
      return desc.Format;
   }
   default: return DXGI_FORMAT_UNKNOWN;
   }
}

bool SameDimensions2D(ID3D11Resource* a, ID3D11Resource* b)
{
   ComPtr<ID3D11Texture2D> ta;
   ComPtr<ID3D11Texture2D> tb;
   if (FAILED(a->QueryInterface(IID_PPV_ARGS(&ta))) || FAILED(b->QueryInterface(IID_PPV_ARGS(&tb))))
      return true;
   D3D11_TEXTURE2D_DESC da;
   D3D11_TEXTURE2D_DESC db;
   ta->GetDesc(&da);
   tb->GetDesc(&db);
   return da.Width == db.Width && da.Height == db.Height;
}

// Propagates a mirror from `source` to `destination` during a Direct copy/resolve when eligible
void TryPropagateDirectChain(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
{
   if (Chains::GetMode() < Chains::Mode::Direct || !destination || !source)
      return;
   if (FindMirrorState<OwnedResource>(destination))
      return;
   const ResourceMirrorState* source_mirror = FindMirrorState<OwnedResource>(source);
   if (!source_mirror)
      return;
   const std::optional<Upgrade::TextureInfo> dst_info = DescribeResource(destination);
   if (!dst_info)
      return;
   const Upgrade::Rules& rules = Upgrade::GetRules();
   if (!Chains::CanPropagate(source_mirror->info, *dst_info, rules.aspect_ratio_pixel_threshold))
      return;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   Upgrade::TextureInfo mirror_info = *dst_info;
   mirror_info.format = source_mirror->info.format;
   const Upgrade::FrameState frame_state = GetFrameState(device.Get());
   const bool source_at_output = source_mirror->is_scaled || (source_mirror->info.width == frame_state.output.x && source_mirror->info.height == frame_state.output.y);
   if (Scaling::IsEnabled() && Scaling::GetOptions().mode == Scaling::Mode::ResourcesAndViewports &&
      Scaling::ShouldScaleTexture(mirror_info, frame_state, Scaling::IsActiveOnCurrentThread(device.Get()), source_at_output, rules.aspect_ratio_pixel_threshold))
   {
      const Math::uint2 scaled = Scaling::ComputeScaledSize({mirror_info.width, mirror_info.height}, frame_state.render, frame_state.output);
      mirror_info.width = scaled.x;
      mirror_info.height = scaled.y;
   }
   const Chains::Origin origin = {.kind = Chains::OriginKind::DirectChain};
   if (context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE)
   {
      if (MaterializeChainMirror(device.Get(), destination, *dst_info, mirror_info, origin))
         Chains::CountDirectPropagation();
   }
   else
   {
      QueueDeferredChainMirror(device.Get(), destination, *dst_info, mirror_info, origin);
   }
}

void ApplyViewportsOnlyExpansion(ID3D11DeviceContext* context, ContextChainState& state, ID3D11Device* device)
{
   if (Scaling::GetOptions().mode != Scaling::Mode::ViewportsOnly || !Scaling::IsEnabled() || !Scaling::IsActiveOnCurrentThread(device))
      return;
   const Math::uint2 target = Scaling::GetTargetResolution(device);
   if (target.x == 0 || target.y == 0)
      return;
   ComPtr<ID3D11RenderTargetView> rtv0;
   context->OMGetRenderTargets(1, &rtv0, nullptr);
   if (!rtv0)
      return;
   ComPtr<ID3D11Resource> res;
   rtv0->GetResource(&res);
   ComPtr<ID3D11Texture2D> tex;
   if (!res || FAILED(res->QueryInterface(IID_PPV_ARGS(&tex))))
      return;
   D3D11_TEXTURE2D_DESC desc;
   tex->GetDesc(&desc);
   if (desc.Width != target.x || desc.Height != target.y)
      return;

   if (Scaling::GetOptions().scale_viewports && !state.scaling.viewports_scaled)
   {
      UINT count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
      D3D11_VIEWPORT current[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
      context->RSGetViewports(&count, current);
      if (count > 0)
      {
         const D3D11_VIEWPORT full = {0.f, 0.f, float(target.x), float(target.y), 0.f, 1.f};
         const bool already_full = count == 1 && current[0].TopLeftX == full.TopLeftX && current[0].TopLeftY == full.TopLeftY &&
            current[0].Width == full.Width && current[0].Height == full.Height && current[0].MinDepth == full.MinDepth && current[0].MaxDepth == full.MaxDepth;
         if (!already_full)
         {
            state.scaling.game_viewport_count = count;
            std::copy_n(current, count, state.scaling.game_viewports.begin());
            context->RSSetViewports(1, &full);
            state.scaling.viewports_scaled = true;
            Scaling::CountViewportRescale();
         }
      }
   }
   if (Scaling::GetOptions().scale_scissors && !state.scaling.scissors_scaled)
   {
      UINT count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
      D3D11_RECT current[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
      context->RSGetScissorRects(&count, current);
      if (count > 0)
      {
         const D3D11_RECT full = {0, 0, LONG(target.x), LONG(target.y)};
         const bool already_full =
            count == 1 && current[0].left == full.left && current[0].top == full.top && current[0].right == full.right && current[0].bottom == full.bottom;
         if (!already_full)
         {
            state.scaling.game_scissor_count = count;
            std::copy_n(current, count, state.scaling.game_scissors.begin());
            context->RSSetScissorRects(1, &full);
            state.scaling.scissors_scaled = true;
            Scaling::CountScissorRescale();
         }
      }
   }
}

void RescaleBoundViewportsAndScissors(ID3D11DeviceContext* context, ContextChainState& state, Math::float2 scale)
{
   if (Scaling::GetOptions().scale_viewports && !state.scaling.viewports_scaled)
   {
      UINT count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
      D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
      context->RSGetViewports(&count, viewports);
      if (count > 0)
      {
         state.scaling.game_viewport_count = count;
         std::copy_n(viewports, count, state.scaling.game_viewports.begin());
         bool changed = false;
         for (UINT i = 0; i < count; i++)
         {
            if (viewports[i].Width == 0.f || viewports[i].Height == 0.f)
               continue;
            const Scaling::Viewport scaled =
               Scaling::RescaleViewport({viewports[i].TopLeftX, viewports[i].TopLeftY, viewports[i].Width, viewports[i].Height, viewports[i].MinDepth, viewports[i].MaxDepth}, scale);
            viewports[i] = {scaled.x, scaled.y, scaled.width, scaled.height, scaled.min_depth, scaled.max_depth};
            changed = true;
         }
         if (changed)
         {
            context->RSSetViewports(count, viewports);
            Scaling::CountViewportRescale();
         }
         state.scaling.viewports_scaled = true;
      }
   }
   if (Scaling::GetOptions().scale_scissors && !state.scaling.scissors_scaled)
   {
      UINT count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
      D3D11_RECT rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
      context->RSGetScissorRects(&count, rects);
      if (count > 0)
      {
         state.scaling.game_scissor_count = count;
         std::copy_n(rects, count, state.scaling.game_scissors.begin());
         bool changed = false;
         for (UINT i = 0; i < count; i++)
         {
            if (rects[i].left == rects[i].right && rects[i].top == rects[i].bottom)
               continue;
            const Scaling::Rect scaled = Scaling::RescaleRect({rects[i].left, rects[i].top, rects[i].right, rects[i].bottom}, scale);
            rects[i] = {scaled.left, scaled.top, scaled.right, scaled.bottom};
            changed = true;
         }
         if (changed)
         {
            context->RSSetScissorRects(count, rects);
            Scaling::CountScissorRescale();
         }
         state.scaling.scissors_scaled = true;
      }
   }
}

struct MappedMirrorEntry
{
   ID3D11Resource* resource = nullptr;
   UINT subresource = 0;
   D3D11_MAPPED_SUBRESOURCE mapped = {};
};

struct MappedMirrors
{
   std::vector<MappedMirrorEntry> entries;
};
} // namespace

void CreateMirror(ID3D11Device* device, const D3D11_TEXTURE1D_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture1D* texture)
{
   Create(device, desc, initial_data, texture);
}

void CreateMirror(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture)
{
   Create(device, desc, initial_data, texture);
}

void CreateMirror(ID3D11Device* device, const D3D11_TEXTURE3D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture3D* texture)
{
   Create(device, desc, initial_data, texture);
}

void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC1* desc, ID3D11ShaderResourceView* view)
{
   RegisterViewResource(resource, view);
   CreateMirroredView(device, resource, desc, view);
}

void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC1* desc, ID3D11RenderTargetView* view)
{
   RegisterViewResource(resource, view);
   CreateMirroredView(device, resource, desc, view);
}

void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* desc, ID3D11UnorderedAccessView* view)
{
   RegisterViewResource(resource, view);
   CreateMirroredView(device, resource, desc, view);
}

void CreateMirrorView(ID3D11Device* device, ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc, ID3D11DepthStencilView* view)
{
   RegisterViewResource(resource, view);
   CreateMirroredView(device, resource, desc, view);
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

void OnBindViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports)
{
   if (!Scaling::IsNeeded())
      return;
   ContextChainState* state = Hooks::Lifetime::GetOrCreateStateCached<ContextChainState>(context);
   if (!state)
      return;
   state->scaling.viewports_scaled = false;
   state->scaling.game_viewport_count = std::min<UINT>(count, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
   if (viewports && state->scaling.game_viewport_count > 0)
      std::copy_n(viewports, state->scaling.game_viewport_count, state->scaling.game_viewports.begin());
   if (count == 0 || Scaling::GetOptions().mode != Scaling::Mode::ViewportsOnly)
      return;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   if (Scaling::CheckCrossThreadOverlap(device.Get()))
   {
      state->scaling.cross_thread_overlap = true;
      return;
   }
   ApplyViewportsOnlyExpansion(context, *state, device.Get());
}

void OnBindScissorRects(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects)
{
   if (!Scaling::IsNeeded())
      return;
   ContextChainState* state = Hooks::Lifetime::GetOrCreateStateCached<ContextChainState>(context);
   if (!state)
      return;
   state->scaling.scissors_scaled = false;
   state->scaling.game_scissor_count = std::min<UINT>(count, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
   if (rects && state->scaling.game_scissor_count > 0)
      std::copy_n(rects, state->scaling.game_scissor_count, state->scaling.game_scissors.begin());
   if (count == 0 || Scaling::GetOptions().mode != Scaling::Mode::ViewportsOnly)
      return;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   if (Scaling::CheckCrossThreadOverlap(device.Get()))
   {
      state->scaling.cross_thread_overlap = true;
      return;
   }
   ApplyViewportsOnlyExpansion(context, *state, device.Get());
}

void PrepareDrawOrDispatch(ID3D11DeviceContext* context, bool is_dispatch)
{
   if (!IsSwapping())
      return;
   const bool chains_active = Chains::IsNeeded();
   const bool scaling_active = Scaling::IsNeeded() && Scaling::IsEnabled();
   if (!chains_active && !scaling_active)
      return;

   ContextChainState* state = Hooks::Lifetime::GetOrCreateStateCached<ContextChainState>(context);
   if (!state)
      return;
   state->scaling.chain_seed = false;
   state->scaling.chain_propagated = false;
   state->scaling.cross_thread_overlap = false;

   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   if (!device)
      return;

   // Identify active primary shader hash for seeds and declarative scaling activation
   uint32_t shader_hash = 0;
   if (const Shaders::Variants::D3D11::BoundShaders* bound = Shaders::Variants::D3D11::GetBoundShaders(context))
   {
      const ShaderStage stage = is_dispatch ? ShaderStage::Compute : ShaderStage::Pixel;
      if (bound->shaders[size_t(stage)])
         shader_hash = bound->shaders[size_t(stage)]->hash;
   }

   if (scaling_active && shader_hash != 0 && Scaling::ShouldDeactivateAt(shader_hash))
      Scaling::End(device.Get());

   if (scaling_active && Scaling::CheckCrossThreadOverlap(device.Get()))
      state->scaling.cross_thread_overlap = true;

   const Chains::Seed* seed = chains_active && shader_hash != 0 ? Chains::FindSeed(shader_hash) : nullptr;
   const bool indirect = chains_active && !seed && Chains::GetMode() >= Chains::Mode::DirectAndIndirect &&
      (is_dispatch ? (state->any_cs_srvs_upgraded || state->any_cs_uavs_upgraded) : (state->any_ps_srvs_upgraded || state->any_ps_uavs_upgraded));

   const bool is_immediate = context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
   const Upgrade::FrameState frame_state = GetFrameState(device.Get());
   const Upgrade::Rules& rules = Upgrade::GetRules();

   // Chain / seed mirror creation on output slots
   if (seed || indirect)
   {
      const ResourceMirrorState* source_mirror = nullptr;
      if (indirect)
      {
         const auto& srvs_flags = is_dispatch ? state->cs_srvs_upgraded : state->ps_srvs_upgraded;
         for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
         {
            if (!srvs_flags[i])
               continue;
            ComPtr<ID3D11ShaderResourceView> srv;
            if (is_dispatch)
               context->CSGetShaderResources(i, 1, &srv);
            else
               context->PSGetShaderResources(i, 1, &srv);
            if (!srv)
               continue;
            ComPtr<ID3D11Resource> res;
            srv->GetResource(&res);
            ID3D11Resource* orig = FindOriginal(res.Get());
            source_mirror = FindMirrorState<OwnedResource>(orig ? orig : res.Get());
            if (source_mirror)
               break;
         }
      }

      if (seed || source_mirror)
      {
         const bool can_scale_here = scaling_active && Scaling::IsActiveOnCurrentThread(device.Get()) &&
            (seed ? seed->scale
                  : (source_mirror &&
                       (source_mirror->is_scaled || (source_mirror->info.width == frame_state.output.x && source_mirror->info.height == frame_state.output.y))));
         const auto try_upgrade_resource = [&](ID3D11Resource* resource) -> bool {
            if (!resource || FindOriginal(resource) || FindMirrorState<OwnedResource>(resource))
               return false;
            const std::optional<Upgrade::TextureInfo> info = DescribeResource(resource);
            if (!info)
               return false;
            Upgrade::TextureInfo target = *info;
            if (seed)
            {
               const std::optional<Upgrade::TextureInfo> matched = Upgrade::Match(*info, frame_state, false, true, true);
               if (!matched)
                  return false;
               target = *matched;
            }
            else
            {
               if (!Chains::CanPropagate(source_mirror->info, *info, rules.aspect_ratio_pixel_threshold))
                  return false;
               target.format = source_mirror->info.format;
            }
            if (can_scale_here && Scaling::ShouldScaleTexture(target, frame_state, true, true, rules.aspect_ratio_pixel_threshold))
            {
               const Math::uint2 scaled = Scaling::ComputeScaledSize({target.width, target.height}, frame_state.render, frame_state.output);
               target.width = scaled.x;
               target.height = scaled.y;
            }
            const Chains::Origin origin = seed ? Chains::Origin{Chains::OriginKind::Seed, shader_hash} : Chains::Origin{Chains::OriginKind::IndirectChain, shader_hash};
            if (is_immediate)
            {
               if (MaterializeChainMirror(device.Get(), resource, *info, target, origin))
               {
                  if (seed)
                     state->scaling.chain_seed = true;
                  else
                  {
                     state->scaling.chain_propagated = true;
                     Chains::CountIndirectPropagation();
                  }
                  return true;
               }
            }
            else
            {
               QueueDeferredChainMirror(device.Get(), resource, *info, target, origin);
               if (seed)
                  state->scaling.chain_seed = true;
               else
                  state->scaling.chain_propagated = true;
            }
            return false;
         };

         if (!is_dispatch)
         {
            ComPtr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            ComPtr<ID3D11DepthStencilView> dsv;
            context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs[0].GetAddressOf(), &dsv);
            bool rebind = false;
            UINT max_rtv = 0;
            for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
            {
               if (!rtvs[i])
                  continue;
               max_rtv = i + 1;
               if (seed && std::ranges::find(seed->rtv_slots, uint8_t(i)) == seed->rtv_slots.end())
                  continue;
               ComPtr<ID3D11Resource> res;
               rtvs[i]->GetResource(&res);
               if (try_upgrade_resource(res.Get()))
               {
                  if (ID3D11RenderTargetView* mirror_rtv = FindMirrorView(rtvs[i].Get()))
                  {
                     rtvs[i] = mirror_rtv;
                     rebind = true;
                  }
               }
            }
            if (rebind)
            {
               ID3D11RenderTargetView* raw[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
               for (UINT i = 0; i < max_rtv; i++)
                  raw[i] = rtvs[i].Get();
               context->OMSetRenderTargets(max_rtv, raw, dsv.Get());
            }
         }
         else
         {
            ComPtr<ID3D11UnorderedAccessView> uavs[D3D11_PS_CS_UAV_REGISTER_COUNT];
            context->CSGetUnorderedAccessViews(0, D3D11_PS_CS_UAV_REGISTER_COUNT, uavs[0].GetAddressOf());
            bool rebind = false;
            for (UINT i = 0; i < D3D11_PS_CS_UAV_REGISTER_COUNT; i++)
            {
               if (!uavs[i])
                  continue;
               if (seed && std::ranges::find(seed->uav_slots, uint8_t(i)) == seed->uav_slots.end())
                  continue;
               ComPtr<ID3D11Resource> res;
               uavs[i]->GetResource(&res);
               if (try_upgrade_resource(res.Get()))
               {
                  if (ID3D11UnorderedAccessView* mirror_uav = FindMirrorView(uavs[i].Get()))
                  {
                     uavs[i] = mirror_uav;
                     rebind = true;
                  }
               }
            }
            if (rebind)
            {
               ID3D11UnorderedAccessView* raw[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};
               for (UINT i = 0; i < D3D11_PS_CS_UAV_REGISTER_COUNT; i++)
                  raw[i] = uavs[i].Get();
               context->CSSetUnorderedAccessViews(0, D3D11_PS_CS_UAV_REGISTER_COUNT, raw, nullptr);
            }
         }
      }
   }

   if (is_dispatch || !scaling_active)
      return;

   if (Scaling::GetOptions().mode == Scaling::Mode::ViewportsOnly)
   {
      ApplyViewportsOnlyExpansion(context, *state, device.Get());
      return;
   }

   // ResourcesAndViewports / custom_dimensions: rescale viewports/scissors if any bound RTV or DSV is a scaled mirror
   if (!HasAnyMirrors() || (state->scaling.viewports_scaled && state->scaling.scissors_scaled))
      return;
   ComPtr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
   ComPtr<ID3D11DepthStencilView> dsv;
   context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs[0].GetAddressOf(), &dsv);
   for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
   {
      if (!rtvs[i])
         continue;
      ComPtr<ID3D11Resource> res;
      rtvs[i]->GetResource(&res);
      ID3D11Resource* orig = FindOriginal(res.Get());
      if (const ResourceMirrorState* mirror = FindMirrorState<OwnedResource>(orig ? orig : res.Get()); mirror && mirror->is_scaled)
      {
         const Math::float2 scale = {
            float(mirror->info.width) / float(std::max(mirror->original_info.width, 1u)),
            float(mirror->info.height) / float(std::max(mirror->original_info.height, 1u)),
         };
         RescaleBoundViewportsAndScissors(context, *state, scale);
         return;
      }
   }
   if (dsv)
   {
      ComPtr<ID3D11Resource> res;
      dsv->GetResource(&res);
      ID3D11Resource* orig = FindOriginal(res.Get());
      if (const ResourceMirrorState* mirror = FindMirrorState<OwnedResource>(orig ? orig : res.Get()); mirror && mirror->is_scaled)
      {
         const Math::float2 scale = {
            float(mirror->info.width) / float(std::max(mirror->original_info.width, 1u)),
            float(mirror->info.height) / float(std::max(mirror->original_info.height, 1u)),
         };
         RescaleBoundViewportsAndScissors(context, *state, scale);
      }
   }
}

void FinishDrawOrDispatch(ID3D11DeviceContext* context, bool is_dispatch)
{
   if (!Scaling::IsNeeded() || !Scaling::IsEnabled())
      return;
   const Shaders::Variants::D3D11::BoundShaders* bound = Shaders::Variants::D3D11::GetBoundShaders(context);
   if (!bound)
      return;
   const ShaderStage stage = is_dispatch ? ShaderStage::Compute : ShaderStage::Pixel;
   if (!bound->shaders[size_t(stage)])
      return;
   if (Scaling::ShouldActivateAfter(bound->shaders[size_t(stage)]->hash))
   {
      ComPtr<ID3D11Device> device;
      context->GetDevice(&device);
      Scaling::Begin(device.Get());
   }
}

void ForgetContextState(ID3D11DeviceContext* context)
{
   if (ContextChainState* state = Hooks::Lifetime::GetState<ContextChainState>(context))
      *state = {};
}

void InvalidateScaledMirrors(ID3D11Device* device)
{
   DeviceMirrorState* dev = Hooks::Lifetime::GetState<DeviceMirrorState>(device);
   if (!dev)
      return;
   std::vector<ID3D11Resource*> originals;
   uint64_t frame = Shaders::GetFrame();
   {
      std::scoped_lock lock(dev->registry->mutex);
      dev->registry->scale_generation++;
      originals.assign(dev->registry->scaled_originals.begin(), dev->registry->scaled_originals.end());
      dev->registry->scaled_originals.clear();
   }
   if (originals.empty())
      return;
   Scaling::CountInvalidation();
   std::vector<RetiredMirror> newly_retired;
   for (ID3D11Resource* original : originals)
   {
      const ResourceMirrorState* state = Hooks::Lifetime::GetState<ResourceMirrorState>(original);
      if (!state || !state->is_scaled)
         continue;
      RetiredMirror entry = {.mirror = state->mirror, .retire_frame = frame};
      ComPtr<ID3D11Texture2D> tex;
      if (SUCCEEDED(state->mirror->QueryInterface(IID_PPV_ARGS(&tex))))
      {
         tex->GetDesc(&entry.desc);
         entry.poolable = true;
      }
      newly_retired.push_back(std::move(entry));
      Hooks::Lifetime::RemoveState<ResourceMirrorState>(original);
      Hooks::Lifetime::RemoveState<ScaledTracker>(original);
   }
   if (!newly_retired.empty())
   {
      std::scoped_lock lock(dev->registry->mutex);
      for (RetiredMirror& entry : newly_retired)
         dev->registry->retired.push_back(std::move(entry));
   }
}

void OnPresent(ID3D11Device* device)
{
   if (!device)
      return;
   const Frame::Resolutions res = Frame::Get(device);
   if (Scaling::IsNeeded() && Scaling::ConsumeResolutionChange(device, res.render, res.output))
      InvalidateScaledMirrors(device);
   Scaling::OnPresent(device);

   DeviceMirrorState* dev = Hooks::Lifetime::GetState<DeviceMirrorState>(device);
   if (!dev)
      return;

   // Materialize deferred-context chain mirrors at the frame boundary before the next frame records
   std::vector<PendingChainMirror> pending;
   {
      std::scoped_lock lock(dev->registry->mutex);
      pending.swap(dev->registry->pending_queue);
      dev->registry->pending_set.clear();
   }
   if (!pending.empty())
   {
      Chains::CountDeferredQueued(-int32_t(pending.size()));
      for (const PendingChainMirror& req : pending)
      {
         if (MaterializeChainMirror(device, req.original.Get(), req.original_info, req.mirror_info, req.origin))
         {
            if (req.origin.kind == Chains::OriginKind::DirectChain)
               Chains::CountDirectPropagation();
            else if (req.origin.kind == Chains::OriginKind::IndirectChain)
               Chains::CountIndirectPropagation();
         }
      }
   }

   // Advance retired mirrors into the recycle pool and evict expired pool entries
   const uint64_t frame = Shaders::GetFrame();
   std::vector<ComPtr<ID3D11Resource>> to_release;
   {
      std::scoped_lock lock(dev->registry->mutex);
      for (auto it = dev->registry->retired.begin(); it != dev->registry->retired.end();)
      {
         if (frame >= it->retire_frame + RETIRE_PRESENTS)
         {
            if (it->poolable && !dev->registry->released)
            {
               ComPtr<ID3D11Texture2D> tex;
               if (SUCCEEDED(it->mirror->QueryInterface(IID_PPV_ARGS(&tex))))
               {
                  dev->registry->pool.push_back({std::move(tex), it->desc, frame});
                  Scaling::CountPoolSize(1);
               }
            }
            else
            {
               to_release.push_back(std::move(it->mirror));
            }
            it = dev->registry->retired.erase(it);
         }
         else
         {
            ++it;
         }
      }
      for (auto it = dev->registry->pool.begin(); it != dev->registry->pool.end();)
      {
         if (frame >= it->stored_frame + POOL_TTL_FRAMES)
         {
            to_release.push_back(std::move(it->texture));
            it = dev->registry->pool.erase(it);
            Scaling::CountPoolSize(-1);
         }
         else
         {
            ++it;
         }
      }
   }
}

void ReleaseDeviceObjects(ID3D11Device* device)
{
   DeviceMirrorState* dev = Hooks::Lifetime::GetState<DeviceMirrorState>(device);
   if (!dev)
      return;
   std::vector<PendingChainMirror> pending;
   std::vector<RetiredMirror> retired;
   std::vector<PooledTexture> pool;
   {
      std::scoped_lock lock(dev->registry->mutex);
      dev->registry->released = true;
      dev->registry->scaled_originals.clear();
      dev->registry->pending_set.clear();
      pending.swap(dev->registry->pending_queue);
      retired.swap(dev->registry->retired);
      pool.swap(dev->registry->pool);
   }
   if (!pending.empty())
      Chains::CountDeferredQueued(-int32_t(pending.size()));
   if (!pool.empty())
      Scaling::CountPoolSize(-int32_t(pool.size()));
}

ID3D11Resource* FindMirror(ID3D11Resource* resource)
{
   return Mirrors::FindMirror<OwnedResource>(resource);
}

ID3D11Resource* FindOriginal(ID3D11Resource* mirror)
{
   return Mirrors::FindOriginal(mirror);
}

ID3D11Resource* GetEffectiveResource(ID3D11Resource* resource)
{
   if (!resource || !IsSwapping())
      return resource;
   if (ID3D11Resource* mirror = FindMirror(resource))
      return mirror;
   return resource;
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

void PrepareCopyTargets(ID3D11DeviceContext* context, ID3D11Resource*& destination, ID3D11Resource*& source)
{
   if (!IsSwapping())
      return;
   TryPropagateDirectChain(context, destination, source);
   destination = GetEffectiveResource(destination);
   source = GetEffectiveResource(source);
}

std::optional<MirrorSummary> FindMirrorSummary(ID3D11Resource* resource)
{
   if (!resource)
      return std::nullopt;
   if (const ResourceMirrorState* state = FindMirrorState<OwnedResource>(resource))
   {
      return MirrorSummary{
         .original_info = state->original_info,
         .mirror_info = state->info,
         .origin = state->origin,
         .is_scaled = state->is_scaled,
         .pending = false,
      };
   }
   ComPtr<ID3D11Device> device;
   resource->GetDevice(&device);
   if (IsPendingMirror(device.Get(), resource))
   {
      if (const std::optional<Upgrade::TextureInfo> info = DescribeResource(resource))
         return MirrorSummary{.original_info = *info, .mirror_info = *info, .pending = true};
   }
   return std::nullopt;
}

std::optional<ContextScalingSnapshot> GetContextScalingSnapshot(ID3D11DeviceContext* context)
{
   if (const ContextChainState* state = Hooks::Lifetime::GetState<ContextChainState>(context))
      return state->scaling;
   return std::nullopt;
}

bool RedirectCopy(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source, bool* out_scaled)
{
   if (!IsSwapping())
      return false;
   TryPropagateDirectChain(context, destination, source);
   const CopyPlan<ID3D11Resource> plan = PlanCopy(destination, source, &FindMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return false;
   const bool same_size = SameDimensions2D(plan.destination, plan.source);
   if (plan.action == CopyAction::DirectMirrorCopy && same_size)
   {
      context->CopyResource(plan.destination, plan.source);
      return true;
   }
   if (ConvertingCopy(context, plan.destination, plan.source, out_scaled))
      return true;
   ASSERT_ONCE_MSG(false, "A copy between a mirror and a texture of another format couldn't be converted: the game's textures are copied, the mirror is stale");
   return false;
}

bool RedirectCopyRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z, ID3D11Resource* source,
   UINT source_subresource, const D3D11_BOX* box, UINT copy_flags, bool* out_scaled, std::optional<D3D11_BOX>* out_rescaled_box)
{
   if (!IsSwapping())
      return false;
   TryPropagateDirectChain(context, destination, source);
   const CopyPlan<ID3D11Resource> plan = PlanCopy(destination, source, &FindMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return false;
   const bool same_size = SameDimensions2D(plan.destination, plan.source);
   if (plan.action == CopyAction::DirectMirrorCopy && same_size)
   {
      D3D11_BOX scaled_box = {};
      const D3D11_BOX* use_box = box;
      const ResourceMirrorState* src_mirror = FindMirrorState<OwnedResource>(source);
      const ResourceMirrorState* dst_mirror = FindMirrorState<OwnedResource>(destination);
      if (src_mirror && src_mirror->is_scaled && box)
      {
         const Math::float2 scale = {
            float(src_mirror->info.width) / float(std::max(src_mirror->original_info.width, 1u)),
            float(src_mirror->info.height) / float(std::max(src_mirror->original_info.height, 1u)),
         };
         const Scaling::Box rescaled = Scaling::RescaleBox({box->left, box->top, box->front, box->right, box->bottom, box->back}, scale);
         scaled_box = {rescaled.left, rescaled.top, rescaled.front, rescaled.right, rescaled.bottom, rescaled.back};
         use_box = &scaled_box;
         Scaling::CountCopyBoxRescale();
         if (out_scaled)
            *out_scaled = true;
         if (out_rescaled_box)
            *out_rescaled_box = scaled_box;
      }
      if (dst_mirror && dst_mirror->is_scaled && (x != 0 || y != 0))
      {
         const Math::float2 scale = {
            float(dst_mirror->info.width) / float(std::max(dst_mirror->original_info.width, 1u)),
            float(dst_mirror->info.height) / float(std::max(dst_mirror->original_info.height, 1u)),
         };
         x = UINT(std::lround(double(x) * double(scale.x)));
         y = UINT(std::lround(double(y) * double(scale.y)));
         if (out_scaled)
            *out_scaled = true;
      }
      ComPtr<ID3D11DeviceContext1> context1;
      if (copy_flags && SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1))))
         context1->CopySubresourceRegion1(plan.destination, destination_subresource, x, y, z, plan.source, source_subresource, use_box, copy_flags);
      else
         context->CopySubresourceRegion(plan.destination, destination_subresource, x, y, z, plan.source, source_subresource, use_box);
      return true;
   }
   if (ConvertingCopySubresourceRegion(context, plan.destination, destination_subresource, x, y, z, plan.source, source_subresource, box, out_scaled))
      return true;
   ASSERT_ONCE_MSG(false, "A region copy between a mirror and a texture of another format couldn't be converted: the game's textures are copied, the mirror is stale");
   return false;
}

bool RedirectResolve(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source, UINT source_subresource,
   DXGI_FORMAT format)
{
   if (!IsSwapping())
      return false;
   TryPropagateDirectChain(context, destination, source);
   const CopyPlan<ID3D11Resource> plan = PlanCopy(destination, source, &FindMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return false;
   const DXGI_FORMAT destination_format = GetFormat(plan.destination);
   const DXGI_FORMAT source_format = GetFormat(plan.source);
   // ResolveSubresource requires source and destination to share the same typeless format family (stricter than CopyResource,
   // which allows cross-family copies of equal-bit-size formats). When a mirror crosses families, resolve into a temporary
   // single-sample texture of the source's family and blit-convert into the destination.
   if (GetTypeless(destination_format) != GetTypeless(source_format) || destination_format == DXGI_FORMAT_UNKNOWN)
   {
      const DXGI_FORMAT resolve_format = GetResolveFormat(format, GetFormat(source), source_format);
      if (ConvertingCopySubresourceRegion(context, plan.destination, destination_subresource, 0, 0, 0, plan.source, source_subresource, nullptr, nullptr,
             resolve_format))
         return true;
      unhandled_resolves++;
      ASSERT_ONCE_MSG(false, "Resolving between a mirror and a texture of another format failed to convert: the game's textures are resolved");
      return false;
   }
   const DXGI_FORMAT resolve_format = GetResolveFormat(format, GetFormat(source), destination_format);
   context->ResolveSubresource(plan.destination, destination_subresource, plan.source, source_subresource, resolve_format);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4])
{
   if (!IsSwapping())
      return false;
   ID3D11RenderTargetView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearRenderTargetView(mirror, color);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil)
{
   if (!IsSwapping())
      return false;
   ID3D11DepthStencilView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearDepthStencilView(mirror, flags, depth, stencil);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const UINT values[4])
{
   if (!IsSwapping())
      return false;
   ID3D11UnorderedAccessView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearUnorderedAccessViewUint(mirror, values);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const FLOAT values[4])
{
   if (!IsSwapping())
      return false;
   ID3D11UnorderedAccessView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearUnorderedAccessViewFloat(mirror, values);
   return true;
}

bool RedirectClearView(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count)
{
   if (!IsSwapping())
      return false;
   ID3D11View* mirror = FindMirrorView(view);
   ComPtr<ID3D11DeviceContext1> context1;
   if (!mirror || FAILED(context->QueryInterface(IID_PPV_ARGS(&context1))))
      return false;
   std::vector<D3D11_RECT> scaled_rects;
   const D3D11_RECT* use_rects = rects;
   if (rects && rect_count > 0)
   {
      ID3D11Resource* res = GetViewResource(view);
      if (const ResourceMirrorState* state = FindMirrorState<OwnedResource>(res); state && state->is_scaled)
      {
         const Math::float2 scale = {
            float(state->info.width) / float(std::max(state->original_info.width, 1u)),
            float(state->info.height) / float(std::max(state->original_info.height, 1u)),
         };
         scaled_rects.reserve(rect_count);
         for (UINT i = 0; i < rect_count; i++)
         {
            const Scaling::Rect r = Scaling::RescaleRect({rects[i].left, rects[i].top, rects[i].right, rects[i].bottom}, scale);
            scaled_rects.push_back({r.left, r.top, r.right, r.bottom});
         }
         use_rects = scaled_rects.data();
         Scaling::CountClearRectRescale();
      }
   }
   context1->ClearView(mirror, color, use_rects, rect_count);
   return true;
}

void WriteToMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT row_pitch, UINT depth_pitch)
{
   const ResourceMirrorState* state = FindMirrorState<OwnedResource>(resource);
   if (!state || !data)
      return;
   const Upgrade::TextureInfo& desc = state->original_info;
   const uint32_t level = subresource % std::max(desc.levels, 1u);
   const bool volume = desc.dimension == Upgrade::Dimension::Texture3D;
   const uint32_t width = box ? box->right - box->left : MipSize(desc.width, level);
   const uint32_t height = box ? box->bottom - box->top : MipSize(desc.height, level);
   const uint32_t depth = box ? box->back - box->front : volume ? MipSize(desc.depth_or_layers, level)
                                                                : 1;
   std::optional<Converted> converted;
   if (state->info.format == DXGI_FORMAT_R16G16B16A16_FLOAT)
      converted = ConvertToRgba16Float(data, row_pitch, depth_pitch, desc.format, width, height, depth);
   if (!converted)
   {
      ASSERT_ONCE_MSG(false, "CPU data written to a mirrored texture can't be converted to its mirror's format: the mirror is stale");
      return;
   }
   context->UpdateSubresource(state->mirror.Get(), subresource, box, converted->bytes.data(), converted->row_pitch, converted->depth_pitch);
}

void TrackMappedMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, const D3D11_MAPPED_SUBRESOURCE& mapped)
{
   if (!FindMirrorState<OwnedResource>(resource))
      return;
   if (type == D3D11_MAP_READ || type == D3D11_MAP_READ_WRITE)
      ASSERT_ONCE_MSG(false, "Game mapped a mirrored texture for CPU read: mirror contents are not read back");
   if (type == D3D11_MAP_READ || !mapped.pData)
      return;
   if (MappedMirrors* state = Hooks::Lifetime::GetOrCreateStateCached<MappedMirrors>(context))
   {
      for (MappedMirrorEntry& entry : state->entries)
      {
         if (entry.resource == resource && entry.subresource == subresource)
         {
            entry.mapped = mapped;
            return;
         }
      }
      state->entries.push_back({resource, subresource, mapped});
   }
}

// Must run while the game's Map() pointer (mapped.pData) is still valid, before the native Unmap() invalidates it.
void CommitMappedMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   MappedMirrors* state = Hooks::Lifetime::GetState<MappedMirrors>(context);
   if (!state || state->entries.empty())
      return;
   for (size_t i = 0; i < state->entries.size(); i++)
   {
      if (state->entries[i].resource == resource && state->entries[i].subresource == subresource)
      {
         const D3D11_MAPPED_SUBRESOURCE mapped = state->entries[i].mapped;
         state->entries.erase(state->entries.begin() + ptrdiff_t(i));
         WriteToMirror(context, resource, subresource, nullptr, mapped.pData, mapped.RowPitch, mapped.DepthPitch);
         return;
      }
   }
}

// Discards a tracked mapping without reading mapped.pData (used when a game event took ownership of Unmap).
void DiscardMappedMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   MappedMirrors* state = Hooks::Lifetime::GetState<MappedMirrors>(context);
   if (!state || state->entries.empty())
      return;
   for (size_t i = 0; i < state->entries.size(); i++)
   {
      if (state->entries[i].resource == resource && state->entries[i].subresource == subresource)
      {
         state->entries.erase(state->entries.begin() + ptrdiff_t(i));
         return;
      }
   }
}

uint32_t GetUnhandledResolves()
{
   return unhandled_resolves.load();
}
} // namespace D3D11
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
