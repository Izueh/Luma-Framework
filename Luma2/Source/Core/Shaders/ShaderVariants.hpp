#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Shaders/Shaders.hpp"

// Luma's versions of game shaders. A game shader hash has a list of variants, each with a source (a replacement file or a patch)
// and a priority: the one bound is the highest-priority variant that compiled (and the device took). Files and patches
// behave the same from there: draw callbacks, requests and fulfil, trace. Each API creates the objects and swaps them in at bind.
namespace Luma {
namespace Shaders {
struct Store;

namespace Variants {
enum class Source : uint8_t
{
   File, // A "Name_0x########.ps_5_0.hlsl" replacement
   Patch, // A recipe or a game's bytecode patch
};

inline constexpr size_t SOURCE_COUNT = 2;
// The priority of a replacement file's variant
inline constexpr int32_t FILE_PRIORITY = 100;
// A patch's: below a file's, so a replacement file wins over a patch
inline constexpr int32_t PATCH_PRIORITY = 50;

// "file", "patch"
std::string_view GetName(Source source);

struct Variant
{
   uint32_t hash = 0;
   Source source = Source::File;
   int32_t priority = FILE_PRIORITY;
   std::string file; // From the Shaders root
   // The last version that compiled: a failed build keeps it
   std::shared_ptr<const CompiledShader> compiled;
   std::string messages;
   bool failed = false;
   bool rejected = false; // Compiled, but failed D3D12 invariants
};

// What runs for a hash: its variants by priority, or one chosen in trace until cleared (Development builds)
enum class Choice : uint8_t
{
   Auto,
   Original, // The game's shader
   File,
   Patch,
};

constexpr Choice GetChoice(Source source)
{
   return Choice(uint8_t(Choice::File) + uint8_t(source));
}

// "auto", "original", "file", "patch"
std::string_view GetName(Choice choice);
// "Auto", "Original", "File", "Patch", for the trace's picker
std::string_view GetLabel(Choice choice);

// A device's variants (Shaders::Store). Lock order: `publish_mutex` before `mutex`.
struct State
{
   // Serializes publishers: files on the presenting thread, patches from their jobs
   std::mutex publish_mutex;
   mutable std::shared_mutex mutex;
   // Highest priority first
   std::unordered_map<uint32_t, std::vector<Variant>> by_hash;
   std::atomic<uint64_t> generation = 0;
};

// The trace picker's options for a hash whose `compiled` variants are these sources, highest priority first: Auto, Original, then each
std::vector<Choice> GetChoices(std::span<const Source> compiled);
// Where `choice` is in `choices`, Auto's place (0) when it isn't there (a variant that stopped compiling)
size_t FindChoice(std::span<const Choice> choices, Choice choice);

// Any thread, publishers serialized: a build's variants of one source, replacing that source's. Hashes it doesn't have lose their variant of the
// source; a failed one keeps its last bytecode.
void Publish(Store& store, Source source, std::vector<Variant> variants);
// Any thread, publishers serialized: some hashes' variants of one source, replacing only those hashes' (`removed` lose theirs).
// For a source that changes a few hashes at a time (patches, as the game creates shaders).
void Update(Store& store, Source source, std::vector<Variant> changed, std::span<const uint32_t> removed);
// Any thread
void Reject(Store& store, uint32_t hash, Source source, std::string_view reason);
// Any thread: the hash's variants, highest priority first
std::vector<Variant> GetVariants(const Store& store, uint32_t hash);
// Any thread: null if the hash has no variant of the source that compiled
std::shared_ptr<const CompiledShader> GetCompiledShader(const Store& store, uint32_t hash, Source source);
// Any thread: the hash has a variant that compiled
bool HasVariant(const Store& store, uint32_t hash);
// Any thread: every hash's variants
std::vector<Variant> GetAll(const Store& store);
// Any thread: changes with every publication to the store, and with every choice forced in trace
uint64_t GetGeneration(const Store& store);
// Any thread: monotonic across all stores, changes with any store's publication or forced choice
uint64_t GetAnyGeneration();
// Any thread, after a Database::Shader's forced_variant changed: every device builds the pipelines of the new choice
void MarkChoiceChanged();

// The load and unload toggles: unloaded, binds keep the game's shaders and the variants stay for the next load. Any thread
// reads it.
void SetLoaded(bool loaded);
bool IsLoaded();
} // namespace Variants
} // namespace Shaders
} // namespace Luma
