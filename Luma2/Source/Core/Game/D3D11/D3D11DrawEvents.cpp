#include "Game/D3D11/D3D11DrawEvents.hpp"

#include <optional>
#include <utility>

#include <wrl/client.h>

#include "Settings/SettingsRegistry.hpp"
#include "Game/GameRegistry.hpp"
#include "Hooks/HookFunctions.hpp"
#include "Resources/D3D11/D3D11ResourceDatabase.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Utils/Debug.hpp"

using Microsoft::WRL::ComPtr;

// Inside Luma::Game, `Shaders` and `Frame` are the game API's namespaces: Core's are named with Luma::
namespace Luma {
namespace Game {
namespace DrawEvents {
namespace D3D11 {
namespace {
using Context = ID3D11DeviceContext;

struct Running
{
   Context* context = nullptr;
   Luma::Shaders::StageSet luma_stages;
};

thread_local Running* running = nullptr;

DrawResult RunEvents(Context* context, const Game::D3D11::DrawCall* draw, const Game::D3D11::DispatchCall* dispatch, Luma::Shaders::StageSet stages)
{
   const Luma::Shaders::Variants::D3D11::BoundShaders* bound = Luma::Shaders::Variants::D3D11::GetBoundShaders(context);
   Game::D3D11::Call call = {
      .context = context,
      .draw = draw,
      .dispatch = dispatch,
      .variant_stages = bound ? bound->variant_stages & stages : Luma::Shaders::StageSet(),
   };
   for (size_t stage = 0; bound && stage < bound->shaders.size(); stage++)
      call.hashes[stage] = bound->shaders[stage] ? bound->shaders[stage]->hash : 0;
   const Luma::Shaders::StageSet callback_stages = bound ? bound->callback_stages & stages : Luma::Shaders::StageSet();
   return Registry::DispatchDrawEvents<Api::D3D11>(call, callback_stages, [&] { return bound->variant_stages & stages; });
}

Result RunReporting(Context* context, const Game::D3D11::DrawCall* draw, const Game::D3D11::DispatchCall* dispatch, Luma::Shaders::StageSet stages)
{
   if (!Settings::IsEnabled())
      return {};
   Running current = {.context = context};
   Running* const previous = std::exchange(running, &current);
   const DrawResult result = RunEvents(context, draw, dispatch, stages);
   running = previous;
   return {result, current.luma_stages};
}
} // namespace

Result Run(Context* context, const Game::D3D11::DrawCall& draw)
{
   return RunReporting(context, &draw, nullptr, Luma::Shaders::GRAPHICS_STAGES);
}

Result Run(Context* context, const Game::D3D11::DispatchCall& dispatch)
{
   return RunReporting(context, nullptr, &dispatch, Luma::Shaders::COMPUTE_STAGES);
}
} // namespace D3D11
} // namespace DrawEvents

void Game::D3D11::BindLumaShader(ID3D11DeviceContext* context, uint64_t name_hash, const Luma::Resources::Database::CustomData& custom)
{
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   const Luma::Shaders::D3D11::Live* shader = Luma::Resources::Database::D3D11::GetShader(device.Get(), name_hash);
   const std::optional<uint32_t> entry = Luma::Resources::Database::Find(name_hash);
   if (!shader || !entry)
      return;
   const ShaderStage stage = Luma::Resources::Database::Get(*entry).stage;
   Luma::Shaders::D3D11::Bind(context, stage, shader->object.Get());
   // From a draw event, the handler restores the game's buffers after the call and binds no other requests
   using DrawEvents::D3D11::running;
   if (Luma::Resources::Database::D3D11::BindRequested(context, stage, *shader->compiled, custom) && running && running->context == context)
      running->luma_stages.Set(stage);
}

ID3D11ShaderResourceView* Game::D3D11::GetTexture(ID3D11Device* device, uint64_t name_hash)
{
   return Luma::Resources::Database::D3D11::GetTexture(device, name_hash);
}

void Game::D3D11::UseOriginal(const Call& call, ShaderStage stage)
{
   Luma::Shaders::Variants::D3D11::UseOriginal(call.context, stage);
}

void Game::D3D11::RunOriginal(const Call& call)
{
   using Function = Hooks::D3D11::Function;
   ID3D11DeviceContext* context = call.context;
   if (const DispatchCall* dispatch = call.dispatch)
   {
      if (dispatch->arguments)
         context->DispatchIndirect(dispatch->arguments, dispatch->arguments_offset);
      else
         context->Dispatch(dispatch->x, dispatch->y, dispatch->z);
      return;
   }
   const DrawCall& draw = *call.draw;
   switch (draw.function)
   {
   case Function::Draw: context->Draw(draw.vertex_or_index_count, draw.start_vertex_or_index); break;
   case Function::DrawInstanced: context->DrawInstanced(draw.vertex_or_index_count, draw.instance_count, draw.start_vertex_or_index, draw.start_instance); break;
   case Function::DrawIndexed: context->DrawIndexed(draw.vertex_or_index_count, draw.start_vertex_or_index, draw.base_vertex); break;
   case Function::DrawIndexedInstanced:
      context->DrawIndexedInstanced(draw.vertex_or_index_count, draw.instance_count, draw.start_vertex_or_index, draw.base_vertex, draw.start_instance);
      break;
   case Function::DrawAuto: context->DrawAuto(); break;
   case Function::DrawInstancedIndirect: context->DrawInstancedIndirect(draw.arguments, draw.arguments_offset); break;
   case Function::DrawIndexedInstancedIndirect: context->DrawIndexedInstancedIndirect(draw.arguments, draw.arguments_offset); break;
   default: ASSERT_ONCE_MSG(false, "Not a draw function"); break;
   }
}
} // namespace Game
} // namespace Luma
