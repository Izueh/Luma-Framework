#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/D3D12/D3D12MirrorsInternal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Utils/Debug.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D12 {
namespace Internal {
// Luma's CPU heaps grow by this many descriptors
constexpr uint32_t POOL_CHUNK = 4096;

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

Slot::~Slot()
{
   std::scoped_lock lock(pools->mutex);
   if (!pools->released)
      pools->pools[type].free.push_back(handle);
   if (sampler)
      sampler_count--;
   else if (counted)
      CountView(-1);
}
} // namespace Internal

namespace {
using Internal::InPlaceTarget;
using Internal::OwnedResource;
using Internal::Pool;
using Internal::POOL_CHUNK;
using Internal::Pools;
using Internal::ResourceMirrorState;
using Internal::Slot;

struct DescriptorPoolState
{
   std::shared_ptr<Pools> pools = std::make_shared<Pools>();
};

DescriptorPoolState* GetOrCreatePoolState(ID3D12Device* device)
{
   if (!device)
      return nullptr;
   if (DescriptorPoolState* state = Hooks::Lifetime::GetState<DescriptorPoolState>(device))
      return state;
   return Hooks::Lifetime::SetState<DescriptorPoolState>(device);
}

std::array<std::atomic<uint32_t>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> increments = {};

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

std::shared_mutex in_place_targets_mutex; // Never held with another
std::map<SIZE_T, InPlaceTarget> in_place_targets;
std::atomic<size_t> in_place_target_count = 0;

void EraseInPlaceTargets(SIZE_T start, SIZE_T end)
{
   if (in_place_target_count.load(std::memory_order_relaxed) == 0)
      return;
   std::unique_lock lock(in_place_targets_mutex);
   in_place_targets.erase(in_place_targets.lower_bound(start), in_place_targets.lower_bound(end));
   in_place_target_count = in_place_targets.size();
}

struct HeapRange
{
   uint64_t start = 0;
   uint64_t end = 0;
   bool shader_visible = false;
};

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
            Internal::entry_count -= uint32_t(std::distance(first, last));
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
   DescriptorPoolState* state = GetOrCreatePoolState(device);
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
      pool.increment = Internal::GetIncrement(device, type);
   }
   slot->handle = pool.heaps.back()->GetCPUDescriptorHandleForHeapStart().ptr + uint64_t(pool.used_in_last++) * pool.increment;
   return slot;
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
   if (Internal::entry_count.load(std::memory_order_relaxed) == 0)
      return;
   std::unique_lock lock(handles_mutex);
   if (const auto it = handles.find(handle); it != handles.end())
   {
      if (it->second)
         mirror_to_original_handle.erase(it->second->handle);
      handles.erase(it);
      Internal::entry_count--;
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
   Internal::entry_count += added;
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

void SwapCopiedDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type,
   bool swap_views, bool swap_samplers)
{
   if (Internal::entry_count.load(std::memory_order_relaxed) == 0 || !count)
      return;
   const uint64_t increment = Internal::GetIncrement(device, type);
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
         Internal::swapped_copies++;
      }
      copied.clear();
      return;
   }
   std::unique_lock lock(handles_mutex);
   const auto first = handles.lower_bound(destination.ptr);
   const auto last = handles.lower_bound(destination_end);
   Internal::entry_count -= uint32_t(std::distance(first, last));
   handles.erase(first, last);
   for (auto& [handle, slot] : copied)
      handles.emplace(handle, std::move(slot));
   Internal::entry_count += uint32_t(copied.size());
   copied.clear();
}

