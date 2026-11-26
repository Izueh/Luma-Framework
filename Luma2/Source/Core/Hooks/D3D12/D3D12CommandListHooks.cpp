#include "Hooks/HookManager.hpp"

#include <functional>
#include <initializer_list>
#include <span>
#include <vector>

#include <d3d12.h>
#include <intrin.h>

#include "Game/GameEvents.hpp"
#include "Handlers/D3D12/D3D12Handlers.hpp"
#include "Hooks/Detours.hpp"
#include "Hooks/VtableSlots.h"

// One vtable for every command list version and kind (direct, compute, bundles): the hooks cover them all. Luma's own lists
// pass through untouched (Luma scope).
namespace Luma {
namespace Hooks {
namespace {
using Detours::original;
using Game::D3D12::DrawFunction;

HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator, ID3D12PipelineState* pipeline)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreReset, list, std::ref(pipeline));
   return original<&Reset>(list, allocator, pipeline);
}

void STDMETHODCALLTYPE ClearState(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pipeline)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreClearState, list, std::ref(pipeline));
   original<&ClearState>(list, pipeline);
}

void STDMETHODCALLTYPE SetPipelineState(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pipeline)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetPipelineState, list, std::ref(pipeline));
   original<&SetPipelineState>(list, pipeline);
}

void STDMETHODCALLTYPE SetPipelineState1(ID3D12GraphicsCommandList4* list, ID3D12StateObject* state_object)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetStateObject, static_cast<ID3D12GraphicsCommandList*>(list));
   original<&SetPipelineState1>(list, state_object);
}

void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root_signature)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetRootSignature, list, false, root_signature);
   original<&SetGraphicsRootSignature>(list, root_signature);
}

void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root_signature)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetRootSignature, list, true, root_signature);
   original<&SetComputeRootSignature>(list, root_signature);
}

void STDMETHODCALLTYPE OMSetRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreOMSetRenderTargets, list, count, std::ref(render_targets), std::ref(single_range), std::ref(depth_stencil));
   original<&OMSetRenderTargets>(list, count, render_targets, single_range, depth_stencil);
}

void STDMETHODCALLTYPE SetDescriptorHeaps(ID3D12GraphicsCommandList* list, UINT count, ID3D12DescriptorHeap* const* heaps)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetDescriptorHeaps, list, count, heaps);
   original<&SetDescriptorHeaps>(list, count, heaps);
}

// The root arguments, graphics and compute: one detour per function, the same handlers
template <bool Compute>
void STDMETHODCALLTYPE SetRootDescriptorTable(ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetRootTable, list, Compute, index, table);
   original<&SetRootDescriptorTable<Compute>>(list, index, table);
}

template <bool Compute>
void STDMETHODCALLTYPE SetRoot32BitConstant(ID3D12GraphicsCommandList* list, UINT index, UINT value, UINT offset)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetRootConstants, list, Compute, index, 1u, static_cast<const void*>(&value), offset);
   original<&SetRoot32BitConstant<Compute>>(list, index, value, offset);
}

template <bool Compute>
void STDMETHODCALLTYPE SetRoot32BitConstants(ID3D12GraphicsCommandList* list, UINT index, UINT count, const void* values, UINT offset)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetRootConstants, list, Compute, index, count, values, offset);
   original<&SetRoot32BitConstants<Compute>>(list, index, count, values, offset);
}

// Root CBVs, SRVs and UAVs: the address alone (the parameter's type says which)
template <bool Compute, int Kind>
void STDMETHODCALLTYPE SetRootDescriptor(ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetRootDescriptor, list, Compute, index, address);
   original<&SetRootDescriptor<Compute, Kind>>(list, index, address);
}

void STDMETHODCALLTYPE RSSetViewports(ID3D12GraphicsCommandList* list, UINT count, const D3D12_VIEWPORT* viewports)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetViewports, list, std::ref(count), std::ref(viewports));
   original<&RSSetViewports>(list, count, viewports);
}

