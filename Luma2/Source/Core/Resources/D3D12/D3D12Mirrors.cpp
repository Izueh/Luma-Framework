#include "Resources/D3D12/D3D12Mirrors.hpp"

#include <algorithm>
#include <atomic>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

#include <wrl/client.h>

#include "Frame/Frame.hpp"
#include "Hooks/Lifetime.hpp"
#include "Hooks/Scope.hpp"
#include "Resources/Mirrors.hpp"
#include "Resources/Samplers.hpp"
#include "Resources/Upgrade.hpp"
#include "Shaders/Shaders.hpp"
#include "Utils/Debug.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Utils/Format.hpp"
#include "Utils/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D12 {
namespace {
// Luma's CPU heaps grow by this many descriptors
constexpr uint32_t POOL_CHUNK = 4096;

std::atomic<uint32_t> entry_count = 0;
std::atomic<uint32_t> sampler_count = 0;
std::atomic<uint32_t> swapped_copies = 0;
std::atomic<uint32_t> direct_writes = 0;
std::atomic<uint32_t> unhandled_copies = 0;
std::atomic<uint32_t> unhandled_clears = 0;
std::atomic<uint32_t> layout_resources = 0;
std::atomic<uint32_t> aliasing_barriers = 0;
std::array<std::atomic<uint32_t>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> increments = {};

using OwnedResource = ComPtr<ID3D12Resource>;
using ResourceMirrorState = MirrorState<ID3D12Resource, OwnedResource>;

// Luma's CPU descriptor heaps of a device, per type, in its private data. A slot goes back to its pool when no game handle holds it.
struct Pool
{
   std::vector<ComPtr<ID3D12DescriptorHeap>> heaps;
   std::vector<uint64_t> free;
   uint32_t used_in_last = POOL_CHUNK;
   uint32_t increment = 0;
};

struct Pools
{
   std::mutex mutex; // After the handles' lock
   std::array<Pool, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> pools;
   bool released = false;
};

struct DeviceState
{
   std::shared_ptr<Pools> pools = std::make_shared<Pools>();
};

// One of Luma's descriptors, shared by every game handle that holds it (copies)
struct Slot
{
   std::shared_ptr<Pools> pools;
   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   uint64_t handle = 0;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; // A view's
   bool sampler = false;
   // Samplers: the game's desc and the offset generation the mirror was written for
   D3D12_SAMPLER_DESC game_sampler = {};
   uint32_t generation = 0;
   ID3D12Device* device = nullptr; // Not held: the pools hold it

   ~Slot()
   {
      std::scoped_lock lock(pools->mutex);
      if (!pools->released)
         pools->pools[type].free.push_back(handle);
      if (sampler)
         sampler_count--;
      else
         CountView(-1);
   }
};

// Game CPU handles (not shader-visible ones: those are never copied from) to the mirror's descriptor they stand for
std::shared_mutex handles_mutex; // Before a pool's lock
std::map<uint64_t, std::shared_ptr<Slot>> handles;

struct HeapRange
{
   uint64_t start = 0;
   uint64_t end = 0;
   bool shader_visible = false;
};

std::shared_mutex heaps_mutex; // Never held with another
std::vector<HeapRange> heaps; // By start

// In the game's heap's private data: its handles are forgotten with it
struct HeapState
{
   HeapRange range;

