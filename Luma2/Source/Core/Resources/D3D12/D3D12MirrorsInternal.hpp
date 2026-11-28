#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceUpgrades.hpp"

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D12 {
namespace Internal {
using OwnedResource = Microsoft::WRL::ComPtr<ID3D12Resource>;
using ResourceMirrorState = MirrorState<ID3D12Resource, OwnedResource>;

inline constexpr Math::float2 NO_SCALE = {1.f, 1.f};

inline std::atomic<uint32_t> entry_count = 0;
inline std::atomic<uint32_t> sampler_count = 0;
inline std::atomic<uint32_t> swapped_copies = 0;
inline std::atomic<uint32_t> direct_writes = 0;
inline std::atomic<uint32_t> converted_copies = 0;
inline std::atomic<uint32_t> unhandled_copies = 0;
inline std::atomic<uint32_t> in_place_converted_copies = 0;
inline std::atomic<uint32_t> in_place_unhandled_copies = 0;
inline std::atomic<uint32_t> in_place_unasked_placed = 0;
inline std::atomic<uint32_t> in_place_unfit_placed = 0;
inline std::atomic<uint32_t> uav_clears = 0;
inline std::atomic<uint32_t> unhandled_clears = 0;
inline std::atomic<uint32_t> layout_resources = 0;
inline std::atomic<uint32_t> reserved_resources = 0;
inline std::atomic<uint32_t> aliasing_barriers = 0;

// Lightweight committed state tracked per resource when Chains::IsNeeded()
struct TrackedResourceState
{
   std::atomic<uint32_t> committed_state = uint32_t(D3D12_RESOURCE_STATE_COMMON);
   bool has_clear = false;
   D3D12_CLEAR_VALUE clear = {};
   bool reserved = false;
   bool enhanced_barrier = false;
};

struct Pools;

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

   ~Slot();
};

// Render target and depth handles of in-place resources: their view format and scale
struct InPlaceTarget
{
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   Math::float2 scale = NO_SCALE;
};

struct BarrierTransition
{
   ID3D12Resource* resource = nullptr;
   D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_COMMON;
};

// What Luma's scaling, chains, and barriers track on a list
struct ListState
{
   std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> game_render_targets = {};
   UINT game_render_target_count = 0;
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

// D3D12Mirrors.cpp
Upgrade::TextureInfo Describe(const D3D12_RESOURCE_DESC& desc, bool gpu_only);
Upgrade::FrameState GetFrameState(ID3D12Device* device);
void QueueChainMirror(ID3D12Device* device, ID3D12Resource* resource, const Upgrade::TextureInfo& original_info, const Upgrade::TextureInfo& mirror_info,
   Chains::Origin origin);

// D3D12MirrorsDescriptors.cpp
uint32_t GetIncrement(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type);
uint32_t GetCachedIncrement(D3D12_DESCRIPTOR_HEAP_TYPE type);
std::shared_ptr<Slot> FindSlot(uint64_t handle);
void ReleaseDescriptorPools(ID3D12Device* device);
void EraseDescriptorHandlesForOriginal(ID3D12Resource* original);
void BackfillMirrorViews(ID3D12Device* device, ID3D12Resource* resource, const ResourceMirrorState& state);
ID3D12Resource* FindUnmirroredViewResource(uint64_t handle);
std::optional<InPlaceTarget> FindInPlaceTarget(SIZE_T handle);
bool HasInPlaceTargets();

// D3D12MirrorsScaling.cpp
void TryQueueDirectChain(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source);
} // namespace Internal
} // namespace D3D12
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