void STDMETHODCALLTYPE RSSetScissorRects(ID3D12GraphicsCommandList* list, UINT count, const D3D12_RECT* rects)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetScissorRects, list, std::ref(count), std::ref(rects));
   original<&RSSetScissorRects>(list, count, rects);
}

void STDMETHODCALLTYPE IASetPrimitiveTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetTopology, list, topology);
   original<&IASetPrimitiveTopology>(list, topology);
}

void STDMETHODCALLTYPE OMSetBlendFactor(ID3D12GraphicsCommandList* list, const FLOAT factor[4])
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetBlendFactor, list, factor);
   original<&OMSetBlendFactor>(list, factor);
}

void STDMETHODCALLTYPE OMSetStencilRef(ID3D12GraphicsCommandList* list, UINT stencil_ref)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreSetStencilRef, list, stencil_ref);
   original<&OMSetStencilRef>(list, stencil_ref);
}

// Draws and dispatches: the handler may skip the game's call (a draw event, the trace)
void STDMETHODCALLTYPE DrawInstanced(ID3D12GraphicsCommandList* list, UINT vertex_count, UINT instance_count, UINT start_vertex, UINT start_instance)
{
   const bool game_call = Detours::IsGameCall(_ReturnAddress());
   const bool handled = game_call &&
      Detours::Report(&Handlers::PreDraw, list,
         Game::D3D12::DrawCall{.function = DrawFunction::DrawInstanced, .vertex_or_index_count = vertex_count, .instance_count = instance_count, .start_vertex_or_index = start_vertex, .start_instance = start_instance});
   if (!handled)
      original<&DrawInstanced>(list, vertex_count, instance_count, start_vertex, start_instance);
   if (game_call)
      Detours::Report(&Handlers::PostDraw, list);
}

void STDMETHODCALLTYPE DrawIndexedInstanced(ID3D12GraphicsCommandList* list, UINT index_count, UINT instance_count, UINT start_index, INT base_vertex,
   UINT start_instance)
{
   const bool game_call = Detours::IsGameCall(_ReturnAddress());
   const bool handled = game_call &&
      Detours::Report(&Handlers::PreDraw, list,
         Game::D3D12::DrawCall{.function = DrawFunction::DrawIndexedInstanced, .vertex_or_index_count = index_count, .instance_count = instance_count, .start_vertex_or_index = start_index, .base_vertex = base_vertex, .start_instance = start_instance});
   if (!handled)
      original<&DrawIndexedInstanced>(list, index_count, instance_count, start_index, base_vertex, start_instance);
   if (game_call)
      Detours::Report(&Handlers::PostDraw, list);
}

void STDMETHODCALLTYPE DispatchMesh(ID3D12GraphicsCommandList6* list, UINT x, UINT y, UINT z)
{
   const bool game_call = Detours::IsGameCall(_ReturnAddress());
   const bool handled = game_call &&
      Detours::Report(&Handlers::PreDraw, static_cast<ID3D12GraphicsCommandList*>(list), Game::D3D12::DrawCall{.function = DrawFunction::DispatchMesh, .mesh_groups = {x, y, z}});
   if (!handled)
      original<&DispatchMesh>(list, x, y, z);
   if (game_call)
      Detours::Report(&Handlers::PostDraw, static_cast<ID3D12GraphicsCommandList*>(list));
}

void STDMETHODCALLTYPE Dispatch(ID3D12GraphicsCommandList* list, UINT x, UINT y, UINT z)
{
   const bool game_call = Detours::IsGameCall(_ReturnAddress());
   const bool handled = game_call && Detours::Report(&Handlers::PreDispatch, list, Game::D3D12::DispatchCall{.x = x, .y = y, .z = z});
   if (!handled)
      original<&Dispatch>(list, x, y, z);
   if (game_call)
      Detours::Report(&Handlers::PostDispatch, list);
}

