#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D11/D3D11MirrorsInternal.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

#include <wrl/client.h>

#include "Hooks/ObjectLifetime.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D11 {
namespace {
using Internal::ContextChainState;
using Internal::OwnedResource;
using Internal::ResourceMirrorState;

constexpr Math::float2 NO_SCALE = {1.f, 1.f};

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
         const bool already_full = count == 1 && current[0].TopLeftX == full.TopLeftX && current[0].TopLeftY == full.TopLeftY && current[0].Width == full.Width &&
            current[0].Height == full.Height && current[0].MinDepth == full.MinDepth && current[0].MaxDepth == full.MaxDepth;
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
            const Scaling::Viewport scaled = Scaling::RescaleViewport(
               {viewports[i].TopLeftX, viewports[i].TopLeftY, viewports[i].Width, viewports[i].Height, viewports[i].MinDepth, viewports[i].MaxDepth}, scale);
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

// Draws into an in-place texture of another size than the game's: the game's viewports and scissors scaled while one is the bound target,
// and the game's again once none is. The game's are read back when Luma first scales them; the game binding new ones makes them its own.
struct InPlaceDrawScaling
{
   static constexpr UINT MAX = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
   Math::float2 viewport_scale = {1.f, 1.f};
   UINT viewport_count = 0;
   std::array<D3D11_VIEWPORT, MAX> game_viewports = {};
   std::array<D3D11_VIEWPORT, MAX> scaled_viewports = {};
   Math::float2 scissor_scale = {1.f, 1.f};
   UINT scissor_count = 0;
   std::array<D3D11_RECT, MAX> game_scissors = {};
   std::array<D3D11_RECT, MAX> scaled_scissors = {};
};

// The first bound render target (else the depth target) that is an in-place texture of another size: its scale
Math::float2 GetBoundInPlaceScale(ID3D11DeviceContext* context)
{
   ComPtr<ID3D11RenderTargetView> targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
   ComPtr<ID3D11DepthStencilView> depth;
   context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets[0].GetAddressOf(), &depth);
   const auto scale_of = [](ID3D11View* view) -> std::optional<Math::float2> {
      if (!view)
         return std::nullopt;
      ComPtr<ID3D11Resource> resource;
      view->GetResource(&resource);
      const InPlaceState* state = FindInPlace(resource.Get());
      return state && state->resized ? std::optional(GetInPlaceScale(state)) : std::nullopt;
   };
   for (const ComPtr<ID3D11RenderTargetView>& target : targets)
   {
      if (const std::optional<Math::float2> scale = scale_of(target.Get()))
         return *scale;
   }
   return scale_of(depth.Get()).value_or(NO_SCALE);
}

void RescaleInPlaceDraw(ID3D11DeviceContext* context)
{
   InPlaceDrawScaling* state = Hooks::Lifetime::GetOrCreateStateCached<InPlaceDrawScaling>(context);
   if (!state)
      return;
   const Math::float2 scale = GetBoundInPlaceScale(context);
   if (scale != state->viewport_scale)
   {
      if (state->viewport_scale == NO_SCALE)
      {
         state->viewport_count = InPlaceDrawScaling::MAX;
         context->RSGetViewports(&state->viewport_count, state->game_viewports.data());
      }
      for (UINT i = 0; i < state->viewport_count; i++)
      {
         const D3D11_VIEWPORT& game = state->game_viewports[i];
         const Scaling::Viewport scaled = Scaling::RescaleViewport({game.TopLeftX, game.TopLeftY, game.Width, game.Height, game.MinDepth, game.MaxDepth}, scale);
         state->scaled_viewports[i] = {scaled.x, scaled.y, scaled.width, scaled.height, scaled.min_depth, scaled.max_depth};
      }
      if (state->viewport_count > 0)
      {
         context->RSSetViewports(state->viewport_count, scale == NO_SCALE ? state->game_viewports.data() : state->scaled_viewports.data());
         Scaling::CountViewportRescale();
      }
      state->viewport_scale = scale;
   }
   if (scale != state->scissor_scale)
   {
      if (state->scissor_scale == NO_SCALE)
      {
         state->scissor_count = InPlaceDrawScaling::MAX;
         context->RSGetScissorRects(&state->scissor_count, state->game_scissors.data());
      }
      for (UINT i = 0; i < state->scissor_count; i++)
      {
         const D3D11_RECT& game = state->game_scissors[i];
         const Scaling::Rect scaled = Scaling::RescaleRect({game.left, game.top, game.right, game.bottom}, scale);
         state->scaled_scissors[i] = {scaled.left, scaled.top, scaled.right, scaled.bottom};
      }
      if (state->scissor_count > 0)
      {
         context->RSSetScissorRects(state->scissor_count, scale == NO_SCALE ? state->game_scissors.data() : state->scaled_scissors.data());
         Scaling::CountScissorRescale();
      }
      state->scissor_scale = scale;
   }
}

