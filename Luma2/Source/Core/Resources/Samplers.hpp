#pragma once

#include <cstdint>
#include <optional>

#include "Hooks/Functions.hpp"

// Sampler mirrors: the game's anisotropic samplers get 16x anisotropy and the LOD bias offset added, which SR sets from its render
// scale (0 until SR sets one, or a Development setting). A mirror is made per sampler
// and offset the first time a bind needs it, and swapped in at every bind while Luma is on. Shared by the APIs: the rules and the offset.
namespace Luma {
namespace Resources {
namespace Samplers {
struct Options
{
   bool upgrade = false;
};

void SetOptions(const Options& options);
const Options& GetOptions();
// Its settings, before the game's Init
void Register();
bool IsNeeded();
// Any thread, on the bind path: mirrors are swapped in (Luma.Enabled and Luma.Resources.Samplers)
bool IsSwapping();
Hooks::D3D11::Functions GetD3D11Functions();

// The offset added to the LOD bias of the mirrors: SR's, or the Development setting's when on. Any thread.
float GetLodBiasOffset();
// Changes whenever the offset does: a sampler's mirror for an older one is made again at its next bind
uint32_t GetGeneration();
// The offset SR picked. Any thread.
void SetLodBiasOffset(float offset);

// The parts of a sampler desc the mirror changes, as both APIs have them (D3D11_FILTER and D3D12_FILTER share their values)
struct Desc
{
   uint32_t filter = 0;
   float mip_lod_bias = 0.f;
   uint32_t max_anisotropy = 0;
   float min_lod = 0.f;
};

// The mirror's desc (comparison samplers included), none when the sampler isn't upgraded or wouldn't change
std::optional<Desc> GetMirrorDesc(const Desc& desc, float offset);
} // namespace Samplers
} // namespace Resources
} // namespace Luma
