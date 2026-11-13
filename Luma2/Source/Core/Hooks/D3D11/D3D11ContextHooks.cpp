#include "Hooks/Hooks.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <d3d11_4.h>
#include <wrl/client.h>

#include "Handlers/D3D11/D3D11Handlers.hpp"
#include "Hooks/D3D11/D3D11Stages.hpp"
#include "Hooks/Detours.hpp"
#include "Hooks/Lifetime.hpp"
#include "Hooks/Slots.h"

namespace Luma {
namespace Hooks {
namespace {
using Detours::original;

thread_local uint32_t context_depth = 0;

// Detours are instanced per context vtable: 0 is the immediate one, 1 the deferred one

template <auto Detour, auto Pre, typename Context, typename... Args>
void ReportBefore(Context* context, Args... args)
{
   DepthGuard guard(context_depth);
   if (Detours::IsGameCall(guard) && Detours::Report(Pre, context, args...))
      return;
   original<Detour>(context, args...);
}

template <auto Detour, auto Post, typename... Args>
void ReportAfter(ID3D11DeviceContext* context, Args... args)
{
   DepthGuard guard(context_depth);
   original<Detour>(context, args...);
   if (Detours::IsGameCall(guard))
      Detours::Report(Post, context, args...);
}

template <auto Detour, ShaderStage Stage, typename Shader>
void SetShader(ID3D11DeviceContext* context, Shader* shader, ID3D11ClassInstance* const* instances, UINT instance_count)
{
   DepthGuard guard(context_depth);
   if (!Detours::IsGameCall(guard))
      return original<Detour>(context, shader, instances, instance_count);
   ID3D11DeviceChild* game_shader = shader;
   if (Detours::Report(&Handlers::PreSetShader, context, Stage, game_shader))
      return;
   original<Detour>(context, static_cast<Shader*>(game_shader), instances, instance_count);
}

// The handler edits a copy of the game's array
template <auto Detour, auto Pre, size_t Max, typename Context, typename T, typename... Extra>
void SetArray(Context* context, UINT start, UINT count, T* const* items, Extra... extra)
{
   DepthGuard guard(context_depth);
   if (!Detours::IsGameCall(guard) || !items || count > Max)
      return original<Detour>(context, start, count, items, extra...);
   std::array<T*, Max> game_items;
   std::copy_n(items, count, game_items.begin());
   if (Detours::Report(Pre, context, start, count, game_items.data(), extra...))
      return;
   original<Detour>(context, start, count, game_items.data(), extra...);
}

// The same for the per-stage functions, whose handlers take the stage
template <auto Detour, auto Pre, ShaderStage Stage, size_t Max, typename Context, typename T, typename... Extra>
void SetStageArray(Context* context, UINT start, UINT count, T* const* items, Extra... extra)
{
   DepthGuard guard(context_depth);
   if (!Detours::IsGameCall(guard) || !items || count > Max)
      return original<Detour>(context, start, count, items, extra...);
   std::array<T*, Max> game_items;
   std::copy_n(items, count, game_items.begin());
   if (Detours::Report(Pre, context, Stage, start, count, game_items.data(), extra...))
      return;
   original<Detour>(context, start, count, game_items.data(), extra...);
}

bool PreSetConstantBuffers(ID3D11DeviceContext* context, ShaderStage stage, UINT start, UINT count, ID3D11Buffer** buffers)
{
   return Handlers::PreSetConstantBuffers(context, stage, start, count, buffers, nullptr, nullptr);
}

// One detour per method kind, instanced per stage (D3D11::StageTraits) and per vtable
template <ShaderStage Stage, size_t V>
void STDMETHODCALLTYPE StageSetShader(ID3D11DeviceContext* context, typename D3D11::StageTraits<Stage>::Shader* shader, ID3D11ClassInstance* const* instances,
   UINT count)
{
   SetShader<&StageSetShader<Stage, V>, Stage>(context, shader, instances, count);
}

template <ShaderStage Stage, size_t V>
void STDMETHODCALLTYPE StageSetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView* const* views)
{
   SetStageArray<&StageSetShaderResources<Stage, V>, &Handlers::PreSetShaderResources, Stage, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT>(context, start,
      count, views);
}

template <ShaderStage Stage, size_t V>
void STDMETHODCALLTYPE StageSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers)
{
   SetStageArray<&StageSetConstantBuffers<Stage, V>, &PreSetConstantBuffers, Stage, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>(context, start, count,
      buffers);
}

template <ShaderStage Stage, size_t V>
void STDMETHODCALLTYPE StageSetConstantBuffers1(ID3D11DeviceContext1* context, UINT start, UINT count, ID3D11Buffer* const* buffers,
   const UINT* first_constant, const UINT* num_constants)
{
   SetStageArray<&StageSetConstantBuffers1<Stage, V>, &Handlers::PreSetConstantBuffers, Stage, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>(context,
      start, count, buffers, first_constant, num_constants);
}

template <ShaderStage Stage, size_t V>
void STDMETHODCALLTYPE StageSetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState* const* samplers)
{
   SetStageArray<&StageSetSamplers<Stage, V>, &Handlers::PreSetSamplers, Stage, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT>(context, start, count, samplers);
}

