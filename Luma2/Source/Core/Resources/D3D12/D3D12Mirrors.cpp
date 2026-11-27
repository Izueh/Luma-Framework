#include "Resources/D3D12/D3D12Mirrors.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "Frame/FrameState.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Log/Log.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Resources/D3D12/D3D12HeapTail.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Shaders/ShaderStore.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"
#include "Shaders/Shaders.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Format.hpp"
#include "Utils/Hash.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D12 {
namespace {
// Luma's CPU heaps grow by this many descriptors
constexpr uint32_t POOL_CHUNK = 4096;
constexpr uint64_t RETIRE_PRESENTS = 3;
constexpr uint64_t POOL_TTL_FRAMES = 120;

std::atomic<uint32_t> entry_count = 0;
std::atomic<uint32_t> sampler_count = 0;
std::atomic<uint32_t> swapped_copies = 0;
std::atomic<uint32_t> direct_writes = 0;
std::atomic<uint32_t> converted_copies = 0;
std::atomic<uint32_t> unhandled_copies = 0;
std::atomic<uint32_t> in_place_converted_copies = 0;
std::atomic<uint32_t> in_place_unhandled_copies = 0;
std::atomic<uint32_t> in_place_unasked_placed = 0;
std::atomic<uint32_t> in_place_unfit_placed = 0;
std::atomic<uint32_t> uav_clears = 0;
std::atomic<uint32_t> unhandled_clears = 0;
std::atomic<uint32_t> layout_resources = 0;
std::atomic<uint32_t> reserved_resources = 0;
std::atomic<uint32_t> aliasing_barriers = 0;
std::array<std::atomic<uint32_t>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> increments = {};

using OwnedResource = ComPtr<ID3D12Resource>;
using ResourceMirrorState = MirrorState<ID3D12Resource, OwnedResource>;

struct Pool
{
   std::vector<ComPtr<ID3D12DescriptorHeap>> heaps;
   std::vector<uint64_t> free;
   uint32_t used_in_last = POOL_CHUNK;
   uint32_t increment = 0;
};

struct Pools
{
   std::mutex mutex; // After handles_mutex
   std::array<Pool, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> pools;
   bool released = false;
};

struct PendingChainMirror
{
   ComPtr<ID3D12Resource> original;
   Upgrade::TextureInfo original_info;
   Upgrade::TextureInfo mirror_info;
   Chains::Origin origin = {};
};

struct RetiredD3D12Mirror
{
   ComPtr<ID3D12Resource> mirror;
   D3D12_RESOURCE_DESC desc = {};
   D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
   uint64_t retire_frame = 0;
   bool poolable = false;
};

struct PooledD3D12Texture
{
   ComPtr<ID3D12Resource> resource;
   D3D12_RESOURCE_DESC desc = {};
   D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
   uint64_t stored_frame = 0;
};

// Per-device registry for scaled mirrors, deferred chain queue and recycle pool. Never held with handles_mutex.
struct DeviceRegistry
{
   std::mutex mutex;
   uint64_t scale_generation = 0;
   std::unordered_set<ID3D12Resource*> scaled_originals;
   std::unordered_set<const ID3D12Resource*> pending_set;
   std::vector<PendingChainMirror> pending_queue;
   std::vector<RetiredD3D12Mirror> retired;
   std::vector<PooledD3D12Texture> pool;
   bool released = false;
};

struct DeviceState
{
   std::shared_ptr<Pools> pools = std::make_shared<Pools>();
   std::shared_ptr<DeviceRegistry> registry = std::make_shared<DeviceRegistry>();
};

DeviceState* GetOrCreateDeviceState(ID3D12Device* device)
{
   if (!device)
      return nullptr;
   if (DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device))
      return state;
   return Hooks::Lifetime::SetState<DeviceState>(device);
}

struct ScaledTracker
{
   std::shared_ptr<DeviceRegistry> registry;
   ID3D12Resource* original = nullptr;

   ~ScaledTracker()
   {
      if (!registry || !original)
         return;
      std::scoped_lock lock(registry->mutex);
      registry->scaled_originals.erase(original);
   }
};

// Lightweight committed state tracked per resource when Chains::IsNeeded()
struct TrackedResourceState
{
   std::atomic<uint32_t> committed_state = uint32_t(D3D12_RESOURCE_STATE_COMMON);
   bool has_clear = false;
   D3D12_CLEAR_VALUE clear = {};
   bool reserved = false;
   bool enhanced_barrier = false;
};

struct Slot
{
   std::shared_ptr<Pools> pools;
   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   uint64_t handle = 0;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   bool sampler = false;
   bool counted = false;
   ID3D12Resource* original_resource = nullptr; // Non-owning: used to check scaling ratio on clears/RTVs
   D3D12_SAMPLER_DESC game_sampler = {};
   uint32_t generation = 0;
   ID3D12Device* device = nullptr;

   ~Slot()
   {
      std::scoped_lock lock(pools->mutex);
      if (!pools->released)
         pools->pools[type].free.push_back(handle);
      if (sampler)
         sampler_count--;
      else if (counted)
         CountView(-1);
   }
};

// Game CPU handles (not shader-visible) to the mirror's descriptor they stand for
std::shared_mutex handles_mutex; // Before pools->mutex
std::map<uint64_t, std::shared_ptr<Slot>> handles;
std::unordered_map<uint64_t, uint64_t> mirror_to_original_handle;

// Tracked CPU views of unmirrored 2D textures (only populated when Chains::IsNeeded()), for backfill on lazy mirror creation
struct TrackedUnmirroredView
{
   uint64_t handle = 0;
   ViewKind kind = ViewKind::ShaderResource;
   ID3D12Resource* resource = nullptr;
   bool has_desc = false;

   union
   {
      D3D12_SHADER_RESOURCE_VIEW_DESC srv;
      D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
      D3D12_RENDER_TARGET_VIEW_DESC rtv;
      D3D12_DEPTH_STENCIL_VIEW_DESC dsv;
   } desc = {};
};

std::mutex unmirrored_views_mutex; // Never held with handles_mutex
std::map<uint64_t, TrackedUnmirroredView> unmirrored_by_handle;
std::unordered_map<ID3D12Resource*, std::vector<uint64_t>> unmirrored_by_resource;

struct UnmirroredViewCleanup
{
   ID3D12Resource* resource = nullptr;

   ~UnmirroredViewCleanup()
   {
      if (!resource)
         return;
      std::scoped_lock lock(unmirrored_views_mutex);
      const auto it = unmirrored_by_resource.find(resource);
      if (it == unmirrored_by_resource.end())
         return;
      for (uint64_t handle : it->second)
         unmirrored_by_handle.erase(handle);
      unmirrored_by_resource.erase(it);
   }
};

struct HeapRange
{
   uint64_t start = 0;
   uint64_t end = 0;
   bool shader_visible = false;
};

void EraseInPlaceTargets(SIZE_T start, SIZE_T end);

std::shared_mutex heaps_mutex; // Never held with another
std::vector<HeapRange> heaps; // By start

struct HeapState
{
   HeapRange range;

   ~HeapState()
   {
      {
         std::scoped_lock lock(heaps_mutex);
         std::erase_if(heaps, [this](const HeapRange& item) { return item.start == range.start && item.end == range.end; });
      }
      if (!range.shader_visible)
      {
         {
            std::unique_lock lock(handles_mutex);
            const auto first = handles.lower_bound(range.start);
            const auto last = handles.lower_bound(range.end);
            entry_count -= uint32_t(std::distance(first, last));
            handles.erase(first, last);
         }
         EraseInPlaceTargets(SIZE_T(range.start), SIZE_T(range.end));
         if (Chains::IsNeeded())
         {
            std::scoped_lock lock(unmirrored_views_mutex);
            for (auto it = unmirrored_by_handle.lower_bound(range.start); it != unmirrored_by_handle.end() && it->first < range.end;)
            {
               if (auto res_it = unmirrored_by_resource.find(it->second.resource); res_it != unmirrored_by_resource.end())
                  std::erase(res_it->second, it->first);
               it = unmirrored_by_handle.erase(it);
            }
         }
      }
   }
};

uint32_t GetIncrement(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   if (uint32_t cached = increments[type].load(std::memory_order_relaxed))
      return cached;
   const uint32_t size = device->GetDescriptorHandleIncrementSize(type);
   increments[type].store(size, std::memory_order_relaxed);
   return size;
}

bool IsShaderVisible(uint64_t handle)
{
   std::shared_lock lock(heaps_mutex);
   const auto it = std::ranges::upper_bound(heaps, handle, {}, &HeapRange::start);
   if (it == heaps.begin())
      return false;
   const HeapRange& range = *std::prev(it);
   return handle < range.end && range.shader_visible;
}

std::shared_ptr<Slot> Allocate(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   DeviceState* state = GetOrCreateDeviceState(device);
   if (!state)
      return nullptr;
   auto slot = std::make_shared<Slot>();
   slot->pools = state->pools;
   slot->type = type;
   slot->device = device;
   std::scoped_lock lock(state->pools->mutex);
   if (state->pools->released)
      return nullptr;
   Pool& pool = state->pools->pools[type];
   if (!pool.free.empty())
   {
      slot->handle = pool.free.back();
      pool.free.pop_back();
      return slot;
   }
   if (pool.used_in_last == POOL_CHUNK)
   {
      const D3D12_DESCRIPTOR_HEAP_DESC desc = {type, POOL_CHUNK, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
      ComPtr<ID3D12DescriptorHeap> heap;
      Hooks::Scope::Guard scope;
      if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap))))
         return nullptr;
      pool.heaps.push_back(std::move(heap));
      pool.used_in_last = 0;
      pool.increment = GetIncrement(device, type);
   }
   slot->handle = pool.heaps.back()->GetCPUDescriptorHandleForHeapStart().ptr + uint64_t(pool.used_in_last++) * pool.increment;
   return slot;
}

