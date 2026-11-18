#include "Hooks/Hooks.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <type_traits>
#include <span>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Handlers/D3D12/D3D12Handlers.hpp"
#include "Hooks/Detours.hpp"
#include "Hooks/Slots.h"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Shaders/D3D12/D3D12Database.hpp"
#include "Shaders/D3D12/D3D12RootSignatures.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Hooks {
namespace {
using Detours::original;

thread_local uint32_t device_depth = 0;

template <typename T>
ComPtr<T> GetCreated(HRESULT result, void** object)
{
   ComPtr<T> created;
   if (SUCCEEDED(result) && object && *object)
      static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&created));
   return created;
}

using Shaders::Database::D3D12::PipelineKind;

template <typename Desc>
constexpr PipelineKind GetKind()
{
   if constexpr (std::is_same_v<Desc, D3D12_GRAPHICS_PIPELINE_STATE_DESC>)
      return PipelineKind::Graphics;
   else if constexpr (std::is_same_v<Desc, D3D12_COMPUTE_PIPELINE_STATE_DESC>)
      return PipelineKind::Compute;
   else
      return PipelineKind::Stream;
}

// Pipelines from the device and from the game's pipeline library, tracked the same way. `call` calls the original with a desc.
template <typename Desc, typename Call>
HRESULT CreatePipeline(ID3D12Device* device, bool from_library, const Desc* desc, void** pipeline, Call call)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard) || !desc)
      return call(desc);
   // A root signature that comes with the pipeline is replaced by its extended one
   const std::optional<Shaders::RootSignatures::D3D12::RewrittenDesc> rewritten = Detours::Report(&Handlers::PreCreatePipelineState, device, GetKind<Desc>(), static_cast<const void*>(desc));
   const Desc* final_desc = rewritten ? reinterpret_cast<const Desc*>(rewritten->bytes.data()) : desc;
   const HRESULT result = call(final_desc);
   Scope::Guard scope;
   if (from_library)
      Handlers::PostLoadPipeline(result);
   if (const ComPtr<ID3D12PipelineState> created = GetCreated<ID3D12PipelineState>(result, pipeline))
      Handlers::PostCreatePipelineState(GetKind<Desc>(), final_desc, from_library, created.Get());
   return result;
}

ComPtr<ID3D12Device> GetDevice(ID3D12DeviceChild* child)
{
   ComPtr<ID3D12Device> device;
   child->GetDevice(IID_PPV_ARGS(&device));
   return device;
}

HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(device, false, desc, pipeline, [&](const auto* final_desc) { return original<&CreateGraphicsPipelineState>(device, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE CreateComputePipelineState(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(device, false, desc, pipeline, [&](const auto* final_desc) { return original<&CreateComputePipelineState>(device, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE CreatePipelineState(ID3D12Device2* device, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(device, false, desc, pipeline, [&](const auto* final_desc) { return original<&CreatePipelineState>(device, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE LoadGraphicsPipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(GetDevice(library).Get(), true, desc, pipeline, [&](const auto* final_desc) { return original<&LoadGraphicsPipeline>(library, name, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE LoadComputePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(GetDevice(library).Get(), true, desc, pipeline, [&](const auto* final_desc) { return original<&LoadComputePipeline>(library, name, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE LoadPipeline(ID3D12PipelineLibrary1* library, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(GetDevice(library).Get(), true, desc, pipeline, [&](const auto* final_desc) { return original<&LoadPipeline>(library, name, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE CreatePipelineLibrary(ID3D12Device1* device, const void* blob, SIZE_T length, REFIID riid, void** library)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&CreatePipelineLibrary>(device, blob, length, riid, library);
   if (Detours::IsGameCall(guard))
      Detours::Report(&Handlers::PostCreatePipelineLibrary, size_t(length), result);
   return result;
}

// The game gets its signature extended with Luma's parameters (Shaders::RootSignatures::D3D12)
HRESULT STDMETHODCALLTYPE CreateRootSignature(ID3D12Device* device, UINT node_mask, const void* blob, SIZE_T length, REFIID riid, void** root_signature)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard) || !blob)
      return original<&CreateRootSignature>(device, node_mask, blob, length, riid, root_signature);
   const std::span game_blob(static_cast<const uint8_t*>(blob), length);
   const Shaders::RootSignatures::D3D12::Extended extended = Detours::Report(&Handlers::PreCreateRootSignature, game_blob);
   uint32_t luma_parameter = UINT32_MAX;
   bool luma_table = false;
   HRESULT result = E_FAIL;
   if (!extended.blob.empty())
   {
      result = original<&CreateRootSignature>(device, node_mask, extended.blob.data(), extended.blob.size(), riid, root_signature);
      if (SUCCEEDED(result))
      {
         luma_parameter = extended.luma_parameter;
         luma_table = extended.table;
      }
      else
         Detours::Report(&Handlers::PostExtendedRootSignatureRefused, result);
   }
   if (FAILED(result))
      result = original<&CreateRootSignature>(device, node_mask, blob, length, riid, root_signature);
   if (const ComPtr<ID3D12RootSignature> created = GetCreated<ID3D12RootSignature>(result, root_signature))
      Detours::Report(&Handlers::PostCreateRootSignature, game_blob, created.Get(), luma_parameter, luma_table);
   return result;
}

HRESULT STDMETHODCALLTYPE CreateRootSignatureFromSubobjectInLibrary(ID3D12Device14* device, UINT node_mask, const void* library, SIZE_T length, LPCWSTR name,
   REFIID riid, void** root_signature)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&CreateRootSignatureFromSubobjectInLibrary>(device, node_mask, library, length, name, riid, root_signature);
   if (Detours::IsGameCall(guard) && SUCCEEDED(result))
      Detours::Report(&Handlers::PostCreateRootSignatureFromLibrary);
   return result;
}

HRESULT STDMETHODCALLTYPE CreateStateObject(ID3D12Device5* device, const D3D12_STATE_OBJECT_DESC* desc, REFIID riid, void** state_object)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&CreateStateObject>(device, desc, riid, state_object);
   if (Detours::IsGameCall(guard) && SUCCEEDED(result))
      Detours::Report(&Handlers::PostCreateStateObject);
   return result;
}

HRESULT STDMETHODCALLTYPE AddToStateObject(ID3D12Device7* device, const D3D12_STATE_OBJECT_DESC* addition, ID3D12StateObject* existing, REFIID riid, void** state_object)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&AddToStateObject>(device, addition, existing, riid, state_object);
   if (Detours::IsGameCall(guard) && SUCCEEDED(result))
      Detours::Report(&Handlers::PostCreateStateObject);
   return result;
}

// Shader-visible heaps get Luma's tail (Resources::HeapTail::D3D12), which GetDesc hides
HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC* desc, REFIID riid, void** heap)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard) || !desc)
      return original<&CreateDescriptorHeap>(device, desc, riid, heap);
   const std::optional<D3D12_DESCRIPTOR_HEAP_DESC> extended = Detours::Report(&Handlers::PreCreateDescriptorHeap, *desc);
   HRESULT result = E_FAIL;
   if (extended)
      result = original<&CreateDescriptorHeap>(device, &*extended, riid, heap);
   const bool with_tail = SUCCEEDED(result);
   if (!with_tail)
      result = original<&CreateDescriptorHeap>(device, desc, riid, heap);
   if (const ComPtr<ID3D12DescriptorHeap> created = GetCreated<ID3D12DescriptorHeap>(result, heap))
      Detours::Report(&Handlers::PostCreateDescriptorHeap, device, *desc, created.Get(), with_tail);
   return result;
}

// D3D12's C ABI: the desc is returned through a pointer after `this`
D3D12_DESCRIPTOR_HEAP_DESC* STDMETHODCALLTYPE GetDescriptorHeapDesc(ID3D12DescriptorHeap* heap, D3D12_DESCRIPTOR_HEAP_DESC* desc)
{
   D3D12_DESCRIPTOR_HEAP_DESC* result = original<&GetDescriptorHeapDesc>(heap, desc);
   if (result)
   {
      if (const std::optional<uint32_t> game_size = Handlers::GetGameHeapSize(heap))
         result->NumDescriptors = *game_size;
   }
   return result;
}

using Resources::Descriptors::D3D12::Descriptor;
using Resources::Descriptors::D3D12::Kind;

// Views and samplers, with what they describe
void ReportView(D3D12_CPU_DESCRIPTOR_HANDLE destination, Kind kind, ID3D12Resource* resource, const void* desc, size_t desc_size)
{
   if (Scope::IsActive())
      return;
   Descriptor descriptor = {.kind = kind, .has_desc = desc != nullptr, .resource = resource};
   if (desc)
      std::memcpy(&descriptor.desc, desc, desc_size);
   Detours::Report(&Handlers::PostCreateView, destination, descriptor);
}

void STDMETHODCALLTYPE CreateConstantBufferView(ID3D12Device* device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   original<&CreateConstantBufferView>(device, desc, destination);
   ReportView(destination, Kind::ConstantBuffer, nullptr, desc, sizeof(*desc));
}

void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc,
   D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   original<&CreateShaderResourceView>(device, resource, desc, destination);
   ReportView(destination, Kind::ShaderResource, resource, desc, sizeof(*desc));
}

void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Device* device, ID3D12Resource* resource, ID3D12Resource* counter,
   const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   original<&CreateUnorderedAccessView>(device, resource, counter, desc, destination);
   ReportView(destination, Kind::UnorderedAccess, resource, desc, sizeof(*desc));
}

void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc,
   D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   original<&CreateRenderTargetView>(device, resource, desc, destination);
   ReportView(destination, Kind::RenderTarget, resource, desc, sizeof(*desc));
}

void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
   D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   original<&CreateDepthStencilView>(device, resource, desc, destination);
   ReportView(destination, Kind::DepthStencil, resource, desc, sizeof(*desc));
}

void STDMETHODCALLTYPE CreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   original<&CreateSampler>(device, desc, destination);
   ReportView(destination, Kind::Sampler, nullptr, desc, sizeof(*desc));
}

// The destination and source ranges can be split differently: reported a pair of runs at a time
void STDMETHODCALLTYPE CopyDescriptors(ID3D12Device* device, UINT destination_count, const D3D12_CPU_DESCRIPTOR_HANDLE* destination_starts,
   const UINT* destination_sizes, UINT source_count, const D3D12_CPU_DESCRIPTOR_HANDLE* source_starts, const UINT* source_sizes, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   original<&CopyDescriptors>(device, destination_count, destination_starts, destination_sizes, source_count, source_starts, source_sizes, type);
   if (Scope::IsActive() || !destination_starts || !source_starts)
      return;
   Scope::Guard scope;
   const SIZE_T increment = device->GetDescriptorHandleIncrementSize(type);
   UINT destination = 0;
   UINT source = 0;
   UINT destination_done = 0;
   UINT source_done = 0;
   while (destination < destination_count && source < source_count)
   {
      const UINT destination_size = destination_sizes ? destination_sizes[destination] : 1;
      const UINT source_size = source_sizes ? source_sizes[source] : 1;
      const UINT count = std::min(destination_size - destination_done, source_size - source_done);
      if (count)
         Handlers::PostCopyDescriptors({destination_starts[destination].ptr + destination_done * increment}, {source_starts[source].ptr + source_done * increment}, count);
      destination_done += count;
      source_done += count;
      if (destination_done >= destination_size)
      {
         destination++;
         destination_done = 0;
      }
      if (source_done >= source_size)
      {
         source++;
         source_done = 0;
      }
   }
}

void STDMETHODCALLTYPE CopyDescriptorsSimple(ID3D12Device* device, UINT count, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source,
   D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   original<&CopyDescriptorsSimple>(device, count, destination, source, type);
   if (!Scope::IsActive())
      Detours::Report(&Handlers::PostCopyDescriptors, destination, source, count);
}

// Every resource creation: the outermost the game made (versions call each other)
HRESULT ReportResource(const DepthGuard& guard, HRESULT result, void** resource)
{
   if (Detours::IsGameCall(guard))
   {
      if (const ComPtr<ID3D12Resource> created = GetCreated<ID3D12Resource>(result, resource))
         Detours::Report(&Handlers::PostCreateResource, created.Get());
   }
   return result;
}

HRESULT STDMETHODCALLTYPE CreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC* desc,
   D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear, REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreateCommittedResource>(device, heap, flags, desc, state, clear, riid, resource), resource);
}

HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Device* device, ID3D12Heap* heap, UINT64 offset, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES state,
   const D3D12_CLEAR_VALUE* clear, REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreatePlacedResource>(device, heap, offset, desc, state, clear, riid, resource), resource);
}

HRESULT STDMETHODCALLTYPE CreateReservedResource(ID3D12Device* device, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear,
   REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreateReservedResource>(device, desc, state, clear, riid, resource), resource);
}

