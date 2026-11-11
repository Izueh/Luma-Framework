#include "hooks/hooks.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <vector>

#include <d3d11_4.h>

#include "hooks/callbacks.hpp"
#include "hooks/detours.hpp"
#include "hooks/lifetime.hpp"
#include "hooks/slots.h"

namespace Luma {
namespace Hooks {
namespace {
using Detours::original;

thread_local uint32_t context_depth = 0;

// Detours are instanced per context vtable: 0 is the immediate one, 1 the deferred one

template <auto Detour, auto Pre, typename Context, typename... Args>
void ReportBefore(Context* context, Args... args)
{
   Utils::DepthGuard guard(context_depth);
   if (Detours::IsGameCall(guard) && Detours::Report(Pre, context, args...))
      return;
   original<Detour>(context, args...);
}

template <auto Detour, auto Post, typename... Args>
void ReportAfter(ID3D11DeviceContext* context, Args... args)
{
   Utils::DepthGuard guard(context_depth);
   original<Detour>(context, args...);
   if (Detours::IsGameCall(guard))
      Detours::Report(Post, context, args...);
}

template <auto Detour, auto Pre, typename Shader>
void SetShader(ID3D11DeviceContext* context, Shader* shader, ID3D11ClassInstance* const* instances, UINT instance_count)
{
   Utils::DepthGuard guard(context_depth);
   if (Detours::IsGameCall(guard) && Detours::Report(Pre, context, shader))
      return;
   original<Detour>(context, shader, instances, instance_count);
}

// The callback edits a copy of the game's array
template <auto Detour, auto Pre, size_t Max, typename Context, typename T, typename... Extra>
void SetArray(Context* context, UINT start, UINT count, T* const* items, Extra... extra)
{
   Utils::DepthGuard guard(context_depth);
   if (!Detours::IsGameCall(guard) || !items || count > Max)
      return original<Detour>(context, start, count, items, extra...);
   std::array<T*, Max> game_items;
   std::copy_n(items, count, game_items.begin());
   if (Detours::Report(Pre, context, start, count, game_items.data(), extra...))
      return;
   original<Detour>(context, start, count, game_items.data(), extra...);
}

template <auto Pre>
bool PreSetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer** buffers)
{
   return Pre(context, start, count, buffers, nullptr, nullptr);
}

#define LUMA_STAGE_DETOURS(Stage, Shader)                                                                                                    \
   template <size_t V>                                                                                                                      \
   void STDMETHODCALLTYPE Stage##SetShader(ID3D11DeviceContext* context, Shader* shader, ID3D11ClassInstance* const* instances, UINT count) \
   {                                                                                                                                        \
      SetShader<&Stage##SetShader<V>, &Callbacks::Pre##Stage##SetShader>(context, shader, instances, count);                               \
   }                                                                                                                                        \
   template <size_t V>                                                                                                                      \
   void STDMETHODCALLTYPE Stage##SetShaderResources(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11ShaderResourceView* const* views) \
   {                                                                                                                                        \
      SetArray<&Stage##SetShaderResources<V>, &Callbacks::Pre##Stage##SetShaderResources, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT>(  \
         context, start, count, views);                                                                                                     \
   }                                                                                                                                        \
   template <size_t V>                                                                                                                      \
   void STDMETHODCALLTYPE Stage##SetConstantBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers)   \
   {                                                                                                                                        \
      SetArray<&Stage##SetConstantBuffers<V>, &PreSetConstantBuffers<&Callbacks::Pre##Stage##SetConstantBuffers>,                         \
         D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>(context, start, count, buffers);                                                \
   }                                                                                                                                        \
   template <size_t V>                                                                                                                      \
   void STDMETHODCALLTYPE Stage##SetConstantBuffers1(ID3D11DeviceContext1* context, UINT start, UINT count, ID3D11Buffer* const* buffers,  \
      const UINT* first_constant, const UINT* num_constants)                                                                                \
   {                                                                                                                                        \
      SetArray<&Stage##SetConstantBuffers1<V>, &Callbacks::Pre##Stage##SetConstantBuffers, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>( \
         context, start, count, buffers, first_constant, num_constants);                                                                    \
   }                                                                                                                                        \
   template <size_t V>                                                                                                                      \
   void STDMETHODCALLTYPE Stage##SetSamplers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11SamplerState* const* samplers)   \
   {                                                                                                                                        \
      SetArray<&Stage##SetSamplers<V>, &Callbacks::Pre##Stage##SetSamplers, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT>(context, start, count, samplers); \
   }