Upgrade::TextureInfo Describe(const D3D12_RESOURCE_DESC& desc, bool gpu_only)
{
   Upgrade::TextureInfo described = {
      .width = uint32_t(desc.Width),
      .height = desc.Height,
      .depth_or_layers = desc.DepthOrArraySize,
      .levels = desc.MipLevels,
      .samples = desc.SampleDesc.Count,
      .format = desc.Format,
      .render_target = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0,
      .unordered_access = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0,
      .depth_stencil = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0,
      .shader_resource = (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0,
      .gpu_only = gpu_only,
   };
   switch (desc.Dimension)
   {
   case D3D12_RESOURCE_DIMENSION_TEXTURE1D: described.dimension = Upgrade::Dimension::Texture1D; break;
   case D3D12_RESOURCE_DIMENSION_TEXTURE3D: described.dimension = Upgrade::Dimension::Texture3D; break;
   default: described.dimension = Upgrade::Dimension::Texture2D; break;
   }
   return described;
}

Upgrade::FrameState GetFrameState(ID3D12Device* device)
{
   const Frame::Resolutions resolutions = Frame::Read(Frame::Find(device));
   return {.render = resolutions.render, .output = resolutions.output, .display = resolutions.display};
}

Upgrade::ViewKind ToUpgradeKind(ViewKind kind)
{
   switch (kind)
   {
   case ViewKind::RenderTarget: return Upgrade::ViewKind::RenderTarget;
   case ViewKind::UnorderedAccess: return Upgrade::ViewKind::UnorderedAccess;
   case ViewKind::DepthStencil: return Upgrade::ViewKind::DepthStencil;
   default: return Upgrade::ViewKind::ShaderResource;
   }
}

template <typename Desc>
Desc MapFormat(const void* desc, ViewKind kind, const ResourceMirrorState& state, DXGI_FORMAT& format)
{
   Desc mapped = *static_cast<const Desc*>(desc);
   const DXGI_FORMAT best = Upgrade::GetBestViewFormat(mapped.Format, ToUpgradeKind(kind), state.original_info.format, state.info.format);
   if (best != DXGI_FORMAT_UNKNOWN)
      mapped.Format = best;
   format = mapped.Format;
   return mapped;
}

DXGI_FORMAT WriteMirrorView(ID3D12Device* device, ID3D12Resource* mirror, ViewKind kind, const void* desc, const ResourceMirrorState& state, uint64_t handle)
{
   DXGI_FORMAT format = state.info.format;
   const D3D12_CPU_DESCRIPTOR_HANDLE destination = {SIZE_T(handle)};
   Hooks::Scope::Guard scope;
   switch (kind)
   {
   case ViewKind::ShaderResource:
   {
      const auto mapped = desc ? std::optional(MapFormat<D3D12_SHADER_RESOURCE_VIEW_DESC>(desc, kind, state, format)) : std::nullopt;
      device->CreateShaderResourceView(mirror, mapped ? &*mapped : nullptr, destination);
      break;
   }
   case ViewKind::UnorderedAccess:
   {
      const auto mapped = desc ? std::optional(MapFormat<D3D12_UNORDERED_ACCESS_VIEW_DESC>(desc, kind, state, format)) : std::nullopt;
      device->CreateUnorderedAccessView(mirror, nullptr, mapped ? &*mapped : nullptr, destination);
      break;
   }
   case ViewKind::RenderTarget:
   {
      const auto mapped = desc ? std::optional(MapFormat<D3D12_RENDER_TARGET_VIEW_DESC>(desc, kind, state, format)) : std::nullopt;
      device->CreateRenderTargetView(mirror, mapped ? &*mapped : nullptr, destination);
      break;
   }
   case ViewKind::DepthStencil:
   {
      const auto mapped = desc ? std::optional(MapFormat<D3D12_DEPTH_STENCIL_VIEW_DESC>(desc, kind, state, format)) : std::nullopt;
      device->CreateDepthStencilView(mirror, mapped ? &*mapped : nullptr, destination);
      break;
   }
   default: return DXGI_FORMAT_UNKNOWN;
   }
   return format;
}

D3D12_DESCRIPTOR_HEAP_TYPE GetHeapType(ViewKind kind)
{
   switch (kind)
   {
   case ViewKind::RenderTarget: return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
   case ViewKind::DepthStencil: return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
   default: return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   }
}

void RefreshSampler(Slot& slot)
{
   const uint32_t generation = Samplers::GetGeneration(slot.device);
   if (slot.generation == generation)
      return;
   slot.generation = generation;
   const D3D12_SAMPLER_DESC mirror = Samplers::GetMirrorDesc(slot.game_sampler, Samplers::GetLodBiasOffset(slot.device)).value_or(slot.game_sampler);
   Hooks::Scope::Guard scope;
   slot.device->CreateSampler(&mirror, {SIZE_T(slot.handle)});
}

void Forget(uint64_t handle)
{
   if (entry_count.load(std::memory_order_relaxed) == 0)
      return;
   std::unique_lock lock(handles_mutex);
   if (const auto it = handles.find(handle); it != handles.end())
   {
      if (it->second)
         mirror_to_original_handle.erase(it->second->handle);
      handles.erase(it);
      entry_count--;
   }
}

void Remember(uint64_t handle, std::shared_ptr<Slot> slot)
{
   std::unique_lock lock(handles_mutex);
   const uint64_t mirror_handle = slot ? slot->handle : 0;
   if (const auto it = handles.find(handle); it != handles.end() && it->second)
      mirror_to_original_handle.erase(it->second->handle);
   auto [it, added] = handles.insert_or_assign(handle, std::move(slot));
   if (mirror_handle)
      mirror_to_original_handle[mirror_handle] = handle;
   entry_count += added;
}

std::shared_ptr<Slot> Find(uint64_t handle)
{
   if (entry_count.load(std::memory_order_relaxed) == 0)
      return nullptr;
   std::shared_lock lock(handles_mutex);
   const auto it = handles.find(handle);
   return it != handles.end() ? it->second : nullptr;
}

void RememberUnmirroredView(uint64_t handle, ViewKind kind, ID3D12Resource* resource, const void* desc)
{
   if (!Chains::IsNeeded() || !resource || kind == ViewKind::ConstantBuffer || IsShaderVisible(handle))
      return;
   if (resource->GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
      return;
   TrackedUnmirroredView entry = {.handle = handle, .kind = kind, .resource = resource, .has_desc = desc != nullptr};
   if (desc)
   {
      switch (kind)
      {
      case ViewKind::ShaderResource: entry.desc.srv = *static_cast<const D3D12_SHADER_RESOURCE_VIEW_DESC*>(desc); break;
      case ViewKind::UnorderedAccess: entry.desc.uav = *static_cast<const D3D12_UNORDERED_ACCESS_VIEW_DESC*>(desc); break;
      case ViewKind::RenderTarget: entry.desc.rtv = *static_cast<const D3D12_RENDER_TARGET_VIEW_DESC*>(desc); break;
      case ViewKind::DepthStencil: entry.desc.dsv = *static_cast<const D3D12_DEPTH_STENCIL_VIEW_DESC*>(desc); break;
      default: break;
      }
   }
   std::scoped_lock lock(unmirrored_views_mutex);
   if (const auto old = unmirrored_by_handle.find(handle); old != unmirrored_by_handle.end() && old->second.resource != resource)
   {
      if (auto res_it = unmirrored_by_resource.find(old->second.resource); res_it != unmirrored_by_resource.end())
         std::erase(res_it->second, handle);
   }
   unmirrored_by_handle[handle] = entry;
   auto& list = unmirrored_by_resource[resource];
   if (std::ranges::find(list, handle) == list.end())
      list.push_back(handle);
   if (!Hooks::Lifetime::GetState<UnmirroredViewCleanup>(resource))
      Hooks::Lifetime::SetState<UnmirroredViewCleanup>(resource, resource);
}

void ForgetUnmirroredView(uint64_t handle)
{
   if (!Chains::IsNeeded())
      return;
   std::scoped_lock lock(unmirrored_views_mutex);
   const auto it = unmirrored_by_handle.find(handle);
   if (it == unmirrored_by_handle.end())
      return;
   if (auto res_it = unmirrored_by_resource.find(it->second.resource); res_it != unmirrored_by_resource.end())
      std::erase(res_it->second, handle);
   unmirrored_by_handle.erase(it);
}

void BackfillMirrorViews(ID3D12Device* device, ID3D12Resource* resource, const ResourceMirrorState& state)
{
   std::vector<TrackedUnmirroredView> views;
   {
      std::scoped_lock lock(unmirrored_views_mutex);
      const auto res_it = unmirrored_by_resource.find(resource);
      if (res_it == unmirrored_by_resource.end())
         return;
      for (uint64_t handle : res_it->second)
      {
         if (const auto it = unmirrored_by_handle.find(handle); it != unmirrored_by_handle.end())
            views.push_back(it->second);
      }
   }
   for (const TrackedUnmirroredView& view : views)
   {
      std::shared_ptr<Slot> slot = Allocate(device, GetHeapType(view.kind));
      if (!slot)
         continue;
      const void* desc = view.has_desc ? static_cast<const void*>(&view.desc) : nullptr;
      slot->format = WriteMirrorView(device, state.mirror.Get(), view.kind, desc, state, slot->handle);
      slot->counted = true;
      slot->original_resource = resource;
      CountView(1);
      Remember(view.handle, std::move(slot));
   }
}

bool DescMatches(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b)
{
   return a.Dimension == b.Dimension && a.Width == b.Width && a.Height == b.Height && a.DepthOrArraySize == b.DepthOrArraySize && a.MipLevels == b.MipLevels &&
      a.Format == b.Format && a.SampleDesc.Count == b.SampleDesc.Count && a.SampleDesc.Quality == b.SampleDesc.Quality && a.Flags == b.Flags;
}

ComPtr<ID3D12Resource> AcquireFromPool(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state)
{
   DeviceState* dev = GetOrCreateDeviceState(device);
   if (!dev)
      return nullptr;
   std::scoped_lock lock(dev->registry->mutex);
   for (size_t i = 0; i < dev->registry->pool.size(); i++)
   {
      if (DescMatches(dev->registry->pool[i].desc, desc) && dev->registry->pool[i].state == state)
      {
         ComPtr<ID3D12Resource> res = std::move(dev->registry->pool[i].resource);
         dev->registry->pool.erase(dev->registry->pool.begin() + ptrdiff_t(i));
         Scaling::CountPoolSize(-1);
         return res;
      }
   }
   return nullptr;
}

bool MaterializeMirror(ID3D12Device* device, ID3D12Resource* resource, D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE* clear,
   const Upgrade::TextureInfo& described, const Upgrade::TextureInfo& mirrored, Chains::Origin origin)
{
   if (!device || !resource || FindMirrorState<OwnedResource>(resource))
      return FindMirrorState<OwnedResource>(resource) != nullptr;
   D3D12_RESOURCE_DESC mirror_desc = resource->GetDesc();
   mirror_desc.Format = mirrored.format;
   mirror_desc.Width = mirrored.width;
   mirror_desc.Height = mirrored.height;
   mirror_desc.DepthOrArraySize = UINT16(mirrored.depth_or_layers);
   mirror_desc.MipLevels = UINT16(mirrored.levels);
   mirror_desc.SampleDesc.Count = mirrored.samples;
   if (mirror_desc.SampleDesc.Count == 1)
      mirror_desc.SampleDesc.Quality = 0;
   mirror_desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

   const bool scaled = mirrored.width != described.width || mirrored.height != described.height;
   ComPtr<ID3D12Resource> mirror;
   if (scaled)
      mirror = AcquireFromPool(device, mirror_desc, initial_state);
   if (!mirror)
   {
      std::optional<D3D12_CLEAR_VALUE> mirror_clear;
      if (clear && !mirrored.depth_stencil)
      {
         mirror_clear = *clear;
         mirror_clear->Format = Utils::Format::DXGI::IsTypeless(mirrored.format) ? Utils::Format::DXGI::GetDefaultTyped(mirrored.format) : mirrored.format;
      }
      const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
      HRESULT result = E_FAIL;
      {
         Hooks::Scope::Guard scope;
         result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &mirror_desc, initial_state, mirror_clear ? &*mirror_clear : nullptr, IID_PPV_ARGS(&mirror));
      }
      if (FAILED(result) || !mirror)
      {
         LogCreationFailure(std::format("a {} mirror of a {} texture", Utils::Format::GetName(mirrored.format), Utils::Format::GetName(described.format)), result);
         return false;
      }
   }
   uint64_t generation = 0;
   if (DeviceState* dev = GetOrCreateDeviceState(device); dev && scaled)
   {
      std::scoped_lock lock(dev->registry->mutex);
      generation = dev->registry->scale_generation;
      dev->registry->scaled_originals.insert(resource);
      Hooks::Lifetime::SetState<ScaledTracker>(resource, dev->registry, resource);
   }
   AttachMirror(resource, std::move(mirror), described, mirrored, origin, generation);
   if (const ResourceMirrorState* attached = FindMirrorState<OwnedResource>(resource))
      BackfillMirrorViews(device, resource, *attached);
   return true;
}

void QueueChainMirror(ID3D12Device* device, ID3D12Resource* resource, const Upgrade::TextureInfo& original_info, const Upgrade::TextureInfo& mirror_info,
   Chains::Origin origin)
{
   DeviceState* dev = GetOrCreateDeviceState(device);
   if (!dev || !resource)
      return;
   const TrackedResourceState* tracked = Hooks::Lifetime::GetState<TrackedResourceState>(resource);
   if (tracked && (tracked->reserved || tracked->enhanced_barrier))
      return;
   std::scoped_lock lock(dev->registry->mutex);
   if (dev->registry->released || !dev->registry->pending_set.insert(resource).second)
      return;
   dev->registry->pending_queue.push_back({resource, original_info, mirror_info, origin});
   Chains::CountDeferredQueued(1);
}

bool IsPendingMirror(ID3D12Device* device, const ID3D12Resource* resource)
{
   if (!device || !resource)
      return false;
   DeviceState* dev = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!dev)
      return false;
   std::scoped_lock lock(dev->registry->mutex);
   return dev->registry->pending_set.contains(resource);
}

DXGI_FORMAT GetFormat(ID3D12Resource* resource)
{
   return resource->GetDesc().Format;
}

struct BarrierTransition
{
   ID3D12Resource* resource = nullptr;
   D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_COMMON;
};

// What Luma's converting copy and scaling track on a list
struct ListState
{
   ID3D12RootSignature* root_signature = nullptr; // Graphics
   std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets = {};
   std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> game_render_targets = {};
   UINT render_target_count = 0;
   D3D12_CPU_DESCRIPTOR_HANDLE depth = {};
   bool has_depth = false;
   std::array<D3D12_VIEWPORT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
   UINT viewport_count = 0;
   std::array<D3D12_RECT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors = {};
   UINT scissor_count = 0;
   D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
   // Scaling state for bound targets on this list
   Math::float2 target_scale = {1.f, 1.f};
   bool target_is_scaled = false;
   Math::uint2 rtv0_size = {};
   ListScalingSnapshot scaling = {};
   // Barrier transitions recorded on this list, committed in PostExecuteCommandLists order
   std::vector<BarrierTransition> recorded_barriers;
   // The scale of the bound resized in-place target, and the game's viewports and scissors it scales
   Math::float2 in_place_scale = {1.f, 1.f};
   UINT in_place_viewport_count = 0;
   std::array<D3D12_VIEWPORT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> in_place_viewports = {};
   UINT in_place_scissor_count = 0;
   std::array<D3D12_RECT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> in_place_scissors = {};
};

struct CopyPipelineEntry
{
   ID3D12RootSignature* root_signature = nullptr;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   bool scale = false;
   std::unique_ptr<Hooks::Lifetime::D3D12::Object> object;
};

struct CopyState
{
   std::mutex mutex; // Never held with another
   bool released = false;
   ComPtr<ID3D12DescriptorHeap> rtv_heap;
   std::shared_ptr<const Shaders::CompiledShader> copy_vs;
   std::shared_ptr<const Shaders::CompiledShader> copy_ps;
   std::shared_ptr<const Shaders::CompiledShader> scale_vs;
   std::shared_ptr<const Shaders::CompiledShader> scale_ps;
   std::vector<CopyPipelineEntry> pipelines;
};

std::mutex copy_init_mutex; // Never held with another

CopyState* GetOrCreateCopyState(ID3D12Device* device)
{
   if (CopyState* state = Hooks::Lifetime::GetState<CopyState>(device))
      return state;
   std::scoped_lock lock(copy_init_mutex);
   if (CopyState* state = Hooks::Lifetime::GetState<CopyState>(device))
      return state;
   return Hooks::Lifetime::SetState<CopyState>(device);
}

std::shared_ptr<const Shaders::CompiledShader> GetCompiled(ID3D12Device* device, uint64_t name_hash)
{
   const std::optional<uint32_t> index = Database::Find(name_hash);
   const std::shared_ptr<Shaders::Store> store = Shaders::GetStore(device);
   return index && store ? Database::GetShader(*store, *index).compiled : nullptr;
}

