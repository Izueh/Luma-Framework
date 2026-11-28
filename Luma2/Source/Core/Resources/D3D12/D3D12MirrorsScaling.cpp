#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/D3D12/D3D12MirrorsInternal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <span>
#include <vector>

#include <wrl/client.h>

#include "Draw/D3D12/D3D12Draw.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D12 {
namespace {
using Internal::BarrierTransition;
using Internal::InPlaceTarget;
using Internal::ListState;
using Internal::NO_SCALE;
using Internal::OwnedResource;
using Internal::ResourceMirrorState;
using Internal::Slot;
using Internal::TrackedResourceState;

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
   Draw::D3D12::RecordViewports(list, count, viewports.data());
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
   Draw::D3D12::RecordScissorRects(list, count, rects.data());
}

void SwapRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*& render_targets, BOOL& single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE*& depth_stencil, Shaders::Database::D3D12::TargetFormats& formats)
{
   formats = {};
   ListState* list_state = list ? Hooks::Lifetime::GetOrCreateStateCached<ListState>(list) : nullptr;
   if (list_state)
   {
      list_state->game_render_target_count = 0;
      list_state->target_scale = {1.f, 1.f};
      list_state->target_is_scaled = false;
      list_state->rtv0_size = {};
   }
   if (depth_stencil && Internal::entry_count.load(std::memory_order_relaxed) != 0)
   {
      if (const std::shared_ptr<Slot> slot = Internal::FindSlot(depth_stencil->ptr))
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
   const uint64_t increment = Internal::GetCachedIncrement(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
   bool any = false;
   count = std::min<UINT>(count, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
   if (list_state)
      list_state->game_render_target_count = count;
   for (UINT i = 0; i < count; i++)
   {
      const D3D12_CPU_DESCRIPTOR_HANDLE game_handle = single_range ? D3D12_CPU_DESCRIPTOR_HANDLE{SIZE_T(render_targets->ptr + i * increment)} : render_targets[i];
      if (list_state)
         list_state->game_render_targets[i] = game_handle;
      swapped[i] = game_handle;
      if (const std::shared_ptr<Slot> slot = Internal::FindSlot(swapped[i].ptr))
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
         if (ID3D12Resource* resource = Internal::FindUnmirroredViewResource(game_handle.ptr))
         {
            const D3D12_RESOURCE_DESC d = resource->GetDesc();
            list_state->rtv0_size = {uint32_t(d.Width), d.Height};
         }
      }
   }
   if (!any)
      return;
   render_targets = swapped.data();
   single_range = FALSE;
}

void RecordViewports(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_VIEWPORT*& viewports)
{
   ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
   if (!state || !viewports)
      return;
   const UINT clamped = std::min<UINT>(count, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
   state->scaling.viewports_scaled = false;
   state->scaling.game_viewport_count = clamped;
   std::copy_n(viewports, clamped, state->scaling.game_viewports.begin());

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
      const bool already_full = clamped == 1 && viewports[0].TopLeftX == full.TopLeftX && viewports[0].TopLeftY == full.TopLeftY && viewports[0].Width == full.Width &&
         viewports[0].Height == full.Height && viewports[0].MinDepth == full.MinDepth && viewports[0].MaxDepth == full.MaxDepth;
      if (!already_full)
      {
         scaled[0] = full;
         count = 1;
         viewports = scaled.data();
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
      }
      viewports = scaled.data();
      state->scaling.viewports_scaled = true;
      Scaling::CountViewportRescale();
   }
}

void RecordScissorRects(ID3D12GraphicsCommandList* list, UINT& count, const D3D12_RECT*& rects)
{
   ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
   if (!state || !rects)
      return;
   const UINT clamped = std::min<UINT>(count, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
   state->scaling.scissors_scaled = false;
   state->scaling.game_scissor_count = clamped;
   std::copy_n(rects, clamped, state->scaling.game_scissors.begin());

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
      const bool already_full = clamped == 1 && rects[0].left == full.left && rects[0].top == full.top && rects[0].right == full.right && rects[0].bottom == full.bottom;
      if (!already_full)
      {
         scaled[0] = full;
         count = 1;
         rects = scaled.data();
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
      }
      rects = scaled.data();
      state->scaling.scissors_scaled = true;
      Scaling::CountScissorRescale();
   }
}

void BindInPlaceTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil, Shaders::Database::D3D12::TargetFormats& formats)
{
   if (!list || !HasAnyInPlace())
      return;
   Math::float2 scale = NO_SCALE;
   if (Internal::HasInPlaceTargets())
   {
      uint64_t increment = 0;
      if (render_targets && single_range && count > 1)
      {
         ComPtr<ID3D12Device> device;
         if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))))
            increment = Internal::GetIncrement(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
      }
      count = render_targets ? std::min<UINT>(count, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) : 0;
      for (UINT i = 0; i < count; i++)
      {
         const SIZE_T handle = single_range ? SIZE_T(render_targets->ptr + i * increment) : render_targets[i].ptr;
         if (single_range && i > 0 && increment == 0)
            break;
         if (const std::optional<InPlaceTarget> target = Internal::FindInPlaceTarget(handle))
         {
            if (formats.render_targets[i] == DXGI_FORMAT_UNKNOWN)
               formats.render_targets[i] = target->format;
            if (scale == NO_SCALE)
               scale = target->scale;
         }
      }
      if (depth_stencil)
      {
         if (const std::optional<InPlaceTarget> target = Internal::FindInPlaceTarget(depth_stencil->ptr))
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

namespace Internal {
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
      Scaling::ShouldScaleTexture(mirror_info, frame_state,
         {.active = Scaling::IsActiveOnCurrentThread(device.Get()), .from_output_sized_source = source_at_output, .aspect_ratio_pixel_threshold = rules.aspect_ratio_pixel_threshold}))
   {
      const Math::uint2 scaled = Scaling::ComputeScaledSize({mirror_info.width, mirror_info.height}, frame_state.render, frame_state.output);
      mirror_info.width = scaled.x;
      mirror_info.height = scaled.y;
   }
   QueueChainMirror(device.Get(), destination, dst_info, mirror_info, {.kind = Chains::OriginKind::DirectChain});
}
} // namespace Internal

