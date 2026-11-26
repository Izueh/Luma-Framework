#include "Resources/D3D11/D3D11Samplers.hpp"

#include <algorithm>
#include <atomic>
#include <optional>
#include <mutex>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "Hooks/ObjectLifetime.hpp"
#include "Hooks/HookScope.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Utils/Debug.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Samplers {
namespace D3D11 {
namespace {
constexpr uint32_t NO_GENERATION = UINT32_MAX;

struct SamplerState;

// In the device's private data: its tracked samplers, for the present after its offset changed. Its samplers hold the device, so
// it outlives them.
struct Registry
{
   // Lock order: before a sampler's
   std::mutex mutex;
   std::vector<SamplerState*> samplers;
   // Presenting thread
   uint32_t refreshed_generation = NO_GENERATION;
};

// Serializes first attachment of Registry to a device (CreateSamplerState is thread-safe)
std::mutex registry_create_mutex;

Registry* GetRegistry(ID3D11Device* device)
{
   if (Registry* registry = Hooks::Lifetime::GetState<Registry>(device))
      return registry;
   std::scoped_lock lock(registry_create_mutex);
   if (Registry* registry = Hooks::Lifetime::GetState<Registry>(device))
      return registry;
   return Hooks::Lifetime::SetState<Registry>(device);
}

// In the game's sampler's private data
struct SamplerState
{
   ID3D11SamplerState* sampler = nullptr; // Compared only: its final release destroys this
   ID3D11Device* device = nullptr;
   Registry* registry = nullptr;
   D3D11_SAMPLER_DESC desc = {};
   // The mirror for the offset of `generation`, null when the sampler isn't upgraded. Read lock-free at every bind.
   std::atomic<ID3D11SamplerState*> current = nullptr;
   std::atomic<uint32_t> generation = NO_GENERATION;
   // Every mirror made, by offset: they keep `current` alive
   std::mutex mutex;
   std::vector<std::pair<float, ComPtr<ID3D11SamplerState>>> mirrors;

   SamplerState(ID3D11SamplerState* sampler_, ID3D11Device* device_, Registry* registry_, const D3D11_SAMPLER_DESC& desc_)
      : sampler(sampler_), device(device_), registry(registry_), desc(desc_)
   {
      std::scoped_lock lock(registry->mutex);
      registry->samplers.push_back(this);
   }

   ~SamplerState()
   {
      std::scoped_lock lock(registry->mutex);
      std::erase(registry->samplers, this);
   }
};

// The mirror for the current offset, made the first time
void Refresh(SamplerState& state, uint32_t generation)
{
   std::scoped_lock lock(state.mutex);
   if (state.generation.load(std::memory_order_acquire) == generation)
      return;
   const float offset = GetLodBiasOffset(state.device);
   ID3D11SamplerState* mirror = nullptr;
   const auto found = std::ranges::find(state.mirrors, offset, &std::pair<float, ComPtr<ID3D11SamplerState>>::first);
   if (found != state.mirrors.end())
      mirror = found->second.Get();
   else if (const std::optional<D3D11_SAMPLER_DESC> mirror_desc = GetMirrorDesc(state.desc, offset))
   {
      ComPtr<ID3D11SamplerState> created;
      {
         const Hooks::Scope::Guard scope;
         ASSERT_ONCE(SUCCEEDED(state.device->CreateSamplerState(&*mirror_desc, &created)));
      }
      mirror = created.Get();
      // D3D11 deduplicates samplers by desc: never store a self-reference or back-edge to another tracked sampler that could cycle
      if (mirror == state.sampler)
         mirror = nullptr;
      else if (mirror && Hooks::Lifetime::GetState<SamplerState>(mirror))
         state.mirrors.emplace_back(offset, nullptr);
      else
         state.mirrors.emplace_back(offset, std::move(created));
   }
   else
      state.mirrors.emplace_back(offset, nullptr);
   state.current.store(mirror, std::memory_order_relaxed);
   state.generation.store(generation, std::memory_order_release);
}
} // namespace

void TrackSampler(ID3D11Device* device, const D3D11_SAMPLER_DESC& desc, ID3D11SamplerState* sampler)
{
   // D3D11 gives the same object for the same desc: tracked once
   if (!sampler || Hooks::Lifetime::GetState<SamplerState>(sampler))
      return;
   Registry* registry = GetRegistry(device);
   if (!registry)
      return;
   if (SamplerState* state = Hooks::Lifetime::SetState<SamplerState>(sampler, sampler, device, registry, desc))
      Refresh(*state, GetGeneration(device));
}

void RefreshMirrors(ID3D11Device* device)
{
   Registry* registry = Hooks::Lifetime::GetState<Registry>(device);
   if (!registry)
      return;
   const uint32_t generation = GetGeneration(device);
   if (std::exchange(registry->refreshed_generation, generation) == generation)
      return;
   std::scoped_lock lock(registry->mutex);
   for (SamplerState* state : registry->samplers)
      Refresh(*state, generation);
}

void SwapSamplers(ID3D11SamplerState** samplers, UINT count)
{
   if (!samplers)
      return;
   for (UINT i = 0; i < count; i++)
   {
      const SamplerState* state = samplers[i] ? Hooks::Lifetime::GetState<SamplerState>(samplers[i]) : nullptr;
      if (ID3D11SamplerState* mirror = state ? state->current.load(std::memory_order_acquire) : nullptr)
         samplers[i] = mirror;
   }
}
} // namespace D3D11
} // namespace Samplers
} // namespace Resources
} // namespace Luma
