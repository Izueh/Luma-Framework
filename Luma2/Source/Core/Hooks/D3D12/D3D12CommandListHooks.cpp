#include "Hooks/Hooks.hpp"

#include <functional>
#include <initializer_list>
#include <vector>

#include <d3d12.h>

#include "Game/Events.hpp"
#include "Handlers/D3D12/D3D12Handlers.hpp"
#include "Hooks/Detours.hpp"
#include "Hooks/Slots.h"

// One vtable for every command list version and kind (direct, compute, bundles): the hooks cover them all. Luma's own lists
// pass through untouched (Luma scope).
namespace Luma {
namespace Hooks {
namespace {
using Detours::original;
using Game::D3D12::DrawFunction;

HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator, ID3D12PipelineState* pipeline)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreReset, list, std::ref(pipeline));
   return original<&Reset>(list, allocator, pipeline);
}

void STDMETHODCALLTYPE ClearState(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pipeline)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreClearState, list, std::ref(pipeline));
   original<&ClearState>(list, pipeline);
}

void STDMETHODCALLTYPE SetPipelineState(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pipeline)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetPipelineState, list, std::ref(pipeline));
   original<&SetPipelineState>(list, pipeline);
}

void STDMETHODCALLTYPE SetPipelineState1(ID3D12GraphicsCommandList4* list, ID3D12StateObject* state_object)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetStateObject, static_cast<ID3D12GraphicsCommandList*>(list));
   original<&SetPipelineState1>(list, state_object);
}

void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root_signature)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetRootSignature, list, false, root_signature);
   original<&SetGraphicsRootSignature>(list, root_signature);
}

void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root_signature)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetRootSignature, list, true, root_signature);
   original<&SetComputeRootSignature>(list, root_signature);
}

void STDMETHODCALLTYPE OMSetRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreOMSetRenderTargets, list, count, render_targets, single_range, depth_stencil);
   original<&OMSetRenderTargets>(list, count, render_targets, single_range, depth_stencil);
}

void STDMETHODCALLTYPE SetDescriptorHeaps(ID3D12GraphicsCommandList* list, UINT count, ID3D12DescriptorHeap* const* heaps)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetDescriptorHeaps, list);
   original<&SetDescriptorHeaps>(list, count, heaps);
}

// The root arguments, graphics and compute: one detour per function, the same handlers
template <bool Compute>
void STDMETHODCALLTYPE SetRootDescriptorTable(ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetRootTable, list, Compute, index, table);
   original<&SetRootDescriptorTable<Compute>>(list, index, table);
}

template <bool Compute>
void STDMETHODCALLTYPE SetRoot32BitConstant(ID3D12GraphicsCommandList* list, UINT index, UINT value, UINT offset)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetRootConstants, list, Compute, index, 1u, static_cast<const void*>(&value), offset);
   original<&SetRoot32BitConstant<Compute>>(list, index, value, offset);
}

template <bool Compute>
void STDMETHODCALLTYPE SetRoot32BitConstants(ID3D12GraphicsCommandList* list, UINT index, UINT count, const void* values, UINT offset)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetRootConstants, list, Compute, index, count, values, offset);
   original<&SetRoot32BitConstants<Compute>>(list, index, count, values, offset);
}

// Root CBVs, SRVs and UAVs: the address alone (the parameter's type says which)
template <bool Compute, int Kind>
void STDMETHODCALLTYPE SetRootDescriptor(ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetRootDescriptor, list, Compute, index, address);
   original<&SetRootDescriptor<Compute, Kind>>(list, index, address);
}

void STDMETHODCALLTYPE RSSetViewports(ID3D12GraphicsCommandList* list, UINT count, const D3D12_VIEWPORT* viewports)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetViewports, list, count, viewports);
   original<&RSSetViewports>(list, count, viewports);
}

void STDMETHODCALLTYPE RSSetScissorRects(ID3D12GraphicsCommandList* list, UINT count, const D3D12_RECT* rects)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetScissorRects, list, count, rects);
   original<&RSSetScissorRects>(list, count, rects);
}

void STDMETHODCALLTYPE IASetPrimitiveTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetTopology, list, topology);
   original<&IASetPrimitiveTopology>(list, topology);
}

void STDMETHODCALLTYPE OMSetBlendFactor(ID3D12GraphicsCommandList* list, const FLOAT factor[4])
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetBlendFactor, list, factor);
   original<&OMSetBlendFactor>(list, factor);
}

void STDMETHODCALLTYPE OMSetStencilRef(ID3D12GraphicsCommandList* list, UINT stencil_ref)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreSetStencilRef, list, stencil_ref);
   original<&OMSetStencilRef>(list, stencil_ref);
}