void STDMETHODCALLTYPE ExecuteIndirect(ID3D12GraphicsCommandList* list, ID3D12CommandSignature* signature, UINT max_count, ID3D12Resource* arguments,
   UINT64 arguments_offset, ID3D12Resource* count_buffer, UINT64 count_offset)
{
   const bool game_call = Detours::IsGameCall(_ReturnAddress());
   const bool handled = game_call &&
      Detours::Report(&Handlers::PreExecuteIndirect, list, Game::D3D12::IndirectArguments{signature, max_count, arguments, arguments_offset, count_buffer, count_offset});
   if (!handled)
      original<&ExecuteIndirect>(list, signature, max_count, arguments, arguments_offset, count_buffer, count_offset);
   if (game_call)
      Detours::Report(&Handlers::PostExecuteIndirect, list);
}

void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList* list, ID3D12GraphicsCommandList* bundle)
{
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PreExecuteBundle, list, bundle);
   original<&ExecuteBundle>(list, bundle);
}

void STDMETHODCALLTYPE ResourceBarrier(ID3D12GraphicsCommandList* list, UINT count, const D3D12_RESOURCE_BARRIER* barriers)
{
   std::span<const D3D12_RESOURCE_BARRIER> final_barriers(barriers, barriers ? count : 0);
   if (Detours::IsGameCall(_ReturnAddress()))
      final_barriers = Detours::Report(&Handlers::PreResourceBarrier, list, final_barriers);
   original<&ResourceBarrier>(list, UINT(final_barriers.size()), final_barriers.empty() ? barriers : final_barriers.data());
}

// Copies and clears: the handlers may change the resources and handles (Luma's mirrors) or skip the call
void STDMETHODCALLTYPE CopyResource(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source)
{
   if (Detours::IsGameCall(_ReturnAddress()) && Detours::Report(&Handlers::PreCopyResource, list, std::ref(destination), std::ref(source)))
      return;
   original<&CopyResource>(list, destination, source);
}

void STDMETHODCALLTYPE CopyTextureRegion(ID3D12GraphicsCommandList* list, const D3D12_TEXTURE_COPY_LOCATION* destination, UINT x, UINT y, UINT z,
   const D3D12_TEXTURE_COPY_LOCATION* source, const D3D12_BOX* box)
{
   if (!Detours::IsGameCall(_ReturnAddress()) || !destination || !source)
      return original<&CopyTextureRegion>(list, destination, x, y, z, source, box);
   D3D12_TEXTURE_COPY_LOCATION destination_copy = *destination;
   D3D12_TEXTURE_COPY_LOCATION source_copy = *source;
   if (Detours::Report(&Handlers::PreCopyTextureRegion, list, std::ref(destination_copy), std::ref(x), std::ref(y), std::ref(z), std::ref(source_copy), std::ref(box)))
      return;
   original<&CopyTextureRegion>(list, &destination_copy, x, y, z, &source_copy, box);
}

void STDMETHODCALLTYPE CopyBufferRegion(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, UINT64 destination_offset, ID3D12Resource* source,
   UINT64 source_offset, UINT64 size)
{
   if (Detours::IsGameCall(_ReturnAddress()) && Detours::Report(&Handlers::PreCopyBufferRegion, list, destination, destination_offset, source, source_offset, size))
      return;
   original<&CopyBufferRegion>(list, destination, destination_offset, source, source_offset, size);
}

void STDMETHODCALLTYPE ResolveSubresource(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, UINT destination_subresource, ID3D12Resource* source,
   UINT source_subresource, DXGI_FORMAT format)
{
   if (Detours::IsGameCall(_ReturnAddress()) &&
      Detours::Report(&Handlers::PreResolveSubresource, list, std::ref(destination), destination_subresource, std::ref(source), source_subresource, std::ref(format)))
      return;
   original<&ResolveSubresource>(list, destination, destination_subresource, source, source_subresource, format);
}

