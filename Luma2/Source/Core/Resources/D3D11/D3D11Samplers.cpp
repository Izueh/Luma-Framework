#include "Resources/D3D11/D3D11Samplers.hpp"

#include <algorithm>
#include <atomic>
#include <optional>
#include <mutex>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "Hooks/Lifetime.hpp"
#include "Resources/Samplers.hpp"
#include "Utils/Debug.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Samplers {
namespace D3D11 {
namespace {
constexpr uint32_t NO_GENERATION = UINT32_MAX;

// In the game's sampler's private data
struct SamplerState
{
   ID3D11SamplerState* sampler = nullptr; // Its own: the state lives in it
   D3D11_SAMPLER_DESC desc = {};
   // The mirror for the offset of `generation`, null when the sampler isn't upgraded. Read lock-free at every bind.
   std::atomic<ID3D11SamplerState*> current = nullptr;
   std::atomic<uint32_t> generation = NO_GENERATION;
   // Every mirror made, by offset: they keep `current` alive
   std::mutex mutex;
   std::vector<std::pair<float, ComPtr<ID3D11SamplerState>>> mirrors;

   SamplerState(ID3D11SamplerState* sampler, const D3D11_SAMPLER_DESC& desc) : sampler(sampler), desc(desc) {}
};

// The mirror for the current offset, made the first time
ID3D11SamplerState* Refresh(SamplerState& state, uint32_t generation)
{
   std::scoped_lock lock(state.mutex);
   if (state.generation.load(std::memory_order_acquire) == generation)
      return state.current.load(std::memory_order_relaxed);
   const float offset = GetLodBiasOffset();
   ID3D11SamplerState* mirror = nullptr;
   const auto found = std::ranges::find(state.mirrors, offset, &std::pair<float, ComPtr<ID3D11SamplerState>>::first);
   if (found != state.mirrors.end())
      mirror = found->second.Get();
   else if (const std::optional<Desc> mirror_desc = GetMirrorDesc({uint32_t(state.desc.Filter), state.desc.MipLODBias, state.desc.MaxAnisotropy, state.desc.MinLOD}, offset))
   {
      D3D11_SAMPLER_DESC desc = state.desc;
      desc.Filter = D3D11_FILTER(mirror_desc->filter);
      desc.MipLODBias = mirror_desc->mip_lod_bias;
      desc.MaxAnisotropy = mirror_desc->max_anisotropy;
      desc.MinLOD = mirror_desc->min_lod;
      ComPtr<ID3D11Device> device;
      state.sampler->GetDevice(&device);
      ComPtr<ID3D11SamplerState> created;
      // D3D11 shares state objects: a desc another sampler has gives that one
      ASSERT_ONCE(SUCCEEDED(device->CreateSamplerState(&desc, &created)));
      mirror = created.Get();
      state.mirrors.emplace_back(offset, std::move(created));
   }
   else
      state.mirrors.emplace_back(offset, nullptr);
   state.current.store(mirror, std::memory_order_relaxed);
   state.generation.store(generation, std::memory_order_release);
   return mirror;
}
} // namespace

void TrackSampler([[maybe_unused]] ID3D11Device* device, const D3D11_SAMPLER_DESC& desc, ID3D11SamplerState* sampler)
{
   // D3D11 gives the same object for the same desc: tracked once
   if (!sampler || Hooks::Lifetime::GetState<SamplerState>(sampler))
      return;
   Hooks::Lifetime::SetState<SamplerState>(sampler, sampler, desc);
}

void SwapSamplers(ID3D11SamplerState** samplers, UINT count)
{
   if (!samplers)
      return;
   const uint32_t generation = GetGeneration();
   for (UINT i = 0; i < count; i++)
   {
      SamplerState* state = samplers[i] ? Hooks::Lifetime::GetState<SamplerState>(samplers[i]) : nullptr;
      if (!state)
         continue;
      ID3D11SamplerState* mirror =
         state->generation.load(std::memory_order_acquire) == generation ? state->current.load(std::memory_order_relaxed) : Refresh(*state, generation);
      if (mirror)
         samplers[i] = mirror;
   }
}
} // namespace D3D11
} // namespace Samplers
} // namespace Resources
} // namespace Luma