// Draws and dispatches: the handler may skip the game's call (a draw event, the trace)
void STDMETHODCALLTYPE DrawInstanced(ID3D12GraphicsCommandList* list, UINT vertex_count, UINT instance_count, UINT start_vertex, UINT start_instance)
{
   if (!Scope::IsActive() &&
      Detours::Report(&Handlers::PreDraw, list,
         Game::D3D12::DrawCall{.function = DrawFunction::DrawInstanced, .vertex_or_index_count = vertex_count, .instance_count = instance_count,
            .start_vertex_or_index = start_vertex, .start_instance = start_instance}))
      return;
   original<&DrawInstanced>(list, vertex_count, instance_count, start_vertex, start_instance);
}

void STDMETHODCALLTYPE DrawIndexedInstanced(ID3D12GraphicsCommandList* list, UINT index_count, UINT instance_count, UINT start_index, INT base_vertex,
   UINT start_instance)
{
   if (!Scope::IsActive() &&
      Detours::Report(&Handlers::PreDraw, list,
         Game::D3D12::DrawCall{.function = DrawFunction::DrawIndexedInstanced, .vertex_or_index_count = index_count, .instance_count = instance_count,
            .start_vertex_or_index = start_index, .base_vertex = base_vertex, .start_instance = start_instance}))
      return;
   original<&DrawIndexedInstanced>(list, index_count, instance_count, start_index, base_vertex, start_instance);
}

void STDMETHODCALLTYPE DispatchMesh(ID3D12GraphicsCommandList6* list, UINT x, UINT y, UINT z)
{
   if (!Scope::IsActive() &&
      Detours::Report(&Handlers::PreDraw, static_cast<ID3D12GraphicsCommandList*>(list), Game::D3D12::DrawCall{.function = DrawFunction::DispatchMesh, .mesh_groups = {x, y, z}}))
      return;
   original<&DispatchMesh>(list, x, y, z);
}

void STDMETHODCALLTYPE Dispatch(ID3D12GraphicsCommandList* list, UINT x, UINT y, UINT z)
{
   if (!Scope::IsActive() && Detours::Report(&Handlers::PreDispatch, list, Game::D3D12::DispatchCall{.x = x, .y = y, .z = z}))
      return;
   original<&Dispatch>(list, x, y, z);
}

void STDMETHODCALLTYPE ExecuteIndirect(ID3D12GraphicsCommandList* list, ID3D12CommandSignature* signature, UINT max_count, ID3D12Resource* arguments,
   UINT64 arguments_offset, ID3D12Resource* count_buffer, UINT64 count_offset)
{
   if (!Scope::IsActive() &&
      Detours::Report(&Handlers::PreExecuteIndirect, list, Game::D3D12::IndirectArguments{signature, max_count, arguments, arguments_offset, count_buffer, count_offset}))
      return;
   original<&ExecuteIndirect>(list, signature, max_count, arguments, arguments_offset, count_buffer, count_offset);
}

void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList* list, ID3D12GraphicsCommandList* bundle)
{
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PreExecuteBundle, list, bundle);
   original<&ExecuteBundle>(list, bundle);
}

template <auto Detour>
Hook ListHook(uint32_t slot, const char* name, const IID& version = GUID_NULL)
{
   return {VtableTarget{Vtables::Interface::D3D12GraphicsCommandList, slot, name, version}, reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&original<Detour>)};
}
} // namespace

