#pragma once

#include <filesystem>
#include <span>

// Everything a mod reads or writes at runtime is in one "Luma" folder next to the game's executable. Shaders and Textures
// are an overlay of two roots, the game's then Global's: packaged mods have them merged into the Luma folder, Development
// builds read the repository's Luma2/Resources in place when it exists.
namespace Luma {
namespace System {
namespace Paths {
// Creates the Luma folder. Before the log opens, under the loader lock.
void Initialize();
// After the game's Init, which names the game's folder in the repository
void ResolveRoots();

const std::filesystem::path& GetRoot();
std::filesystem::path GetConfig();
std::filesystem::path GetLog();
// Rebuilt freely: Shaders, Patches and Pipelines below it
std::filesystem::path GetCache();
// Development and Test: dumped game shaders and patched shaders
std::filesystem::path GetDump();

// In lookup order: a name in the game's root wins over the same one in Global's
std::span<const std::filesystem::path> GetShaderRoots();
std::span<const std::filesystem::path> GetTextureRoots();
// The game's only
std::filesystem::path GetRecipes();
// The first root holding `relative` ("Includes/Color/Color.hlsl"), empty if none does
std::filesystem::path Find(std::span<const std::filesystem::path> roots, const std::filesystem::path& relative);
} // namespace Paths
} // namespace System
} // namespace Luma
