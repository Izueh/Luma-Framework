#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Hooks/HookFunctions.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Shaders/Shaders.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"

namespace Luma {
namespace Resources {
namespace Mirrors {
void Register();
bool IsSupported();
bool IsNeeded();
bool IsSwapping();
bool HasAnyMirrors();
Hooks::D3D11::Functions GetD3D11Functions();
Hooks::D3D12::Functions GetD3D12Functions();

struct Stats
{
   uint32_t mirrors = 0;
   uint64_t bytes = 0;
   uint32_t views = 0;
   // Upgrade::Method::InPlace textures, and the bytes the mirrors they don't have would have taken
   uint32_t in_place = 0;
   uint64_t in_place_saved_bytes = 0;
   // Of those, the ones whose size was upgraded too (custom dimensions)
   uint32_t in_place_resized = 0;
};

Stats GetStats();
void CountMirror(int32_t count, int64_t bytes);
void CountInPlace(int32_t count, int64_t saved_bytes, bool resized);
bool HasAnyInPlace();
// Draws, copies and clears of these are rescaled
bool HasAnyResizedInPlace();
void CountView(int32_t count);
void LogCreationFailure(std::string_view what, HRESULT result);
uint64_t EstimateBytes(const Upgrade::TextureInfo& info);
DXGI_FORMAT GetResolveFormat(DXGI_FORMAT game_format, DXGI_FORMAT original_format, DXGI_FORMAT mirror_format);

#if DEVELOPMENT
struct Info
{
   std::string original;
   std::string debug_name;
   Upgrade::TextureInfo original_desc;
   Upgrade::TextureInfo mirror_desc;
   Chains::Origin origin = {};
   bool is_scaled = false;
   uint64_t frame = 0;
};

void AddInfo(const void* original, Info info);
void RemoveInfo(const void* original);
std::vector<Info> GetInfos();
#endif

template <typename Resource, typename OwnedResource>
struct MirrorState
{
   OwnedResource mirror;
   Upgrade::TextureInfo original_info;
   Upgrade::TextureInfo info;
   Chains::Origin origin = {};
   int64_t bytes = 0;
   Resource* original = nullptr;
   uint64_t generation = 0;
   bool is_scaled = false;

   ~MirrorState()
   {
      CountMirror(-1, -bytes);
      if (is_scaled)
         Scaling::CountScaledMirror(-1);
#if DEVELOPMENT
      RemoveInfo(original);
#endif
   }
};

// A texture upgraded in place: its own desc is `info`, the game asked for `original_info` (another format, and with custom dimensions
// another width, height and mip count)
struct InPlaceState
{
   Upgrade::TextureInfo original_info;
   Upgrade::TextureInfo info;
   int64_t saved_bytes = 0;
   bool resized = false;

