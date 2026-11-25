#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Hooks/HookFunctions.hpp"
#include "Resources/ResourceUpgrades.hpp"

namespace Luma {
namespace Resources {
namespace Chains {
enum class Mode : uint8_t
{
   None,
   // Copies and resolves from a mirrored source propagate to the destination
   Direct,
   // Draws and dispatches reading a mirrored SRV or UAV also propagate to their bound RTVs and UAVs
   DirectAndIndirect,
};

// A shader pass that seeds mirror creation on specific output slots
struct Seed
{
   std::vector<uint8_t> rtv_slots;
   std::vector<uint8_t> uav_slots;
   // When scaling is active this frame, the mirror created by this seed scales to output resolution
   bool scale = false;
};

struct Options
{
   Mode mode = Mode::None;
   std::unordered_map<uint32_t, Seed> seeds;
};

enum class OriginKind : uint8_t
{
   Rule,
   CustomDimensions,
   Seed,
   DirectChain,
   IndirectChain,
};

struct Origin
{
   OriginKind kind = OriginKind::Rule;
   uint32_t shader_hash = 0;

   bool operator==(const Origin&) const = default;
};

struct Stats
{
   Mode mode = Mode::None;
   uint32_t seeds = 0;
   uint32_t direct_propagations = 0;
   uint32_t indirect_propagations = 0;
   uint32_t deferred_queued = 0;
};

void Register();
void SetOptions(const Options& options);
const Options& GetOptions();
Mode GetMode();
bool IsNeeded();
const Seed* FindSeed(uint32_t shader_hash);
bool HasScaleSeeds();
// Whether a mirror on `source` can safely propagate its format to `target`
bool CanPropagate(const Upgrade::TextureInfo& source, const Upgrade::TextureInfo& target, uint32_t aspect_ratio_pixel_threshold = 1);
std::string_view GetModeName(Mode mode);
std::string_view GetOriginName(OriginKind kind);
Hooks::D3D11::Functions GetD3D11Functions();
Hooks::D3D12::Functions GetD3D12Functions();
void CountDirectPropagation();
void CountIndirectPropagation();
void CountDeferredQueued(int32_t delta);
Stats GetStats();
} // namespace Chains
} // namespace Resources
} // namespace Luma