LUMA_STAGE_DETOURS(VS, ID3D11VertexShader)
LUMA_STAGE_DETOURS(HS, ID3D11HullShader)
LUMA_STAGE_DETOURS(DS, ID3D11DomainShader)
LUMA_STAGE_DETOURS(GS, ID3D11GeometryShader)
LUMA_STAGE_DETOURS(PS, ID3D11PixelShader)
LUMA_STAGE_DETOURS(CS, ID3D11ComputeShader)
#undef LUMA_STAGE_DETOURS

template <size_t V>
void STDMETHODCALLTYPE CSSetUnorderedAccessViews(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11UnorderedAccessView* const* views,
   const UINT* initial_counts)
{
   SetArray<&CSSetUnorderedAccessViews<V>, &Callbacks::PreCSSetUnorderedAccessViews, D3D11_1_UAV_SLOT_COUNT>(context, start, count, views, initial_counts);
}

template <size_t V>
void STDMETHODCALLTYPE OMSetRenderTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth_stencil)
{
   Utils::DepthGuard guard(context_depth);
   if (!Detours::IsGameCall(guard) || count > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT)
      return original<&OMSetRenderTargets<V>>(context, count, views, depth_stencil);
   std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_views;
   ID3D11RenderTargetView** edited_views = nullptr;
   if (views)
   {
      std::copy_n(views, count, game_views.begin());
      edited_views = game_views.data();
   }
   if (Detours::Report(&Callbacks::PreOMSetRenderTargets, context, count, edited_views, depth_stencil))
      return;
   original<&OMSetRenderTargets<V>>(context, count, edited_views, depth_stencil);
}

template <size_t V>
void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView* const* views,
   ID3D11DepthStencilView* depth_stencil, UINT uav_start, UINT uav_count, ID3D11UnorderedAccessView* const* uavs, const UINT* initial_counts)
{
   Utils::DepthGuard guard(context_depth);
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
   if (Detours::Report(&Callbacks::PreOMSetRenderTargetsAndUnorderedAccessViews, context, count, edited_views, depth_stencil, uav_start, uav_count,
          edited_uavs, initial_counts))
      return;
   original<&OMSetRenderTargetsAndUnorderedAccessViews<V>>(context, count, keep_views ? views : edited_views, depth_stencil, uav_start, uav_count,
      keep_uavs ? uavs : edited_uavs, initial_counts);
}

template <size_t V>
void STDMETHODCALLTYPE RSSetViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports)
{
   ReportAfter<&RSSetViewports<V>, &Callbacks::PostRSSetViewports>(context, count, viewports);
}

template <size_t V>
void STDMETHODCALLTYPE RSSetScissorRects(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects)
{
   ReportAfter<&RSSetScissorRects<V>, &Callbacks::PostRSSetScissorRects>(context, count, rects);
}

template <size_t V>
void STDMETHODCALLTYPE OMSetBlendState(ID3D11DeviceContext* context, ID3D11BlendState* state, const FLOAT blend_factor[4], UINT sample_mask)
{
   ReportAfter<&OMSetBlendState<V>, &Callbacks::PostOMSetBlendState>(context, state, blend_factor, sample_mask);
}

template <size_t V>
void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D11DeviceContext* context, ID3D11DepthStencilState* state, UINT stencil_ref)
{
   ReportAfter<&OMSetDepthStencilState<V>, &Callbacks::PostOMSetDepthStencilState>(context, state, stencil_ref);
}

template <size_t V>
void STDMETHODCALLTYPE IASetVertexBuffers(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets)
{
   ReportAfter<&IASetVertexBuffers<V>, &Callbacks::PostIASetVertexBuffers>(context, start, count, buffers, strides, offsets);
}

template <size_t V>
void STDMETHODCALLTYPE IASetIndexBuffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset)
{
   ReportAfter<&IASetIndexBuffer<V>, &Callbacks::PostIASetIndexBuffer>(context, buffer, format, offset);
}

template <size_t V>
void STDMETHODCALLTYPE ClearState(ID3D11DeviceContext* context)
{
   ReportAfter<&ClearState<V>, &Callbacks::PostClearState>(context);
}