std::optional<ListScalingSnapshot> GetListScalingSnapshot(ID3D12GraphicsCommandList* list)
{
   if (const ListState* state = Hooks::Lifetime::GetState<ListState>(list))
      return state->scaling;
   return std::nullopt;
}

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
            Internal::aliasing_barriers++;
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

void ForgetListState(ID3D12GraphicsCommandList* list)
{
   if (ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list))
      *state = {};
}

Shaders::Database::D3D12::TargetFormats BindRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*& render_targets, BOOL& single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE*& depth_stencil, bool swapping)
{
   Shaders::Database::D3D12::TargetFormats formats;
   if (swapping)
      SwapRenderTargets(list, count, render_targets, single_range, depth_stencil, formats);
   // In-place targets aren't swapped: their formats, and their scale, whether Luma swaps or not
   BindInPlaceTargets(list, count, render_targets, single_range, depth_stencil, formats);
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
         const Upgrade::FrameState frame_state = Internal::GetFrameState(device.Get());
         const Upgrade::Rules& rules = Upgrade::GetRules();
         const bool can_scale = scaling_active && seed->scale && Scaling::IsActiveOnCurrentThread(device.Get());
         for (uint8_t slot : seed->rtv_slots)
         {
            if (slot >= state->game_render_target_count)
               continue;
            const uint64_t handle = state->game_render_targets[slot].ptr;
            if (!handle)
               continue;
            ID3D12Resource* resource = Internal::FindUnmirroredViewResource(handle);
            if (!resource || FindMirrorState<OwnedResource>(resource) || FindInPlace(resource))
               continue;
            D3D12_HEAP_PROPERTIES props = {D3D12_HEAP_TYPE_DEFAULT};
            resource->GetHeapProperties(&props, nullptr);
            const Upgrade::TextureInfo info = Internal::Describe(resource->GetDesc(), props.Type == D3D12_HEAP_TYPE_DEFAULT);
            const std::optional<Upgrade::UpgradeInfo> upgrade =
               Upgrade::GetUpgradeInfo(info, frame_state, {.allow_writable_sr = true, .force_format = true, .mirror = true});
            if (!upgrade)
               continue;
            Upgrade::TextureInfo target = upgrade->upgraded;
            if (can_scale &&
               Scaling::ShouldScaleTexture(target, frame_state, {.active = true, .from_output_sized_source = true, .aspect_ratio_pixel_threshold = rules.aspect_ratio_pixel_threshold}))
            {
               const Math::uint2 scaled = Scaling::ComputeScaledSize({target.width, target.height}, frame_state.render, frame_state.output);
               target.width = scaled.x;
               target.height = scaled.y;
            }
            Internal::QueueChainMirror(device.Get(), resource, info, target, {Chains::OriginKind::Seed, shader_hash});
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
         if (Scaling::GetOptions().scale_viewports && !state->scaling.viewports_scaled && state->scaling.game_viewport_count > 0)
         {
            const D3D12_VIEWPORT full = {0.f, 0.f, float(target.x), float(target.y), 0.f, 1.f};
            list->RSSetViewports(1, &full);
            Draw::D3D12::RecordViewports(list, 1, &full);
            state->scaling.viewports_scaled = true;
            Scaling::CountViewportRescale();
         }
         if (Scaling::GetOptions().scale_scissors && !state->scaling.scissors_scaled && state->scaling.game_scissor_count > 0)
         {
            const D3D12_RECT full = {0, 0, LONG(target.x), LONG(target.y)};
            list->RSSetScissorRects(1, &full);
            Draw::D3D12::RecordScissorRects(list, 1, &full);
            state->scaling.scissors_scaled = true;
            Scaling::CountScissorRescale();
         }
      }
   }
   else if (state->target_is_scaled)
   {
      Hooks::Scope::Guard scope;
      if (Scaling::GetOptions().scale_viewports && !state->scaling.viewports_scaled && state->scaling.game_viewport_count > 0)
      {
         std::array<D3D12_VIEWPORT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scaled = {};
         for (UINT i = 0; i < state->scaling.game_viewport_count; i++)
         {
            const D3D12_VIEWPORT& game = state->scaling.game_viewports[i];
            const Scaling::Viewport r =
               Scaling::RescaleViewport({game.TopLeftX, game.TopLeftY, game.Width, game.Height, game.MinDepth, game.MaxDepth}, state->target_scale);
            scaled[i] = {r.x, r.y, r.width, r.height, r.min_depth, r.max_depth};
         }
         list->RSSetViewports(state->scaling.game_viewport_count, scaled.data());
         Draw::D3D12::RecordViewports(list, state->scaling.game_viewport_count, scaled.data());
         state->scaling.viewports_scaled = true;
         Scaling::CountViewportRescale();
      }
      if (Scaling::GetOptions().scale_scissors && !state->scaling.scissors_scaled && state->scaling.game_scissor_count > 0)
      {
         std::array<D3D12_RECT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scaled = {};
         for (UINT i = 0; i < state->scaling.game_scissor_count; i++)
         {
            const D3D12_RECT& game = state->scaling.game_scissors[i];
            const Scaling::Rect r = Scaling::RescaleRect({game.left, game.top, game.right, game.bottom}, state->target_scale);
            scaled[i] = {r.left, r.top, r.right, r.bottom};
         }
         list->RSSetScissorRects(state->scaling.game_scissor_count, scaled.data());
         Draw::D3D12::RecordScissorRects(list, state->scaling.game_scissor_count, scaled.data());
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
} // namespace D3D12
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