ID3D12PipelineState* GetCopyPipeline(ID3D12GraphicsCommandList* list, ID3D12Device* device, CopyState& copy, ID3D12RootSignature* root_signature, DXGI_FORMAT format,
   bool scale)
{
   std::shared_ptr<const Shaders::CompiledShader> vertex_shader = GetCompiled(device, scale ? "Luma_Scale_VS"_h : "Luma_Copy_VS"_h);
   std::shared_ptr<const Shaders::CompiledShader> pixel_shader = GetCompiled(device, scale ? "Luma_Scale_Table_PS"_h : "Luma_Copy_Table_PS"_h);
   if (!vertex_shader || !pixel_shader)
      return nullptr;
   std::scoped_lock lock(copy.mutex);
   if (copy.released)
      return nullptr;
   if (!copy.rtv_heap)
   {
      Hooks::Scope::Guard scope;
      const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
      if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&copy.rtv_heap))))
         return nullptr;
   }
   auto& cached_vs = scale ? copy.scale_vs : copy.copy_vs;
   auto& cached_ps = scale ? copy.scale_ps : copy.copy_ps;
   if (cached_vs != vertex_shader || cached_ps != pixel_shader)
   {
      std::erase_if(copy.pipelines, [scale](const CopyPipelineEntry& entry) { return entry.scale == scale; });
      cached_vs = std::move(vertex_shader);
      cached_ps = std::move(pixel_shader);
   }
   for (const CopyPipelineEntry& entry : copy.pipelines)
   {
      if (entry.root_signature == root_signature && entry.format == format && entry.scale == scale)
         return static_cast<ID3D12PipelineState*>(Hooks::Lifetime::D3D12::Reference(list, *entry.object));
   }
   D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
   desc.pRootSignature = root_signature;
   desc.VS = {cached_vs->bytecode.data(), cached_vs->bytecode.size()};
   desc.PS = {cached_ps->bytecode.data(), cached_ps->bytecode.size()};
   desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
   desc.SampleMask = UINT_MAX;
   desc.RasterizerState = {D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE, FALSE, D3D12_DEFAULT_DEPTH_BIAS, D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
      D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
   desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
   desc.NumRenderTargets = 1;
   desc.RTVFormats[0] = format;
   desc.SampleDesc = {1, 0};
   ComPtr<ID3D12PipelineState> pipeline;
   {
      Hooks::Scope::Guard scope;
      if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline))))
         Log::Warning("Mirrors: Luma's D3D12 {} pass couldn't be made for {}", scale ? "scale" : "copy", Utils::Format::GetName(format));
   }
   CopyPipelineEntry& entry = copy.pipelines.emplace_back(root_signature, format, scale, std::make_unique<Hooks::Lifetime::D3D12::Object>());
   Hooks::Lifetime::D3D12::Replace(*entry.object, std::move(pipeline));
   return static_cast<ID3D12PipelineState*>(Hooks::Lifetime::D3D12::Reference(list, *entry.object));
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, UINT subresource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
   D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
   barrier.Transition = {resource, subresource, before, after};
   return barrier;
}

DXGI_FORMAT GetCopyFormat(DXGI_FORMAT format)
{
   using namespace Utils::Format::DXGI;
   const DXGI_FORMAT typed = IsTypeless(format) ? GetDefaultTyped(format) : format;
   if (typed == DXGI_FORMAT_UNKNOWN || IsDepthStencil(typed) || IsBlockCompressed(typed))
      return DXGI_FORMAT_UNKNOWN;
   const Encoding encoding = GetEncoding(typed);
   return encoding == Encoding::Uint || encoding == Encoding::Sint || encoding == Encoding::Other ? DXGI_FORMAT_UNKNOWN : typed;
}

bool ConvertingCopy(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source, const CopyRegion& region, bool* out_scaled)
{
   const UINT destination_subresource = region.destination_subresource;
   const UINT source_subresource = region.source_subresource;
   const ListState* state = Hooks::Lifetime::GetState<ListState>(list);
   if (list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT || !state || !state->root_signature)
      return false;
   const D3D12_RESOURCE_DESC destination_desc = destination->GetDesc();
   const D3D12_RESOURCE_DESC source_desc = source->GetDesc();
   const auto single = [](const D3D12_RESOURCE_DESC& desc, UINT subresource) {
      return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.DepthOrArraySize == 1 && desc.SampleDesc.Count == 1 && subresource < desc.MipLevels;
   };
   if (!single(destination_desc, destination_subresource) || !single(source_desc, source_subresource) || !(destination_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
      return false;
   const UINT dst_width = UINT(std::max<UINT64>(destination_desc.Width >> destination_subresource, 1));
   const UINT dst_height = std::max<UINT>(destination_desc.Height >> destination_subresource, 1);
   const UINT src_width = UINT(std::max<UINT64>(source_desc.Width >> source_subresource, 1));
   const UINT src_height = std::max<UINT>(source_desc.Height >> source_subresource, 1);
   const bool same_size = dst_width == src_width && dst_height == src_height;
   if (region.whole_resource && (destination_desc.MipLevels != 1 || source_desc.MipLevels != 1))
      return false;
   if (region.offset != std::array<UINT, 3>{})
      return false;
   if (region.box && (region.box->left != 0 || region.box->top != 0 || region.box->front != 0 || region.box->right != src_width || region.box->bottom != src_height || region.box->back != 1))
      return false;
   const DXGI_FORMAT target_format = GetCopyFormat(destination_desc.Format);
   const DXGI_FORMAT source_format = GetCopyFormat(source_desc.Format);
   if (target_format == DXGI_FORMAT_UNKNOWN || source_format == DXGI_FORMAT_UNKNOWN)
      return false;
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
      return false;
   CopyState* copy = GetOrCreateCopyState(device.Get());
   ID3D12PipelineState* pipeline = copy ? GetCopyPipeline(list, device.Get(), *copy, state->root_signature, target_format, !same_size) : nullptr;
   if (!pipeline)
      return false;
   Database::D3D12::TableViews views = {};
   views[0].resource = source;
   views[0].desc = {source_format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
   views[0].desc.Texture2D = {source_subresource, 1};
   if (!Database::D3D12::BindTableViews(list, false, Database::D3D12::NO_TABLE, views))
      return false;

   Hooks::Scope::Guard scope;
   {
      std::scoped_lock lock(copy->mutex);
      if (copy->released || !copy->rtv_heap)
         return false;
      const D3D12_RESOURCE_BARRIER to_draw[] = {Transition(source, source_subresource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
         Transition(destination, destination_subresource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET)};
      list->ResourceBarrier(UINT(std::size(to_draw)), to_draw);
      D3D12_RENDER_TARGET_VIEW_DESC view = {target_format, D3D12_RTV_DIMENSION_TEXTURE2D};
      view.Texture2D.MipSlice = destination_subresource;
      const D3D12_CPU_DESCRIPTOR_HANDLE handle = copy->rtv_heap->GetCPUDescriptorHandleForHeapStart();
      device->CreateRenderTargetView(destination, &view, handle);
      list->OMSetRenderTargets(1, &handle, FALSE, nullptr);
   }
   const D3D12_VIEWPORT viewport = {0.f, 0.f, float(dst_width), float(dst_height), 0.f, 1.f};
   const D3D12_RECT scissor = {0, 0, LONG(dst_width), LONG(dst_height)};
   list->RSSetViewports(1, &viewport);
   list->RSSetScissorRects(1, &scissor);
   list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   list->SetPipelineState(pipeline);
   list->DrawInstanced(4, 1, 0, 0);
   const D3D12_RESOURCE_BARRIER back[] = {Transition(source, source_subresource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
      Transition(destination, destination_subresource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST)};
   list->ResourceBarrier(UINT(std::size(back)), back);

   list->OMSetRenderTargets(state->render_target_count, state->render_target_count ? state->render_targets.data() : nullptr, FALSE,
      state->has_depth ? &state->depth : nullptr);
   if (state->viewport_count)
      list->RSSetViewports(state->viewport_count, state->viewports.data());
   if (state->scissor_count)
      list->RSSetScissorRects(state->scissor_count, state->scissors.data());
   if (state->topology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
      list->IASetPrimitiveTopology(state->topology);
   Shaders::Variants::D3D12::RestorePipeline(list);
   if (!same_size)
   {
      Scaling::CountScaledBlit();
      if (out_scaled)
         *out_scaled = true;
   }
   return true;
}

void TryQueueDirectChain(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source)
{
   if (Chains::GetMode() < Chains::Mode::Direct || !destination || !source)
      return;
   if (FindMirrorState<OwnedResource>(destination) || FindInPlace(destination))
      return;
   const ResourceMirrorState* source_mirror = FindMirrorState<OwnedResource>(source);
   if (!source_mirror)
      return;
   const D3D12_RESOURCE_DESC dst_desc = destination->GetDesc();
   if (dst_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
      return;
   D3D12_HEAP_PROPERTIES props = {D3D12_HEAP_TYPE_DEFAULT};
   destination->GetHeapProperties(&props, nullptr);
   const Upgrade::TextureInfo dst_info = Describe(dst_desc, props.Type == D3D12_HEAP_TYPE_DEFAULT);
   const Upgrade::Rules& rules = Upgrade::GetRules();
   if (!Chains::CanPropagate(source_mirror->info, dst_info, rules.aspect_ratio_pixel_threshold))
      return;
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
      return;
   Upgrade::TextureInfo mirror_info = dst_info;
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
   QueueChainMirror(device.Get(), destination, dst_info, mirror_info, {.kind = Chains::OriginKind::DirectChain});
}

bool SameDimensions2D(ID3D12Resource* a, ID3D12Resource* b)
{
   if (!a || !b)
      return true;
   const D3D12_RESOURCE_DESC da = a->GetDesc();
   const D3D12_RESOURCE_DESC db = b->GetDesc();
   if (da.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || db.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
      return true;
   return da.Width == db.Width && da.Height == db.Height;
}

constexpr Math::float2 NO_SCALE = {1.f, 1.f};

struct InPlaceUpgrade
{
   D3D12_RESOURCE_DESC desc = {};
   Upgrade::TextureInfo game;
   Upgrade::TextureInfo info;
};

// A desc's fields (its padding isn't the game's to keep)
using DescKey = std::array<uint64_t, 8>;

DescKey GetKey(const D3D12_RESOURCE_DESC& desc)
{
   return {uint64_t(desc.Dimension), desc.Alignment, desc.Width, (uint64_t(desc.Height) << 32) | desc.DepthOrArraySize, (uint64_t(desc.MipLevels) << 32) | uint64_t(desc.Format),
      (uint64_t(desc.SampleDesc.Count) << 32) | desc.SampleDesc.Quality, uint64_t(desc.Layout), uint64_t(desc.Flags)};
}

// What GetResourceAllocationInfo told the game, scoped per device, for its creation. Cleared past its size (a placed resource then gets a mirror).
using AllocationKey = std::pair<ID3D12Device*, DescKey>;
std::mutex allocation_upgrades_mutex; // Never held with another
std::map<AllocationKey, InPlaceUpgrade> allocation_upgrades;
constexpr size_t MAX_ALLOCATION_UPGRADES = 4096;

// What changes in place: the format, and for a 2D texture its size and mips
template <typename Desc>
void WriteInPlaceDesc(Desc& desc, const Upgrade::TextureInfo& info)
{
   desc.Format = info.format;
   desc.Width = info.width;
   desc.Height = info.height;
   desc.MipLevels = UINT16(info.levels);
}

std::optional<InPlaceUpgrade> GetInPlaceUpgrade(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc)
{
   if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER || desc.Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
      return std::nullopt;
   const std::optional<Upgrade::UpgradeInfo> texture_upgrade =
      Upgrade::GetUpgradeInfo(Describe(desc, true), GetFrameState(device), {.allow_writable_sr = Chains::GetMode() >= Chains::Mode::Direct});
   if (!texture_upgrade || texture_upgrade->method != Upgrade::Method::InPlace)
      return std::nullopt;
   InPlaceUpgrade upgrade = {.desc = desc, .game = texture_upgrade->game, .info = texture_upgrade->upgraded};
   WriteInPlaceDesc(upgrade.desc, texture_upgrade->upgraded);
   // A small placement may not hold the upgraded size: the default one
   if (upgrade.desc.Alignment == D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT)
      upgrade.desc.Alignment = 0;
   return upgrade;
}

std::optional<InPlaceUpgrade> FindAllocationUpgrade(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc)
{
   std::scoped_lock lock(allocation_upgrades_mutex);
   const auto it = allocation_upgrades.find({device, GetKey(desc)});
   return it != allocation_upgrades.end() ? std::optional(it->second) : std::nullopt;
}

// Between PrepareResource and UpgradeResource on the same thread (the game's creation call)
struct PendingInPlace
{
   bool active = false;
   Upgrade::TextureInfo game;
   Upgrade::TextureInfo info;
};

thread_local PendingInPlace pending_in_place;

// Render target and depth handles of in-place resources: their view format and scale
struct InPlaceTarget
{
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   Math::float2 scale = NO_SCALE;
};

std::shared_mutex in_place_targets_mutex; // Never held with another
std::map<SIZE_T, InPlaceTarget> in_place_targets;
std::atomic<size_t> in_place_target_count = 0;

std::optional<InPlaceTarget> FindInPlaceTarget(SIZE_T handle)
{
   if (in_place_target_count.load(std::memory_order_relaxed) == 0)
      return std::nullopt;
   std::shared_lock lock(in_place_targets_mutex);
   const auto it = in_place_targets.find(handle);
   return it != in_place_targets.end() ? std::optional(it->second) : std::nullopt;
}

void EraseInPlaceTargets(SIZE_T start, SIZE_T end)
{
   if (in_place_target_count.load(std::memory_order_relaxed) == 0)
      return;
   std::unique_lock lock(in_place_targets_mutex);
   in_place_targets.erase(in_place_targets.lower_bound(start), in_place_targets.lower_bound(end));
   in_place_target_count = in_place_targets.size();
}

void LogInPlaceCopy(std::string_view what)
{
   LUMA_UNIMPLEMENTED("D3D12 conversions of a partial region, on a copy list, or into a texture that can't be a render target, with a texture upgraded in place");
   if (in_place_unhandled_copies++ == 0)
      Log::Warning("Mirrors: {} with a texture upgraded in place couldn't be converted on D3D12 (a partial region, a copy list, or a texture that can't "
                   "be a render target): it was skipped",
         what);
}

// A committed texture of Luma's the list keeps alive until it has executed
ID3D12Resource* CreateTemporary(ID3D12GraphicsCommandList* list, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state)
{
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
      return nullptr;
   const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
   ComPtr<ID3D12Resource> resource;
   if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource))))
      return nullptr;
   Hooks::Lifetime::D3D12::Object holder;
   Hooks::Lifetime::D3D12::Replace(holder, resource);
   return static_cast<ID3D12Resource*>(Hooks::Lifetime::D3D12::Reference(list, holder));
}

D3D12_RESOURCE_DESC GetTemporaryDesc(DXGI_FORMAT format, uint32_t width, uint32_t height, D3D12_RESOURCE_FLAGS flags)
{
   return {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, .Width = width, .Height = height, .DepthOrArraySize = 1, .MipLevels = 1, .Format = format, .SampleDesc = {1, 0}, .Flags = flags};
}

// `source` (in COPY_SOURCE) converted and scaled into a whole subresource of `destination` (in COPY_DEST); a destination that can't be a
// render target through one of its format, copied natively
bool ConvertInto(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, UINT destination_subresource, ID3D12Resource* source, UINT source_subresource)
{
   const D3D12_RESOURCE_DESC destination_desc = destination->GetDesc();
   const CopyRegion region = {.convertible = true, .destination_subresource = destination_subresource, .source_subresource = source_subresource};
   if (destination_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
      return ConvertingCopy(list, destination, source, region, nullptr);
   if (destination_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || destination_desc.DepthOrArraySize != 1 || destination_desc.SampleDesc.Count != 1 ||
      (destination_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
      return false;
   const uint32_t level = destination_subresource % std::max<UINT16>(destination_desc.MipLevels, 1);
   ID3D12Resource* target = CreateTemporary(list,
      GetTemporaryDesc(destination_desc.Format, Upgrade::GetMipSize(uint32_t(destination_desc.Width), level), Upgrade::GetMipSize(destination_desc.Height, level),
         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
      D3D12_RESOURCE_STATE_COPY_DEST);
   if (!target || !ConvertingCopy(list, target, source, {.convertible = true, .source_subresource = source_subresource}, nullptr))
      return false;
   const D3D12_RESOURCE_BARRIER to_source = Transition(target, 0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
   list->ResourceBarrier(1, &to_source);
   const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = destination, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = destination_subresource};
   const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = target, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0};
   list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
   return true;
}

// The scaled values the list has bound for the game's (`scale` 1: the game's)
void BindInPlaceViewports(ID3D12GraphicsCommandList* list, ListState& state)
{
   const UINT count = state.in_place_viewport_count;
   if (count == 0)
      return;
   std::array<D3D12_VIEWPORT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = state.in_place_viewports;
   for (UINT i = 0; i < count && state.in_place_scale != NO_SCALE; i++)
   {
      const D3D12_VIEWPORT& game = state.in_place_viewports[i];
      const Scaling::Viewport scaled = Scaling::RescaleViewport({game.TopLeftX, game.TopLeftY, game.Width, game.Height, game.MinDepth, game.MaxDepth}, state.in_place_scale);
      viewports[i] = {scaled.x, scaled.y, scaled.width, scaled.height, scaled.min_depth, scaled.max_depth};
   }
   Hooks::Scope::Guard scope;
   list->RSSetViewports(count, viewports.data());
   state.viewport_count = count;
   state.viewports = viewports;
}

void BindInPlaceScissors(ID3D12GraphicsCommandList* list, ListState& state)
{
   const UINT count = state.in_place_scissor_count;
   if (count == 0)
      return;
   std::array<D3D12_RECT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> rects = state.in_place_scissors;
   for (UINT i = 0; i < count && state.in_place_scale != NO_SCALE; i++)
   {
      const D3D12_RECT& game = state.in_place_scissors[i];
      const Scaling::Rect scaled = Scaling::RescaleRect({game.left, game.top, game.right, game.bottom}, state.in_place_scale);
      rects[i] = {scaled.left, scaled.top, scaled.right, scaled.bottom};
   }
   Hooks::Scope::Guard scope;
   list->RSSetScissorRects(count, rects.data());
   state.scissor_count = count;
   state.scissors = rects;
}
} // namespace

namespace {
void CreateMirror(ID3D12Device* device, ID3D12Resource* resource, std::optional<D3D12_RESOURCE_STATES> state, const D3D12_CLEAR_VALUE* clear, bool reserved)
{
   const D3D12_RESOURCE_DESC desc = resource->GetDesc();
   if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER || desc.Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
      return;
   if (Chains::IsNeeded())
   {
      TrackedResourceState* tracked = Hooks::Lifetime::SetState<TrackedResourceState>(resource);
      if (tracked)
      {
         tracked->committed_state.store(uint32_t(state.value_or(D3D12_RESOURCE_STATE_COMMON)), std::memory_order_relaxed);
         tracked->reserved = reserved;
         tracked->enhanced_barrier = !state.has_value();
         if (clear)
         {
            tracked->has_clear = true;
            tracked->clear = *clear;
         }
      }
   }
   D3D12_HEAP_PROPERTIES properties = {D3D12_HEAP_TYPE_DEFAULT};
   resource->GetHeapProperties(&properties, nullptr);
   const Upgrade::TextureInfo described = Describe(desc, properties.Type == D3D12_HEAP_TYPE_DEFAULT);
   const std::optional<Upgrade::UpgradeInfo> upgrade =
      Upgrade::GetUpgradeInfo(described, GetFrameState(device), {.allow_writable_sr = Chains::GetMode() >= Chains::Mode::Direct, .mirror = true});
   if (!upgrade)
      return;
   const Upgrade::TextureInfo* mirrored = &upgrade->upgraded;
   const bool resized = upgrade->resized;
   if (reserved)
   {
      reserved_resources++;
      LUMA_UNIMPLEMENTED("Mirrors of D3D12 reserved resources");
      return;
   }
   if (!state)
   {
      layout_resources++;
      LUMA_UNIMPLEMENTED("Mirrors of D3D12 resources created with an enhanced barrier layout");
      return;
   }
   const Chains::Origin origin = resized ? Chains::Origin{Chains::OriginKind::CustomDimensions} : Chains::Origin{Chains::OriginKind::Rule};
   MaterializeMirror(device, resource, *state, clear, described, *mirrored, origin);
}
} // namespace

ID3D12Resource* FindMirror(ID3D12Resource* resource)
{
   return Mirrors::FindMirror<OwnedResource>(resource);
}

ID3D12Resource* FindOriginal(ID3D12Resource* mirror)
{
   return Mirrors::FindOriginal(mirror);
}

ID3D12Resource* GetEffectiveResource(ID3D12Resource* resource)
{
   if (!resource || !IsSwapping())
      return resource;
   if (ID3D12Resource* mirror = FindMirror(resource))
      return mirror;
   return resource;
}

D3D12_CPU_DESCRIPTOR_HANDLE FindMirrorHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
   if (!handle.ptr)
      return {0};
   if (const std::shared_ptr<Slot> slot = Find(handle.ptr); slot && !slot->sampler)
      return {SIZE_T(slot->handle)};
   return {0};
}

D3D12_CPU_DESCRIPTOR_HANDLE FindOriginalHandle(D3D12_CPU_DESCRIPTOR_HANDLE mirror_handle)
{
   if (!mirror_handle.ptr || entry_count.load(std::memory_order_relaxed) == 0)
      return {0};
   std::shared_lock lock(handles_mutex);
   const auto it = mirror_to_original_handle.find(mirror_handle.ptr);
   return it != mirror_to_original_handle.end() ? D3D12_CPU_DESCRIPTOR_HANDLE{SIZE_T(it->second)} : D3D12_CPU_DESCRIPTOR_HANDLE{0};
}

ID3D12Resource* GetDescriptorResource(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
   if (!handle.ptr)
      return nullptr;
   if (entry_count.load(std::memory_order_relaxed) != 0)
   {
      std::shared_lock lock(handles_mutex);
      if (const auto it = handles.find(handle.ptr); it != handles.end() && it->second && !it->second->sampler)
         return it->second->original_resource;
      if (const auto rev = mirror_to_original_handle.find(handle.ptr); rev != mirror_to_original_handle.end())
      {
         if (const auto it = handles.find(rev->second); it != handles.end() && it->second && !it->second->sampler)
            return FindMirror(it->second->original_resource);
      }
   }
   {
      std::scoped_lock lock(unmirrored_views_mutex);
      if (const auto it = unmirrored_by_handle.find(handle.ptr); it != unmirrored_by_handle.end())
         return it->second.resource;
   }
   if (const std::optional<Descriptors::D3D12::Descriptor> desc = Descriptors::D3D12::Read(handle))
      return desc->resource;
   return nullptr;
}

std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> GetResourceDescriptors(ID3D12Resource* resource)
{
   std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> result;
   if (!resource)
      return result;
   ID3D12Resource* original = FindOriginal(resource);
   const bool query_mirror = original != nullptr;
   ID3D12Resource* match_original = query_mirror ? original : resource;
   if (entry_count.load(std::memory_order_relaxed) != 0)
   {
      std::shared_lock lock(handles_mutex);
      for (const auto& [game_handle, slot] : handles)
      {
         if (!slot || slot->sampler || slot->original_resource != match_original)
            continue;
         result.push_back({SIZE_T(query_mirror ? slot->handle : game_handle)});
      }
   }
   if (!query_mirror)
   {
      std::scoped_lock lock(unmirrored_views_mutex);
      if (const auto it = unmirrored_by_resource.find(resource); it != unmirrored_by_resource.end())
      {
         for (uint64_t h : it->second)
            result.push_back({SIZE_T(h)});
      }
   }
   return result;
}

std::optional<MirrorSummary> FindMirrorSummary(ID3D12Resource* resource)
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
   ComPtr<ID3D12Device> device;
   if (SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&device))) && IsPendingMirror(device.Get(), resource))
   {
      const Upgrade::TextureInfo info = Describe(resource->GetDesc(), true);
      return MirrorSummary{.original_info = info, .mirror_info = info, .pending = true};
   }
   return std::nullopt;
}

std::optional<ListScalingSnapshot> GetListScalingSnapshot(ID3D12GraphicsCommandList* list)
{
   if (const ListState* state = Hooks::Lifetime::GetState<ListState>(list))
      return state->scaling;
   return std::nullopt;
}

void ReleaseDeviceObjects(ID3D12Device* device)
{
   {
      std::scoped_lock lock(allocation_upgrades_mutex);
      std::erase_if(allocation_upgrades, [device](const auto& entry) { return entry.first.first == device; });
   }
   if (DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device))
   {
      {
         std::scoped_lock lock(state->pools->mutex);
         state->pools->released = true;
         for (Pool& pool : state->pools->pools)
         {
            pool.heaps.clear();
            pool.free.clear();
         }
      }
      std::vector<PendingChainMirror> pending;
      std::vector<RetiredD3D12Mirror> retired;
      std::vector<PooledD3D12Texture> pool;
      {
         std::scoped_lock lock(state->registry->mutex);
         state->registry->released = true;
         state->registry->scaled_originals.clear();
         state->registry->pending_set.clear();
         pending.swap(state->registry->pending_queue);
         retired.swap(state->registry->retired);
         pool.swap(state->registry->pool);
      }
      if (!pending.empty())
         Chains::CountDeferredQueued(-int32_t(pending.size()));
      if (!pool.empty())
         Scaling::CountPoolSize(-int32_t(pool.size()));
   }
   if (CopyState* copy = Hooks::Lifetime::GetState<CopyState>(device))
   {
      std::scoped_lock lock(copy->mutex);
      copy->released = true;
      copy->rtv_heap.Reset();
      copy->pipelines.clear();
   }
}