template <size_t V>
void STDMETHODCALLTYPE CSSetUnorderedAccessViews(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11UnorderedAccessView* const* views,
   const UINT* initial_counts)
{
   SetArray<&CSSetUnorderedAccessViews<V>, &Handlers::PreCSSetUnorderedAccessViews, D3D11_1_UAV_SLOT_COUNT>(context, start, count, views, initial_counts);
}

template <size_t V>
void STDMETHODCALLTYPE OMSetRenderTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth_stencil)
{
   DepthGuard guard(context_depth);
   if (!Detours::IsGameCall(guard) || count > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT)
      return original<&OMSetRenderTargets<V>>(context, count, views, depth_stencil);
   std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_views;
   ID3D11RenderTargetView** edited_views = nullptr;
   if (views)
   {
      std::copy_n(views, count, game_views.begin());
      edited_views = game_views.data();
   }
   if (Detours::Report(&Handlers::PreOMSetRenderTargets, context, count, edited_views, depth_stencil))
      return;
   original<&OMSetRenderTargets<V>>(context, count, edited_views, depth_stencil);
}

template <size_t V>
void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView* const* views,
   ID3D11DepthStencilView* depth_stencil, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView* const* uavs, const UINT* initial_counts)
{
   DepthGuard guard(context_depth);
   const bool keep_views = count == D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL;
   const bool keep_uavs = uav_count == D3D11_KEEP_UNORDERED_ACCESS_VIEWS;
   if (!Detours::IsGameCall(guard) || (!keep_views && count > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) || (!keep_uavs && uav_count > D3D11_1_UAV_SLOT_COUNT))
      return original<&OMSetRenderTargetsAndUnorderedAccessViews<V>>(context, count, views, depth_stencil, uav_start, uav_count, uavs, initial_counts);
   std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_views;
   std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> game_uavs;
   ID3D11RenderTargetView** edited_views = nullptr;
   if (!keep_views && views)
   {
      std::copy_n(views, count, game_views.begin());
      edited_views = game_views.data();
   }
   ID3D11UnorderedAccessView** edited_uavs = nullptr;
   if (!keep_uavs && uavs)
   {
      std::copy_n(uavs, uav_count, game_uavs.begin());
      edited_uavs = game_uavs.data();
   }
   if (Detours::Report(&Handlers::PreOMSetRenderTargetsAndUnorderedAccessViews, context, count, edited_views, depth_stencil, uav_start, uav_count,
          edited_uavs, initial_counts))
      return;
   original<&OMSetRenderTargetsAndUnorderedAccessViews<V>>(context, count, keep_views ? views : edited_views, depth_stencil, uav_start, uav_count,
      keep_uavs ? uavs : edited_uavs, initial_counts);
}

template <size_t V>
void STDMETHODCALLTYPE RSSetViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports)
{
   ReportAfter<&RSSetViewports<V>, &Handlers::PostRSSetViewports>(context, count, viewports);
}

template <size_t V>
void STDMETHODCALLTYPE RSSetScissorRects(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects)
{
   ReportAfter<&RSSetScissorRects<V>, &Handlers::PostRSSetScissorRects>(context, count, rects);
}