// The game's bind replaces what Luma scaled, unless it binds back what Luma had set (a save and restore around its own work)
void OnGameViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports)
{
   InPlaceDrawScaling* state = Hooks::Lifetime::GetOrCreateStateCached<InPlaceDrawScaling>(context);
   if (!state || state->viewport_scale == NO_SCALE)
      return;
   if (viewports && count == state->viewport_count && std::memcmp(viewports, state->scaled_viewports.data(), sizeof(D3D11_VIEWPORT) * count) == 0)
      return;
   state->viewport_scale = NO_SCALE;
}

void OnGameScissors(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects)
{
   InPlaceDrawScaling* state = Hooks::Lifetime::GetOrCreateStateCached<InPlaceDrawScaling>(context);
   if (!state || state->scissor_scale == NO_SCALE)
      return;
   if (rects && count == state->scissor_count && std::memcmp(rects, state->scaled_scissors.data(), sizeof(D3D11_RECT) * count) == 0)
      return;
   state->scissor_scale = NO_SCALE;
}
} // namespace

namespace Internal {
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
      Scaling::ShouldScaleTexture(mirror_info, frame_state,
         {.active = Scaling::IsActiveOnCurrentThread(device.Get()), .from_output_sized_source = source_at_output, .aspect_ratio_pixel_threshold = rules.aspect_ratio_pixel_threshold}))
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
} // namespace Internal

void BindViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports)
{
   if (!Scaling::IsNeeded())
      return;
   if (HasAnyResizedInPlace())
      OnGameViewports(context, count, viewports);
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

void BindScissorRects(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects)
{
   if (!Scaling::IsNeeded())
      return;
   if (HasAnyResizedInPlace())
      OnGameScissors(context, count, rects);
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
   // In-place textures stay resized: whether Luma swaps or not
   if (!is_dispatch && HasAnyResizedInPlace())
      RescaleInPlaceDraw(context);
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
   const Upgrade::FrameState frame_state = Internal::GetFrameState(device.Get());
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
            const std::optional<Upgrade::TextureInfo> info = Internal::DescribeResource(resource);
            if (!info)
               return false;
            Upgrade::TextureInfo target = *info;
            if (seed)
            {
               const std::optional<Upgrade::UpgradeInfo> upgrade =
                  Upgrade::GetUpgradeInfo(*info, frame_state, {.allow_writable_sr = true, .force_format = true, .mirror = true});
               if (!upgrade)
                  return false;
               target = upgrade->upgraded;
            }
            else
            {
               if (!Chains::CanPropagate(source_mirror->info, *info, rules.aspect_ratio_pixel_threshold))
                  return false;
               target.format = source_mirror->info.format;
            }
            if (can_scale_here &&
               Scaling::ShouldScaleTexture(target, frame_state, {.active = true, .from_output_sized_source = true, .aspect_ratio_pixel_threshold = rules.aspect_ratio_pixel_threshold}))
            {
               const Math::uint2 scaled = Scaling::ComputeScaledSize({target.width, target.height}, frame_state.render, frame_state.output);
               target.width = scaled.x;
               target.height = scaled.y;
            }
            const Chains::Origin origin = seed ? Chains::Origin{Chains::OriginKind::Seed, shader_hash} : Chains::Origin{Chains::OriginKind::IndirectChain, shader_hash};
            if (is_immediate)
            {
               if (Internal::MaterializeChainMirror(device.Get(), resource, *info, target, origin))
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
               Internal::QueueDeferredChainMirror(device.Get(), resource, *info, target, origin);
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
   if (InPlaceDrawScaling* state = Hooks::Lifetime::GetState<InPlaceDrawScaling>(context))
      *state = {};
}

std::optional<ContextScalingSnapshot> GetContextScalingSnapshot(ID3D11DeviceContext* context)
{
   if (const ContextChainState* state = Hooks::Lifetime::GetState<ContextChainState>(context))
      return state->scaling;
   return std::nullopt;
}
} // namespace D3D11
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