void InvalidateScaledMirrors(ID3D12Device* device)
{
   DeviceState* dev = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!dev)
      return;
   std::vector<ID3D12Resource*> originals;
   const uint64_t frame = Shaders::GetFrame();
   {
      std::scoped_lock lock(dev->registry->mutex);
      dev->registry->scale_generation++;
      originals.assign(dev->registry->scaled_originals.begin(), dev->registry->scaled_originals.end());
      dev->registry->scaled_originals.clear();
   }
   if (originals.empty())
      return;
   Scaling::CountInvalidation();
   std::vector<RetiredD3D12Mirror> newly_retired;
   for (ID3D12Resource* original : originals)
   {
      const ResourceMirrorState* state = Hooks::Lifetime::GetState<ResourceMirrorState>(original);
      if (!state || !state->is_scaled)
         continue;
      const TrackedResourceState* tracked = Hooks::Lifetime::GetState<TrackedResourceState>(original);
      const D3D12_RESOURCE_STATES committed = tracked ? D3D12_RESOURCE_STATES(tracked->committed_state.load(std::memory_order_relaxed)) : D3D12_RESOURCE_STATE_COMMON;
      newly_retired.push_back({state->mirror, state->mirror->GetDesc(), committed, frame, true});
      Hooks::Lifetime::RemoveState<ResourceMirrorState>(original);
      Hooks::Lifetime::RemoveState<ScaledTracker>(original);
      // Erase stale descriptor mappings referencing this original so next creation/backfill refreshes them
      std::unique_lock lock(handles_mutex);
      for (auto it = handles.begin(); it != handles.end();)
      {
         if (it->second && it->second->original_resource == original)
         {
            it = handles.erase(it);
            entry_count--;
         }
         else
         {
            ++it;
         }
      }
   }
   if (!newly_retired.empty())
   {
      std::scoped_lock lock(dev->registry->mutex);
      for (RetiredD3D12Mirror& entry : newly_retired)
         dev->registry->retired.push_back(std::move(entry));
   }
}