template <size_t V>
void STDMETHODCALLTYPE OMSetBlendState(ID3D11DeviceContext* context, ID3D11BlendState* state, const FLOAT blend_factor[4], UINT sample_mask)
{
   ReportAfter<&OMSetBlendState<V>, &Handlers::PostOMSetBlendState>(context, state, blend_factor, sample_mask);
}

template <size_t V>
void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D11DeviceContext* context, ID3D11DepthStencilState* state, UINT stencil_ref)
{
   ReportAfter<&OMSetDepthStencilState<V>, &Handlers::PostOMSetDepthStencilState>(context, state, stencil_ref);
}

template <size_t V>
void STDMETHODCALLTYPE IASetVertexBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets)
{
   ReportAfter<&IASetVertexBuffers<V>, &Handlers::PostIASetVertexBuffers>(context, start, count, buffers, strides, offsets);
}

template <size_t V>
void STDMETHODCALLTYPE IASetIndexBuffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset)
{
   ReportAfter<&IASetIndexBuffer<V>, &Handlers::PostIASetIndexBuffer>(context, buffer, format, offset);
}

template <size_t V>
void STDMETHODCALLTYPE ClearState(ID3D11DeviceContext* context)
{
   ReportAfter<&ClearState<V>, &Handlers::PostClearState>(context);
}

template <size_t V>
HRESULT STDMETHODCALLTYPE Map(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags,
   D3D11_MAPPED_SUBRESOURCE* mapped)
{
   DepthGuard guard(context_depth);
   const HRESULT result = original<&Map<V>>(context, resource, subresource, type, flags, mapped);
   if (Detours::IsGameCall(guard) && SUCCEEDED(result) && mapped)
      Detours::Report(&Handlers::PostMap, context, resource, subresource, type, flags, *mapped);
   return result;
}

template <size_t V>
void STDMETHODCALLTYPE Unmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   DepthGuard guard(context_depth);
   const bool game_call = Detours::IsGameCall(guard);
   if (game_call && Detours::Report(&Handlers::PreUnmap, context, resource, subresource))
      return;
   original<&Unmap<V>>(context, resource, subresource);
   if (game_call)
      Detours::Report(&Handlers::PostUnmap, context, resource, subresource);
}

template <size_t V>
void STDMETHODCALLTYPE UpdateSubresource(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box,
   const void* data, UINT row_pitch, UINT depth_pitch)
{
   DepthGuard guard(context_depth);
   if (Detours::IsGameCall(guard) && Detours::Report(&Handlers::PreUpdateSubresource, context, resource, subresource, box, data, row_pitch, depth_pitch, 0u))
      return;
   original<&UpdateSubresource<V>>(context, resource, subresource, box, data, row_pitch, depth_pitch);
}

template <size_t V>
void STDMETHODCALLTYPE UpdateSubresource1(ID3D11DeviceContext1* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box,
   const void* data, UINT row_pitch, UINT depth_pitch, UINT copy_flags)
{
   ReportBefore<&UpdateSubresource1<V>, &Handlers::PreUpdateSubresource>(context, resource, subresource, box, data, row_pitch, depth_pitch, copy_flags);
}

template <size_t V>
void STDMETHODCALLTYPE CopyResource(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
{
   ReportBefore<&CopyResource<V>, &Handlers::PreCopyResource>(context, destination, source);
}

template <size_t V>
void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y,
   UINT z, ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box)
{
   DepthGuard guard(context_depth);
   if (Detours::IsGameCall(guard) &&
      Detours::Report(&Handlers::PreCopySubresourceRegion, context, destination, destination_subresource, x, y, z, source, source_subresource, box, 0u))
      return;
   original<&CopySubresourceRegion<V>>(context, destination, destination_subresource, x, y, z, source, source_subresource, box);
}

template <size_t V>
void STDMETHODCALLTYPE CopySubresourceRegion1(ID3D11DeviceContext1* context, ID3D11Resource* destination, UINT destination_subresource, UINT x,
   UINT y, UINT z, ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, UINT copy_flags)
{
   ReportBefore<&CopySubresourceRegion1<V>, &Handlers::PreCopySubresourceRegion>(context, destination, destination_subresource, x, y, z, source,
      source_subresource, box, copy_flags);
}