   ~InPlaceState() { CountInPlace(-1, -saved_bytes, resized); }
};

template <typename Resource>
void AttachInPlace(Resource* texture, const Upgrade::TextureInfo& original_info, const Upgrade::TextureInfo& info)
{
   const int64_t saved = int64_t(EstimateBytes(info));
   const bool resized = info.width != original_info.width || info.height != original_info.height || info.levels != original_info.levels;
   // Counted before SetState so a failed attach uncounts on destruction
   CountInPlace(1, saved, resized);
   Hooks::Lifetime::SetState<InPlaceState>(texture, original_info, info, saved, resized);
}

template <typename Resource>
const InPlaceState* FindInPlace(Resource* resource)
{
   if (!resource || !HasAnyInPlace())
      return nullptr;
   return Hooks::Lifetime::GetState<InPlaceState>(resource);
}

inline Math::float2 GetInPlaceScale(const InPlaceState* state)
{
   if (!state || !state->resized)
      return {1.f, 1.f};
   return {float(state->info.width) / float(std::max(state->original_info.width, 1u)),
      float(state->info.height) / float(std::max(state->original_info.height, 1u))};
}

// What the game asked for, when the rules spoof GetDesc
template <typename Resource>
const Upgrade::TextureInfo* FindSpoofedDesc(Resource* resource)
{
   if (!Upgrade::GetRules().spoof_description)
      return nullptr;
   const InPlaceState* state = FindInPlace(resource);
   return state ? &state->original_info : nullptr;
}

template <typename Resource>
struct OriginalState
{
   Resource* original = nullptr;
};

template <typename Resource, typename OwnedResource>
void AttachMirror(Resource* original, OwnedResource mirror, const Upgrade::TextureInfo& original_info, const Upgrade::TextureInfo& mirror_info,
   Chains::Origin origin = {}, uint64_t generation = 0)
{
   const int64_t bytes = int64_t(EstimateBytes(mirror_info));
   const bool scaled = mirror_info.width != original_info.width || mirror_info.height != original_info.height;
   Hooks::Lifetime::SetState<OriginalState<Resource>>(mirror.Get(), original);
   // Counted before SetState so a failed attach uncounts on destruction
   CountMirror(1, bytes);
   if (scaled)
      Scaling::CountScaledMirror(1);
   if (!Hooks::Lifetime::SetState<MirrorState<Resource, OwnedResource>>(original, std::move(mirror), original_info, mirror_info, origin, bytes, original,
          generation, scaled))
      return;
#if DEVELOPMENT
   AddInfo(original, {.original = std::format("0x{:X}", reinterpret_cast<uintptr_t>(original)), .original_desc = original_info, .mirror_desc = mirror_info, .origin = origin, .is_scaled = scaled, .frame = Shaders::GetFrame()});
#endif
}

template <typename OwnedResource, typename Resource>
const MirrorState<Resource, OwnedResource>* FindMirrorState(Resource* resource)
{
   if (!resource || !HasAnyMirrors())
      return nullptr;
   return Hooks::Lifetime::GetState<MirrorState<Resource, OwnedResource>>(resource);
}

template <typename OwnedResource, typename Resource>
Resource* FindMirror(Resource* resource)
{
   const auto* state = FindMirrorState<OwnedResource>(resource);
   return state ? state->mirror.Get() : nullptr;
}

template <typename Resource>
Resource* FindOriginal(Resource* mirror)
{
   if (!mirror || !HasAnyMirrors())
      return nullptr;
   const auto* state = Hooks::Lifetime::GetState<OriginalState<Resource>>(mirror);
   return state ? state->original : nullptr;
}

enum class CopyAction : uint8_t
{
   Original,
   DirectMirrorCopy,
   NeedsConvertingCopy,
};

template <typename Resource>
struct CopyPlan
{
   CopyAction action = CopyAction::Original;
   Resource* destination = nullptr;
   Resource* source = nullptr;
   bool destination_mirrored = false;
   bool source_mirrored = false;
};

template <typename Resource, typename GetFormatFn>
CopyPlan<Resource> PlanCopy(Resource* destination, Resource* source, Resource* (*get_mirror)(Resource*), GetFormatFn&& get_format)
{
   if (!HasAnyMirrors())
      return {};
   Resource* mirror_dst = destination ? get_mirror(destination) : nullptr;
   Resource* mirror_src = source ? get_mirror(source) : nullptr;
   if (!mirror_dst && !mirror_src)
      return {};
   Resource* final_dst = mirror_dst ? mirror_dst : destination;
   Resource* final_src = mirror_src ? mirror_src : source;
   const bool compatible = final_dst && final_src && Utils::Format::DXGI::AreCopyCompatible(get_format(final_dst), get_format(final_src));
   return {
      .action = compatible ? CopyAction::DirectMirrorCopy : CopyAction::NeedsConvertingCopy,
      .destination = final_dst,
      .source = final_src,
      .destination_mirrored = mirror_dst != nullptr,
      .source_mirrored = mirror_src != nullptr,
   };
}
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