void OnPresent(ID3D12Device* device)
{
   if (!device)
      return;
   const Frame::Resolutions res = Frame::Get(device);
   if (Scaling::IsNeeded() && Scaling::ConsumeResolutionChange(device, res.render, res.output))
      InvalidateScaledMirrors(device);
   Scaling::OnPresent(device);

   DeviceState* dev = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!dev)
      return;

   // Materialize queued chain mirrors in their exact committed state before the next frame records
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
         const TrackedResourceState* tracked = Hooks::Lifetime::GetState<TrackedResourceState>(req.original.Get());
         const D3D12_RESOURCE_STATES state = tracked ? D3D12_RESOURCE_STATES(tracked->committed_state.load(std::memory_order_acquire)) : D3D12_RESOURCE_STATE_COMMON;
         const D3D12_CLEAR_VALUE* clear = tracked && tracked->has_clear ? &tracked->clear : nullptr;
         if (MaterializeMirror(device, req.original.Get(), state, clear, req.original_info, req.mirror_info, req.origin))
         {
            if (req.origin.kind == Chains::OriginKind::DirectChain)
               Chains::CountDirectPropagation();
            else if (req.origin.kind == Chains::OriginKind::IndirectChain)
               Chains::CountIndirectPropagation();
         }
      }
   }

   // Advance retired mirrors into the recycle pool and evict expired entries
   const uint64_t frame = Shaders::GetFrame();
   std::vector<ComPtr<ID3D12Resource>> to_release;
   {
      std::scoped_lock lock(dev->registry->mutex);
      for (auto it = dev->registry->retired.begin(); it != dev->registry->retired.end();)
      {
         if (frame >= it->retire_frame + RETIRE_PRESENTS)
         {
            if (it->poolable && !dev->registry->released)
            {
               dev->registry->pool.push_back({std::move(it->mirror), it->desc, it->state, frame});
               Scaling::CountPoolSize(1);
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
            to_release.push_back(std::move(it->resource));
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

bool HasMirrors()
{
   return HasAnyMirrors() || entry_count.load(std::memory_order_relaxed) != 0;
}

void TrackHeap(ID3D12Device* device, ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC& desc)
{
   const uint64_t start = heap->GetCPUDescriptorHandleForHeapStart().ptr;
   const HeapRange range = {start, start + uint64_t(desc.NumDescriptors) * GetIncrement(device, desc.Type), (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0};
   if (!Hooks::Lifetime::SetState<HeapState>(heap, range))
      return;
   std::scoped_lock lock(heaps_mutex);
   heaps.insert(std::ranges::upper_bound(heaps, range.start, {}, &HeapRange::start), range);
}

void TrackView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, ViewKind kind, ID3D12Resource* resource, const void* desc, bool swapping)
{
   const ResourceMirrorState* state = kind != ViewKind::ConstantBuffer ? FindMirrorState<OwnedResource>(resource) : nullptr;
   if (!state)
   {
      Forget(destination.ptr);
      if (resource && kind != ViewKind::ConstantBuffer)
         RememberUnmirroredView(destination.ptr, kind, resource, desc);
      else
         ForgetUnmirroredView(destination.ptr);
      return;
   }
   ForgetUnmirroredView(destination.ptr);
   if (IsShaderVisible(destination.ptr))
   {
      if (swapping)
      {
         WriteMirrorView(device, state->mirror.Get(), kind, desc, *state, destination.ptr);
         if (direct_writes++ == 0)
            ASSERT_ONCE_MSG(false, "A view of a mirrored texture was written straight into a shader-visible heap: it won't follow a toggle");
      }
      return;
   }
   std::shared_ptr<Slot> slot = Allocate(device, GetHeapType(kind));
   if (!slot)
      return Forget(destination.ptr);
   slot->format = WriteMirrorView(device, state->mirror.Get(), kind, desc, *state, slot->handle);
   slot->counted = true;
   slot->original_resource = resource;
   CountView(1);
   Remember(destination.ptr, std::move(slot));
}

void TrackSampler(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, const D3D12_SAMPLER_DESC& desc, bool swapping)
{
   const std::optional<D3D12_SAMPLER_DESC> mirror = Samplers::GetMirrorDesc(desc, Samplers::GetLodBiasOffset(device));
   if (!mirror)
      return Forget(destination.ptr);
   if (IsShaderVisible(destination.ptr))
   {
      if (swapping)
      {
         Hooks::Scope::Guard scope;
         device->CreateSampler(&*mirror, destination);
         direct_writes++;
      }
      return;
   }
   std::shared_ptr<Slot> slot = Allocate(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
   if (!slot)
      return Forget(destination.ptr);
   slot->sampler = true;
   slot->game_sampler = desc;
   slot->generation = Samplers::GetGeneration(device);
   sampler_count++;
   {
      Hooks::Scope::Guard scope;
      device->CreateSampler(&*mirror, {SIZE_T(slot->handle)});
   }
   Remember(destination.ptr, std::move(slot));
}

namespace {
void SwapCopiedDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type,
   bool swap_views, bool swap_samplers)
{
   if (entry_count.load(std::memory_order_relaxed) == 0 || !count)
      return;
   const uint64_t increment = GetIncrement(device, type);
   const uint64_t source_end = source.ptr + count * increment;
   const uint64_t destination_end = destination.ptr + count * increment;
   const bool shader_visible = IsShaderVisible(destination.ptr);
   thread_local std::vector<std::pair<uint64_t, std::shared_ptr<Slot>>> copied;
   copied.clear();
   {
      std::shared_lock lock(handles_mutex);
      for (auto it = handles.lower_bound(source.ptr); it != handles.end() && it->first < source_end; ++it)
         copied.emplace_back(destination.ptr + (it->first - source.ptr), it->second);
      if (!shader_visible && copied.empty())
      {
         const auto first = handles.lower_bound(destination.ptr);
         if (first == handles.end() || first->first >= destination_end)
            return;
      }
   }
   if (shader_visible)
   {
      Hooks::Scope::Guard scope;
      for (auto& [handle, slot] : copied)
      {
         if (!(slot->sampler ? swap_samplers : swap_views))
            continue;
         if (slot->sampler)
            RefreshSampler(*slot);
         device->CopyDescriptorsSimple(1, {SIZE_T(handle)}, {SIZE_T(slot->handle)}, type);
         swapped_copies++;
      }
      copied.clear();
      return;
   }
   std::unique_lock lock(handles_mutex);
   const auto first = handles.lower_bound(destination.ptr);
   const auto last = handles.lower_bound(destination_end);
   entry_count -= uint32_t(std::distance(first, last));
   handles.erase(first, last);
   for (auto& [handle, slot] : copied)
      handles.emplace(handle, std::move(slot));
   entry_count += uint32_t(copied.size());
   copied.clear();
}
} // namespace

std::span<const D3D12_RESOURCE_BARRIER> AddMirrorBarriers(ID3D12GraphicsCommandList* list, std::span<const D3D12_RESOURCE_BARRIER> barriers)
{
   if (Chains::IsNeeded() && list)
   {
      if (ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list))
      {
         for (const D3D12_RESOURCE_BARRIER& barrier : barriers)
         {
            if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && barrier.Transition.pResource && barrier.Flags != D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY)
               state->recorded_barriers.push_back({barrier.Transition.pResource, barrier.Transition.StateAfter});
         }
      }
   }
   if (!HasAnyMirrors())
      return barriers;
   thread_local std::vector<D3D12_RESOURCE_BARRIER> extended;
   extended.clear();
   for (const D3D12_RESOURCE_BARRIER& barrier : barriers)
   {
      D3D12_RESOURCE_BARRIER mirrored = barrier;
      switch (barrier.Type)
      {
      case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
         if ((mirrored.Transition.pResource = FindMirror(barrier.Transition.pResource)))
            extended.push_back(mirrored);
         break;
      case D3D12_RESOURCE_BARRIER_TYPE_UAV:
         if ((mirrored.UAV.pResource = FindMirror(barrier.UAV.pResource)))
            extended.push_back(mirrored);
         break;
      default:
         if (FindMirror(barrier.Aliasing.pResourceBefore) || FindMirror(barrier.Aliasing.pResourceAfter))
            aliasing_barriers++;
         break;
      }
   }
   if (extended.empty())
      return barriers;
   extended.insert(extended.begin(), barriers.begin(), barriers.end());
   return extended;
}

void CommitExecutedBarriers(UINT count, ID3D12CommandList* const* lists)
{
   if (!Chains::IsNeeded() || !lists)
      return;
   for (UINT i = 0; i < count; i++)
   {
      if (!lists[i])
         continue;
      const ListState* state = Hooks::Lifetime::GetState<ListState>(lists[i]);
      if (!state || state->recorded_barriers.empty())
         continue;
      for (const BarrierTransition& transition : state->recorded_barriers)
      {
         if (TrackedResourceState* tracked = Hooks::Lifetime::GetState<TrackedResourceState>(transition.resource))
            tracked->committed_state.store(uint32_t(transition.after), std::memory_order_release);
      }
   }
}

namespace {
void SwapRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*& render_targets, BOOL& single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE*& depth_stencil, Shaders::Database::D3D12::TargetFormats& formats)
{
   formats = {};
   ListState* list_state = list ? Hooks::Lifetime::GetOrCreateStateCached<ListState>(list) : nullptr;
   if (list_state)
   {
      list_state->target_scale = {1.f, 1.f};
      list_state->target_is_scaled = false;
      list_state->rtv0_size = {};
   }
   if (depth_stencil && entry_count.load(std::memory_order_relaxed) != 0)
   {
      if (const std::shared_ptr<Slot> slot = Find(depth_stencil->ptr))
      {
         thread_local D3D12_CPU_DESCRIPTOR_HANDLE depth;
         depth.ptr = SIZE_T(slot->handle);
         depth_stencil = &depth;
         formats.depth = slot->format;
         if (list_state && slot->original_resource)
         {
            if (const ResourceMirrorState* ms = FindMirrorState<OwnedResource>(slot->original_resource); ms && ms->is_scaled)
            {
               list_state->target_is_scaled = true;
               list_state->target_scale = {
                  float(ms->info.width) / float(std::max(ms->original_info.width, 1u)),
                  float(ms->info.height) / float(std::max(ms->original_info.height, 1u)),
               };
            }
         }
      }
   }
   if (!render_targets)
      return;
   thread_local std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> swapped;
   const uint64_t increment = increments[D3D12_DESCRIPTOR_HEAP_TYPE_RTV].load(std::memory_order_relaxed);
   bool any = false;
   count = std::min<UINT>(count, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
   for (UINT i = 0; i < count; i++)
   {
      const D3D12_CPU_DESCRIPTOR_HANDLE game_handle = single_range ? D3D12_CPU_DESCRIPTOR_HANDLE{SIZE_T(render_targets->ptr + i * increment)} : render_targets[i];
      if (list_state)
         list_state->game_render_targets[i] = game_handle;
      swapped[i] = game_handle;
      if (const std::shared_ptr<Slot> slot = Find(swapped[i].ptr))
      {
         swapped[i].ptr = SIZE_T(slot->handle);
         formats.render_targets[i] = slot->format;
         any = true;
         if (list_state && slot->original_resource)
         {
            if (const ResourceMirrorState* ms = FindMirrorState<OwnedResource>(slot->original_resource))
            {
               if (i == 0)
                  list_state->rtv0_size = {ms->info.width, ms->info.height};
               if (ms->is_scaled)
               {
                  list_state->target_is_scaled = true;
                  list_state->target_scale = {
                     float(ms->info.width) / float(std::max(ms->original_info.width, 1u)),
                     float(ms->info.height) / float(std::max(ms->original_info.height, 1u)),
                  };
               }
            }
         }
      }
      else if (i == 0 && list_state && Scaling::GetOptions().mode == Scaling::Mode::ViewportsOnly)
      {
         std::scoped_lock lock(unmirrored_views_mutex);
         if (const auto it = unmirrored_by_handle.find(game_handle.ptr); it != unmirrored_by_handle.end() && it->second.resource)
         {
            const D3D12_RESOURCE_DESC d = it->second.resource->GetDesc();
            list_state->rtv0_size = {uint32_t(d.Width), d.Height};
         }
      }
   }
   if (!any)
      return;
   render_targets = swapped.data();
   single_range = FALSE;
}
} // namespace

namespace {
bool SwapHandle(D3D12_CPU_DESCRIPTOR_HANDLE& handle, UINT rect_count = 0, const D3D12_RECT** rects = nullptr)
{
   const std::shared_ptr<Slot> slot = Find(handle.ptr);
   if (!slot || slot->sampler)
      return false;
   handle.ptr = SIZE_T(slot->handle);
   if (rects && *rects && rect_count > 0 && slot->original_resource)
   {
      if (const ResourceMirrorState* ms = FindMirrorState<OwnedResource>(slot->original_resource); ms && ms->is_scaled)
      {
         const Math::float2 scale = {
            float(ms->info.width) / float(std::max(ms->original_info.width, 1u)),
            float(ms->info.height) / float(std::max(ms->original_info.height, 1u)),
         };
         thread_local std::vector<D3D12_RECT> scaled_rects;
         scaled_rects.clear();
         scaled_rects.reserve(rect_count);
         for (UINT i = 0; i < rect_count; i++)
         {
            const Scaling::Rect r = Scaling::RescaleRect({(*rects)[i].left, (*rects)[i].top, (*rects)[i].right, (*rects)[i].bottom}, scale);
            scaled_rects.push_back({r.left, r.top, r.right, r.bottom});
         }
         *rects = scaled_rects.data();
         Scaling::CountClearRectRescale();
      }
   }
   return true;
}
} // namespace

namespace {
bool SwapCopy(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, ID3D12Resource*& source, CopyRegion& region, bool* out_scaled = nullptr)
{
   TryQueueDirectChain(list, destination, source);
   const CopyPlan<ID3D12Resource> plan = PlanCopy(destination, source, &FindMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return false;
   const bool same_size = SameDimensions2D(plan.destination, plan.source);
   if (plan.action == CopyAction::DirectMirrorCopy && same_size)
   {
      const ResourceMirrorState* src_mirror = FindMirrorState<OwnedResource>(source);
      const ResourceMirrorState* dst_mirror = FindMirrorState<OwnedResource>(destination);
      if (src_mirror && src_mirror->is_scaled && region.box)
      {
         const Math::float2 scale = {
            float(src_mirror->info.width) / float(std::max(src_mirror->original_info.width, 1u)),
            float(src_mirror->info.height) / float(std::max(src_mirror->original_info.height, 1u)),
         };
         const Scaling::Box rescaled = Scaling::RescaleBox(
            {region.box->left, region.box->top, region.box->front, region.box->right, region.box->bottom, region.box->back}, scale);
         thread_local D3D12_BOX scaled_box;
         scaled_box = {rescaled.left, rescaled.top, rescaled.front, rescaled.right, rescaled.bottom, rescaled.back};
         region.box = &scaled_box;
         Scaling::CountCopyBoxRescale();
         if (out_scaled)
            *out_scaled = true;
      }
      if (dst_mirror && dst_mirror->is_scaled && (region.offset[0] != 0 || region.offset[1] != 0))
      {
         const Math::float2 scale = {
            float(dst_mirror->info.width) / float(std::max(dst_mirror->original_info.width, 1u)),
            float(dst_mirror->info.height) / float(std::max(dst_mirror->original_info.height, 1u)),
         };
         region.offset[0] = UINT(std::lround(double(region.offset[0]) * double(scale.x)));
         region.offset[1] = UINT(std::lround(double(region.offset[1]) * double(scale.y)));
         if (out_scaled)
            *out_scaled = true;
      }
      destination = plan.destination;
      source = plan.source;
      return false;
   }
   if (!source || !destination)
      return false;
   if (region.convertible && ConvertingCopy(list, plan.destination, plan.source, region, out_scaled))
   {
      converted_copies++;
      return true;
   }
   if (unhandled_copies++ == 0)
      Log::Warning("Mirrors: a copy between a {} mirror and a {} texture without one couldn't run through Luma's copy pass on D3D12, the game's textures are copied",
         Utils::Format::GetName(GetFormat(plan.destination_mirrored ? plan.destination : plan.source)),
         Utils::Format::GetName(GetFormat(plan.destination_mirrored ? source : destination)));
   return false;
}
} // namespace

namespace {
bool SwapResolve(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, UINT destination_subresource, ID3D12Resource*& source,
   UINT source_subresource, DXGI_FORMAT& format)
{
   if (!list || !destination || !source)
      return false;
   TryQueueDirectChain(list, destination, source);
   const CopyPlan<ID3D12Resource> plan = PlanCopy(destination, source, &FindMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return false;
   const DXGI_FORMAT dst_format = GetFormat(plan.destination);
   const DXGI_FORMAT src_format = GetFormat(plan.source);
   const bool same_family = Utils::Format::DXGI::GetTypeless(dst_format) == Utils::Format::DXGI::GetTypeless(src_format) && dst_format != DXGI_FORMAT_UNKNOWN;
   if (same_family && SameDimensions2D(plan.destination, plan.source))
   {
      if (plan.source != source)
         format = GetResolveFormat(format, GetFormat(source), src_format);
      destination = plan.destination;
      source = plan.source;
      return false;
   }
   // Cross-family MSAA resolve: resolve plan.source into a temporary single-sample texture of its own family,
   // then blit-convert into plan.destination via ConvertingCopy.
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))) || !device)
      return false;
   const D3D12_RESOURCE_DESC src_desc = plan.source->GetDesc();
   if (src_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || src_desc.SampleDesc.Count <= 1 || source_subresource >= src_desc.MipLevels)
      return false;
   const DXGI_FORMAT resolve_format = GetResolveFormat(format, GetFormat(source), src_format);
   if (resolve_format == DXGI_FORMAT_UNKNOWN)
      return false;
   D3D12_RESOURCE_DESC temp_desc = src_desc;
   temp_desc.Alignment = 0;
   temp_desc.Width = std::max<UINT64>(src_desc.Width >> source_subresource, 1);
   temp_desc.Height = std::max<UINT>(src_desc.Height >> source_subresource, 1);
   temp_desc.DepthOrArraySize = 1;
   temp_desc.MipLevels = 1;
   temp_desc.Format = resolve_format;
   temp_desc.SampleDesc = {1, 0};
   temp_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
   const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
   ComPtr<ID3D12Resource> resolved;
   {
      Hooks::Scope::Guard scope;
      if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &temp_desc, D3D12_RESOURCE_STATE_RESOLVE_DEST, nullptr, IID_PPV_ARGS(&resolved))) ||
         !resolved)
         return false;
      Hooks::Lifetime::D3D12::Object temp_holder;
      Hooks::Lifetime::D3D12::Replace(temp_holder, resolved);
      Hooks::Lifetime::D3D12::Reference(list, temp_holder);
      list->ResolveSubresource(resolved.Get(), 0, plan.source, source_subresource, resolve_format);
      const D3D12_RESOURCE_BARRIER to_copy[] = {
         Transition(resolved.Get(), 0, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE),
         Transition(plan.destination, destination_subresource, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_DEST),
      };
      list->ResourceBarrier(UINT(std::size(to_copy)), to_copy);
   }
   const CopyRegion region = {.convertible = true, .destination_subresource = destination_subresource, .source_subresource = 0};
   const bool ok = ConvertingCopy(list, plan.destination, resolved.Get(), region, nullptr);
   {
      Hooks::Scope::Guard scope;
      const D3D12_RESOURCE_BARRIER back = Transition(plan.destination, destination_subresource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RESOLVE_DEST);
      list->ResourceBarrier(1, &back);
   }
   if (ok)
   {
      converted_copies++;
      return true;
   }
   if (unhandled_copies++ == 0)
      Log::Warning("Mirrors: a cross-format resolve between {} and {} couldn't run through Luma's copy pass on D3D12, the game's textures are resolved",
         Utils::Format::GetName(src_format), Utils::Format::GetName(dst_format));
   return false;
}
} // namespace