void STDMETHODCALLTYPE ClearRenderTargetView(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE view, const FLOAT color[4], UINT rect_count,
   const D3D12_RECT* rects)
{
   if (Detours::IsGameCall(_ReturnAddress()) && Detours::Report(&Handlers::PreClearRenderTargetView, list, std::ref(view), color, rect_count, std::ref(rects)))
      return;
   original<&ClearRenderTargetView>(list, view, color, rect_count, rects);
}

void STDMETHODCALLTYPE ClearDepthStencilView(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE view, D3D12_CLEAR_FLAGS flags, FLOAT depth,
   UINT8 stencil, UINT rect_count, const D3D12_RECT* rects)
{
   if (Detours::IsGameCall(_ReturnAddress()) &&
      Detours::Report(&Handlers::PreClearDepthStencilView, list, std::ref(view), flags, depth, stencil, rect_count, std::ref(rects)))
      return;
   original<&ClearDepthStencilView>(list, view, flags, depth, stencil, rect_count, rects);
}

void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE gpu, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
   ID3D12Resource* resource, const UINT values[4], UINT rect_count, const D3D12_RECT* rects)
{
   if (Detours::IsGameCall(_ReturnAddress()) &&
      Detours::Report(&Handlers::PreClearUnorderedAccessView, list, std::ref(gpu), std::ref(cpu), std::ref(resource), Handlers::ClearValues{values, nullptr}, rect_count,
         rects))
      return;
   original<&ClearUnorderedAccessViewUint>(list, gpu, cpu, resource, values, rect_count, rects);
}

void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE gpu, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
   ID3D12Resource* resource, const FLOAT values[4], UINT rect_count, const D3D12_RECT* rects)
{
   if (Detours::IsGameCall(_ReturnAddress()) &&
      Detours::Report(&Handlers::PreClearUnorderedAccessView, list, std::ref(gpu), std::ref(cpu), std::ref(resource), Handlers::ClearValues{nullptr, values}, rect_count,
         rects))
      return;
   original<&ClearUnorderedAccessViewFloat>(list, gpu, cpu, resource, values, rect_count, rects);
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
   add(Function::ResourceBarrier, {ListHook<&ResourceBarrier>(LumaSlot_ID3D12GraphicsCommandList_ResourceBarrier, "ID3D12GraphicsCommandList::ResourceBarrier")});
   add(Function::Copy, {
                          ListHook<&CopyResource>(LumaSlot_ID3D12GraphicsCommandList_CopyResource, "ID3D12GraphicsCommandList::CopyResource"),
                          ListHook<&CopyTextureRegion>(LumaSlot_ID3D12GraphicsCommandList_CopyTextureRegion, "ID3D12GraphicsCommandList::CopyTextureRegion"),
                          ListHook<&CopyBufferRegion>(LumaSlot_ID3D12GraphicsCommandList_CopyBufferRegion, "ID3D12GraphicsCommandList::CopyBufferRegion"),
                          ListHook<&ResolveSubresource>(LumaSlot_ID3D12GraphicsCommandList_ResolveSubresource, "ID3D12GraphicsCommandList::ResolveSubresource"),
                       });
   add(Function::Clear, {
                           ListHook<&ClearRenderTargetView>(LumaSlot_ID3D12GraphicsCommandList_ClearRenderTargetView, "ID3D12GraphicsCommandList::ClearRenderTargetView"),
                           ListHook<&ClearDepthStencilView>(LumaSlot_ID3D12GraphicsCommandList_ClearDepthStencilView, "ID3D12GraphicsCommandList::ClearDepthStencilView"),
                           ListHook<&ClearUnorderedAccessViewUint>(LumaSlot_ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint, "ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint"),
                           ListHook<&ClearUnorderedAccessViewFloat>(LumaSlot_ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat, "ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat"),
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