   ~HeapState()
   {
      {
         std::scoped_lock lock(heaps_mutex);
         std::erase_if(heaps, [&](const HeapRange& other) { return other.start == range.start; });
      }
      if (entry_count.load(std::memory_order_relaxed) == 0)
         return;
      std::unique_lock lock(handles_mutex);
      const auto first = handles.lower_bound(range.start);
      const auto last = handles.lower_bound(range.end);
      entry_count -= uint32_t(std::distance(first, last));
      handles.erase(first, last);
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

uint32_t GetIncrement(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   uint32_t increment = increments[type].load(std::memory_order_relaxed);
   if (!increment)
   {
      increment = device->GetDescriptorHandleIncrementSize(type);
      increments[type] = increment;
   }
   return increment;
}

std::shared_ptr<Slot> Allocate(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      state = Hooks::Lifetime::SetState<DeviceState>(device);
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
   Upgrade::TextureInfo described = {.width = uint32_t(desc.Width),
      .height = desc.Height,
      .depth_or_layers = desc.DepthOrArraySize,
      .levels = desc.MipLevels,
      .samples = desc.SampleDesc.Count,
      .format = desc.Format,
      .render_target = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0,
      .unordered_access = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0,
      .depth_stencil = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0,
      .gpu_only = gpu_only};
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
   const uint32_t generation = Samplers::GetGeneration();
   if (slot.generation == generation)
      return;
   slot.generation = generation;
   const D3D12_SAMPLER_DESC mirror = Samplers::GetMirrorDesc(slot.game_sampler).value_or(slot.game_sampler);
   Hooks::Scope::Guard scope;
   slot.device->CreateSampler(&mirror, {SIZE_T(slot.handle)});
}

void Forget(uint64_t handle)
{
   if (entry_count.load(std::memory_order_relaxed) == 0)
      return;
   std::unique_lock lock(handles_mutex);
   if (handles.erase(handle))
      entry_count--;
}

void Remember(uint64_t handle, std::shared_ptr<Slot> slot)
{
   std::unique_lock lock(handles_mutex);
   auto [it, added] = handles.insert_or_assign(handle, std::move(slot));
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

DXGI_FORMAT GetFormat(ID3D12Resource* resource)
{
   return resource->GetDesc().Format;
}
} // namespace

void CreateMirror(ID3D12Device* device, ID3D12Resource* resource, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear)
{
   const D3D12_RESOURCE_DESC desc = resource->GetDesc();
   if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER || desc.Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
      return;
   // Placed resources have no heap properties of their own: they're GPU only unless their heap says otherwise, which nothing renders into
   D3D12_HEAP_PROPERTIES properties = {D3D12_HEAP_TYPE_DEFAULT};
   resource->GetHeapProperties(&properties, nullptr);
   const Upgrade::TextureInfo described = Describe(desc, properties.Type == D3D12_HEAP_TYPE_DEFAULT);
   const std::optional<Upgrade::TextureInfo> mirrored = Upgrade::Match(described, GetFrameState(device), false);
   if (!mirrored)
      return;
   D3D12_RESOURCE_DESC mirror_desc = desc;
   mirror_desc.Format = mirrored->format;
   mirror_desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
   std::optional<D3D12_CLEAR_VALUE> mirror_clear;
   if (clear && !mirrored->depth_stencil)
   {
      mirror_clear = *clear;
      mirror_clear->Format = Utils::Format::DXGI::IsTypeless(mirrored->format) ? Utils::Format::DXGI::GetDefaultTyped(mirrored->format) : mirrored->format;
   }
   const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
   ComPtr<ID3D12Resource> mirror;
   HRESULT result = E_FAIL;
   {
      Hooks::Scope::Guard scope;
      result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &mirror_desc, state, mirror_clear ? &*mirror_clear : nullptr, IID_PPV_ARGS(&mirror));
   }
   if (FAILED(result) || !mirror)
      return LogCreationFailure(std::format("a {} mirror of a {} texture", Utils::Format::GetName(mirrored->format), Utils::Format::GetName(desc.Format)), result);
   AttachMirror(resource, std::move(mirror), described, *mirrored);
}

ID3D12Resource* GetMirror(ID3D12Resource* resource)
{
   return FindMirror<OwnedResource>(resource);
}

ID3D12Resource* GetOriginal(ID3D12Resource* mirror)
{
   return FindOriginal(mirror);
}

void ReleaseDeviceObjects(ID3D12Device* device)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      return;
   std::scoped_lock lock(state->pools->mutex);
   state->pools->released = true;
   for (Pool& pool : state->pools->pools)
   {
      pool.heaps.clear();
      pool.free.clear();
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

void OnCreateView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, ViewKind kind, ID3D12Resource* resource, const void* desc, bool swapping)
{
   const ResourceMirrorState* state = kind != ViewKind::ConstantBuffer ? FindMirrorState<OwnedResource>(resource) : nullptr;
   if (!state)
      return Forget(destination.ptr);
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
   CountView(1);
   Remember(destination.ptr, std::move(slot));
}

void OnCreateSampler(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, const D3D12_SAMPLER_DESC& desc, bool swapping)
{
   const std::optional<D3D12_SAMPLER_DESC> mirror = Samplers::GetMirrorDesc(desc);
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
   slot->generation = Samplers::GetGeneration();
   sampler_count++;
   {
      Hooks::Scope::Guard scope;
      device->CreateSampler(&*mirror, {SIZE_T(slot->handle)});
   }
   Remember(destination.ptr, std::move(slot));
}

void OnCopyDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type,
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
      // A shader-visible heap is never copied from: only what it shows matters
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

std::span<const D3D12_RESOURCE_BARRIER> AddMirrorBarriers(std::span<const D3D12_RESOURCE_BARRIER> barriers)
{
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
         if ((mirrored.Transition.pResource = GetMirror(barrier.Transition.pResource)))
            extended.push_back(mirrored);
         break;
      case D3D12_RESOURCE_BARRIER_TYPE_UAV:
         if ((mirrored.UAV.pResource = GetMirror(barrier.UAV.pResource)))
            extended.push_back(mirrored);
         break;
      default:
         // Mirrors are committed: they alias nothing
         if (GetMirror(barrier.Aliasing.pResourceBefore) || GetMirror(barrier.Aliasing.pResourceAfter))
            aliasing_barriers++;
         break;
      }
   }
   if (extended.empty())
      return barriers;
   extended.insert(extended.begin(), barriers.begin(), barriers.end());
   return extended;
}

