#include "Shaders/D3D11/D3D11Variants.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "Hooks/Lifetime.hpp"
#include "Hooks/Scope.hpp"
#include "Luma.hpp"
#include "Resources/D3D11/D3D11ResourceDatabase.hpp"
#include "Shaders/D3D11/D3D11Database.hpp"
#include "Shaders/D3D11/D3D11Shaders.hpp"
#include "Shaders/Variants.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Log.hpp"

namespace Luma {
namespace Shaders {
namespace Variants {
namespace D3D11 {
namespace {
constexpr uint16_t NO_SOURCE = 0xF;

// A hash's variant objects, one per source
struct Variant
{
   const Database::Shader* shader;
   // An object of the hash was made with stream output, which a replacement can't have
   bool stream_output = false;
   std::array<Shaders::D3D11::Published, SOURCE_COUNT> published;
   // The sources with a compiled shader, highest priority first, 4 bits each and NO_SOURCE after the last: read at bind
   std::atomic<uint16_t> order = NO_SOURCE;

   explicit Variant(const Database::Shader* shader_) : shader(shader_) {}
};

// In the device's private data, so its memory outlives every game object pointing into it. Its objects hold the device:
// they're released when the game releases it (ReleaseVariants), which breaks the cycle.
struct Store
{
   // Lock order: before Variants' publication lock (GetCompiledShader)
   std::mutex mutex;
   std::unordered_map<uint32_t, std::unique_ptr<Variant>> variants;
   Shaders::D3D11::Retired retired;
   uint64_t generation = 0; // Of the publication applied
   bool released = false;
};

// In the private data of each of the game's shader objects
struct Binding
{
   Variant* variant;
   bool stream_output; // Made with stream output: never replaced
};

// With the store locked: the objects of the hash's variants as published now, and their order
void PublishVariant(ID3D11Device* device, Store& store, Variant& variant)
{
   const uint32_t hash = variant.shader->hash;
   for (size_t index = 0; index < SOURCE_COUNT; index++)
   {
      const Source source = Source(index);
      std::shared_ptr<const CompiledShader> compiled = GetCompiledShader(hash, source);
      if (compiled && variant.stream_output)
         LUMA_UNIMPLEMENTED("Shaders::Variants of geometry shaders created with stream output");
      Shaders::D3D11::Published& published = variant.published[index];
      const bool had_object = published.live != nullptr;
      if (!Shaders::D3D11::Publish(device, variant.shader->stage, published, std::move(compiled), store.retired))
         Utils::Log::Error("Shaders: the device refused the {} variant of 0x{:08X}", GetName(source), hash);
      else if (had_object != (published.live != nullptr))
         Utils::Log::Verbose("Shaders: 0x{:08X} {} its {} variant", hash, had_object ? "no longer has" : "has", GetName(source));
   }
   uint16_t order = NO_SOURCE;
   uint32_t shift = 0;
   for (const Variants::Variant& published : GetVariants(hash))
   {
      if (!published.compiled)
         continue;
      order = uint16_t((order & ~(0xFu << shift)) | (uint32_t(published.source) << shift) | (NO_SOURCE << (shift + 4)));
      shift += 4;
   }
   variant.order.store(order, std::memory_order_release);
}

// The variant a bind takes: the forced one (trace), else the highest-priority one the device has an object for. Null for the
// game's shader.
const Shaders::D3D11::Live* Choose(const Variant& variant, Choice forced, Source& source)
{
   if (forced == Choice::Original)
      return nullptr;
   if (forced != Choice::Auto)
   {
      source = Source(uint8_t(forced) - uint8_t(Choice::File));
      return variant.published[size_t(source)].current.load(std::memory_order_acquire);
   }
   for (uint16_t order = variant.order.load(std::memory_order_acquire); (order & 0xF) != NO_SOURCE; order >>= 4)
   {
      source = Source(order & 0xF);
      if (const Shaders::D3D11::Live* live = variant.published[size_t(source)].current.load(std::memory_order_acquire))
         return live;
   }
   return nullptr;
}
} // namespace

void CreateVariantStore(ID3D11Device* device)
{
   Hooks::Lifetime::SetState<Store>(device);
}

void AttachVariant(ID3D11Device* device, ID3D11DeviceChild* object, bool stream_output)
{
   const Database::Shader* shader = Database::D3D11::Find(object);
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   if (!shader || !store)
      return;
   std::scoped_lock lock(store->mutex);
   if (store->released)
      return;
   std::unique_ptr<Variant>& variant = store->variants[shader->hash];
   const bool created = !variant;
   if (created)
      variant = std::make_unique<Variant>(shader);
   if (stream_output && !variant->stream_output)
   {
      variant->stream_output = true;
      if (!created && std::ranges::any_of(variant->published, [](const Shaders::D3D11::Published& published) { return published.compiled != nullptr; }))
         LUMA_UNIMPLEMENTED("Shaders::Variants of geometry shaders created with stream output");
   }
   if (created)
      PublishVariant(device, *store, *variant);
   Hooks::Lifetime::SetState<Binding>(object, variant.get(), stream_output);
}

void SwapInVariant(ID3D11DeviceContext* context, ShaderStage stage, ID3D11DeviceChild*& shader)
{
   // Tracked whether Luma is on or not: draw callbacks and trace find the game's shaders by it
   const Binding* binding = shader ? Hooks::Lifetime::GetState<Binding>(shader) : nullptr;
   const Database::Shader* game_shader = binding ? binding->variant->shader : nullptr;
   const Shaders::D3D11::Live* replacement = nullptr;
   Source source = Source::File;
   if (binding && !binding->stream_output && IsEnabled() && IsLoaded())
   {
#if DEVELOPMENT
      const Choice forced = game_shader->forced_variant.load(std::memory_order_relaxed);
#else
      constexpr Choice forced = Choice::Auto;
#endif
      replacement = Choose(*binding->variant, forced, source);
   }
   if (BoundShaders* state = Hooks::Lifetime::GetOrCreateStateCached<BoundShaders>(context))
   {
      const uint8_t bit = uint8_t(1u << uint32_t(stage));
      state->shaders[size_t(stage)] = game_shader;
      state->originals[size_t(stage)] = replacement ? shader : nullptr;
      state->compiled[size_t(stage)] = replacement ? replacement->compiled.get() : nullptr;
      state->sources[size_t(stage)] = source;
      state->variant_stages = replacement ? (state->variant_stages | bit) : (state->variant_stages & ~bit);
      state->callback_stages = game_shader && game_shader->draw_callback ? (state->callback_stages | bit) : (state->callback_stages & ~bit);
   }
   if (replacement)
      shader = replacement->object.Get();
}

void UseOriginal(ID3D11DeviceContext* context, ShaderStage stage)
{
   BoundShaders* state = Hooks::Lifetime::GetOrCreateStateCached<BoundShaders>(context);
   const uint8_t bit = uint8_t(1u << uint32_t(stage));
   if (!state || !(state->variant_stages & bit))
      return;
   Hooks::Scope::Guard scope;
   Shaders::D3D11::Bind(context, stage, state->originals[size_t(stage)].Get());
   state->originals[size_t(stage)].Reset();
   state->compiled[size_t(stage)] = nullptr;
   state->variant_stages &= uint8_t(~bit);
}

void ForgetBoundShaders(ID3D11DeviceContext* context)
{
   if (BoundShaders* state = Hooks::Lifetime::GetOrCreateStateCached<BoundShaders>(context))
      *state = {};
}

const BoundShaders* GetBoundShaders(ID3D11DeviceContext* context)
{
   return Hooks::Lifetime::GetOrCreateStateCached<BoundShaders>(context);
}

uint8_t BindRequestedResources(ID3D11DeviceContext* context, uint8_t stages)
{
   const BoundShaders* bound = GetBoundShaders(context);
   if (!bound || !(bound->variant_stages & stages))
      return 0;
   uint8_t fulfilled = 0;
   for (uint32_t stage = 0; stage < bound->compiled.size(); stage++)
   {
      if ((bound->variant_stages & stages & (1u << stage)) && bound->compiled[stage] &&
         Resources::Database::D3D11::BindRequested(context, ShaderStage(stage), *bound->compiled[stage], {}))
         fulfilled |= uint8_t(1u << stage);
   }
   return fulfilled;
}

void PublishVariants(ID3D11Device* device)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   if (!store)
      return;
   std::scoped_lock lock(store->mutex);
   if (store->released)
      return;
   Shaders::D3D11::ReleaseRetired(store->retired);
   const uint64_t generation = GetGeneration();
   if (store->generation == generation)
      return;
   store->generation = generation;
   for (const auto& [hash, variant] : store->variants)
      PublishVariant(device, *store, *variant);
}

void ReleaseVariants(ID3D11Device* device)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   if (!store)
      return;
   std::scoped_lock lock(store->mutex);
   for (const auto& [hash, variant] : store->variants)
   {
      for (Shaders::D3D11::Published& published : variant->published)
         Shaders::D3D11::Release(published);
      variant->order.store(NO_SOURCE, std::memory_order_release);
   }
   store->retired.lives.clear();
   store->released = true;
}
} // namespace D3D11
} // namespace Variants
} // namespace Shaders
} // namespace Luma
