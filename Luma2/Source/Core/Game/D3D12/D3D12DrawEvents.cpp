#include "Game/D3D12/D3D12DrawEvents.hpp"

#include <array>

#include <wrl/client.h>

#include "Game/Registry.hpp"
#include "Hooks/Scope.hpp"
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

// The callback of the first stage with one, pixel first, then vertex
Game::D3D12::DrawCallback FindCallback(const Game::D3D12::Call& call, Luma::Shaders::StageSet stages)
{
   constexpr std::array ORDER = {ShaderStage::Pixel, ShaderStage::Vertex, ShaderStage::Geometry, ShaderStage::Hull, ShaderStage::Domain, ShaderStage::Compute};
   for (const ShaderStage stage : ORDER)
   {
      if (!stages.Has(stage) || !call.hashes[size_t(stage)])
         continue;
      if (const Game::D3D12::DrawCallback callback = Registry::FindD3D12DrawCallback(call.hashes[size_t(stage)]))
         return callback;
   }
   return nullptr;
}

Luma::Shaders::StageSet GetVariantStages(List* list, Luma::Shaders::StageSet stages)
{
   const BoundPipeline bound = Luma::Shaders::Variants::D3D12::GetBoundPipeline(list);
   return bound.built ? bound.built->variant_stages & stages : Luma::Shaders::StageSet();
}

DrawResult RunEvents(List* list, const Game::D3D12::DrawCall* draw, const Game::D3D12::DispatchCall* dispatch, Luma::Shaders::StageSet stages)
{
   if (!IsEnabled())
      return DrawResult::None;
   const BoundPipeline bound = Luma::Shaders::Variants::D3D12::GetBoundPipeline(list);
   Game::D3D12::Call call = {.list = list, .draw = draw, .dispatch = dispatch, .pipeline = bound.game};
   for (size_t stage = 0; bound.tracked && stage < call.hashes.size(); stage++)
   {
      if (stages.Has(ShaderStage(stage)) && bound.tracked->shaders[stage])
         call.hashes[stage] = bound.tracked->shaders[stage]->hash;
   }
   call.variant_stages = bound.built ? bound.built->variant_stages & stages : Luma::Shaders::StageSet();
   const Game::D3D12::Events& events = Registry::GetD3D12Events();
   const Game::D3D12::DrawCallback variant_callback = call.variant_stages.Any() ? events.on_variant_draw : nullptr;
   const Game::D3D12::DrawCallback shader_callback = FindCallback(call, stages);
   const Game::D3D12::DrawCallback callback = shader_callback ? shader_callback : events.on_draw_or_dispatch;
   if (!variant_callback && !callback)
      return DrawResult::None;
   if (variant_callback)
   {
      if (const DrawResult result = variant_callback(call); result != DrawResult::None)
         return result;
      // It may have put the game's pipeline back (UseOriginal)
      call.variant_stages = GetVariantStages(list, stages);
   }
   return callback ? callback(call) : DrawResult::None;
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
