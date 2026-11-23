#pragma once

#include <cstdint>
#include <filesystem>

// Textures from disk: every .dds in the Textures roots (the game's, then Global's) is a Resources::Database entry named after its
// file ("fast_noise.dds" is "fast_noise"_h), created on each device. Anything resolves them by name: a patch's binding, a game's
// draw callback.
namespace Luma {
namespace Resources {
namespace Textures {
// Game::Options::textures: fixed for the process
struct Options
{
   // All of them or none, for now: a game that uses none loads nothing
   bool load = false;
};

// Init only (Game::SetOptions)
void SetOptions(const Options& options);
const Options& GetOptions();

// After the game's Init and the roots: the files become entries. Nothing without Options::load.
void Register();
// The file of a texture entry, empty if no root has it any more
std::filesystem::path Find(uint32_t entry);
} // namespace Textures
} // namespace Resources
} // namespace Luma
