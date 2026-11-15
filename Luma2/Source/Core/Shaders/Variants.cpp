#include "Shaders/Variants.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "Shaders/Database.hpp"
#include "Shaders/Signatures.hpp"

namespace Luma {
namespace Shaders {
namespace Variants {
namespace {
std::atomic<bool> loaded = true;
std::atomic<uint64_t> generation = 0;
// Serializes publishers (files on the presenting thread, patches from Step 3's jobs). Lock order: before `mutex`.
std::mutex publish_mutex;
// Written by a publisher, read by shader creations on any thread. Never held with another lock but `publish_mutex`.
std::shared_mutex mutex;
// Highest priority first
std::unordered_map<uint32_t, std::vector<Variant>> variants;
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

void Publish(Source source, std::vector<Variant> published)
{
   // Two publishers copying the same map would each drop the other's change
   std::scoped_lock publishing(publish_mutex);
   std::unordered_map<uint32_t, std::vector<Variant>> next;
   {
      std::shared_lock lock(mutex);
      next = variants;
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
      std::scoped_lock lock(mutex);
      variants.swap(next);
   }
   generation.fetch_add(1, std::memory_order_release);
}

void Update(Source source, std::vector<Variant> changed, std::span<const uint32_t> removed)
{
   if (changed.empty() && removed.empty())
      return;
   std::scoped_lock publishing(publish_mutex);
   {
      std::scoped_lock lock(mutex);
      const auto drop = [&](uint32_t hash) -> std::shared_ptr<const CompiledShader> {
         const auto it = variants.find(hash);
         if (it == variants.end())
            return nullptr;
         std::shared_ptr<const CompiledShader> previous;
         for (const Variant& variant : it->second)
         {
            if (variant.source == source)
               previous = variant.compiled;
         }
         std::erase_if(it->second, [source](const Variant& variant) { return variant.source == source; });
         if (it->second.empty())
            variants.erase(it);
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
         std::vector<Variant>& list = variants[variant.hash];
         list.push_back(std::move(variant));
         std::ranges::stable_sort(list, std::ranges::greater(), &Variant::priority);
      }
   }
   generation.fetch_add(1, std::memory_order_release);
}

std::vector<Variant> GetVariants(uint32_t hash)
{
   std::shared_lock lock(mutex);
   const auto it = variants.find(hash);
   return it != variants.end() ? it->second : std::vector<Variant>();
}

std::shared_ptr<const CompiledShader> GetCompiledShader(uint32_t hash, Source source)
{
   std::shared_lock lock(mutex);
   const auto it = variants.find(hash);
   if (it == variants.end())
      return nullptr;
   const auto variant = std::ranges::find(it->second, source, &Variant::source);
   return variant != it->second.end() ? variant->compiled : nullptr;
}

bool HasVariant(uint32_t hash)
{
   std::shared_lock lock(mutex);
   const auto it = variants.find(hash);
   return it != variants.end() && std::ranges::any_of(it->second, [](const Variant& variant) { return variant.compiled != nullptr; });
}

std::vector<Variant> GetAll()
{
   std::shared_lock lock(mutex);
   std::vector<Variant> list;
   for (const auto& [hash, hash_variants] : variants)
      list.append_range(hash_variants);
   return list;
}

void MarkChoiceChanged()
{
   generation.fetch_add(1, std::memory_order_release);
}

uint64_t GetGeneration()
{
   return generation.load(std::memory_order_acquire);
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
