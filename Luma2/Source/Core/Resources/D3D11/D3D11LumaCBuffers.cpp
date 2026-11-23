#include "Resources/D3D11/D3D11LumaCBuffers.hpp"

#include <array>

#include <wrl/client.h>

#include "Hooks/D3D11/D3D11Stages.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/LumaCBuffers.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace LumaCBuffers {
namespace D3D11 {
namespace {
struct Slot
{
   // Held: the game may release a buffer it bound, which Luma's bind then unbinds
   ComPtr<ID3D11Buffer> buffer;
   // The D3D11.1 range, 0 constants when the whole buffer is bound
   UINT first_constant = 0;
   UINT constant_count = 0;
};

// In the context's private data: the settings slot, then the data slot, per stage
struct GameBuffers
{
   std::array<std::array<Slot, 2>, 6> stages;
};
} // namespace

void RecordGameBuffers(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first_constants,
   const UINT* constant_counts)
{
   const Options& options = GetOptions();
   const std::array<uint32_t, 2> slots = {options.settings_slot, options.data_slot};
   GameBuffers* state = nullptr;
   for (size_t i = 0; i < slots.size(); i++)
   {
      if (slots[i] < start || slots[i] >= start + count || !buffers)
         continue;
      if (!state && !(state = Hooks::Lifetime::GetOrCreateStateCached<GameBuffers>(context)))
         return;
      const UINT index = slots[i] - start;
      Slot& slot = state->stages[size_t(stage)][i];
      slot.buffer = buffers[index];
      slot.first_constant = first_constants && constant_counts ? first_constants[index] : 0;
      slot.constant_count = first_constants && constant_counts ? constant_counts[index] : 0;
   }
}

void ForgetGameBuffers(ID3D11DeviceContext* context)
{
   if (GameBuffers* state = Hooks::Lifetime::GetOrCreateStateCached<GameBuffers>(context))
      *state = {};
}

void RestoreGameBuffers(ID3D11DeviceContext* context, Shaders::StageSet stages)
{
   GameBuffers* state = Hooks::Lifetime::GetOrCreateStateCached<GameBuffers>(context);
   if (!state)
      return;
   const Options& options = GetOptions();
   const std::array<uint32_t, 2> slots = {options.settings_slot, options.data_slot};
   ComPtr<ID3D11DeviceContext1> context1;
   for (uint32_t index = 0; index < state->stages.size(); index++)
   {
      if (!stages.Has(ShaderStage(index)))
         continue;
      Hooks::D3D11::WithStage(ShaderStage(index), [&]<ShaderStage Stage>() {
         using Traits = Hooks::D3D11::StageTraits<Stage>;
         for (size_t i = 0; i < slots.size(); i++)
         {
            const Slot& slot = state->stages[index][i];
            ID3D11Buffer* buffer = slot.buffer.Get();
            if (slot.constant_count && (context1 || SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1)))))
               (context1.Get()->*Traits::SetConstantBuffers1)(slots[i], 1, &buffer, &slot.first_constant, &slot.constant_count);
            else
               (context->*Traits::SetConstantBuffers)(slots[i], 1, &buffer);
         }
      });
   }
}
} // namespace D3D11
} // namespace LumaCBuffers
} // namespace Resources
} // namespace Luma