HRESULT STDMETHODCALLTYPE CreateCommittedResource1(ID3D12Device4* device, const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC* desc,
   D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear, ID3D12ProtectedResourceSession* session, REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreateCommittedResource1>(device, heap, flags, desc, state, clear, session, riid, resource), resource);
}

HRESULT STDMETHODCALLTYPE CreateReservedResource1(ID3D12Device4* device, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear,
   ID3D12ProtectedResourceSession* session, REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreateReservedResource1>(device, desc, state, clear, session, riid, resource), resource);
}

HRESULT STDMETHODCALLTYPE CreateCommittedResource2(ID3D12Device8* device, const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC1* desc,
   D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear, ID3D12ProtectedResourceSession* session, REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreateCommittedResource2>(device, heap, flags, desc, state, clear, session, riid, resource), resource);
}

HRESULT STDMETHODCALLTYPE CreatePlacedResource1(ID3D12Device8* device, ID3D12Heap* heap, UINT64 offset, const D3D12_RESOURCE_DESC1* desc, D3D12_RESOURCE_STATES state,
   const D3D12_CLEAR_VALUE* clear, REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreatePlacedResource1>(device, heap, offset, desc, state, clear, riid, resource), resource);
}

HRESULT STDMETHODCALLTYPE CreateCommittedResource3(ID3D12Device10* device, const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC1* desc,
   D3D12_BARRIER_LAYOUT layout, const D3D12_CLEAR_VALUE* clear, ID3D12ProtectedResourceSession* session, UINT32 castable_count, const DXGI_FORMAT* castable,
   REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreateCommittedResource3>(device, heap, flags, desc, layout, clear, session, castable_count, castable, riid, resource), resource);
}

