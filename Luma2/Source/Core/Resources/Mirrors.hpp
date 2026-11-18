#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Hooks/Functions.hpp"
#include "Resources/Upgrade.hpp"

// Luma's resources standing in for the game's (Guidelines.md section 6): a texture the upgrade rules match gets a mirror at its
// creation, its views get mirrored views, and binds, copies, clears and writes are redirected to them. The game's objects are never
// changed. This part is shared by the APIs: the need, the runtime switch, the counts and (Development) the list for the dev tools.
namespace Luma {
namespace Resources {
namespace Mirrors {
// Its settings, before the game's Init
void Register();
// The upgrade rules exist: mirrors are made, and their hooks installed
bool IsNeeded();
// Any thread, on the bind path: mirrors are swapped in (Luma.Enabled and Luma.Resources.Upgrades). Mirrors are made and kept in sync
// either way, so switching back needs no catch-up.
bool IsSwapping();
Hooks::D3D11::Functions GetD3D11Functions();
// D3D12's mirrors and sampler mirrors (Resources::Mirrors::D3D12 has both)
Hooks::D3D12::Functions GetD3D12Functions();

struct Stats
{
   uint32_t mirrors = 0;
   uint64_t bytes = 0; // Of the mirrors, estimated from their descs
   uint32_t views = 0;
};

Stats GetStats();
// Any thread: a mirror (its estimated size) or a mirrored view made, or released (negative)
void CountMirror(int32_t count, int64_t bytes);
void CountView(int32_t count);

// The memory a texture of this desc takes, every mip and slice (an estimate: no padding)
uint64_t EstimateBytes(const Upgrade::TextureDesc& desc);

#if DEVELOPMENT
// What the Resources tab lists
struct Info
{
   std::string original; // The game's texture's address
   std::string debug_name;
   Upgrade::TextureDesc original_desc;
   Upgrade::TextureDesc mirror_desc;
   uint64_t frame = 0; // When it was made
};

void AddInfo(const void* original, Info info);
void RemoveInfo(const void* original);
std::vector<Info> GetInfos();
#endif
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