namespace {
void SwapClearUnorderedAccess(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE& gpu, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, ID3D12Resource*& resource)
{
   ID3D12Resource* mirror = FindMirror(resource);
   if (!mirror)
      return;
   const std::shared_ptr<Slot> slot = Find(cpu.ptr);
   ID3D12DescriptorHeap* heap = Database::D3D12::GetBoundHeap(list);
   const std::optional<HeapTail::D3D12::RingRange> range = slot && heap ? HeapTail::D3D12::AllocateRing(list, heap, 1) : std::nullopt;
   if (!range)
   {
      unhandled_clears++;
      LUMA_UNIMPLEMENTED("UAV clears of mirrored textures on D3D12 without the mirror's view or a ring in the bound heap");
      return;
   }
   {
      Hooks::Scope::Guard scope;
      slot->device->CopyDescriptorsSimple(1, range->cpu, {SIZE_T(slot->handle)}, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   }
   gpu = range->gpu;
   cpu.ptr = SIZE_T(slot->handle);
   resource = mirror;
   uav_clears++;
}
} // namespace

void RecordRootSignature(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root_signature)
{
   if (ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list))
      state->root_signature = root_signature;
}

namespace {
void RecordRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil)
{
   ListState* state = list ? Hooks::Lifetime::GetOrCreateStateCached<ListState>(list) : nullptr;
   if (!state)
      return;
   count = render_targets ? std::min<UINT>(count, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) : 0;
   uint64_t increment = increments[D3D12_DESCRIPTOR_HEAP_TYPE_RTV].load(std::memory_order_relaxed);
   if (single_range && count > 1 && !increment)
   {
      ComPtr<ID3D12Device> device;
      if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))))
         increment = GetIncrement(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
   }
   for (UINT i = 0; i < count; i++)
      state->render_targets[i] = single_range ? D3D12_CPU_DESCRIPTOR_HANDLE{SIZE_T(render_targets->ptr + i * increment)} : render_targets[i];
   state->render_target_count = count;
   state->has_depth = depth_stencil != nullptr;
   state->depth = depth_stencil ? *depth_stencil : D3D12_CPU_DESCRIPTOR_HANDLE{};
}
} // namespace

namespace {
void RecordViewports(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_VIEWPORT*& viewports)
{
   ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
   if (!state || !viewports)
      return;
   const UINT clamped = std::min<UINT>(count, UINT(state->viewports.size()));
   state->scaling.viewports_scaled = false;
   state->scaling.game_viewport_count = clamped;
   std::copy_n(viewports, clamped, state->scaling.game_viewports.begin());
   state->viewport_count = clamped;
   std::copy_n(viewports, clamped, state->viewports.begin());

   if (!Scaling::IsNeeded() || !Scaling::IsEnabled() || !Scaling::GetOptions().scale_viewports || clamped == 0)
      return;
   thread_local std::array<D3D12_VIEWPORT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scaled;
   if (Scaling::GetOptions().mode == Scaling::Mode::ViewportsOnly)
   {
      ComPtr<ID3D12Device> device;
      if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
         return;
      if (Scaling::CheckCrossThreadOverlap(device.Get()))
      {
         state->scaling.cross_thread_overlap = true;
         return;
      }
      if (!Scaling::IsActiveOnCurrentThread(device.Get()))
         return;
      const Math::uint2 target = Scaling::GetTargetResolution(device.Get());
      if (target.x == 0 || target.y == 0 || (state->rtv0_size.x != 0 && (state->rtv0_size.x != target.x || state->rtv0_size.y != target.y)))
         return;
      const D3D12_VIEWPORT full = {0.f, 0.f, float(target.x), float(target.y), 0.f, 1.f};
      const bool already_full = clamped == 1 && viewports[0].TopLeftX == full.TopLeftX && viewports[0].TopLeftY == full.TopLeftY &&
         viewports[0].Width == full.Width && viewports[0].Height == full.Height && viewports[0].MinDepth == full.MinDepth && viewports[0].MaxDepth == full.MaxDepth;
      if (!already_full)
      {
         scaled[0] = full;
         count = 1;
         viewports = scaled.data();
         state->viewport_count = 1;
         state->viewports[0] = full;
         state->scaling.viewports_scaled = true;
         Scaling::CountViewportRescale();
      }
   }
   else if (state->target_is_scaled)
   {
      for (UINT i = 0; i < clamped; i++)
      {
         const Scaling::Viewport r = Scaling::RescaleViewport(
            {viewports[i].TopLeftX, viewports[i].TopLeftY, viewports[i].Width, viewports[i].Height, viewports[i].MinDepth, viewports[i].MaxDepth}, state->target_scale);
         scaled[i] = {r.x, r.y, r.width, r.height, r.min_depth, r.max_depth};
         state->viewports[i] = scaled[i];
      }
      viewports = scaled.data();
      state->scaling.viewports_scaled = true;
      Scaling::CountViewportRescale();
   }
}
} // namespace

namespace {
void RecordScissorRects(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_RECT*& rects)
{
   ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
   if (!state || !rects)
      return;
   const UINT clamped = std::min<UINT>(count, UINT(state->scissors.size()));
   state->scaling.scissors_scaled = false;
   state->scaling.game_scissor_count = clamped;
   std::copy_n(rects, clamped, state->scaling.game_scissors.begin());
   state->scissor_count = clamped;
   std::copy_n(rects, clamped, state->scissors.begin());

   if (!Scaling::IsNeeded() || !Scaling::IsEnabled() || !Scaling::GetOptions().scale_scissors || clamped == 0)
      return;
   thread_local std::array<D3D12_RECT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scaled;
   if (Scaling::GetOptions().mode == Scaling::Mode::ViewportsOnly)
   {
      ComPtr<ID3D12Device> device;
      if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
         return;
      if (Scaling::CheckCrossThreadOverlap(device.Get()))
      {
         state->scaling.cross_thread_overlap = true;
         return;
      }
      if (!Scaling::IsActiveOnCurrentThread(device.Get()))
         return;
      const Math::uint2 target = Scaling::GetTargetResolution(device.Get());
      if (target.x == 0 || target.y == 0 || (state->rtv0_size.x != 0 && (state->rtv0_size.x != target.x || state->rtv0_size.y != target.y)))
         return;
      const D3D12_RECT full = {0, 0, LONG(target.x), LONG(target.y)};
      const bool already_full =
         clamped == 1 && rects[0].left == full.left && rects[0].top == full.top && rects[0].right == full.right && rects[0].bottom == full.bottom;
      if (!already_full)
      {
         scaled[0] = full;
         count = 1;
         rects = scaled.data();
         state->scissor_count = 1;
         state->scissors[0] = full;
         state->scaling.scissors_scaled = true;
         Scaling::CountScissorRescale();
      }
   }
   else if (state->target_is_scaled)
   {
      for (UINT i = 0; i < clamped; i++)
      {
         const Scaling::Rect r = Scaling::RescaleRect({rects[i].left, rects[i].top, rects[i].right, rects[i].bottom}, state->target_scale);
         scaled[i] = {r.left, r.top, r.right, r.bottom};
         state->scissors[i] = scaled[i];
      }
      rects = scaled.data();
      state->scaling.scissors_scaled = true;
      Scaling::CountScissorRescale();
   }
}
} // namespace

void RecordTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology)
{
   if (ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list))
      state->topology = topology;
}

void ForgetListState(ID3D12GraphicsCommandList* list)
{
   if (ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list))
      *state = {};
}

