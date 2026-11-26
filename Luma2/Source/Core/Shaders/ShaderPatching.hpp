#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "Common/ShaderStage.hpp"

// Game shaders changed in their bytecode: DXP recipes from the Recipes folder, and the game's own bytecode patches. A patch is a
// variant of its shader (Variants::Source::Patch, below a replacement file), made when the game creates the shader, so binds,
// requests and fulfil, the draw events, trace and D3D12's pipelines treat it as any other. Each recipe is its own setting (and
// each of its environment values): a change patches every shader the game created again, at once, mostly from the cache.
namespace Luma {
namespace Shaders {
struct Store;

namespace Database {
struct Shader;
}

namespace Patching {
// A game's bytecode patch: `code` is the program's instructions
// (Container::GetProgramCode), `container` the whole shader. The new instructions, or nothing to leave the shader as it is.
// Luma puts them back in the container and signs it.
using BytecodePatcher = std::optional<std::vector<uint8_t>> (*)(ShaderStage stage, uint32_t hash, std::span<const uint8_t> code, std::span<const uint8_t> container);

// Game::Options::patching: fixed for the process
struct Options
{
   // In the game's creation call, so the patch is there before the shader's first bind
   BytecodePatcher patch_sync = nullptr;
   // On a job, after the creation
   BytecodePatcher patch_async = nullptr;
};

// Init only (Game::SetOptions)
void SetOptions(const Options& options);
const Options& GetOptions();

// After the game's Init and the roots, before the settings load: reads the recipes and registers their settings
void Register();
// Recipes, or a bytecode patch callback. Decided by Register.
bool IsNeeded();

// Any thread, at each of the device's shader creations (Database::Track), once per hash: the sync callback runs now, the rest at
// the next present's job
void TrackShader(Store& store, const Database::Shader& shader);
// Presenting thread: queues each device's patch job for the shaders it created since, or for all of them after a change
void OnPresent();
// Presenting thread: the recipes read again from disk (a new file needs a restart: its settings are registered at boot), and
// every device's shaders patched again
void Reload();
// Presenting thread: unloaded, every patch variant goes and new shaders aren't patched; loading patches everything again
void SetLoaded(bool loaded);
bool IsLoaded();

// For the dev tools and MCP. Any thread.
struct EnvValue
{
   std::string name;
   std::string label;
   std::string key; // Its setting
   std::string default_value; // The recipe's, as text
   std::string value;
};

struct RecipeInfo
{
   std::string identity; // From the Recipes folder: "PS/dithering_fix.recipe.yml"
   std::string name;
   std::string description;
   std::string stage; // Its stage folder, empty for any stage
   std::string key; // Its switch's setting
   bool enabled = false;
   bool registered = false; // It has a `meta:` name (otherwise it's shown in Development builds only)
   std::string error; // Didn't parse, or the file is gone since boot
   uint32_t matched = 0; // Shaders it changed
   std::vector<EnvValue> env;
};

struct BindingInfo
{
   std::string handle;
   std::string kind; // "texture", "cbuffer"...
   uint32_t slot = 0;
   uint32_t space = 0;
   bool resolved = false; // A Resources::Database entry of that name and kind
};

struct PatchInfo
{
   uint32_t hash = 0;
   ShaderStage stage = ShaderStage::Pixel;
   bool patched = false; // Its variant is published
   bool callback = false; // The game's bytecode patch, not recipes
   bool cached = false; // From the cache
   std::vector<std::string> recipes; // The ones that changed it, in chain order
   std::vector<BindingInfo> bindings;
   std::string error; // Why it isn't used
   std::string report; // The recipes' step results, for this shader (Development builds, not when cached)
};

// A device's patches (Shaders::Store). Each lock is never held with another.
struct State
{
   mutable std::mutex shaders_mutex;
   // Every shader the device created, once each: what a change patches again
   std::vector<const Database::Shader*> created;
   std::unordered_set<uint32_t> created_hashes;
   // Created since the last job
   std::vector<const Database::Shader*> pending;
   std::atomic<bool> running = false;
   // Set on any thread by recipe setting changes, consumed on the presenting thread
   std::atomic<bool> patch_all = false;

   mutable std::mutex patches_mutex;
   std::map<uint32_t, PatchInfo> patches;
   // Patched in their creation call (Options::patch_sync): a job doesn't touch them, the game's bytecode patch wins
   std::unordered_set<uint32_t> sync_patched;
   std::atomic<uint64_t> generation = 0;
};

struct Status
{
   bool needed = false;
   bool loaded = true;
   bool running = false;
   uint32_t recipes = 0;
   uint32_t shaders = 0; // Considered so far
   uint32_t patched = 0;
   uint32_t unmatched = 0;
   uint32_t failed = 0; // Matched, but unusable (a missing resource, D3D12 bindings) or the recipe failed
   uint32_t pending = 0; // Created since the last job
   uint32_t broken_recipes = 0; // Didn't parse: their shaders run as the game made them
   uint64_t generation = 0; // Moves with each finished job
};

// Null `store`: the recipes alone, before any device
Status GetStatus(const Store* store);
std::vector<RecipeInfo> GetRecipes(const Store* store);
// The device's shaders a patch changed or tried to, by hash
std::vector<PatchInfo> GetPatches(const Store& store);
std::optional<PatchInfo> FindPatch(const Store& store, uint32_t hash);
} // namespace Patching
} // namespace Shaders
} // namespace Luma