HRESULT STDMETHODCALLTYPE CreatePlacedResource2(ID3D12Device10* device, ID3D12Heap* heap, UINT64 offset, const D3D12_RESOURCE_DESC1* desc, D3D12_BARRIER_LAYOUT layout,
   const D3D12_CLEAR_VALUE* clear, UINT32 castable_count, const DXGI_FORMAT* castable, REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreatePlacedResource2>(device, heap, offset, desc, layout, clear, castable_count, castable, riid, resource), resource);
}

HRESULT STDMETHODCALLTYPE CreateReservedResource2(ID3D12Device10* device, const D3D12_RESOURCE_DESC* desc, D3D12_BARRIER_LAYOUT layout, const D3D12_CLEAR_VALUE* clear,
   ID3D12ProtectedResourceSession* session, UINT32 castable_count, const DXGI_FORMAT* castable, REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreateReservedResource2>(device, desc, layout, clear, session, castable_count, castable, riid, resource), resource);
}

template <auto Detour>
Hook MethodHook(Vtables::Interface vtable, uint32_t slot, const char* name, const IID& version = GUID_NULL)
{
   return {VtableTarget{vtable, slot, name, version}, reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&original<Detour>)};
}
} // namespace

void InstallD3D12Device(const D3D12::Functions& functions)
{
   using D3D12::Function;
   using Vtables::Interface;
   std::vector<Hook> hooks;
   const auto add = [&](Function function, std::initializer_list<Hook> function_hooks) {
      if (functions.test(size_t(function)))
         hooks.insert(hooks.end(), function_hooks);
   };
   add(Function::CreatePipelineState, {
                                         MethodHook<&CreateGraphicsPipelineState>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateGraphicsPipelineState, "ID3D12Device::CreateGraphicsPipelineState"),
                                         MethodHook<&CreateComputePipelineState>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateComputePipelineState, "ID3D12Device::CreateComputePipelineState"),
                                         MethodHook<&CreatePipelineState>(Interface::D3D12Device, LumaSlot_ID3D12Device2_CreatePipelineState, "ID3D12Device2::CreatePipelineState", __uuidof(ID3D12Device2)),
                                         MethodHook<&LoadGraphicsPipeline>(Interface::D3D12PipelineLibrary, LumaSlot_ID3D12PipelineLibrary_LoadGraphicsPipeline, "ID3D12PipelineLibrary::LoadGraphicsPipeline"),
                                         MethodHook<&LoadComputePipeline>(Interface::D3D12PipelineLibrary, LumaSlot_ID3D12PipelineLibrary_LoadComputePipeline, "ID3D12PipelineLibrary::LoadComputePipeline"),
                                         MethodHook<&LoadPipeline>(Interface::D3D12PipelineLibrary, LumaSlot_ID3D12PipelineLibrary1_LoadPipeline, "ID3D12PipelineLibrary1::LoadPipeline", __uuidof(ID3D12PipelineLibrary1)),
                                      });
   add(Function::CreatePipelineLibrary, {MethodHook<&CreatePipelineLibrary>(Interface::D3D12Device, LumaSlot_ID3D12Device1_CreatePipelineLibrary, "ID3D12Device1::CreatePipelineLibrary", __uuidof(ID3D12Device1))});
   add(Function::CreateRootSignature, {
                                         MethodHook<&CreateRootSignature>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateRootSignature, "ID3D12Device::CreateRootSignature"),
                                         MethodHook<&CreateRootSignatureFromSubobjectInLibrary>(Interface::D3D12Device, LumaSlot_ID3D12Device14_CreateRootSignatureFromSubobjectInLibrary, "ID3D12Device14::CreateRootSignatureFromSubobjectInLibrary", __uuidof(ID3D12Device14)),
                                      });
   add(Function::CreateStateObject, {
                                       MethodHook<&CreateStateObject>(Interface::D3D12Device, LumaSlot_ID3D12Device5_CreateStateObject, "ID3D12Device5::CreateStateObject", __uuidof(ID3D12Device5)),
                                       MethodHook<&AddToStateObject>(Interface::D3D12Device, LumaSlot_ID3D12Device7_AddToStateObject, "ID3D12Device7::AddToStateObject", __uuidof(ID3D12Device7)),
                                    });
   add(Function::CreateDescriptorHeap, {
                                          MethodHook<&CreateDescriptorHeap>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateDescriptorHeap, "ID3D12Device::CreateDescriptorHeap"),
                                          MethodHook<&GetDescriptorHeapDesc>(Interface::D3D12DescriptorHeap, LumaSlot_ID3D12DescriptorHeap_GetDesc, "ID3D12DescriptorHeap::GetDesc"),
                                       });
   add(Function::CreateView, {
                                MethodHook<&CreateConstantBufferView>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateConstantBufferView, "ID3D12Device::CreateConstantBufferView"),
                                MethodHook<&CreateShaderResourceView>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateShaderResourceView, "ID3D12Device::CreateShaderResourceView"),
                                MethodHook<&CreateUnorderedAccessView>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateUnorderedAccessView, "ID3D12Device::CreateUnorderedAccessView"),
                                MethodHook<&CreateRenderTargetView>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateRenderTargetView, "ID3D12Device::CreateRenderTargetView"),
                                MethodHook<&CreateDepthStencilView>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateDepthStencilView, "ID3D12Device::CreateDepthStencilView"),
                                MethodHook<&CreateSampler>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateSampler, "ID3D12Device::CreateSampler"),
                             });
   add(Function::CreateResource, {
                                    MethodHook<&CreateCommittedResource>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateCommittedResource, "ID3D12Device::CreateCommittedResource"),
                                    MethodHook<&CreatePlacedResource>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreatePlacedResource, "ID3D12Device::CreatePlacedResource"),
                                    MethodHook<&CreateReservedResource>(Interface::D3D12Device, LumaSlot_ID3D12Device_CreateReservedResource, "ID3D12Device::CreateReservedResource"),
                                    MethodHook<&CreateCommittedResource1>(Interface::D3D12Device, LumaSlot_ID3D12Device4_CreateCommittedResource1, "ID3D12Device4::CreateCommittedResource1", __uuidof(ID3D12Device4)),
                                    MethodHook<&CreateReservedResource1>(Interface::D3D12Device, LumaSlot_ID3D12Device4_CreateReservedResource1, "ID3D12Device4::CreateReservedResource1", __uuidof(ID3D12Device4)),
                                    MethodHook<&CreateCommittedResource2>(Interface::D3D12Device, LumaSlot_ID3D12Device8_CreateCommittedResource2, "ID3D12Device8::CreateCommittedResource2", __uuidof(ID3D12Device8)),
                                    MethodHook<&CreatePlacedResource1>(Interface::D3D12Device, LumaSlot_ID3D12Device8_CreatePlacedResource1, "ID3D12Device8::CreatePlacedResource1", __uuidof(ID3D12Device8)),
                                    MethodHook<&CreateCommittedResource3>(Interface::D3D12Device, LumaSlot_ID3D12Device10_CreateCommittedResource3, "ID3D12Device10::CreateCommittedResource3", __uuidof(ID3D12Device10)),
                                    MethodHook<&CreatePlacedResource2>(Interface::D3D12Device, LumaSlot_ID3D12Device10_CreatePlacedResource2, "ID3D12Device10::CreatePlacedResource2", __uuidof(ID3D12Device10)),
                                    MethodHook<&CreateReservedResource2>(Interface::D3D12Device, LumaSlot_ID3D12Device10_CreateReservedResource2, "ID3D12Device10::CreateReservedResource2", __uuidof(ID3D12Device10)),
                                 });
   add(Function::CopyDescriptors, {
                                     MethodHook<&CopyDescriptors>(Interface::D3D12Device, LumaSlot_ID3D12Device_CopyDescriptors, "ID3D12Device::CopyDescriptors"),
                                     MethodHook<&CopyDescriptorsSimple>(Interface::D3D12Device, LumaSlot_ID3D12Device_CopyDescriptorsSimple, "ID3D12Device::CopyDescriptorsSimple"),
                                  });
   if (!hooks.empty())
      Declare(hooks);
}
} // namespace Hooks
} // namespace Luma
