#include "Shaders/ShaderVariants.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderSignatures.hpp"
#include "Shaders/ShaderStore.hpp"

namespace Luma {
namespace Shaders {
namespace Variants {
namespace {
std::atomic<bool> loaded = true;
std::atomic<uint64_t> any_generation = 0;

void BumpGeneration(State& state)
{
   state.generation.fetch_add(1, std::memory_order_release);
   any_generation.fetch_add(1, std::memory_order_release);
}
} // namespace

std::string_view GetName(Source source)
{
   constexpr std::string_view NAMES[] = {"file", "patch"};
   static_assert(std::size(NAMES) == SOURCE_COUNT);
   return NAMES[size_t(source)];
}

std::string_view GetName(Choice choice)
{
   constexpr std::string_view NAMES[] = {"auto", "original", "file", "patch"};
   return NAMES[size_t(choice)];
}

std::string_view GetLabel(Choice choice)
{
   constexpr std::string_view LABELS[] = {"Auto", "Original", "File", "Patch"};
   return LABELS[size_t(choice)];
}

std::vector<Choice> GetChoices(std::span<const Source> compiled)
{
   std::vector<Choice> choices = {Choice::Auto, Choice::Original};
   for (const Source source : compiled)
      choices.push_back(GetChoice(source));
   return choices;
}

size_t FindChoice(std::span<const Choice> choices, Choice choice)
{
   const auto it = std::ranges::find(choices, choice);
   return it != choices.end() ? size_t(it - choices.begin()) : 0;
}

void Publish(Store& store, Source source, std::vector<Variant> published)
{
   State& state = store.variants;
   // Two publishers copying the same map would each drop the other's change
   std::scoped_lock publishing(state.publish_mutex);
   std::unordered_map<uint32_t, std::vector<Variant>> next;
   {
      std::shared_lock lock(state.mutex);
      next = state.by_hash;
   }
   // The source's variants go, keeping what they compiled to for the new ones
   std::unordered_map<uint32_t, std::shared_ptr<const CompiledShader>> previous;
   for (auto& [hash, list] : next)
   {
      for (const Variant& variant : list)
      {
         if (variant.source == source && variant.compiled)
            previous.emplace(hash, variant.compiled);
      }
      std::erase_if(list, [source](const Variant& variant) { return variant.source == source; });
   }
   for (Variant& variant : published)
   {
      variant.source = source;
      if (const auto it = previous.find(variant.hash); it != previous.end())
      {
         // The same bytes as before are the same compiled shader: nothing is recreated from it
         if (variant.failed || (variant.compiled && variant.compiled->bytecode == it->second->bytecode))
            variant.compiled = it->second;
      }
      next[variant.hash].push_back(std::move(variant));
   }
   std::erase_if(next, [](const auto& entry) { return entry.second.empty(); });
   for (auto& [hash, list] : next)
      std::ranges::stable_sort(list, std::ranges::greater(), &Variant::priority);
#if DEVELOPMENT
   // Replacements whose original the game created already; the others are checked when it does (Database::Track)
   for (const auto& [hash, list] : next)
   {
      const Database::Shader* original = Database::Find(hash);
      for (const Variant& variant : list)
      {
         if (original && variant.source == source && variant.compiled)
            Signatures::Check(hash, original->stage, original->signature, variant.compiled->bytecode, variant.file);
      }
   }
#endif
   {
      std::scoped_lock lock(state.mutex);
      state.by_hash.swap(next);
   }
   BumpGeneration(state);
}

void Update(Store& store, Source source, std::vector<Variant> changed, std::span<const uint32_t> removed)
{
   if (changed.empty() && removed.empty())
      return;
   State& state = store.variants;
   std::scoped_lock publishing(state.publish_mutex);
   {
      std::scoped_lock lock(state.mutex);
      const auto drop = [&](uint32_t hash) -> std::shared_ptr<const CompiledShader> {
         const auto it = state.by_hash.find(hash);
         if (it == state.by_hash.end())
            return nullptr;
         std::shared_ptr<const CompiledShader> previous;
         for (const Variant& variant : it->second)
         {
            if (variant.source == source)
               previous = variant.compiled;
         }
         std::erase_if(it->second, [source](const Variant& variant) { return variant.source == source; });
         if (it->second.empty())
            state.by_hash.erase(it);
         return previous;
      };
      for (const uint32_t hash : removed)
         drop(hash);
      for (Variant& variant : changed)
      {
         variant.source = source;
         // The same bytes as before are the same compiled shader: nothing is recreated from it
         if (const std::shared_ptr<const CompiledShader> previous = drop(variant.hash);
            previous && (variant.failed || (variant.compiled && variant.compiled->bytecode == previous->bytecode && variant.compiled->requests == previous->requests)))
            variant.compiled = previous;
         std::vector<Variant>& list = state.by_hash[variant.hash];
         list.push_back(std::move(variant));
         std::ranges::stable_sort(list, std::ranges::greater(), &Variant::priority);
      }
   }
   BumpGeneration(state);
}

void Reject(Store& store, uint32_t hash, Source source, std::string_view reason)
{
   State& state = store.variants;
   std::scoped_lock publishing(state.publish_mutex);
   {
      std::scoped_lock lock(state.mutex);
      const auto it = state.by_hash.find(hash);
      if (it == state.by_hash.end())
         return;
      for (Variant& variant : it->second)
      {
         if (variant.source != source || !variant.compiled)
            continue;
         variant.compiled.reset();
         variant.rejected = true;
         variant.messages += reason;
      }
   }
   BumpGeneration(state);
}

std::vector<Variant> GetVariants(const Store& store, uint32_t hash)
{
   std::shared_lock lock(store.variants.mutex);
   const auto it = store.variants.by_hash.find(hash);
   return it != store.variants.by_hash.end() ? it->second : std::vector<Variant>();
}

std::shared_ptr<const CompiledShader> GetCompiledShader(const Store& store, uint32_t hash, Source source)
{
   std::shared_lock lock(store.variants.mutex);
   const auto it = store.variants.by_hash.find(hash);
   if (it == store.variants.by_hash.end())
      return nullptr;
   const auto variant = std::ranges::find(it->second, source, &Variant::source);
   return variant != it->second.end() ? variant->compiled : nullptr;
}

bool HasVariant(const Store& store, uint32_t hash)
{
   std::shared_lock lock(store.variants.mutex);
   const auto it = store.variants.by_hash.find(hash);
   return it != store.variants.by_hash.end() && std::ranges::any_of(it->second, [](const Variant& variant) { return variant.compiled != nullptr; });
}

std::vector<Variant> GetAll(const Store& store)
{
   std::shared_lock lock(store.variants.mutex);
   std::vector<Variant> list;
   for (const auto& [hash, hash_variants] : store.variants.by_hash)
      list.append_range(hash_variants);
   return list;
}

void MarkChoiceChanged()
{
   for (const std::shared_ptr<Store>& store : GetStores())
      store->variants.generation.fetch_add(1, std::memory_order_release);
   any_generation.fetch_add(1, std::memory_order_release);
}

uint64_t GetGeneration(const Store& store)
{
   return store.variants.generation.load(std::memory_order_acquire);
}

uint64_t GetAnyGeneration()
{
   return any_generation.load(std::memory_order_acquire);
}

void SetLoaded(bool value)
{
   loaded.store(value, std::memory_order_release);
}

bool IsLoaded()
{
   return loaded.load(std::memory_order_acquire);
}
} // namespace Variants
} // namespace Shaders
} // namespace Luma
