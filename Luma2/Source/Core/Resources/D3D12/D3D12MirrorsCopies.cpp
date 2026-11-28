#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/D3D12/D3D12MirrorsInternal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "Draw/D3D12/D3D12Draw.hpp"
#include "Hooks/HookScope.hpp"
#include "Log/Log.hpp"
#include "Resources/D3D12/D3D12HeapTail.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Format.hpp"

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D12 {
namespace {
using Internal::InPlaceTarget;
using Internal::NO_SCALE;
using Internal::OwnedResource;
using Internal::ResourceMirrorState;
using Internal::Slot;

DXGI_FORMAT GetFormat(ID3D12Resource* resource)
{
   return resource->GetDesc().Format;
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

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, UINT subresource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
   D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
   barrier.Transition = {resource, subresource, before, after};
   return barrier;
}

bool ConvertingCopy(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source, const CopyRegion& region)
{
   const UINT destination_subresource = region.destination_subresource;
   const UINT source_subresource = region.source_subresource;
   const D3D12_RESOURCE_DESC destination_desc = destination->GetDesc();
   const D3D12_RESOURCE_DESC source_desc = source->GetDesc();
   if (region.whole_resource && (destination_desc.MipLevels != 1 || source_desc.MipLevels != 1))
      return false;
   if (region.offset != std::array<UINT, 3>{})
      return false;
   const UINT src_width = UINT(std::max<UINT64>(source_desc.Width >> source_subresource, 1));
   const UINT src_height = std::max<UINT>(source_desc.Height >> source_subresource, 1);
   if (region.box &&
      (region.box->left != 0 || region.box->top != 0 || region.box->front != 0 || region.box->right != src_width || region.box->bottom != src_height || region.box->back != 1))
      return false;
   const UINT dst_width = UINT(std::max<UINT64>(destination_desc.Width >> destination_subresource, 1));
   const UINT dst_height = std::max<UINT>(destination_desc.Height >> destination_subresource, 1);
   const bool same_size = dst_width == src_width && dst_height == src_height;
   const Draw::D3D12::CopyOptions options = {.destination_subresource = destination_subresource, .source_subresource = source_subresource};
   if (!(same_size ? Draw::D3D12::Copy(list, destination, source, options) : Draw::D3D12::Scale(list, destination, source, options)))
      return false;
   if (!same_size)
      Scaling::CountScaledBlit();
   return true;
}

void LogInPlaceCopy(std::string_view what)
{
   LUMA_UNIMPLEMENTED("D3D12 conversions of a partial region, on a copy list, or into a texture that can't be a render target, with a texture upgraded in place");
   if (Internal::in_place_unhandled_copies++ == 0)
      Log::Warning("Mirrors: {} with a texture upgraded in place couldn't be converted on D3D12 (a partial region, a copy list, or a texture that can't "
                   "be a render target): it was skipped",
         what);
}

// `source` (in COPY_SOURCE) converted and scaled into a whole subresource of `destination` (in COPY_DEST); a destination that can't be a
// render target through one of its format, copied natively
bool ConvertInto(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, UINT destination_subresource, ID3D12Resource* source, UINT source_subresource)
{
   const D3D12_RESOURCE_DESC destination_desc = destination->GetDesc();
   const CopyRegion region = {.convertible = true, .destination_subresource = destination_subresource, .source_subresource = source_subresource};
   if (destination_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
      return ConvertingCopy(list, destination, source, region);
   if (destination_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || destination_desc.DepthOrArraySize != 1 || destination_desc.SampleDesc.Count != 1 ||
      (destination_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
      return false;
   const uint32_t level = destination_subresource % std::max<UINT16>(destination_desc.MipLevels, 1);
   ID3D12Resource* target = Draw::D3D12::CreateTemporary2D(list, destination_desc.Format, Upgrade::GetMipSize(uint32_t(destination_desc.Width), level),
      Upgrade::GetMipSize(destination_desc.Height, level), D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
   if (!target || !ConvertingCopy(list, target, source, {.convertible = true, .source_subresource = source_subresource}))
      return false;
   const D3D12_RESOURCE_BARRIER to_source = Transition(target, 0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
   list->ResourceBarrier(1, &to_source);
   const D3D12_TEXTURE_COPY_LOCATION to = {.pResource = destination, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = destination_subresource};
   const D3D12_TEXTURE_COPY_LOCATION from = {.pResource = target, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0};
   list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
   return true;
}

bool SwapHandle(D3D12_CPU_DESCRIPTOR_HANDLE& handle, UINT rect_count = 0, const D3D12_RECT** rects = nullptr)
{
   const std::shared_ptr<Slot> slot = Internal::FindSlot(handle.ptr);
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

bool SwapCopy(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, ID3D12Resource*& source, CopyRegion& region)
{
   Internal::TryQueueDirectChain(list, destination, source);
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
         const Scaling::Box rescaled =
            Scaling::RescaleBox({region.box->left, region.box->top, region.box->front, region.box->right, region.box->bottom, region.box->back}, scale);
         thread_local D3D12_BOX scaled_box;
         scaled_box = {rescaled.left, rescaled.top, rescaled.front, rescaled.right, rescaled.bottom, rescaled.back};
         region.box = &scaled_box;
         Scaling::CountCopyBoxRescale();
      }
      if (dst_mirror && dst_mirror->is_scaled && (region.offset[0] != 0 || region.offset[1] != 0))
      {
         const Math::float2 scale = {
            float(dst_mirror->info.width) / float(std::max(dst_mirror->original_info.width, 1u)),
            float(dst_mirror->info.height) / float(std::max(dst_mirror->original_info.height, 1u)),
         };
         region.offset[0] = UINT(std::lround(double(region.offset[0]) * double(scale.x)));
         region.offset[1] = UINT(std::lround(double(region.offset[1]) * double(scale.y)));
      }
      destination = plan.destination;
      source = plan.source;
      return false;
   }
   if (!source || !destination)
      return false;
   if (region.convertible && ConvertingCopy(list, plan.destination, plan.source, region))
   {
      Internal::converted_copies++;
      return true;
   }
   if (Internal::unhandled_copies++ == 0)
      Log::Warning("Mirrors: a copy between a {} mirror and a {} texture without one couldn't run through Luma's copy pass on D3D12, the game's textures are copied",
         Utils::Format::GetName(GetFormat(plan.destination_mirrored ? plan.destination : plan.source)),
         Utils::Format::GetName(GetFormat(plan.destination_mirrored ? source : destination)));
   return false;
}

bool SwapResolve(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, UINT destination_subresource, ID3D12Resource*& source, UINT source_subresource,
   DXGI_FORMAT& format)
{
   if (!list || !destination || !source)
      return false;
   Internal::TryQueueDirectChain(list, destination, source);
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
   const D3D12_RESOURCE_DESC src_desc = plan.source->GetDesc();
   if (src_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || src_desc.SampleDesc.Count <= 1 || source_subresource >= src_desc.MipLevels)
      return false;
   const DXGI_FORMAT resolve_format = GetResolveFormat(format, GetFormat(source), src_format);
   if (resolve_format == DXGI_FORMAT_UNKNOWN)
      return false;
   ID3D12Resource* resolved = Draw::D3D12::CreateTemporary2D(list, resolve_format, UINT(std::max<UINT64>(src_desc.Width >> source_subresource, 1)),
      std::max<UINT>(src_desc.Height >> source_subresource, 1), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
   if (!resolved)
      return false;
   {
      Hooks::Scope::Guard scope;
      list->ResolveSubresource(resolved, 0, plan.source, source_subresource, resolve_format);
      const D3D12_RESOURCE_BARRIER to_copy[] = {
         Transition(resolved, 0, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE),
         Transition(plan.destination, destination_subresource, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_DEST),
      };
      list->ResourceBarrier(UINT(std::size(to_copy)), to_copy);
   }
   const CopyRegion region = {.convertible = true, .destination_subresource = destination_subresource, .source_subresource = 0};
   const bool ok = ConvertingCopy(list, plan.destination, resolved, region);
   {
      Hooks::Scope::Guard scope;
      const D3D12_RESOURCE_BARRIER back = Transition(plan.destination, destination_subresource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RESOLVE_DEST);
      list->ResourceBarrier(1, &back);
   }
   if (ok)
   {
      Internal::converted_copies++;
      return true;
   }
   if (Internal::unhandled_copies++ == 0)
      Log::Warning("Mirrors: a cross-format resolve between {} and {} couldn't run through Luma's copy pass on D3D12, the game's textures are resolved",
         Utils::Format::GetName(src_format), Utils::Format::GetName(dst_format));
   return false;
}

void SwapClearUnorderedAccess(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE& gpu, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, ID3D12Resource*& resource)
{
   ID3D12Resource* mirror = FindMirror(resource);
   if (!mirror)
      return;
   const std::shared_ptr<Slot> slot = Internal::FindSlot(cpu.ptr);
   ID3D12DescriptorHeap* heap = Database::D3D12::GetBoundHeap(list);
   const std::optional<HeapTail::D3D12::RingRange> range = slot && heap ? HeapTail::D3D12::AllocateRing(list, heap, 1) : std::nullopt;
   if (!range)
   {
      Internal::unhandled_clears++;
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
   Internal::uav_clears++;
}

void CheckInPlaceUnorderedAccessClear(ID3D12Resource* resource, UINT rect_count)
{
   if (rect_count == 0 || !HasAnyResizedInPlace())
      return;
   const InPlaceState* state = FindInPlace(resource);
   if (state && state->resized)
      LUMA_UNIMPLEMENTED("UAV clear rects of a texture resized in place on D3D12 (cleared in the game's size)");
}

void ScaleInPlaceClear(D3D12_CPU_DESCRIPTOR_HANDLE view, UINT rect_count, const D3D12_RECT*& rects)
{
   if (!rects || rect_count == 0 || !HasAnyResizedInPlace())
      return;
   const std::optional<InPlaceTarget> target = Internal::FindInPlaceTarget(view.ptr);
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
            const Scaling::Box scaled =
               Scaling::RescaleBox({region.box->left, region.box->top, region.box->front, region.box->right, region.box->bottom, region.box->back}, source_scale);
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
         const Upgrade::TextureInfo game = source_state ? source_state->original_info : Internal::Describe(source_desc, true);
         const bool whole = !region.box ||
            (region.box->left == 0 && region.box->top == 0 && region.box->front == 0 && region.box->right == Upgrade::GetMipSize(game.width, level) &&
               region.box->bottom == Upgrade::GetMipSize(game.height, level) && region.box->back == 1);
         converted = whole && ConvertInto(list, destination, region.destination_subresource, source, region.source_subresource);
      }
   }
   if (converted)
      Internal::in_place_converted_copies++;
   else
      LogInPlaceCopy("a copy");
   return true;
}

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
      const D3D12_TEXTURE_COPY_LOCATION temporary_location = {.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0};
      if (upload)
      {
         // The whole subresource only: the game's data into a texture of its format and size, converted and scaled into the upgraded one
         const uint32_t width = box ? box->right - box->left : footprint.Width;
         const uint32_t height = box ? box->bottom - box->top : footprint.Height;
         if (x == 0 && y == 0 && z == 0 && width == game_width && height == game_height)
         {
            if (ID3D12Resource* temporary =
                   Draw::D3D12::CreateTemporary2D(list, footprint.Format, game_width, game_height, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST))
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
      else if (ID3D12Resource* temporary = Draw::D3D12::CreateTemporary2D(list, footprint.Format, game_width, game_height, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                  D3D12_RESOURCE_STATE_COPY_DEST))
      {
         // The subresource converted and scaled into a texture of the game's format and size, which the game's copy reads (its box is in its size)
         if (ConvertingCopy(list, temporary, source.pResource, {.convertible = true, .source_subresource = source.SubresourceIndex}))
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
      Internal::in_place_converted_copies++;
   else
      LogInPlaceCopy(upload ? "an upload" : "a readback");
   return true;
}

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
} // namespace D3D12
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