void InstallD3D12CommandList(const D3D12::Functions& functions)
{
   using D3D12::Function;
   std::vector<Hook> hooks;
   const auto add = [&](Function function, std::initializer_list<Hook> function_hooks) {
      if (functions.test(size_t(function)))
         hooks.insert(hooks.end(), function_hooks);
   };
   add(Function::Reset, {ListHook<&Reset>(LumaSlot_ID3D12GraphicsCommandList_Reset, "ID3D12GraphicsCommandList::Reset")});
   add(Function::ClearState, {ListHook<&ClearState>(LumaSlot_ID3D12GraphicsCommandList_ClearState, "ID3D12GraphicsCommandList::ClearState")});
   add(Function::SetPipelineState, {
                                      ListHook<&SetPipelineState>(LumaSlot_ID3D12GraphicsCommandList_SetPipelineState, "ID3D12GraphicsCommandList::SetPipelineState"),
                                      ListHook<&SetPipelineState1>(LumaSlot_ID3D12GraphicsCommandList4_SetPipelineState1, "ID3D12GraphicsCommandList4::SetPipelineState1", __uuidof(ID3D12GraphicsCommandList4)),
                                   });
   add(Function::SetRootSignature, {
                                      ListHook<&SetGraphicsRootSignature>(LumaSlot_ID3D12GraphicsCommandList_SetGraphicsRootSignature, "ID3D12GraphicsCommandList::SetGraphicsRootSignature"),
                                      ListHook<&SetComputeRootSignature>(LumaSlot_ID3D12GraphicsCommandList_SetComputeRootSignature, "ID3D12GraphicsCommandList::SetComputeRootSignature"),
                                   });
   add(Function::OMSetRenderTargets, {ListHook<&OMSetRenderTargets>(LumaSlot_ID3D12GraphicsCommandList_OMSetRenderTargets, "ID3D12GraphicsCommandList::OMSetRenderTargets")});
   add(Function::SetDescriptorHeaps, {ListHook<&SetDescriptorHeaps>(LumaSlot_ID3D12GraphicsCommandList_SetDescriptorHeaps, "ID3D12GraphicsCommandList::SetDescriptorHeaps")});
   add(Function::SetRootArguments,
      {
         ListHook<&SetRootDescriptorTable<false>>(LumaSlot_ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable, "ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable"),
         ListHook<&SetRootDescriptorTable<true>>(LumaSlot_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable, "ID3D12GraphicsCommandList::SetComputeRootDescriptorTable"),
         ListHook<&SetRoot32BitConstant<false>>(LumaSlot_ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant, "ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant"),
         ListHook<&SetRoot32BitConstant<true>>(LumaSlot_ID3D12GraphicsCommandList_SetComputeRoot32BitConstant, "ID3D12GraphicsCommandList::SetComputeRoot32BitConstant"),
         ListHook<&SetRoot32BitConstants<false>>(LumaSlot_ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants, "ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants"),
         ListHook<&SetRoot32BitConstants<true>>(LumaSlot_ID3D12GraphicsCommandList_SetComputeRoot32BitConstants, "ID3D12GraphicsCommandList::SetComputeRoot32BitConstants"),
         ListHook<&SetRootDescriptor<false, 0>>(LumaSlot_ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView, "ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView"),
         ListHook<&SetRootDescriptor<true, 0>>(LumaSlot_ID3D12GraphicsCommandList_SetComputeRootConstantBufferView, "ID3D12GraphicsCommandList::SetComputeRootConstantBufferView"),
         ListHook<&SetRootDescriptor<false, 1>>(LumaSlot_ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView, "ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView"),
         ListHook<&SetRootDescriptor<true, 1>>(LumaSlot_ID3D12GraphicsCommandList_SetComputeRootShaderResourceView, "ID3D12GraphicsCommandList::SetComputeRootShaderResourceView"),
         ListHook<&SetRootDescriptor<false, 2>>(LumaSlot_ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView, "ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView"),
         ListHook<&SetRootDescriptor<true, 2>>(LumaSlot_ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView, "ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView"),
      });
   add(Function::SetFixedState, {
                                   ListHook<&RSSetViewports>(LumaSlot_ID3D12GraphicsCommandList_RSSetViewports, "ID3D12GraphicsCommandList::RSSetViewports"),
                                   ListHook<&RSSetScissorRects>(LumaSlot_ID3D12GraphicsCommandList_RSSetScissorRects, "ID3D12GraphicsCommandList::RSSetScissorRects"),
                                   ListHook<&IASetPrimitiveTopology>(LumaSlot_ID3D12GraphicsCommandList_IASetPrimitiveTopology, "ID3D12GraphicsCommandList::IASetPrimitiveTopology"),
                                   ListHook<&OMSetBlendFactor>(LumaSlot_ID3D12GraphicsCommandList_OMSetBlendFactor, "ID3D12GraphicsCommandList::OMSetBlendFactor"),
                                   ListHook<&OMSetStencilRef>(LumaSlot_ID3D12GraphicsCommandList_OMSetStencilRef, "ID3D12GraphicsCommandList::OMSetStencilRef"),
                                });
   add(Function::Draw, {
                          ListHook<&DrawInstanced>(LumaSlot_ID3D12GraphicsCommandList_DrawInstanced, "ID3D12GraphicsCommandList::DrawInstanced"),
                          ListHook<&DrawIndexedInstanced>(LumaSlot_ID3D12GraphicsCommandList_DrawIndexedInstanced, "ID3D12GraphicsCommandList::DrawIndexedInstanced"),
                          ListHook<&DispatchMesh>(LumaSlot_ID3D12GraphicsCommandList6_DispatchMesh, "ID3D12GraphicsCommandList6::DispatchMesh", __uuidof(ID3D12GraphicsCommandList6)),
                       });
   add(Function::Dispatch, {ListHook<&Dispatch>(LumaSlot_ID3D12GraphicsCommandList_Dispatch, "ID3D12GraphicsCommandList::Dispatch")});
   add(Function::ExecuteIndirect, {ListHook<&ExecuteIndirect>(LumaSlot_ID3D12GraphicsCommandList_ExecuteIndirect, "ID3D12GraphicsCommandList::ExecuteIndirect")});
   add(Function::ExecuteBundle, {ListHook<&ExecuteBundle>(LumaSlot_ID3D12GraphicsCommandList_ExecuteBundle, "ID3D12GraphicsCommandList::ExecuteBundle")});
   if (!hooks.empty())
      Declare(hooks);
}
} // namespace Hooks
} // namespace Luma