template <size_t V>
void STDMETHODCALLTYPE ResolveSubresource(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source,
   UINT source_subresource, DXGI_FORMAT format)
{
   ReportBefore<&ResolveSubresource<V>, &Handlers::PreResolveSubresource>(context, destination, destination_subresource, source, source_subresource, format);
}

template <size_t V>
void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4])
{
   ReportBefore<&ClearRenderTargetView<V>, &Handlers::PreClearRenderTargetView>(context, view, color);
}

template <size_t V>
void STDMETHODCALLTYPE CopyStructureCount(ID3D11DeviceContext* context, ID3D11Buffer* dest, UINT dest_offset, ID3D11UnorderedAccessView* source)
{
   ReportBefore<&CopyStructureCount<V>, &Handlers::PreCopyStructureCount>(context, dest, dest_offset, source);
}

template <size_t V>
void STDMETHODCALLTYPE ClearDepthStencilView(ID3D11DeviceContext* context, ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil)
{
   ReportBefore<&ClearDepthStencilView<V>, &Handlers::PreClearDepthStencilView>(context, view, flags, depth, stencil);
}

template <size_t V>
void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const UINT values[4])
{
   ReportBefore<&ClearUnorderedAccessViewUint<V>, &Handlers::PreClearUnorderedAccessViewUint>(context, view, values);
}

template <size_t V>
void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const FLOAT values[4])
{
   ReportBefore<&ClearUnorderedAccessViewFloat<V>, &Handlers::PreClearUnorderedAccessViewFloat>(context, view, values);
}

template <size_t V>
void STDMETHODCALLTYPE ClearView(ID3D11DeviceContext1* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count)
{
   ReportBefore<&ClearView<V>, &Handlers::PreClearView>(context, view, color, rects, rect_count);
}

template <auto Detour, typename... Args>
void ReportDraw(ID3D11DeviceContext* context, const Handlers::DrawCall& draw, Args... args)
{
   DepthGuard guard(context_depth);
   if (Detours::IsGameCall(guard) && Detours::Report(&Handlers::PreDraw, context, draw))
      return;
   original<Detour>(context, args...);
}

template <auto Detour, typename... Args>
void ReportDispatch(ID3D11DeviceContext* context, const Handlers::DispatchCall& dispatch, Args... args)
{
   DepthGuard guard(context_depth);
   if (Detours::IsGameCall(guard) && Detours::Report(&Handlers::PreDispatch, context, dispatch))
      return;
   original<Detour>(context, args...);
}

using Function = D3D11::Function;

template <size_t V>
void STDMETHODCALLTYPE Draw(ID3D11DeviceContext* context, UINT vertex_count, UINT start_vertex)
{
   ReportDraw<&Draw<V>>(context, {.function = Function::Draw, .vertex_or_index_count = vertex_count, .instance_count = 1, .start_vertex_or_index = start_vertex},
      vertex_count, start_vertex);
}

template <size_t V>
void STDMETHODCALLTYPE DrawInstanced(ID3D11DeviceContext* context, UINT vertex_count, UINT instance_count, UINT start_vertex, UINT start_instance)
{
   ReportDraw<&DrawInstanced<V>>(context,
      {.function = Function::DrawInstanced, .vertex_or_index_count = vertex_count, .instance_count = instance_count, .start_vertex_or_index = start_vertex, .start_instance = start_instance},
      vertex_count, instance_count, start_vertex, start_instance);
}

template <size_t V>
void STDMETHODCALLTYPE DrawIndexed(ID3D11DeviceContext* context, UINT index_count, UINT start_index, INT base_vertex)
{
   ReportDraw<&DrawIndexed<V>>(context,
      {.function = Function::DrawIndexed, .vertex_or_index_count = index_count, .instance_count = 1, .start_vertex_or_index = start_index, .base_vertex = base_vertex},
      index_count, start_index, base_vertex);
}