template <size_t V>
HRESULT STDMETHODCALLTYPE Map(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags,
   D3D11_MAPPED_SUBRESOURCE* mapped)
{
   Utils::DepthGuard guard(context_depth);
   const HRESULT result = original<&Map<V>>(context, resource, subresource, type, flags, mapped);
   if (Detours::IsGameCall(guard) && SUCCEEDED(result) && mapped)
      Detours::Report(&Callbacks::PostMap, context, resource, subresource, type, flags, *mapped);
   return result;
}

template <size_t V>
void STDMETHODCALLTYPE Unmap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   Utils::DepthGuard guard(context_depth);
   const bool game_call = Detours::IsGameCall(guard);
   if (game_call && Detours::Report(&Callbacks::PreUnmap, context, resource, subresource))
      return;
   original<&Unmap<V>>(context, resource, subresource);
   if (game_call)
      Detours::Report(&Callbacks::PostUnmap, context, resource, subresource);
}

template <size_t V>
void STDMETHODCALLTYPE UpdateSubresource(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box,
   const void* data, UINT row_pitch, UINT depth_pitch)
{
   Utils::DepthGuard guard(context_depth);
   if (Detours::IsGameCall(guard) && Detours::Report(&Callbacks::PreUpdateSubresource, context, resource, subresource, box, data, row_pitch, depth_pitch, 0u))
      return;
   original<&UpdateSubresource<V>>(context, resource, subresource, box, data, row_pitch, depth_pitch);
}

template <size_t V>
void STDMETHODCALLTYPE UpdateSubresource1(ID3D11DeviceContext1* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box,
   const void* data, UINT row_pitch, UINT depth_pitch, UINT copy_flags)
{
   ReportBefore<&UpdateSubresource1<V>, &Callbacks::PreUpdateSubresource>(context, resource, subresource, box, data, row_pitch, depth_pitch, copy_flags);
}

template <size_t V>
void STDMETHODCALLTYPE CopyResource(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
{
   ReportBefore<&CopyResource<V>, &Callbacks::PreCopyResource>(context, destination, source);
}

template <size_t V>
void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y,
   UINT z, ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box)
{
   Utils::DepthGuard guard(context_depth);
   if (Detours::IsGameCall(guard) &&
      Detours::Report(&Callbacks::PreCopySubresourceRegion, context, destination, destination_subresource, x, y, z, source, source_subresource, box, 0u))
      return;
   original<&CopySubresourceRegion<V>>(context, destination, destination_subresource, x, y, z, source, source_subresource, box);
}

template <size_t V>
void STDMETHODCALLTYPE CopySubresourceRegion1(ID3D11DeviceContext1* context, ID3D11Resource* destination, UINT destination_subresource, UINT x,
   UINT y, UINT z, ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, UINT copy_flags)
{
   ReportBefore<&CopySubresourceRegion1<V>, &Callbacks::PreCopySubresourceRegion>(context, destination, destination_subresource, x, y, z, source,
      source_subresource, box, copy_flags);
}

template <size_t V>
void STDMETHODCALLTYPE ResolveSubresource(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source,
   UINT source_subresource, DXGI_FORMAT format)
{
   ReportBefore<&ResolveSubresource<V>, &Callbacks::PreResolveSubresource>(context, destination, destination_subresource, source, source_subresource, format);
}

template <size_t V>
void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4])
{
   ReportBefore<&ClearRenderTargetView<V>, &Callbacks::PreClearRenderTargetView>(context, view, color);
}

template <size_t V>
void STDMETHODCALLTYPE CopyStructureCount(ID3D11DeviceContext* context, ID3D11Buffer* dest, UINT dest_offset, ID3D11UnorderedAccessView* source)
{
   ReportBefore<&CopyStructureCount<V>, &Callbacks::PreCopyStructureCount>(context, dest, dest_offset, source);
}

template <size_t V>
void STDMETHODCALLTYPE ClearDepthStencilView(ID3D11DeviceContext* context, ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil)
{
   ReportBefore<&ClearDepthStencilView<V>, &Callbacks::PreClearDepthStencilView>(context, view, flags, depth, stencil);
}

