#pragma once

#include <cstdint>

// Where Luma's own bindings go among the game's registers
namespace Luma {
namespace Resources {
namespace Bindings {
// Game::Options::bindings: fixed for the process, compiled into every shader
struct Options
{
   // D3D12: the register space of all of Luma's bindings in the game's extended root signatures (its cbuffers now; its views and samplers
   // with the D3D12 passes, Step 5), declared by SM5.1 variants. A root signature using anything in it is a conflict, asserted in
   // Development: the mod picks another space. 0 puts them in the game's space 0 at D3D11's slots instead.
   uint32_t d3d12_space = 100;
};

// Init only (Game::SetOptions)
void SetOptions(const Options& options);
const Options& GetOptions();
} // namespace Bindings
} // namespace Resources
} // namespace Luma