void PrepareDrawOrDispatch(ID3D12GraphicsCommandList* list, bool is_dispatch)
{
   if (!IsSwapping())
      return;
   const bool chains_active = Chains::IsNeeded();
   const bool scaling_active = Scaling::IsNeeded() && Scaling::IsEnabled();
   if (!chains_active && !scaling_active)
      return;
   ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
   if (!state)
      return;
   state->scaling.chain_seed = false;
   state->scaling.chain_propagated = false;
   state->scaling.cross_thread_overlap = false;

   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
      return;

   uint32_t shader_hash = 0;
   const Shaders::Variants::D3D12::BoundPipeline bound = Shaders::Variants::D3D12::GetBoundPipeline(list);
   if (bound.tracked)
   {
      const ShaderStage stage = is_dispatch ? ShaderStage::Compute : ShaderStage::Pixel;
      if (bound.tracked->shaders[size_t(stage)])
         shader_hash = bound.tracked->shaders[size_t(stage)]->hash;
   }

   if (scaling_active && shader_hash != 0 && Scaling::ShouldDeactivateAt(shader_hash))
      Scaling::End(device.Get());

   if (scaling_active && Scaling::CheckCrossThreadOverlap(device.Get()))
      state->scaling.cross_thread_overlap = true;

   // Seed discovery on bound RTVs: queues PendingChainMirror for next Present (safe across parallel command lists)
   if (chains_active && !is_dispatch && shader_hash != 0)
   {
      if (const Chains::Seed* seed = Chains::FindSeed(shader_hash))
      {
         const Upgrade::FrameState frame_state = GetFrameState(device.Get());
         const Upgrade::Rules& rules = Upgrade::GetRules();
         const bool can_scale = scaling_active && seed->scale && Scaling::IsActiveOnCurrentThread(device.Get());
         for (uint8_t slot : seed->rtv_slots)
         {
            if (slot >= state->render_target_count)
               continue;
            const uint64_t handle = state->game_render_targets[slot].ptr;
            if (!handle)
               continue;
            ID3D12Resource* resource = nullptr;
            {
               std::scoped_lock lock(unmirrored_views_mutex);
               if (const auto it = unmirrored_by_handle.find(handle); it != unmirrored_by_handle.end())
                  resource = it->second.resource;
            }
            if (!resource || FindMirrorState<OwnedResource>(resource) || FindInPlace(resource))
               continue;
            D3D12_HEAP_PROPERTIES props = {D3D12_HEAP_TYPE_DEFAULT};
            resource->GetHeapProperties(&props, nullptr);
            const Upgrade::TextureInfo info = Describe(resource->GetDesc(), props.Type == D3D12_HEAP_TYPE_DEFAULT);
            const std::optional<Upgrade::UpgradeInfo> upgrade =
               Upgrade::GetUpgradeInfo(info, frame_state, {.allow_writable_sr = true, .force_format = true, .mirror = true});
            if (!upgrade)
               continue;
            Upgrade::TextureInfo target = upgrade->upgraded;
            if (can_scale && Scaling::ShouldScaleTexture(target, frame_state, true, true, rules.aspect_ratio_pixel_threshold))
            {
               const Math::uint2 scaled = Scaling::ComputeScaledSize({target.width, target.height}, frame_state.render, frame_state.output);
               target.width = scaled.x;
               target.height = scaled.y;
            }
            QueueChainMirror(device.Get(), resource, info, target, {Chains::OriginKind::Seed, shader_hash});
            state->scaling.chain_seed = true;
         }
      }
   }

   // If viewports/scissors were bound BEFORE OMSetRenderTargets (or before Scaling::Begin), apply scaling right before draw
   if (is_dispatch || !scaling_active)
      return;
   if (Scaling::GetOptions().mode == Scaling::Mode::ViewportsOnly && Scaling::IsActiveOnCurrentThread(device.Get()))
   {
      const Math::uint2 target = Scaling::GetTargetResolution(device.Get());
      if (target.x > 0 && target.y > 0 && (state->rtv0_size.x == 0 || (state->rtv0_size.x == target.x && state->rtv0_size.y == target.y)))
      {
         Hooks::Scope::Guard scope;
         if (Scaling::GetOptions().scale_viewports && !state->scaling.viewports_scaled && state->viewport_count > 0)
         {
            const D3D12_VIEWPORT full = {0.f, 0.f, float(target.x), float(target.y), 0.f, 1.f};
            list->RSSetViewports(1, &full);
            state->viewport_count = 1;
            state->viewports[0] = full;
            state->scaling.viewports_scaled = true;
            Scaling::CountViewportRescale();
         }
         if (Scaling::GetOptions().scale_scissors && !state->scaling.scissors_scaled && state->scissor_count > 0)
         {
            const D3D12_RECT full = {0, 0, LONG(target.x), LONG(target.y)};
            list->RSSetScissorRects(1, &full);
            state->scissor_count = 1;
            state->scissors[0] = full;
            state->scaling.scissors_scaled = true;
            Scaling::CountScissorRescale();
         }
      }
   }
   else if (state->target_is_scaled)
   {
      Hooks::Scope::Guard scope;
      if (Scaling::GetOptions().scale_viewports && !state->scaling.viewports_scaled && state->viewport_count > 0)
      {
         for (UINT i = 0; i < state->viewport_count; i++)
         {
            const Scaling::Viewport r = Scaling::RescaleViewport(
               {state->viewports[i].TopLeftX, state->viewports[i].TopLeftY, state->viewports[i].Width, state->viewports[i].Height, state->viewports[i].MinDepth,
                  state->viewports[i].MaxDepth},
               state->target_scale);
            state->viewports[i] = {r.x, r.y, r.width, r.height, r.min_depth, r.max_depth};
         }
         list->RSSetViewports(state->viewport_count, state->viewports.data());
         state->scaling.viewports_scaled = true;
         Scaling::CountViewportRescale();
      }
      if (Scaling::GetOptions().scale_scissors && !state->scaling.scissors_scaled && state->scissor_count > 0)
      {
         for (UINT i = 0; i < state->scissor_count; i++)
         {
            const Scaling::Rect r = Scaling::RescaleRect({state->scissors[i].left, state->scissors[i].top, state->scissors[i].right, state->scissors[i].bottom}, state->target_scale);
            state->scissors[i] = {r.left, r.top, r.right, r.bottom};
         }
         list->RSSetScissorRects(state->scissor_count, state->scissors.data());
         state->scaling.scissors_scaled = true;
         Scaling::CountScissorRescale();
      }
   }
}

void FinishDrawOrDispatch(ID3D12GraphicsCommandList* list, bool is_dispatch)
{
   if (!Scaling::IsNeeded() || !Scaling::IsEnabled())
      return;
   const Shaders::Variants::D3D12::BoundPipeline bound = Shaders::Variants::D3D12::GetBoundPipeline(list);
   if (!bound.tracked)
      return;
   const ShaderStage stage = is_dispatch ? ShaderStage::Compute : ShaderStage::Pixel;
   if (!bound.tracked->shaders[size_t(stage)])
      return;
   if (Scaling::ShouldActivateAfter(bound.tracked->shaders[size_t(stage)]->hash))
   {
      ComPtr<ID3D12Device> device;
      if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))))
         Scaling::Begin(device.Get());
   }
}

bool PrepareResource(ID3D12Device* device, D3D12_RESOURCE_DESC& desc, D3D12_CLEAR_VALUE* clear, const Creation& creation)
{
   // A creation that failed left its own
   pending_in_place = {};
   if (!device || Upgrade::GetRules().method != Upgrade::Method::InPlace)
      return false;
   std::optional<InPlaceUpgrade> upgrade = FindAllocationUpgrade(device, desc);
   if (creation.placement == Placement::Committed)
   {
      // Opened by other devices or processes in the game's format
      if (creation.heap_type != D3D12_HEAP_TYPE_DEFAULT || (creation.heap_flags & D3D12_HEAP_FLAG_SHARED))
         return false;
      if (!upgrade)
         upgrade = GetInPlaceUpgrade(device, desc);
   }
   else
   {
      if (!creation.heap)
         return false;
      // The game's allocator left room only for what it was told
      if (!upgrade)
      {
         if (GetInPlaceUpgrade(device, desc) && in_place_unasked_placed++ == 0)
            Log::Warning("Mirrors: a placed texture the rules upgrade in place was created without asking its size (GetResourceAllocationInfo): it gets a mirror");
         return false;
      }
      const D3D12_HEAP_DESC heap = creation.heap->GetDesc();
      if (heap.Properties.Type != D3D12_HEAP_TYPE_DEFAULT || (heap.Flags & D3D12_HEAP_FLAG_SHARED))
         return false;
      const D3D12_RESOURCE_ALLOCATION_INFO allocation = device->GetResourceAllocationInfo(0, 1, &upgrade->desc);
      if (allocation.SizeInBytes == UINT64_MAX || (allocation.Alignment != 0 && creation.offset % allocation.Alignment != 0) ||
         creation.offset > heap.SizeInBytes || allocation.SizeInBytes > heap.SizeInBytes - creation.offset)
      {
         if (in_place_unfit_placed++ == 0)
            Log::Warning("Mirrors: a placed texture upgraded in place doesn't fit its heap from its offset: it gets a mirror");
         return false;
      }
   }
   if (!upgrade)
      return false;
   if (clear)
   {
      const Upgrade::ViewKind kind = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ? Upgrade::ViewKind::DepthStencil : Upgrade::ViewKind::RenderTarget;
      const DXGI_FORMAT format = Upgrade::GetBestViewFormat(clear->Format, kind, upgrade->game.format, upgrade->info.format);
      clear->Format = format != DXGI_FORMAT_UNKNOWN              ? format
         : Utils::Format::DXGI::IsTypeless(upgrade->info.format) ? Utils::Format::DXGI::GetDefaultTyped(upgrade->info.format)
                                                                 : upgrade->info.format;
   }
   desc = upgrade->desc;
   pending_in_place = {.active = true, .game = upgrade->game, .info = upgrade->info};
   return true;
}

namespace {
std::optional<Upgrade::TextureInfo> FinishInPlace(ID3D12Resource* resource)
{
   if (!pending_in_place.active)
      return std::nullopt;
   const PendingInPlace pending = std::exchange(pending_in_place, {});
   AttachInPlace(resource, pending.game, pending.info);
   return pending.game;
}
} // namespace

bool UpgradeForAllocation(ID3D12Device* device, D3D12_RESOURCE_DESC& desc)
{
   if (!device || Upgrade::GetRules().method != Upgrade::Method::InPlace)
      return false;
   const AllocationKey key = {device, GetKey(desc)};
   const std::optional<InPlaceUpgrade> upgrade = GetInPlaceUpgrade(device, desc);
   if (!upgrade)
   {
      std::scoped_lock lock(allocation_upgrades_mutex);
      allocation_upgrades.erase(key);
      return false;
   }
   {
      std::scoped_lock lock(allocation_upgrades_mutex);
      if (allocation_upgrades.size() >= MAX_ALLOCATION_UPGRADES)
         allocation_upgrades.clear();
      allocation_upgrades[key] = *upgrade;
   }
   desc = upgrade->desc;
   return true;
}

DXGI_FORMAT PrepareView(ID3D12Device*, D3D12_CPU_DESCRIPTOR_HANDLE destination, ViewKind kind, ID3D12Resource* resource, DXGI_FORMAT game_format, bool has_desc)
{
   const InPlaceState* state = FindInPlace(resource);
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   if (state && has_desc)
   {
      const DXGI_FORMAT mapped = Upgrade::GetBestViewFormat(game_format, ToUpgradeKind(kind), state->original_info.format, state->info.format);
      if (mapped != DXGI_FORMAT_UNKNOWN && mapped != game_format)
         format = mapped;
   }
   if ((kind == ViewKind::RenderTarget || kind == ViewKind::DepthStencil) && destination.ptr)
   {
      if (state)
      {
         const DXGI_FORMAT bound = format != DXGI_FORMAT_UNKNOWN ? format : (has_desc && game_format != DXGI_FORMAT_UNKNOWN) ? game_format
                                                                                                                             : state->info.format;
         std::unique_lock lock(in_place_targets_mutex);
         in_place_targets[destination.ptr] = {bound, GetInPlaceScale(state)};
         in_place_target_count = in_place_targets.size();
      }
      else if (in_place_target_count.load(std::memory_order_relaxed) != 0)
      {
         std::unique_lock lock(in_place_targets_mutex);
         in_place_targets.erase(destination.ptr);
         in_place_target_count = in_place_targets.size();
      }
   }
   return format;
}

namespace {
void CopyInPlaceTargets(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   if ((type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV && type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV) || in_place_target_count.load(std::memory_order_relaxed) == 0 || !device)
      return;
   const uint64_t increment = GetIncrement(device, type);
   std::unique_lock lock(in_place_targets_mutex);
   for (UINT i = 0; i < count; i++)
   {
      const SIZE_T to = SIZE_T(destination.ptr + i * increment);
      if (const auto it = in_place_targets.find(SIZE_T(source.ptr + i * increment)); it != in_place_targets.end())
         in_place_targets[to] = it->second;
      else
         in_place_targets.erase(to);
   }
   in_place_target_count = in_place_targets.size();
}
} // namespace

namespace {
void BindInPlaceTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil, Shaders::Database::D3D12::TargetFormats& formats)
{
   if (!list || !HasAnyInPlace())
      return;
   Math::float2 scale = NO_SCALE;
   if (in_place_target_count.load(std::memory_order_relaxed) != 0)
   {
      uint64_t increment = 0;
      if (render_targets && single_range && count > 1)
      {
         ComPtr<ID3D12Device> device;
         if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))))
            increment = GetIncrement(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
      }
      count = render_targets ? std::min<UINT>(count, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) : 0;
      for (UINT i = 0; i < count; i++)
      {
         const SIZE_T handle = single_range ? SIZE_T(render_targets->ptr + i * increment) : render_targets[i].ptr;
         if (single_range && i > 0 && increment == 0)
            break;
         if (const std::optional<InPlaceTarget> target = FindInPlaceTarget(handle))
         {
            if (formats.render_targets[i] == DXGI_FORMAT_UNKNOWN)
               formats.render_targets[i] = target->format;
            if (scale == NO_SCALE)
               scale = target->scale;
         }
      }
      if (depth_stencil)
      {
         if (const std::optional<InPlaceTarget> target = FindInPlaceTarget(depth_stencil->ptr))
         {
            if (formats.depth == DXGI_FORMAT_UNKNOWN)
               formats.depth = target->format;
            if (scale == NO_SCALE)
               scale = target->scale;
         }
      }
   }
   if (!HasAnyResizedInPlace())
      return;
   ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
   if (!state || state->in_place_scale == scale)
      return;
   state->in_place_scale = scale;
   BindInPlaceViewports(list, *state);
   BindInPlaceScissors(list, *state);
}
} // namespace