void SwapRenderTargets(UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*& render_targets, BOOL& single_range, const D3D12_CPU_DESCRIPTOR_HANDLE*& depth_stencil,
   std::array<DXGI_FORMAT, 8>& formats)
{
   formats.fill(DXGI_FORMAT_UNKNOWN);
   if (entry_count.load(std::memory_order_relaxed) == 0)
      return;
   if (depth_stencil && Find(depth_stencil->ptr))
      LUMA_UNIMPLEMENTED("Depth mirrors on D3D12 (a pipeline variant by DSVFormat)");
   if (!render_targets)
      return;
   thread_local std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> swapped;
   const uint64_t increment = increments[D3D12_DESCRIPTOR_HEAP_TYPE_RTV].load(std::memory_order_relaxed);
   bool any = false;
   count = std::min<UINT>(count, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
   for (UINT i = 0; i < count; i++)
   {
      swapped[i] = single_range ? D3D12_CPU_DESCRIPTOR_HANDLE{SIZE_T(render_targets->ptr + i * increment)} : render_targets[i];
      if (const std::shared_ptr<Slot> slot = Find(swapped[i].ptr))
      {
         swapped[i].ptr = SIZE_T(slot->handle);
         formats[i] = slot->format;
         any = true;
      }
   }
   if (!any)
      return;
   render_targets = swapped.data();
   single_range = FALSE;
}

bool SwapHandle(D3D12_CPU_DESCRIPTOR_HANDLE& handle)
{
   const std::shared_ptr<Slot> slot = Find(handle.ptr);
   if (!slot || slot->sampler)
      return false;
   handle.ptr = SIZE_T(slot->handle);
   return true;
}

void SwapCopy(ID3D12Resource*& destination, ID3D12Resource*& source)
{
   const CopyPlan<ID3D12Resource> plan = PlanCopy(destination, source, &GetMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return;
   if (plan.action == CopyAction::DirectMirrorCopy)
   {
      destination = plan.destination;
      source = plan.source;
      return;
   }
   if (!source)
      return;
   if (unhandled_copies++ == 0)
      Utils::Log::Warning("Mirrors: a copy between a {} mirror and a {} texture without one needs Luma's copy pass on D3D12, the game's textures are copied",
         Utils::Format::GetName(GetFormat(plan.destination_mirrored ? plan.destination : plan.source)),
         Utils::Format::GetName(GetFormat(plan.destination_mirrored ? source : destination)));
   LUMA_UNIMPLEMENTED("Converting copies between a mirror and a texture without one on D3D12 (Luma's copy pass, Step 5)");
}

void OnClearUnorderedAccess(ID3D12Resource* resource)
{
   if (!GetMirror(resource))
      return;
   unhandled_clears++;
   LUMA_UNIMPLEMENTED("UAV clears of mirrored textures on D3D12 (the mirror's view in the heap tail)");
}

Stats GetStats()
{
   return {entry_count.load(), sampler_count.load(), swapped_copies.load(), direct_writes.load(), unhandled_copies.load(), unhandled_clears.load(),
      layout_resources.load(), aliasing_barriers.load()};
}
} // namespace D3D12
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
