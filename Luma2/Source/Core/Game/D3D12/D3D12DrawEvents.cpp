#include "Game/D3D12/D3D12DrawEvents.hpp"

#include <mutex>
#include <unordered_map>

#include <wrl/client.h>

#include "Effects/SR/D3D12/D3D12SuperResolution.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Game/GameRegistry.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/D3D12/D3D12ConstantCapture.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Resources/LumaCBuffers.hpp"
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

ID3D12Resource* Game::D3D12::TagBoundSRV(const Call& call, ShaderStage stage, UINT slot, uint64_t name_hash)
{
   return Luma::Resources::Database::D3D12::TagBoundSRV(call.list, stage, slot, name_hash);
}

ID3D12Resource* Game::D3D12::TagBoundRTV(const Call& call, UINT slot, uint64_t name_hash)
{
   return Luma::Resources::Database::D3D12::TagBoundRTV(call.list, slot, name_hash);
}

ID3D12Resource* Game::D3D12::TagBoundDSV(const Call& call, uint64_t name_hash)
{
   return Luma::Resources::Database::D3D12::TagBoundDSV(call.list, name_hash);
}

ID3D12Resource* Game::D3D12::EnsureTexture(ID3D12Device* device, uint64_t name_hash, UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags)
{
   return Luma::Resources::Database::D3D12::EnsureTexture(device, name_hash, width, height, format, flags);
}

ID3D12Resource* Game::D3D12::GetResource(ID3D12Device* device, uint64_t name_hash)
{
   return Luma::Resources::Database::D3D12::GetResource(device, name_hash);
}

D3D12_CPU_DESCRIPTOR_HANDLE Game::D3D12::GetRTV(ID3D12Device* device, uint64_t name_hash)
{
   return Luma::Resources::Database::D3D12::GetRTV(device, name_hash);
}

bool Game::D3D12::ReadBoundCBuffer(const Call& call, ShaderStage stage, UINT slot, std::span<std::byte> destination)
{
   return Luma::Resources::ConstantCapture::D3D12::ReadBoundCBuffer(call.list, stage, slot, destination);
}

namespace {
struct FullscreenPsoKey
{
   ID3D12RootSignature* root_sig = nullptr;
   uint64_t vs_hash = 0;
   uint64_t ps_hash = 0;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   uint64_t generation = 0;

   bool operator==(const FullscreenPsoKey&) const = default;
};

struct FullscreenPsoKeyHash
{
   size_t operator()(const FullscreenPsoKey& k) const noexcept
   {
      size_t h = reinterpret_cast<uintptr_t>(k.root_sig);
      h ^= k.vs_hash + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      h ^= k.ps_hash + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      h ^= size_t(k.format) * 0x517cc1b727220a95ULL;
      h ^= k.generation;
      return h;
   }
};

struct FullscreenPsoCache
{
   std::mutex mutex;
   std::unordered_map<FullscreenPsoKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, FullscreenPsoKeyHash> psos;
};
} // namespace