template <size_t V>
void STDMETHODCALLTYPE DrawIndexedInstanced(ID3D11DeviceContext* context, UINT index_count, UINT instance_count, UINT start_index, INT base_vertex,
   UINT start_instance)
{
   ReportDraw<&DrawIndexedInstanced<V>>(context,
      {.function = Function::DrawIndexedInstanced, .vertex_or_index_count = index_count, .instance_count = instance_count, .start_vertex_or_index = start_index, .base_vertex = base_vertex, .start_instance = start_instance},
      index_count, instance_count, start_index, base_vertex, start_instance);
}

template <size_t V>
void STDMETHODCALLTYPE DrawAuto(ID3D11DeviceContext* context)
{
   ReportDraw<&DrawAuto<V>>(context, {.function = Function::DrawAuto});
}

template <size_t V>
void STDMETHODCALLTYPE DrawInstancedIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset)
{
   ReportDraw<&DrawInstancedIndirect<V>>(context, {.function = Function::DrawInstancedIndirect, .arguments = arguments, .arguments_offset = offset}, arguments,
      offset);
}

template <size_t V>
void STDMETHODCALLTYPE DrawIndexedInstancedIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset)
{
   ReportDraw<&DrawIndexedInstancedIndirect<V>>(context, {.function = Function::DrawIndexedInstancedIndirect, .arguments = arguments, .arguments_offset = offset},
      arguments, offset);
}

template <size_t V>
void STDMETHODCALLTYPE Dispatch(ID3D11DeviceContext* context, UINT x, UINT y, UINT z)
{
   ReportDispatch<&Dispatch<V>>(context, {.x = x, .y = y, .z = z}, x, y, z);
}

template <size_t V>
void STDMETHODCALLTYPE DispatchIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset)
{
   ReportDispatch<&DispatchIndirect<V>>(context, {.arguments = arguments, .arguments_offset = offset}, arguments, offset);
}

template <size_t V>
HRESULT STDMETHODCALLTYPE FinishCommandList(ID3D11DeviceContext* context, BOOL restore, ID3D11CommandList** list)
{
   DepthGuard guard(context_depth);
   const HRESULT result = original<&FinishCommandList<V>>(context, restore, list);
   if (Detours::IsGameCall(guard) && SUCCEEDED(result) && list && *list)
   {
      Scope::Guard scope;
      Microsoft::WRL::ComPtr<ID3D11Device> device;
      context->GetDevice(&device);
      Lifetime::Track(*list, device.Get(), &Detours::OnDestroyed<Handlers::OnCommandListDestroyed>);
      Handlers::PostFinishCommandList(context, restore, *list);
   }
   return result;
}

template <size_t V>
void STDMETHODCALLTYPE ExecuteCommandList(ID3D11DeviceContext* context, ID3D11CommandList* list, BOOL restore)
{
   ReportBefore<&ExecuteCommandList<V>, &Handlers::PreExecuteCommandList>(context, list, restore);
}

template <size_t V>
void STDMETHODCALLTYPE Flush(ID3D11DeviceContext* context)
{
   ReportBefore<&Flush<V>, &Handlers::PreFlush>(context);
}

// Names for the log, alive as long as the hooks
const char* GetHookName(std::string_view interface_name, std::string_view method, std::string_view vtable)
{
   static std::deque<std::string> names;
   return names.emplace_back(std::format("{}::{} ({})", interface_name, method, vtable)).c_str();
}

struct ContextInterface
{
   std::string_view name;
   IID version; // That introduced the methods
};

const ContextInterface BASE = {"ID3D11DeviceContext", GUID_NULL};
const ContextInterface CONTEXT1 = {"ID3D11DeviceContext1", __uuidof(ID3D11DeviceContext1)};

// A method's hooks on the immediate and the deferred context vtables, `Immediate` and `Deferred` being its detour's instances
template <auto Immediate, auto Deferred>
void AddContextHooks(std::vector<Hook>& hooks, uint32_t slot, const ContextInterface& interface_type, std::string_view method)
{
   hooks.push_back({VtableTarget{Vtables::Interface::D3D11ImmediateContext, slot, GetHookName(interface_type.name, method, "immediate"), interface_type.version},
      reinterpret_cast<void*>(Immediate), reinterpret_cast<void**>(&original<Immediate>), true});
   hooks.push_back({VtableTarget{Vtables::Interface::D3D11DeferredContext, slot, GetHookName(interface_type.name, method, "deferred"), interface_type.version},
      reinterpret_cast<void*>(Deferred), reinterpret_cast<void**>(&original<Deferred>), true});
}