void CopyInPlaceTargets(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   if ((type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV && type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV) || in_place_target_count.load(std::memory_order_relaxed) == 0 || !device)
      return;
   const uint64_t increment = Internal::GetIncrement(device, type);
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

namespace Internal {
uint32_t GetIncrement(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   if (uint32_t cached = increments[type].load(std::memory_order_relaxed))
      return cached;
   const uint32_t size = device->GetDescriptorHandleIncrementSize(type);
   increments[type].store(size, std::memory_order_relaxed);
   return size;
}

uint32_t GetCachedIncrement(D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   return increments[type].load(std::memory_order_relaxed);
}

std::shared_ptr<Slot> FindSlot(uint64_t handle)
{
   if (entry_count.load(std::memory_order_relaxed) == 0)
      return nullptr;
   std::shared_lock lock(handles_mutex);
   const auto it = handles.find(handle);
   return it != handles.end() ? it->second : nullptr;
}

void ReleaseDescriptorPools(ID3D12Device* device)
{
   if (DescriptorPoolState* state = Hooks::Lifetime::GetState<DescriptorPoolState>(device))
   {
      std::scoped_lock lock(state->pools->mutex);
      state->pools->released = true;
      for (Pool& pool : state->pools->pools)
      {
         pool.heaps.clear();
         pool.free.clear();
      }
   }
}

void EraseDescriptorHandlesForOriginal(ID3D12Resource* original)
{
   // Erase stale descriptor mappings referencing this original so next creation/backfill refreshes them
   std::unique_lock lock(handles_mutex);
   for (auto it = handles.begin(); it != handles.end();)
   {
      if (it->second && it->second->original_resource == original)
      {
         mirror_to_original_handle.erase(it->second->handle);
         it = handles.erase(it);
         entry_count--;
      }
      else
      {
         ++it;
      }
   }
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

ID3D12Resource* FindUnmirroredViewResource(uint64_t handle)
{
   std::scoped_lock lock(unmirrored_views_mutex);
   if (const auto it = unmirrored_by_handle.find(handle); it != unmirrored_by_handle.end())
      return it->second.resource;
   return nullptr;
}

std::optional<InPlaceTarget> FindInPlaceTarget(SIZE_T handle)
{
   if (in_place_target_count.load(std::memory_order_relaxed) == 0)
      return std::nullopt;
   std::shared_lock lock(in_place_targets_mutex);
   const auto it = in_place_targets.find(handle);
   return it != in_place_targets.end() ? std::optional(it->second) : std::nullopt;
}

bool HasInPlaceTargets()
{
   return in_place_target_count.load(std::memory_order_relaxed) != 0;
}
} // namespace Internal

D3D12_CPU_DESCRIPTOR_HANDLE FindMirrorHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
   if (!handle.ptr)
      return {0};
   if (const std::shared_ptr<Slot> slot = Internal::FindSlot(handle.ptr); slot && !slot->sampler)
      return {SIZE_T(slot->handle)};
   return {0};
}

D3D12_CPU_DESCRIPTOR_HANDLE FindOriginalHandle(D3D12_CPU_DESCRIPTOR_HANDLE mirror_handle)
{
   if (!mirror_handle.ptr || Internal::entry_count.load(std::memory_order_relaxed) == 0)
      return {0};
   std::shared_lock lock(handles_mutex);
   const auto it = mirror_to_original_handle.find(mirror_handle.ptr);
   return it != mirror_to_original_handle.end() ? D3D12_CPU_DESCRIPTOR_HANDLE{SIZE_T(it->second)} : D3D12_CPU_DESCRIPTOR_HANDLE{0};
}

ID3D12Resource* GetDescriptorResource(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
   if (!handle.ptr)
      return nullptr;
   if (Internal::entry_count.load(std::memory_order_relaxed) != 0)
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
   if (ID3D12Resource* resource = Internal::FindUnmirroredViewResource(handle.ptr))
      return resource;
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
   if (Internal::entry_count.load(std::memory_order_relaxed) != 0)
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

void TrackHeap(ID3D12Device* device, ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC& desc)
{
   const uint64_t start = heap->GetCPUDescriptorHandleForHeapStart().ptr;
   const HeapRange range = {start, start + uint64_t(desc.NumDescriptors) * Internal::GetIncrement(device, desc.Type),
      (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0};
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
         if (Internal::direct_writes++ == 0)
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
         Internal::direct_writes++;
      }
      return;
   }
   std::shared_ptr<Slot> slot = Allocate(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
   if (!slot)
      return Forget(destination.ptr);
   slot->sampler = true;
   slot->game_sampler = desc;
   slot->generation = Samplers::GetGeneration(device);
   Internal::sampler_count++;
   {
      Hooks::Scope::Guard scope;
      device->CreateSampler(&*mirror, {SIZE_T(slot->handle)});
   }
   Remember(destination.ptr, std::move(slot));
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

void TrackCopiedDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type,
   DescriptorSwapOptions swap)
{
   SwapCopiedDescriptors(device, destination, source, count, type, swap.views, swap.samplers);
   CopyInPlaceTargets(device, destination, source, count, type);
}
} // namespace D3D12
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