namespace {
void ScaleInPlaceViewports(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_VIEWPORT*& viewports)
{
   if (!viewports || !HasAnyResizedInPlace())
      return;
   ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
   if (!state)
      return;
   state->in_place_viewport_count = std::min<UINT>(count, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
   std::copy_n(viewports, state->in_place_viewport_count, state->in_place_viewports.begin());
   if (state->in_place_scale == NO_SCALE)
      return;
   thread_local std::array<D3D12_VIEWPORT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scaled;
   for (UINT i = 0; i < state->in_place_viewport_count; i++)
   {
      const D3D12_VIEWPORT& game = viewports[i];
      const Scaling::Viewport rescaled = Scaling::RescaleViewport({game.TopLeftX, game.TopLeftY, game.Width, game.Height, game.MinDepth, game.MaxDepth}, state->in_place_scale);
      scaled[i] = {rescaled.x, rescaled.y, rescaled.width, rescaled.height, rescaled.min_depth, rescaled.max_depth};
   }
   count = state->in_place_viewport_count;
   viewports = scaled.data();
   Scaling::CountViewportRescale();
}
} // namespace

namespace {
void ScaleInPlaceScissors(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_RECT*& rects)
{
   if (!rects || !HasAnyResizedInPlace())
      return;
   ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
   if (!state)
      return;
   state->in_place_scissor_count = std::min<UINT>(count, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
   std::copy_n(rects, state->in_place_scissor_count, state->in_place_scissors.begin());
   if (state->in_place_scale == NO_SCALE)
      return;
   thread_local std::array<D3D12_RECT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scaled;
   for (UINT i = 0; i < state->in_place_scissor_count; i++)
   {
      const Scaling::Rect rescaled = Scaling::RescaleRect({rects[i].left, rects[i].top, rects[i].right, rects[i].bottom}, state->in_place_scale);
      scaled[i] = {rescaled.left, rescaled.top, rescaled.right, rescaled.bottom};
   }
   count = state->in_place_scissor_count;
   rects = scaled.data();
   Scaling::CountScissorRescale();
}
} // namespace

namespace {
void CheckInPlaceUnorderedAccessClear(ID3D12Resource* resource, UINT rect_count)
{
   if (rect_count == 0 || !HasAnyResizedInPlace())
      return;
   const InPlaceState* state = FindInPlace(resource);
   if (state && state->resized)
      LUMA_UNIMPLEMENTED("UAV clear rects of a texture resized in place on D3D12 (cleared in the game's size)");
}
} // namespace

namespace {
void ScaleInPlaceClear(D3D12_CPU_DESCRIPTOR_HANDLE view, UINT rect_count, const D3D12_RECT*& rects)
{
   if (!rects || rect_count == 0 || !HasAnyResizedInPlace())
      return;
   const std::optional<InPlaceTarget> target = FindInPlaceTarget(view.ptr);
   if (!target || target->scale == NO_SCALE)
      return;
   thread_local std::vector<D3D12_RECT> scaled;
   scaled.resize(rect_count);
   for (UINT i = 0; i < rect_count; i++)
   {
      const Scaling::Rect rescaled = Scaling::RescaleRect({rects[i].left, rects[i].top, rects[i].right, rects[i].bottom}, target->scale);
      scaled[i] = {rescaled.left, rescaled.top, rescaled.right, rescaled.bottom};
   }
   rects = scaled.data();
   Scaling::CountClearRectRescale();
}
} // namespace

namespace {
bool CopyInPlace(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source, CopyRegion& region)
{
   if (!list || !destination || !source || !HasAnyInPlace())
      return false;
   const InPlaceState* destination_state = FindInPlace(destination);
   const InPlaceState* source_state = FindInPlace(source);
   if (!destination_state && !source_state)
      return false;
   const D3D12_RESOURCE_DESC destination_desc = destination->GetDesc();
   const D3D12_RESOURCE_DESC source_desc = source->GetDesc();
   const Math::float2 destination_scale = GetInPlaceScale(destination_state);
   const Math::float2 source_scale = GetInPlaceScale(source_state);
   const bool compatible = Utils::Format::DXGI::AreCopyCompatible(destination_desc.Format, source_desc.Format);
   if (compatible && destination_scale == source_scale && (!region.whole_resource || destination_desc.MipLevels == source_desc.MipLevels))
   {
      // Both the same size over the game's: the game's region in it
      if (source_scale != NO_SCALE && !region.whole_resource)
      {
         if (region.box)
         {
            const Scaling::Box scaled = Scaling::RescaleBox({region.box->left, region.box->top, region.box->front, region.box->right, region.box->bottom, region.box->back}, source_scale);
            thread_local D3D12_BOX box;
            box = {scaled.left, scaled.top, scaled.front, scaled.right, scaled.bottom, scaled.back};
            region.box = &box;
         }
         region.offset[0] = UINT(std::lround(double(region.offset[0]) * double(source_scale.x)));
         region.offset[1] = UINT(std::lround(double(region.offset[1]) * double(source_scale.y)));
      }
      return false;
   }
   bool converted = false;
   if (list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT && region.convertible)
   {
      if (region.whole_resource)
      {
         // Each mip the game's copy has (an in-place texture may have more)
         const UINT16 levels = std::min(std::max<UINT16>(destination_desc.MipLevels, 1), std::max<UINT16>(source_desc.MipLevels, 1));
         converted = destination_desc.DepthOrArraySize == 1 && source_desc.DepthOrArraySize == 1;
         for (UINT16 level = 0; level < levels && converted; level++)
            converted = ConvertInto(list, destination, level, source, level);
      }
      else if (region.offset == std::array<UINT, 3>{})
      {
         // Only a whole subresource, in the game's size
         const uint32_t level = region.source_subresource % std::max<UINT16>(source_desc.MipLevels, 1);
         const Upgrade::TextureInfo game = source_state ? source_state->original_info : Describe(source_desc, true);
         const bool whole = !region.box || (region.box->left == 0 && region.box->top == 0 && region.box->front == 0 && region.box->right == Upgrade::GetMipSize(game.width, level) && region.box->bottom == Upgrade::GetMipSize(game.height, level) && region.box->back == 1);
         converted = whole && ConvertInto(list, destination, region.destination_subresource, source, region.source_subresource);
      }
   }
   if (converted)
      in_place_converted_copies++;
   else
      LogInPlaceCopy("a copy");
   return true;
}
} // namespace

namespace {
bool CopyInPlaceFootprint(ID3D12GraphicsCommandList* list, const D3D12_TEXTURE_COPY_LOCATION& destination, UINT x, UINT y, UINT z,
   const D3D12_TEXTURE_COPY_LOCATION& source, const D3D12_BOX* box)
{
   if (!list || !HasAnyInPlace())
      return false;
   const bool upload = destination.Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX && source.Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
   const bool readback = source.Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX && destination.Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
   if (!upload && !readback)
      return false;
   const D3D12_TEXTURE_COPY_LOCATION& texture = upload ? destination : source;
   const D3D12_SUBRESOURCE_FOOTPRINT& footprint = (upload ? source : destination).PlacedFootprint.Footprint;
   const InPlaceState* state = FindInPlace(texture.pResource);
   if (!state)
      return false;
   const D3D12_RESOURCE_DESC desc = texture.pResource->GetDesc();
   // Without the spoof the game laid its data out in the upgraded format
   if (!state->resized && Utils::Format::DXGI::AreCopyCompatible(footprint.Format, desc.Format))
      return false;
   const uint32_t level = texture.SubresourceIndex % std::max<UINT16>(desc.MipLevels, 1);
   const uint32_t game_width = Upgrade::GetMipSize(state->original_info.width, level);
   const uint32_t game_height = Upgrade::GetMipSize(state->original_info.height, level);
   bool converted = false;
   if (list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT && desc.DepthOrArraySize == 1)
   {
      const D3D12_TEXTURE_COPY_LOCATION temporary_location = [&] {
         D3D12_TEXTURE_COPY_LOCATION location = {.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0};
         return location;
      }();
      if (upload)
      {
         // The whole subresource only: the game's data into a texture of its format and size, converted and scaled into the upgraded one
         const uint32_t width = box ? box->right - box->left : footprint.Width;
         const uint32_t height = box ? box->bottom - box->top : footprint.Height;
         if (x == 0 && y == 0 && z == 0 && width == game_width && height == game_height)
         {
            if (ID3D12Resource* temporary = CreateTemporary(list, GetTemporaryDesc(footprint.Format, game_width, game_height, D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_COPY_DEST))
            {
               D3D12_TEXTURE_COPY_LOCATION into = temporary_location;
               into.pResource = temporary;
               list->CopyTextureRegion(&into, 0, 0, 0, &source, box);
               const D3D12_RESOURCE_BARRIER to_source = Transition(temporary, 0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
               list->ResourceBarrier(1, &to_source);
               converted = ConvertInto(list, destination.pResource, destination.SubresourceIndex, temporary, 0);
            }
         }
      }
      else if (ID3D12Resource* temporary = CreateTemporary(list, GetTemporaryDesc(footprint.Format, game_width, game_height, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
                  D3D12_RESOURCE_STATE_COPY_DEST))
      {
         // The subresource converted and scaled into a texture of the game's format and size, which the game's copy reads (its box is in its size)
         if (ConvertingCopy(list, temporary, source.pResource, {.convertible = true, .source_subresource = source.SubresourceIndex}, nullptr))
         {
            const D3D12_RESOURCE_BARRIER to_source = Transition(temporary, 0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->ResourceBarrier(1, &to_source);
            D3D12_TEXTURE_COPY_LOCATION from = temporary_location;
            from.pResource = temporary;
            list->CopyTextureRegion(&destination, x, y, z, &from, box);
            converted = true;
         }
      }
   }
   if (converted)
      in_place_converted_copies++;
   else
      LogInPlaceCopy(upload ? "an upload" : "a readback");
   return true;
}
} // namespace

namespace {
bool ResolveInPlace(ID3D12GraphicsCommandList*, ID3D12Resource* destination, ID3D12Resource* source, DXGI_FORMAT& format)
{
   if (!destination || !source || !HasAnyInPlace())
      return false;
   const InPlaceState* source_state = FindInPlace(source);
   if (!source_state && !FindInPlace(destination))
      return false;
   const DXGI_FORMAT destination_format = GetFormat(destination);
   const DXGI_FORMAT source_format = GetFormat(source);
   if (Utils::Format::DXGI::GetTypeless(destination_format) == Utils::Format::DXGI::GetTypeless(source_format) && SameDimensions2D(destination, source))
   {
      format = GetResolveFormat(format, source_state ? source_state->original_info.format : source_format, destination_format);
      return false;
   }
   LogInPlaceCopy("a resolve");
   return true;
}
} // namespace

std::optional<Upgrade::UpgradeInfo> FindUpgrade(ID3D12Resource* resource)
{
   if (const InPlaceState* state = FindInPlace(resource))
      return Upgrade::UpgradeInfo{state->original_info, state->info, Upgrade::Method::InPlace, state->resized};
   if (const ResourceMirrorState* state = FindMirrorState<OwnedResource>(resource))
      return Upgrade::UpgradeInfo{state->original_info, state->info, Upgrade::Method::Mirror,
         state->info.width != state->original_info.width || state->info.height != state->original_info.height};
   return std::nullopt;
}

void SpoofDesc(ID3D12Resource* resource, D3D12_RESOURCE_DESC& desc)
{
   if (const Upgrade::TextureInfo* game = FindSpoofedDesc(resource))
      WriteInPlaceDesc(desc, *game);
}

void SpoofDesc(ID3D12Resource* resource, D3D12_RESOURCE_DESC1& desc)
{
   if (const Upgrade::TextureInfo* game = FindSpoofedDesc(resource))
      WriteInPlaceDesc(desc, *game);
}

void UpgradeResource(ID3D12Device* device, ID3D12Resource* resource, std::optional<D3D12_RESOURCE_STATES> state, const D3D12_CLEAR_VALUE* clear, bool reserved)
{
   if (!resource)
   {
      if (std::exchange(pending_in_place, {}).active)
      {
         static std::atomic<bool> logged = false;
         if (!logged.exchange(true))
            Log::Warning("Mirrors: a texture upgraded in place failed to be created, it was made as the game asked");
      }
      return;
   }
   if (!FinishInPlace(resource))
      CreateMirror(device, resource, state, clear, reserved);
}

void TrackCopiedDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type,
   DescriptorSwapOptions swap)
{
   SwapCopiedDescriptors(device, destination, source, count, type, swap.views, swap.samplers);
   CopyInPlaceTargets(device, destination, source, count, type);
}

Shaders::Database::D3D12::TargetFormats BindRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*& render_targets, BOOL& single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE*& depth_stencil, bool swapping)
{
   Shaders::Database::D3D12::TargetFormats formats;
   if (swapping)
      SwapRenderTargets(list, count, render_targets, single_range, depth_stencil, formats);
   // In-place targets aren't swapped: their formats, and their scale, whether Luma swaps or not
   BindInPlaceTargets(list, count, render_targets, single_range, depth_stencil, formats);
   // What a converting copy binds back: the targets as they're bound now
   RecordRenderTargets(list, count, render_targets, single_range, depth_stencil);
   return formats;
}

void BindViewports(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_VIEWPORT*& viewports)
{
   const D3D12_VIEWPORT* game_viewports = viewports;
   RecordViewports(list, count, viewports);
   if (viewports == game_viewports)
      ScaleInPlaceViewports(list, count, viewports);
}

void BindScissorRects(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_RECT*& rects)
{
   const D3D12_RECT* game_rects = rects;
   RecordScissorRects(list, count, rects);
   if (rects == game_rects)
      ScaleInPlaceScissors(list, count, rects);
}

bool Copy(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, ID3D12Resource*& source, CopyRegion& region, bool swapping)
{
   if (CopyInPlace(list, destination, source, region))
      return true;
   return swapping && SwapCopy(list, destination, source, region);
}

bool CopyFootprint(ID3D12GraphicsCommandList* list, D3D12_TEXTURE_COPY_LOCATION& destination, D3D12_TEXTURE_COPY_LOCATION& source, CopyRegion& region, bool swapping)
{
   if (CopyInPlaceFootprint(list, destination, region.offset[0], region.offset[1], region.offset[2], source, region.box))
      return true;
   return swapping && SwapCopy(list, destination.pResource, source.pResource, region);
}

bool Resolve(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, UINT destination_subresource, ID3D12Resource*& source, UINT source_subresource,
   DXGI_FORMAT& format, bool swapping)
{
   if (ResolveInPlace(list, destination, source, format))
      return true;
   return swapping && SwapResolve(list, destination, destination_subresource, source, source_subresource, format);
}

void PrepareClear(D3D12_CPU_DESCRIPTOR_HANDLE& view, UINT rect_count, const D3D12_RECT*& rects, bool swapping)
{
   ScaleInPlaceClear(view, rect_count, rects);
   if (swapping)
      SwapHandle(view, rect_count, &rects);
}

void PrepareClearUnorderedAccess(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE& gpu, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, ID3D12Resource*& resource,
   UINT rect_count, bool swapping)
{
   CheckInPlaceUnorderedAccessClear(resource, rect_count);
   if (swapping)
      SwapClearUnorderedAccess(list, gpu, cpu, resource);
}

Stats GetStats()
{
   return {entry_count.load(), sampler_count.load(), swapped_copies.load(), direct_writes.load(), converted_copies.load(), unhandled_copies.load(), uav_clears.load(),
      unhandled_clears.load(), layout_resources.load(), reserved_resources.load(), aliasing_barriers.load(), in_place_converted_copies.load(),
      in_place_unhandled_copies.load(), in_place_unasked_placed.load(), in_place_unfit_placed.load()};
}
} // namespace D3D12
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