// The stage's methods: the functions of a stage follow ShaderStage's order in Function
template <ShaderStage Stage>
void AddStageHooks(std::vector<Hook>& hooks, const D3D11::Functions& functions)
{
   using Traits = D3D11::StageTraits<Stage>;
   using D3D11::Function;
   const auto wants = [&](Function first) { return functions.test(size_t(first) + size_t(Stage)); };
   if (wants(Function::VSSetShader))
      AddContextHooks<&StageSetShader<Stage, 0>, &StageSetShader<Stage, 1>>(hooks, Traits::SetShaderSlot(), BASE, Traits::NAMES[0]);
   if (wants(Function::VSSetShaderResources))
      AddContextHooks<&StageSetShaderResources<Stage, 0>, &StageSetShaderResources<Stage, 1>>(hooks, Traits::SetShaderResourcesSlot(), BASE, Traits::NAMES[1]);
   if (wants(Function::VSSetConstantBuffers))
   {
      AddContextHooks<&StageSetConstantBuffers<Stage, 0>, &StageSetConstantBuffers<Stage, 1>>(hooks, Traits::SetConstantBuffersSlot(), BASE, Traits::NAMES[2]);
      AddContextHooks<&StageSetConstantBuffers1<Stage, 0>, &StageSetConstantBuffers1<Stage, 1>>(hooks, Traits::SetConstantBuffers1Slot(), CONTEXT1,
         Traits::NAMES[3]);
   }
   if (wants(Function::VSSetSamplers))
      AddContextHooks<&StageSetSamplers<Stage, 0>, &StageSetSamplers<Stage, 1>>(hooks, Traits::SetSamplersSlot(), BASE, Traits::NAMES[4]);
}
} // namespace

