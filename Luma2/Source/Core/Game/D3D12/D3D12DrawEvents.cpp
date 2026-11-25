#include "Game/D3D12/D3D12DrawEvents.hpp"

#include <wrl/client.h>

#include "Settings/SettingsRegistry.hpp"
#include "Game/GameRegistry.hpp"
#include "Hooks/HookScope.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"

// Inside Luma::Game, `Shaders` and `Frame` are the game API's namespaces: Core's are named with Luma::
namespace Luma {
namespace Game {
namespace DrawEvents {
namespace D3D12 {
namespace {
using List = ID3D12GraphicsCommandList;
using Luma::Shaders::Variants::D3D12::BoundPipeline;

Luma::Shaders::StageSet GetVariantStages(List* list, Luma::Shaders::StageSet stages)
{
   const BoundPipeline bound = Luma::Shaders::Variants::D3D12::GetBoundPipeline(list);
   return bound.built ? bound.built->variant_stages & stages : Luma::Shaders::StageSet();
}

DrawResult RunEvents(List* list, const Game::D3D12::DrawCall* draw, const Game::D3D12::DispatchCall* dispatch, Luma::Shaders::StageSet stages)
{
   if (!Settings::IsEnabled())
      return DrawResult::None;
   const BoundPipeline bound = Luma::Shaders::Variants::D3D12::GetBoundPipeline(list);
   Game::D3D12::Call call = {
      .list = list,
      .draw = draw,
      .dispatch = dispatch,
      .pipeline = bound.game,
      .variant_stages = bound.built ? bound.built->variant_stages & stages : Luma::Shaders::StageSet(),
   };
   for (size_t stage = 0; bound.tracked && stage < call.hashes.size(); stage++)
   {
      if (stages.Has(ShaderStage(stage)) && bound.tracked->shaders[stage])
         call.hashes[stage] = bound.tracked->shaders[stage]->hash;
   }
   return Registry::DispatchDrawEvents<Api::D3D12>(call, stages, [&] { return GetVariantStages(list, stages); });
}
} // namespace

DrawResult Run(List* list, const Game::D3D12::DrawCall& draw)
{
   return RunEvents(list, &draw, nullptr, Luma::Shaders::GRAPHICS_STAGES);
}

DrawResult Run(List* list, const Game::D3D12::DispatchCall& dispatch)
{
   return RunEvents(list, nullptr, &dispatch, Luma::Shaders::COMPUTE_STAGES);
}
} // namespace D3D12
} // namespace DrawEvents

void Game::D3D12::UseOriginal(const Call& call)
{
   Luma::Shaders::Variants::D3D12::UseOriginal(call.list);
}

std::optional<D3D12_VIEWPORT> Game::D3D12::GetViewport(ID3D12GraphicsCommandList* list, UINT index)
{
   const std::optional<Luma::Resources::Mirrors::D3D12::ListScalingSnapshot> snapshot = Luma::Resources::Mirrors::D3D12::GetListScalingSnapshot(list);
   if (!snapshot || index >= snapshot->game_viewport_count)
      return std::nullopt;
   return snapshot->game_viewports[index];
}

void Game::D3D12::RunOriginal(const Call& call)
{
   ID3D12GraphicsCommandList* list = call.list;
   // The variant bound reads Luma's cbuffers: they're bound first, as for a call that runs untouched
   const bool compute = call.dispatch != nullptr;
   if (const Luma::Shaders::Variants::D3D12::Built* built = Luma::Shaders::Variants::D3D12::GetBound(list))
      Luma::Resources::Database::D3D12::BindRequested(list, compute, built->entries);
   Hooks::Scope::Guard scope;
   const auto indirect = [&](const IndirectArguments& arguments) {
      list->ExecuteIndirect(arguments.signature, arguments.max_count, arguments.arguments, arguments.arguments_offset, arguments.count_buffer, arguments.count_offset);
   };
   if (const DispatchCall* dispatch = call.dispatch)
   {
      if (dispatch->indirect_call)
         indirect(dispatch->indirect);
      else
         list->Dispatch(dispatch->x, dispatch->y, dispatch->z);
      return;
   }
   const DrawCall& draw = *call.draw;
   switch (draw.function)
   {
   case DrawFunction::DrawInstanced: list->DrawInstanced(draw.vertex_or_index_count, draw.instance_count, draw.start_vertex_or_index, draw.start_instance); break;
   case DrawFunction::DrawIndexedInstanced:
      list->DrawIndexedInstanced(draw.vertex_or_index_count, draw.instance_count, draw.start_vertex_or_index, draw.base_vertex, draw.start_instance);
      break;
   case DrawFunction::DispatchMesh:
      if (Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6> list6; SUCCEEDED(list->QueryInterface(IID_PPV_ARGS(&list6))))
         list6->DispatchMesh(draw.mesh_groups[0], draw.mesh_groups[1], draw.mesh_groups[2]);
      break;
   case DrawFunction::ExecuteIndirect: indirect(draw.indirect); break;
   }
}
} // namespace Game
} // namespace Luma
