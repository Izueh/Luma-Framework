#include "Resources/ResourceDatabase.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <shared_mutex>

#include "Shaders/ShaderStore.hpp"
#include "Utils/Debug.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Resources {
namespace Database {
namespace {
// Written while Core initializes, read without a lock after. A deque: Get's references stay valid.
std::deque<Entry> entries;
} // namespace

void RegisterRuntimeDefaults()
{
   constexpr Invalidation SR_INVALIDATION = Invalidation::SwapchainResize | Invalidation::RenderResolutionChange;

   struct DefaultEntry
   {
      uint64_t hash;
      std::string_view name;
   };

   constexpr std::array DEFAULTS = {
      DefaultEntry{Sr::COLOR, "sr_color"},
      DefaultEntry{Sr::DEPTH, "sr_depth"},
      DefaultEntry{Sr::MOTION_VECTORS, "sr_motion_vectors"},
      DefaultEntry{Sr::OUTPUT, "sr_output"},
      DefaultEntry{Sr::EXPOSURE, "sr_exposure"},
      DefaultEntry{Sr::REACTIVE, "sr_reactive"},
      DefaultEntry{Sr::TRANSPARENCY, "sr_transparency"},
   };
   for (const DefaultEntry& entry : DEFAULTS)
   {
      Register({
         .name_hash = entry.hash,
         .name = std::string(entry.name),
         .kind = Kind::RuntimeTexture,
         .owner = Owner::Core,
         .invalidation = SR_INVALIDATION,
      });
   }
}

std::optional<uint32_t> RegisterRuntimeTexture(std::string_view name, Invalidation invalidation)
{
   return Register({
      .name_hash = Utils::Hash::Name(name),
      .name = std::string(name),
      .kind = Kind::RuntimeTexture,
      .owner = Owner::Game,
      .invalidation = invalidation,
   });
}

std::optional<uint32_t> Register(Entry entry)
{
   if (const std::optional<uint32_t> existing = Find(entry.name_hash))
   {
      Entry& registered = entries[*existing];
      if (registered.owner == Owner::Game && entry.owner == Owner::Core)
      {
         ASSERT_ONCE_MSG(false, "A game registered a Resources::Database name Core owns");
         Log::Error("Resources: the game's \"{}\" is replaced by Core's, whose entries are read-only", entry.name);
         registered = std::move(entry);
         return *existing;
      }
      if (registered.owner == Owner::Core && entry.owner == Owner::Game)
      {
         ASSERT_ONCE_MSG(false, "A game registered a Resources::Database name Core owns");
         Log::Error("Resources: the game's \"{}\" is refused, Core's entries are read-only", entry.name);
      }
      else
         Log::Error("Resources: a second \"{}\" is ignored", entry.name);
      return std::nullopt;
   }
   entries.push_back(std::move(entry));
   return uint32_t(entries.size() - 1);
}

std::optional<uint32_t> Find(uint64_t name_hash)
{
   const auto it = std::ranges::find(entries, name_hash, &Entry::name_hash);
   return it != entries.end() ? std::optional(uint32_t(it - entries.begin())) : std::nullopt;
}

const Entry& Get(uint32_t index)
{
   return entries[index];
}

uint32_t GetCount()
{
   return uint32_t(entries.size());
}

bool NeedsUpload(const Upload& upload, uint64_t stamp)
{
   return !upload.done || upload.stamp != stamp;
}

bool ReplaceIfChanged(Upload& upload, std::vector<std::byte>& written)
{
   if (upload.done && upload.bytes == written)
      return false;
   upload.bytes.swap(written);
   upload.done = true;
   return true;
}

void PublishShaders(Shaders::Store& store, std::vector<std::pair<uint32_t, ShaderState>> published)
{
   ShaderStates& shaders = store.luma_shaders;
   {
      std::scoped_lock lock(shaders.mutex);
      for (auto& [index, state] : published)
      {
         ASSERT_ONCE(index < shaders.by_entry.size());
         ShaderState& current = shaders.by_entry[index];
         // The same bytes as before are the same compiled shader: nothing is recreated from it
         if (current.compiled && (state.failed || (state.compiled && state.compiled->bytecode == current.compiled->bytecode)))
            state.compiled = current.compiled;
         current = std::move(state);
      }
   }
   shaders.generation.fetch_add(1, std::memory_order_release);
}

ShaderState GetShader(const Shaders::Store& store, uint32_t index)
{
   const ShaderStates& shaders = store.luma_shaders;
   std::shared_lock lock(shaders.mutex);
   return index < shaders.by_entry.size() ? shaders.by_entry[index] : ShaderState();
}

uint64_t GetGeneration(const Shaders::Store& store)
{
   return store.luma_shaders.generation.load(std::memory_order_acquire);
}
} // namespace Database
} // namespace Resources
} // namespace Luma