void InstallD3D11Context(const D3D11::Functions& functions)
{
   using D3D11::Function;
   static_assert(size_t(Function::CSSetShader) - size_t(Function::VSSetShader) == size_t(ShaderStage::Compute));
   static_assert(size_t(Function::CSSetSamplers) - size_t(Function::VSSetSamplers) == size_t(ShaderStage::Compute));
   std::vector<Hook> hooks;
   AddStageHooks<ShaderStage::Vertex>(hooks, functions);
   AddStageHooks<ShaderStage::Hull>(hooks, functions);
   AddStageHooks<ShaderStage::Domain>(hooks, functions);
   AddStageHooks<ShaderStage::Geometry>(hooks, functions);
   AddStageHooks<ShaderStage::Pixel>(hooks, functions);
   AddStageHooks<ShaderStage::Compute>(hooks, functions);
   const auto wants = [&](Function function) { return functions.test(size_t(function)); };
   // The others, in Function's order
   if (wants(Function::CSSetUnorderedAccessViews))
      AddContextHooks<&CSSetUnorderedAccessViews<0>, &CSSetUnorderedAccessViews<1>>(hooks, LumaSlot_ID3D11DeviceContext_CSSetUnorderedAccessViews, BASE,
         "CSSetUnorderedAccessViews");
   if (wants(Function::OMSetRenderTargets))
      AddContextHooks<&OMSetRenderTargets<0>, &OMSetRenderTargets<1>>(hooks, LumaSlot_ID3D11DeviceContext_OMSetRenderTargets, BASE, "OMSetRenderTargets");
   if (wants(Function::OMSetRenderTargetsAndUnorderedAccessViews))
      AddContextHooks<&OMSetRenderTargetsAndUnorderedAccessViews<0>, &OMSetRenderTargetsAndUnorderedAccessViews<1>>(hooks,
         LumaSlot_ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews, BASE, "OMSetRenderTargetsAndUnorderedAccessViews");
   if (wants(Function::RSSetViewports))
      AddContextHooks<&RSSetViewports<0>, &RSSetViewports<1>>(hooks, LumaSlot_ID3D11DeviceContext_RSSetViewports, BASE, "RSSetViewports");
   if (wants(Function::RSSetScissorRects))
      AddContextHooks<&RSSetScissorRects<0>, &RSSetScissorRects<1>>(hooks, LumaSlot_ID3D11DeviceContext_RSSetScissorRects, BASE, "RSSetScissorRects");
   if (wants(Function::OMSetBlendState))
      AddContextHooks<&OMSetBlendState<0>, &OMSetBlendState<1>>(hooks, LumaSlot_ID3D11DeviceContext_OMSetBlendState, BASE, "OMSetBlendState");
   if (wants(Function::OMSetDepthStencilState))
      AddContextHooks<&OMSetDepthStencilState<0>, &OMSetDepthStencilState<1>>(hooks, LumaSlot_ID3D11DeviceContext_OMSetDepthStencilState, BASE,
         "OMSetDepthStencilState");
   if (wants(Function::IASetVertexBuffers))
      AddContextHooks<&IASetVertexBuffers<0>, &IASetVertexBuffers<1>>(hooks, LumaSlot_ID3D11DeviceContext_IASetVertexBuffers, BASE, "IASetVertexBuffers");
   if (wants(Function::IASetIndexBuffer))
      AddContextHooks<&IASetIndexBuffer<0>, &IASetIndexBuffer<1>>(hooks, LumaSlot_ID3D11DeviceContext_IASetIndexBuffer, BASE, "IASetIndexBuffer");
   if (wants(Function::ClearState))
      AddContextHooks<&ClearState<0>, &ClearState<1>>(hooks, LumaSlot_ID3D11DeviceContext_ClearState, BASE, "ClearState");
   if (wants(Function::Map))
      AddContextHooks<&Map<0>, &Map<1>>(hooks, LumaSlot_ID3D11DeviceContext_Map, BASE, "Map");
   if (wants(Function::Unmap))
      AddContextHooks<&Unmap<0>, &Unmap<1>>(hooks, LumaSlot_ID3D11DeviceContext_Unmap, BASE, "Unmap");
   if (wants(Function::UpdateSubresource))
   {
      AddContextHooks<&UpdateSubresource<0>, &UpdateSubresource<1>>(hooks, LumaSlot_ID3D11DeviceContext_UpdateSubresource, BASE, "UpdateSubresource");
      AddContextHooks<&UpdateSubresource1<0>, &UpdateSubresource1<1>>(hooks, LumaSlot_ID3D11DeviceContext1_UpdateSubresource1, CONTEXT1, "UpdateSubresource1");
   }
   if (wants(Function::CopyResource))
      AddContextHooks<&CopyResource<0>, &CopyResource<1>>(hooks, LumaSlot_ID3D11DeviceContext_CopyResource, BASE, "CopyResource");
   if (wants(Function::CopySubresourceRegion))
   {
      AddContextHooks<&CopySubresourceRegion<0>, &CopySubresourceRegion<1>>(hooks, LumaSlot_ID3D11DeviceContext_CopySubresourceRegion, BASE,
         "CopySubresourceRegion");
      AddContextHooks<&CopySubresourceRegion1<0>, &CopySubresourceRegion1<1>>(hooks, LumaSlot_ID3D11DeviceContext1_CopySubresourceRegion1, CONTEXT1,
         "CopySubresourceRegion1");
   }
   if (wants(Function::ResolveSubresource))
      AddContextHooks<&ResolveSubresource<0>, &ResolveSubresource<1>>(hooks, LumaSlot_ID3D11DeviceContext_ResolveSubresource, BASE, "ResolveSubresource");
   if (wants(Function::ClearRenderTargetView))
      AddContextHooks<&ClearRenderTargetView<0>, &ClearRenderTargetView<1>>(hooks, LumaSlot_ID3D11DeviceContext_ClearRenderTargetView, BASE,
         "ClearRenderTargetView");
   if (wants(Function::CopyStructureCount))
      AddContextHooks<&CopyStructureCount<0>, &CopyStructureCount<1>>(hooks, LumaSlot_ID3D11DeviceContext_CopyStructureCount, BASE, "CopyStructureCount");
   if (wants(Function::ClearDepthStencilView))
      AddContextHooks<&ClearDepthStencilView<0>, &ClearDepthStencilView<1>>(hooks, LumaSlot_ID3D11DeviceContext_ClearDepthStencilView, BASE,
         "ClearDepthStencilView");
   if (wants(Function::ClearUnorderedAccessViewUint))
      AddContextHooks<&ClearUnorderedAccessViewUint<0>, &ClearUnorderedAccessViewUint<1>>(hooks, LumaSlot_ID3D11DeviceContext_ClearUnorderedAccessViewUint,
         BASE, "ClearUnorderedAccessViewUint");
   if (wants(Function::ClearUnorderedAccessViewFloat))
      AddContextHooks<&ClearUnorderedAccessViewFloat<0>, &ClearUnorderedAccessViewFloat<1>>(hooks, LumaSlot_ID3D11DeviceContext_ClearUnorderedAccessViewFloat,
         BASE, "ClearUnorderedAccessViewFloat");
   if (wants(Function::ClearView))
      AddContextHooks<&ClearView<0>, &ClearView<1>>(hooks, LumaSlot_ID3D11DeviceContext1_ClearView, CONTEXT1, "ClearView");
   if (wants(Function::Draw))
      AddContextHooks<&Draw<0>, &Draw<1>>(hooks, LumaSlot_ID3D11DeviceContext_Draw, BASE, "Draw");
   if (wants(Function::DrawInstanced))
      AddContextHooks<&DrawInstanced<0>, &DrawInstanced<1>>(hooks, LumaSlot_ID3D11DeviceContext_DrawInstanced, BASE, "DrawInstanced");
   if (wants(Function::DrawIndexed))
      AddContextHooks<&DrawIndexed<0>, &DrawIndexed<1>>(hooks, LumaSlot_ID3D11DeviceContext_DrawIndexed, BASE, "DrawIndexed");
   if (wants(Function::DrawIndexedInstanced))
      AddContextHooks<&DrawIndexedInstanced<0>, &DrawIndexedInstanced<1>>(hooks, LumaSlot_ID3D11DeviceContext_DrawIndexedInstanced, BASE,
         "DrawIndexedInstanced");
   if (wants(Function::DrawAuto))
      AddContextHooks<&DrawAuto<0>, &DrawAuto<1>>(hooks, LumaSlot_ID3D11DeviceContext_DrawAuto, BASE, "DrawAuto");
   if (wants(Function::DrawInstancedIndirect))
      AddContextHooks<&DrawInstancedIndirect<0>, &DrawInstancedIndirect<1>>(hooks, LumaSlot_ID3D11DeviceContext_DrawInstancedIndirect, BASE,
         "DrawInstancedIndirect");
   if (wants(Function::DrawIndexedInstancedIndirect))
      AddContextHooks<&DrawIndexedInstancedIndirect<0>, &DrawIndexedInstancedIndirect<1>>(hooks, LumaSlot_ID3D11DeviceContext_DrawIndexedInstancedIndirect,
         BASE, "DrawIndexedInstancedIndirect");
   if (wants(Function::Dispatch))
      AddContextHooks<&Dispatch<0>, &Dispatch<1>>(hooks, LumaSlot_ID3D11DeviceContext_Dispatch, BASE, "Dispatch");
   if (wants(Function::DispatchIndirect))
      AddContextHooks<&DispatchIndirect<0>, &DispatchIndirect<1>>(hooks, LumaSlot_ID3D11DeviceContext_DispatchIndirect, BASE, "DispatchIndirect");
   if (wants(Function::FinishCommandList))
      AddContextHooks<&FinishCommandList<0>, &FinishCommandList<1>>(hooks, LumaSlot_ID3D11DeviceContext_FinishCommandList, BASE, "FinishCommandList");
   if (wants(Function::ExecuteCommandList))
      AddContextHooks<&ExecuteCommandList<0>, &ExecuteCommandList<1>>(hooks, LumaSlot_ID3D11DeviceContext_ExecuteCommandList, BASE, "ExecuteCommandList");
   if (wants(Function::Flush))
      AddContextHooks<&Flush<0>, &Flush<1>>(hooks, LumaSlot_ID3D11DeviceContext_Flush, BASE, "Flush");
   if (!hooks.empty())
      Declare(hooks);
}
} // namespace Hooks
} // namespace Luma