bool Game::D3D12::DrawFullscreenToEntry(const Call& call, uint64_t vertex_shader_hash, uint64_t pixel_shader_hash, uint64_t target_entry_hash,
   DXGI_FORMAT target_format, Math::uint2 viewport_size)
{
   if (!call.list || viewport_size.x == 0 || viewport_size.y == 0)
      return false;
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   if (FAILED(call.list->GetDevice(IID_PPV_ARGS(&device))) || !device)
      return false;

   const Luma::Shaders::Variants::D3D12::BoundPipeline bound = Luma::Shaders::Variants::D3D12::GetBoundPipeline(call.list);
   ID3D12RootSignature* root_sig = bound.tracked ? bound.tracked->root_signature.Get() : nullptr;
   if (!root_sig)
      return false;

   const std::optional<uint32_t> vs_entry = Luma::Resources::Database::Find(vertex_shader_hash);
   const std::optional<uint32_t> ps_entry = Luma::Resources::Database::Find(pixel_shader_hash);
   if (!vs_entry || !ps_entry)
      return false;
   const Luma::Resources::Database::ShaderState vs_state = Luma::Resources::Database::GetShader(*vs_entry);
   const Luma::Resources::Database::ShaderState ps_state = Luma::Resources::Database::GetShader(*ps_entry);
   if (!vs_state.compiled || !ps_state.compiled || vs_state.compiled->bytecode.empty() || ps_state.compiled->bytecode.empty())
      return false;

   ID3D12Resource* target = Luma::Resources::Database::D3D12::ReferenceResource(call.list, target_entry_hash);
   const D3D12_CPU_DESCRIPTOR_HANDLE rtv = Luma::Resources::Database::D3D12::GetRTV(device.Get(), target_entry_hash);
   if (!target || rtv.ptr == 0)
      return false;

   FullscreenPsoCache* cache = Hooks::Lifetime::GetOrCreateStateCached<FullscreenPsoCache>(device.Get());
   if (!cache)
      return false;

   const FullscreenPsoKey key = {root_sig, vertex_shader_hash, pixel_shader_hash, target_format, Luma::Resources::Database::GetGeneration()};
   Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
   {
      std::scoped_lock lock(cache->mutex);
      if (const auto it = cache->psos.find(key); it != cache->psos.end())
         pso = it->second;
   }
   if (!pso)
   {
      D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
      desc.pRootSignature = root_sig;
      desc.VS = {vs_state.compiled->bytecode.data(), vs_state.compiled->bytecode.size()};
      desc.PS = {ps_state.compiled->bytecode.data(), ps_state.compiled->bytecode.size()};
      desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
      desc.SampleMask = UINT_MAX;
      desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
      desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      desc.NumRenderTargets = 1;
      desc.RTVFormats[0] = target_format;
      desc.SampleDesc.Count = 1;
      Hooks::Scope::Guard scope;
      if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso))))
         return false;
      std::scoped_lock lock(cache->mutex);
      cache->psos.emplace(key, pso);
   }

   // Bind LumaSettings/LumaData on the game's extended root signature
   std::vector<uint32_t> entries;
   for (const Luma::Shaders::Request& req : Luma::Resources::LumaCBuffers::GetRequests())
      entries.push_back(req.entry);
   Luma::Resources::Database::D3D12::BindRequested(call.list, false, entries);

   Hooks::Scope::Guard scope;
   const D3D12_RESOURCE_STATES before_state = Luma::Resources::Database::D3D12::GetManagedState(device.Get(), target_entry_hash);
   if (before_state != D3D12_RESOURCE_STATE_RENDER_TARGET)
   {
      D3D12_RESOURCE_BARRIER to_rt = {};
      to_rt.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      to_rt.Transition = {target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before_state, D3D12_RESOURCE_STATE_RENDER_TARGET};
      call.list->ResourceBarrier(1, &to_rt);
   }

   const D3D12_VIEWPORT vp = {0.f, 0.f, float(viewport_size.x), float(viewport_size.y), 0.f, 1.f};
   const D3D12_RECT scissor = {0, 0, LONG(viewport_size.x), LONG(viewport_size.y)};
   call.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
   call.list->RSSetViewports(1, &vp);
   call.list->RSSetScissorRects(1, &scissor);
   call.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   call.list->SetPipelineState(pso.Get());
   call.list->DrawInstanced(4, 1, 0, 0);

   D3D12_RESOURCE_BARRIER to_srv = {};
   to_srv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
   to_srv.Transition = {target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
   call.list->ResourceBarrier(1, &to_srv);
   Luma::Resources::Database::D3D12::SetManagedState(device.Get(), target_entry_hash, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

   Luma::Resources::Database::D3D12::RestoreListState(call.list);
   return true;
}

bool Game::Effects::SR::Draw(const Game::D3D12::Call& call, const DrawOverrides& overrides)
{
   return Luma::Effects::SR::D3D12::Draw(call.list, overrides);
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