template <size_t V>
void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const UINT values[4])
{
   ReportBefore<&ClearUnorderedAccessViewUint<V>, &Callbacks::PreClearUnorderedAccessViewUint>(context, view, values);
}

template <size_t V>
void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const FLOAT values[4])
{
   ReportBefore<&ClearUnorderedAccessViewFloat<V>, &Callbacks::PreClearUnorderedAccessViewFloat>(context, view, values);
}

template <size_t V>
void STDMETHODCALLTYPE ClearView(ID3D11DeviceContext1* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count)
{
   ReportBefore<&ClearView<V>, &Callbacks::PreClearView>(context, view, color, rects, rect_count);
}

template <size_t V>
void STDMETHODCALLTYPE Draw(ID3D11DeviceContext* context, UINT vertex_count, UINT start_vertex)
{
   ReportBefore<&Draw<V>, &Callbacks::PreDraw>(context, vertex_count, start_vertex);
}

template <size_t V>
void STDMETHODCALLTYPE DrawInstanced(ID3D11DeviceContext* context, UINT vertex_count, UINT instance_count, UINT start_vertex, UINT start_instance)
{
   ReportBefore<&DrawInstanced<V>, &Callbacks::PreDrawInstanced>(context, vertex_count, instance_count, start_vertex, start_instance);
}

template <size_t V>
void STDMETHODCALLTYPE DrawIndexed(ID3D11DeviceContext* context, UINT index_count, UINT start_index, INT base_vertex)
{
   ReportBefore<&DrawIndexed<V>, &Callbacks::PreDrawIndexed>(context, index_count, start_index, base_vertex);
}

template <size_t V>
void STDMETHODCALLTYPE DrawIndexedInstanced(ID3D11DeviceContext* context, UINT index_count, UINT instance_count, UINT start_index, INT base_vertex,
   UINT start_instance)
{
   ReportBefore<&DrawIndexedInstanced<V>, &Callbacks::PreDrawIndexedInstanced>(context, index_count, instance_count, start_index, base_vertex, start_instance);
}

template <size_t V>
void STDMETHODCALLTYPE DrawAuto(ID3D11DeviceContext* context)
{
   ReportBefore<&DrawAuto<V>, &Callbacks::PreDrawAuto>(context);
}

template <size_t V>
void STDMETHODCALLTYPE DrawInstancedIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset)
{
   ReportBefore<&DrawInstancedIndirect<V>, &Callbacks::PreDrawInstancedIndirect>(context, arguments, offset);
}

template <size_t V>
void STDMETHODCALLTYPE DrawIndexedInstancedIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset)
{
   ReportBefore<&DrawIndexedInstancedIndirect<V>, &Callbacks::PreDrawIndexedInstancedIndirect>(context, arguments, offset);
}

template <size_t V>
void STDMETHODCALLTYPE Dispatch(ID3D11DeviceContext* context, UINT x, UINT y, UINT z)
{
   ReportBefore<&Dispatch<V>, &Callbacks::PreDispatch>(context, x, y, z);
}

template <size_t V>
void STDMETHODCALLTYPE DispatchIndirect(ID3D11DeviceContext* context, ID3D11Buffer* arguments, UINT offset)
{
   ReportBefore<&DispatchIndirect<V>, &Callbacks::PreDispatchIndirect>(context, arguments, offset);
}

template <size_t V>
HRESULT STDMETHODCALLTYPE FinishCommandList(ID3D11DeviceContext* context, BOOL restore, ID3D11CommandList** list)
{
   Utils::DepthGuard guard(context_depth);
   const HRESULT result = original<&FinishCommandList<V>>(context, restore, list);
   if (Detours::IsGameCall(guard) && SUCCEEDED(result) && list && *list)
   {
      Scope::Guard scope;
      Lifetime::Track(*list, &Detours::OnDestroyed<Callbacks::OnCommandListDestroyed>);
      Callbacks::PostFinishCommandList(context, restore, *list);
   }
   return result;
}

template <size_t V>
void STDMETHODCALLTYPE ExecuteCommandList(ID3D11DeviceContext* context, ID3D11CommandList* list, BOOL restore)
{
   ReportBefore<&ExecuteCommandList<V>, &Callbacks::PreExecuteCommandList>(context, list, restore);
}

template <size_t V>
void STDMETHODCALLTYPE Flush(ID3D11DeviceContext* context)
{
   ReportBefore<&Flush<V>, &Callbacks::PreFlush>(context);
}

