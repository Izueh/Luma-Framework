#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/D3D12/D3D12MirrorsInternal.hpp"

#include <array>
#include <atomic>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "FrameState/FrameState.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Log/Log.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Shaders/Shaders.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Format.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D12 {
namespace {
using Internal::OwnedResource;
using Internal::ResourceMirrorState;
using Internal::TrackedResourceState;

constexpr uint64_t RETIRE_PRESENTS = 3;
constexpr uint64_t POOL_TTL_FRAMES = 120;

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
      Internal::BackfillMirrorViews(device, resource, *attached);
   return true;
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
      Upgrade::GetUpgradeInfo(Internal::Describe(desc, true), Internal::GetFrameState(device), {.allow_writable_sr = Chains::GetMode() >= Chains::Mode::Direct});
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

std::optional<Upgrade::TextureInfo> FinishInPlace(ID3D12Resource* resource)
{
   if (!pending_in_place.active)
      return std::nullopt;
   const PendingInPlace pending = std::exchange(pending_in_place, {});
   AttachInPlace(resource, pending.game, pending.info);
   return pending.game;
}

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
   const Upgrade::TextureInfo described = Internal::Describe(desc, properties.Type == D3D12_HEAP_TYPE_DEFAULT);
   const std::optional<Upgrade::UpgradeInfo> upgrade =
      Upgrade::GetUpgradeInfo(described, Internal::GetFrameState(device), {.allow_writable_sr = Chains::GetMode() >= Chains::Mode::Direct, .mirror = true});
   if (!upgrade)
      return;
   const Upgrade::TextureInfo* mirrored = &upgrade->upgraded;
   const bool resized = upgrade->resized;
   if (reserved)
   {
      Internal::reserved_resources++;
      LUMA_UNIMPLEMENTED("Mirrors of D3D12 reserved resources");
      return;
   }
   if (!state)
   {
      Internal::layout_resources++;
      LUMA_UNIMPLEMENTED("Mirrors of D3D12 resources created with an enhanced barrier layout");
      return;
   }
   const Chains::Origin origin = resized ? Chains::Origin{Chains::OriginKind::CustomDimensions} : Chains::Origin{Chains::OriginKind::Rule};
   MaterializeMirror(device, resource, *state, clear, described, *mirrored, origin);
}
} // namespace

namespace Internal {
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
   const FrameState::Resolutions resolutions = FrameState::Read(FrameState::Find(device));
   return {.render = resolutions.render, .output = resolutions.output, .display = resolutions.display};
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
} // namespace Internal

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
      const Upgrade::TextureInfo info = Internal::Describe(resource->GetDesc(), true);
      return MirrorSummary{.original_info = info, .mirror_info = info, .pending = true};
   }
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
   Internal::ReleaseDescriptorPools(device);
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
      Internal::EraseDescriptorHandlesForOriginal(original);
   }
   if (!newly_retired.empty())
   {
      std::scoped_lock lock(dev->registry->mutex);
      for (RetiredD3D12Mirror& entry : newly_retired)
         dev->registry->retired.push_back(std::move(entry));
   }
}

void FlushFrame(ID3D12Device* device)
{
   if (!device)
      return;
   const FrameState::Resolutions res = FrameState::Get(device);
   if (Scaling::IsNeeded() && Scaling::ConsumeResolutionChange(device, res.render, res.output))
      InvalidateScaledMirrors(device);
   Scaling::ResetFrame(device);

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
         if (GetInPlaceUpgrade(device, desc) && Internal::in_place_unasked_placed++ == 0)
            Log::Warning("Mirrors: a placed texture the rules upgrade in place was created without asking its size (GetResourceAllocationInfo): it gets a mirror");
         return false;
      }
      const D3D12_HEAP_DESC heap = creation.heap->GetDesc();
      if (heap.Properties.Type != D3D12_HEAP_TYPE_DEFAULT || (heap.Flags & D3D12_HEAP_FLAG_SHARED))
         return false;
      const D3D12_RESOURCE_ALLOCATION_INFO allocation = device->GetResourceAllocationInfo(0, 1, &upgrade->desc);
      if (allocation.SizeInBytes == UINT64_MAX || (allocation.Alignment != 0 && creation.offset % allocation.Alignment != 0) || creation.offset > heap.SizeInBytes ||
         allocation.SizeInBytes > heap.SizeInBytes - creation.offset)
      {
         if (Internal::in_place_unfit_placed++ == 0)
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

Stats GetStats()
{
   return {Internal::entry_count.load(), Internal::sampler_count.load(), Internal::swapped_copies.load(), Internal::direct_writes.load(), Internal::converted_copies.load(),
      Internal::unhandled_copies.load(), Internal::uav_clears.load(), Internal::unhandled_clears.load(), Internal::layout_resources.load(),
      Internal::reserved_resources.load(), Internal::aliasing_barriers.load(), Internal::in_place_converted_copies.load(), Internal::in_place_unhandled_copies.load(),
      Internal::in_place_unasked_placed.load(), Internal::in_place_unfit_placed.load()};
}
} // namespace D3D12
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