#define LUMA_CONTEXT_HOOKS(Type, Method, Version)                                                                                     \
   Hook{VtableTarget{Vtables::Interface::D3D11ImmediateContext, LumaSlot_##Type##_##Method, #Type "::" #Method " (immediate)", Version}, \
      reinterpret_cast<void*>(&Method<0>), reinterpret_cast<void**>(&original<&Method<0>>), true},                                   \
      Hook                                                                                                                           \
   {                                                                                                                                  \
      VtableTarget{Vtables::Interface::D3D11DeferredContext, LumaSlot_##Type##_##Method, #Type "::" #Method " (deferred)", Version}, \
         reinterpret_cast<void*>(&Method<1>), reinterpret_cast<void**>(&original<&Method<1>>), true                                  \
   }
#define LUMA_CONTEXT1_HOOKS(Method) LUMA_CONTEXT_HOOKS(ID3D11DeviceContext1, Method, __uuidof(ID3D11DeviceContext1))
#define LUMA_BASE_HOOKS(Method) LUMA_CONTEXT_HOOKS(ID3D11DeviceContext, Method, GUID_NULL)
} // namespace

void InstallD3D11Context(const D3D11::Functions& functions)
{
   using D3D11::Function;
   std::vector<Hook> hooks;
   const auto add = [&](Function function, std::initializer_list<Hook> function_hooks) {
      if (functions.test(size_t(function)))
         hooks.insert(hooks.end(), function_hooks);
   };
   add(Function::VSSetShader, {LUMA_BASE_HOOKS(VSSetShader)});
   add(Function::HSSetShader, {LUMA_BASE_HOOKS(HSSetShader)});
   add(Function::DSSetShader, {LUMA_BASE_HOOKS(DSSetShader)});
   add(Function::GSSetShader, {LUMA_BASE_HOOKS(GSSetShader)});
   add(Function::PSSetShader, {LUMA_BASE_HOOKS(PSSetShader)});
   add(Function::CSSetShader, {LUMA_BASE_HOOKS(CSSetShader)});
   add(Function::VSSetShaderResources, {LUMA_BASE_HOOKS(VSSetShaderResources)});
   add(Function::HSSetShaderResources, {LUMA_BASE_HOOKS(HSSetShaderResources)});
   add(Function::DSSetShaderResources, {LUMA_BASE_HOOKS(DSSetShaderResources)});
   add(Function::GSSetShaderResources, {LUMA_BASE_HOOKS(GSSetShaderResources)});
   add(Function::PSSetShaderResources, {LUMA_BASE_HOOKS(PSSetShaderResources)});
   add(Function::CSSetShaderResources, {LUMA_BASE_HOOKS(CSSetShaderResources)});
   add(Function::VSSetConstantBuffers, {LUMA_BASE_HOOKS(VSSetConstantBuffers), LUMA_CONTEXT1_HOOKS(VSSetConstantBuffers1)});
   add(Function::HSSetConstantBuffers, {LUMA_BASE_HOOKS(HSSetConstantBuffers), LUMA_CONTEXT1_HOOKS(HSSetConstantBuffers1)});
   add(Function::DSSetConstantBuffers, {LUMA_BASE_HOOKS(DSSetConstantBuffers), LUMA_CONTEXT1_HOOKS(DSSetConstantBuffers1)});
   add(Function::GSSetConstantBuffers, {LUMA_BASE_HOOKS(GSSetConstantBuffers), LUMA_CONTEXT1_HOOKS(GSSetConstantBuffers1)});
   add(Function::PSSetConstantBuffers, {LUMA_BASE_HOOKS(PSSetConstantBuffers), LUMA_CONTEXT1_HOOKS(PSSetConstantBuffers1)});
   add(Function::CSSetConstantBuffers, {LUMA_BASE_HOOKS(CSSetConstantBuffers), LUMA_CONTEXT1_HOOKS(CSSetConstantBuffers1)});
   add(Function::VSSetSamplers, {LUMA_BASE_HOOKS(VSSetSamplers)});
   add(Function::HSSetSamplers, {LUMA_BASE_HOOKS(HSSetSamplers)});
   add(Function::DSSetSamplers, {LUMA_BASE_HOOKS(DSSetSamplers)});
   add(Function::GSSetSamplers, {LUMA_BASE_HOOKS(GSSetSamplers)});
   add(Function::PSSetSamplers, {LUMA_BASE_HOOKS(PSSetSamplers)});
   add(Function::CSSetSamplers, {LUMA_BASE_HOOKS(CSSetSamplers)});
   add(Function::CSSetUnorderedAccessViews, {LUMA_BASE_HOOKS(CSSetUnorderedAccessViews)});
   add(Function::OMSetRenderTargets, {LUMA_BASE_HOOKS(OMSetRenderTargets)});
   add(Function::OMSetRenderTargetsAndUnorderedAccessViews, {LUMA_BASE_HOOKS(OMSetRenderTargetsAndUnorderedAccessViews)});
   add(Function::RSSetViewports, {LUMA_BASE_HOOKS(RSSetViewports)});
   add(Function::RSSetScissorRects, {LUMA_BASE_HOOKS(RSSetScissorRects)});
   add(Function::OMSetBlendState, {LUMA_BASE_HOOKS(OMSetBlendState)});
   add(Function::OMSetDepthStencilState, {LUMA_BASE_HOOKS(OMSetDepthStencilState)});
   add(Function::IASetVertexBuffers, {LUMA_BASE_HOOKS(IASetVertexBuffers)});
   add(Function::IASetIndexBuffer, {LUMA_BASE_HOOKS(IASetIndexBuffer)});
   add(Function::ClearState, {LUMA_BASE_HOOKS(ClearState)});
   add(Function::Map, {LUMA_BASE_HOOKS(Map)});
   add(Function::Unmap, {LUMA_BASE_HOOKS(Unmap)});
   add(Function::UpdateSubresource, {LUMA_BASE_HOOKS(UpdateSubresource), LUMA_CONTEXT1_HOOKS(UpdateSubresource1)});
   add(Function::CopyResource, {LUMA_BASE_HOOKS(CopyResource)});
   add(Function::CopySubresourceRegion, {LUMA_BASE_HOOKS(CopySubresourceRegion), LUMA_CONTEXT1_HOOKS(CopySubresourceRegion1)});
   add(Function::ResolveSubresource, {LUMA_BASE_HOOKS(ResolveSubresource)});
   add(Function::ClearRenderTargetView, {LUMA_BASE_HOOKS(ClearRenderTargetView)});
   add(Function::CopyStructureCount, {LUMA_BASE_HOOKS(CopyStructureCount)});
   add(Function::ClearDepthStencilView, {LUMA_BASE_HOOKS(ClearDepthStencilView)});
   add(Function::ClearUnorderedAccessViewUint, {LUMA_BASE_HOOKS(ClearUnorderedAccessViewUint)});
   add(Function::ClearUnorderedAccessViewFloat, {LUMA_BASE_HOOKS(ClearUnorderedAccessViewFloat)});
   add(Function::ClearView, {LUMA_CONTEXT1_HOOKS(ClearView)});
   add(Function::Draw, {LUMA_BASE_HOOKS(Draw)});
   add(Function::DrawInstanced, {LUMA_BASE_HOOKS(DrawInstanced)});
   add(Function::DrawIndexed, {LUMA_BASE_HOOKS(DrawIndexed)});
   add(Function::DrawIndexedInstanced, {LUMA_BASE_HOOKS(DrawIndexedInstanced)});
   add(Function::DrawAuto, {LUMA_BASE_HOOKS(DrawAuto)});
   add(Function::DrawInstancedIndirect, {LUMA_BASE_HOOKS(DrawInstancedIndirect)});
   add(Function::DrawIndexedInstancedIndirect, {LUMA_BASE_HOOKS(DrawIndexedInstancedIndirect)});
   add(Function::Dispatch, {LUMA_BASE_HOOKS(Dispatch)});
   add(Function::DispatchIndirect, {LUMA_BASE_HOOKS(DispatchIndirect)});
   add(Function::FinishCommandList, {LUMA_BASE_HOOKS(FinishCommandList)});
   add(Function::ExecuteCommandList, {LUMA_BASE_HOOKS(ExecuteCommandList)});
   add(Function::Flush, {LUMA_BASE_HOOKS(Flush)});
   if (!hooks.empty())
      Declare(hooks);
}
} // namespace Hooks
} // namespace Luma
