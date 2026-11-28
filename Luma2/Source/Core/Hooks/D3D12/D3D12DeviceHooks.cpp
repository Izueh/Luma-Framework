#include "Hooks/HookManager.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <type_traits>
#include <span>
#include <vector>

#include <d3d12.h>
#include <intrin.h>
#include <wrl/client.h>

#include "Handlers/D3D12/D3D12Handlers.hpp"
#include "Hooks/Detours.hpp"
#include "Hooks/VtableSlots.h"
#include "Resources/D3D12/D3D12Descriptors.hpp"

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

// Pipelines from the device and from the game's pipeline library, tracked the same way.
template <typename Handler, typename Desc, typename Call>
HRESULT CreatePipeline(Handler handler, ID3D12Device* device, bool from_library, const Desc* desc, void** pipeline, const void* caller, Call call)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard, caller) || !desc)
      return call(desc);
   return Detours::Report(handler, device, desc, from_library, pipeline, std::function<HRESULT(const Desc*)>(call));
}

ComPtr<ID3D12Device> GetDevice(ID3D12DeviceChild* child)
{
   ComPtr<ID3D12Device> device;
   child->GetDevice(IID_PPV_ARGS(&device));
   return device;
}

HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(&Handlers::CreateGraphicsPipelineState, device, false, desc, pipeline, _ReturnAddress(),
      [&](const auto* final_desc) { return original<&CreateGraphicsPipelineState>(device, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE CreateComputePipelineState(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(&Handlers::CreateComputePipelineState, device, false, desc, pipeline, _ReturnAddress(),
      [&](const auto* final_desc) { return original<&CreateComputePipelineState>(device, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE CreatePipelineState(ID3D12Device2* device, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(&Handlers::CreatePipelineState, device, false, desc, pipeline, _ReturnAddress(),
      [&](const auto* final_desc) { return original<&CreatePipelineState>(device, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE LoadGraphicsPipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(&Handlers::CreateGraphicsPipelineState, GetDevice(library).Get(), true, desc, pipeline, _ReturnAddress(),
      [&](const auto* final_desc) { return original<&LoadGraphicsPipeline>(library, name, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE LoadComputePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(&Handlers::CreateComputePipelineState, GetDevice(library).Get(), true, desc, pipeline, _ReturnAddress(),
      [&](const auto* final_desc) { return original<&LoadComputePipeline>(library, name, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE LoadPipeline(ID3D12PipelineLibrary1* library, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** pipeline)
{
   return CreatePipeline(&Handlers::CreatePipelineState, GetDevice(library).Get(), true, desc, pipeline, _ReturnAddress(),
      [&](const auto* final_desc) { return original<&LoadPipeline>(library, name, final_desc, riid, pipeline); });
}

HRESULT STDMETHODCALLTYPE CreatePipelineLibrary(ID3D12Device1* device, const void* blob, SIZE_T length, REFIID riid, void** library)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&CreatePipelineLibrary>(device, blob, length, riid, library);
   if (Detours::IsGameCall(guard, _ReturnAddress()))
      Detours::Report(&Handlers::PostCreatePipelineLibrary, size_t(length), result);
   return result;
}

// The game gets its signature extended with Luma's parameters (Shaders::RootSignatures::D3D12)
HRESULT STDMETHODCALLTYPE CreateRootSignature(ID3D12Device* device, UINT node_mask, const void* blob, SIZE_T length, REFIID riid, void** root_signature)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard, _ReturnAddress()) || !blob)
      return original<&CreateRootSignature>(device, node_mask, blob, length, riid, root_signature);
   return Detours::Report(&Handlers::CreateRootSignature, std::span(static_cast<const uint8_t*>(blob), length), root_signature,
      std::function<HRESULT(std::span<const uint8_t>)>([&](std::span<const uint8_t> final_blob) {
         return original<&CreateRootSignature>(device, node_mask, final_blob.data(), final_blob.size(), riid, root_signature);
      }));
}

HRESULT STDMETHODCALLTYPE CreateRootSignatureFromSubobjectInLibrary(ID3D12Device14* device, UINT node_mask, const void* library, SIZE_T length, LPCWSTR name,
   REFIID riid, void** root_signature)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&CreateRootSignatureFromSubobjectInLibrary>(device, node_mask, library, length, name, riid, root_signature);
   if (Detours::IsGameCall(guard, _ReturnAddress()) && SUCCEEDED(result))
      Detours::Report(&Handlers::PostCreateRootSignatureFromSubobjectInLibrary);
   return result;
}

HRESULT STDMETHODCALLTYPE CreateStateObject(ID3D12Device5* device, const D3D12_STATE_OBJECT_DESC* desc, REFIID riid, void** state_object)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&CreateStateObject>(device, desc, riid, state_object);
   if (Detours::IsGameCall(guard, _ReturnAddress()) && SUCCEEDED(result))
      Detours::Report(&Handlers::PostCreateStateObject);
   return result;
}

HRESULT STDMETHODCALLTYPE AddToStateObject(ID3D12Device7* device, const D3D12_STATE_OBJECT_DESC* addition, ID3D12StateObject* existing, REFIID riid, void** state_object)
{
   DepthGuard guard(device_depth);
   const HRESULT result = original<&AddToStateObject>(device, addition, existing, riid, state_object);
   if (Detours::IsGameCall(guard, _ReturnAddress()) && SUCCEEDED(result))
      Detours::Report(&Handlers::PostCreateStateObject);
   return result;
}

// Shader-visible heaps get Luma's tail (Resources::HeapTail::D3D12), which GetDesc hides
HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC* desc, REFIID riid, void** heap)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard, _ReturnAddress()) || !desc)
      return original<&CreateDescriptorHeap>(device, desc, riid, heap);
   return Detours::Report(&Handlers::CreateDescriptorHeap, device, *desc, heap,
      std::function<HRESULT(const D3D12_DESCRIPTOR_HEAP_DESC&)>(
         [&](const D3D12_DESCRIPTOR_HEAP_DESC& final_desc) { return original<&CreateDescriptorHeap>(device, &final_desc, riid, heap); }));
}

// D3D12's C ABI: the desc is returned through a pointer after `this`
D3D12_DESCRIPTOR_HEAP_DESC* STDMETHODCALLTYPE GetDescriptorHeapDesc(ID3D12DescriptorHeap* heap, D3D12_DESCRIPTOR_HEAP_DESC* desc)
{
   D3D12_DESCRIPTOR_HEAP_DESC* result = original<&GetDescriptorHeapDesc>(heap, desc);
   if (result)
      Handlers::PostGetDesc(heap, *result);
   return result;
}

using Resources::Descriptors::D3D12::Descriptor;
using Resources::Descriptors::D3D12::Kind;

// Views and samplers, with what they describe
void ReportView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, Kind kind, ID3D12Resource* resource, const void* desc, size_t desc_size, const void* caller)
{
   if (!Detours::IsGameCall(caller))
      return;
   Descriptor descriptor = {.kind = kind, .has_desc = desc != nullptr, .resource = resource};
   if (desc)
      std::memcpy(&descriptor.desc, desc, desc_size);
   Detours::Report(&Handlers::PostCreateView, device, destination, descriptor);
}

// A view of a texture upgraded in place: in its format (each view desc starts with its format)
template <typename Desc>
const Desc* PrepareView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, Kind kind, ID3D12Resource* resource, const Desc* desc, Desc& storage, const void* caller)
{
   if (!Detours::IsGameCall(caller))
      return desc;
   const DXGI_FORMAT format = Detours::Report(&Handlers::PreCreateView, device, destination, kind, resource, desc ? desc->Format : DXGI_FORMAT_UNKNOWN, desc != nullptr);
   if (format == DXGI_FORMAT_UNKNOWN || !desc)
      return desc;
   storage = *desc;
   storage.Format = format;
   return &storage;
}

void STDMETHODCALLTYPE CreateConstantBufferView(ID3D12Device* device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   original<&CreateConstantBufferView>(device, desc, destination);
   ReportView(device, destination, Kind::ConstantBuffer, nullptr, desc, sizeof(*desc), _ReturnAddress());
}

void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc,
   D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   D3D12_SHADER_RESOURCE_VIEW_DESC storage;
   original<&CreateShaderResourceView>(device, resource, PrepareView(device, destination, Kind::ShaderResource, resource, desc, storage, _ReturnAddress()), destination);
   ReportView(device, destination, Kind::ShaderResource, resource, desc, sizeof(*desc), _ReturnAddress());
}

void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Device* device, ID3D12Resource* resource, ID3D12Resource* counter,
   const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   D3D12_UNORDERED_ACCESS_VIEW_DESC storage;
   original<&CreateUnorderedAccessView>(device, resource, counter, PrepareView(device, destination, Kind::UnorderedAccess, resource, desc, storage, _ReturnAddress()), destination);
   ReportView(device, destination, Kind::UnorderedAccess, resource, desc, sizeof(*desc), _ReturnAddress());
}

void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc,
   D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   D3D12_RENDER_TARGET_VIEW_DESC storage;
   original<&CreateRenderTargetView>(device, resource, PrepareView(device, destination, Kind::RenderTarget, resource, desc, storage, _ReturnAddress()), destination);
   ReportView(device, destination, Kind::RenderTarget, resource, desc, sizeof(*desc), _ReturnAddress());
}

void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
   D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   D3D12_DEPTH_STENCIL_VIEW_DESC storage;
   original<&CreateDepthStencilView>(device, resource, PrepareView(device, destination, Kind::DepthStencil, resource, desc, storage, _ReturnAddress()), destination);
   ReportView(device, destination, Kind::DepthStencil, resource, desc, sizeof(*desc), _ReturnAddress());
}

void STDMETHODCALLTYPE CreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
   original<&CreateSampler>(device, desc, destination);
   ReportView(device, destination, Kind::Sampler, nullptr, desc, sizeof(*desc), _ReturnAddress());
}

// The destination and source ranges can be split differently: reported a pair of runs at a time
void STDMETHODCALLTYPE CopyDescriptors(ID3D12Device* device, UINT destination_count, const D3D12_CPU_DESCRIPTOR_HANDLE* destination_starts,
   const UINT* destination_sizes, UINT source_count, const D3D12_CPU_DESCRIPTOR_HANDLE* source_starts, const UINT* source_sizes, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   original<&CopyDescriptors>(device, destination_count, destination_starts, destination_sizes, source_count, source_starts, source_sizes, type);
   if (!Detours::IsGameCall(_ReturnAddress()) || !destination_starts || !source_starts)
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
         Handlers::PostCopyDescriptors(device, {destination_starts[destination].ptr + destination_done * increment}, {source_starts[source].ptr + source_done * increment}, count, type);
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
   if (Detours::IsGameCall(_ReturnAddress()))
      Detours::Report(&Handlers::PostCopyDescriptors, device, destination, source, count, type);
}

// Every resource creation: the outermost the game made (versions call each other). `state` is the legacy initial state, none for a
// creation with an enhanced barrier layout; `reserved` for a reserved (tiled) resource.
HRESULT ReportResource(const DepthGuard& guard, HRESULT result, void** resource, ID3D12Device* device, std::optional<D3D12_RESOURCE_STATES> state,
   const D3D12_CLEAR_VALUE* clear, bool reserved, const void* caller)
{
   if (Detours::IsGameCall(guard, caller))
   {
      if (const ComPtr<ID3D12Resource> created = GetCreated<ID3D12Resource>(result, resource))
         Detours::Report(&Handlers::PostCreateResource, device, created.Get(), state, clear, reserved);
   }
   return result;
}

// The handlers may upgrade the game's texture in place: its desc, its clear value's format, and no castable formats (the upgraded format's
// size wouldn't allow the game's). `prepare(desc, clear)` reports the handler, `call(desc, clear, upgraded)` creates; an upgraded creation
// that fails is made again as the game asked.
template <typename Desc, typename Prepare, typename Call>
HRESULT CreateResource(ID3D12Device* device, const Desc* desc, const D3D12_CLEAR_VALUE* clear, std::optional<D3D12_RESOURCE_STATES> state, void** resource,
   const void* caller, Prepare prepare, Call call)
{
   DepthGuard guard(device_depth);
   Desc upgraded_desc = {};
   D3D12_CLEAR_VALUE upgraded_clear = {};
   bool upgraded = false;
   if (desc && Detours::IsGameCall(guard, caller))
   {
      D3D12_RESOURCE_DESC base;
      std::memcpy(&base, desc, sizeof(base));
      if (clear)
         upgraded_clear = *clear;
      upgraded = prepare(base, clear ? &upgraded_clear : nullptr);
      if (upgraded)
      {
         upgraded_desc = *desc;
         std::memcpy(&upgraded_desc, &base, sizeof(base));
      }
   }
   HRESULT result = upgraded ? call(&upgraded_desc, clear ? &upgraded_clear : nullptr, true) : call(desc, clear, false);
   if (upgraded && FAILED(result))
   {
      Detours::Report(&Handlers::PostCreateResource, device, nullptr, state, clear, false);
      result = call(desc, clear, false);
   }
   else if (upgraded && !GetCreated<ID3D12Resource>(result, resource))
   {
      // Parameter-validation calls (ppvResource == nullptr) return S_FALSE without creating a resource
      Detours::Report(&Handlers::PostCreateResource, device, nullptr, state, clear, false);
   }
   return ReportResource(guard, result, resource, device, state, clear, false, caller);
}

HRESULT STDMETHODCALLTYPE CreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC* desc,
   D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear, REFIID riid, void** resource)
{
   return CreateResource(device, desc, clear, state, resource, _ReturnAddress(), [&](D3D12_RESOURCE_DESC& prepared, D3D12_CLEAR_VALUE* prepared_clear) { return Detours::Report(&Handlers::PreCreateCommittedResource, device, prepared, prepared_clear, heap, flags); }, [&](const D3D12_RESOURCE_DESC* final_desc, const D3D12_CLEAR_VALUE* final_clear, bool) { return original<&CreateCommittedResource>(device, heap, flags, final_desc, state, final_clear, riid, resource); });
}

HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Device* device, ID3D12Heap* heap, UINT64 offset, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES state,
   const D3D12_CLEAR_VALUE* clear, REFIID riid, void** resource)
{
   return CreateResource(device, desc, clear, state, resource, _ReturnAddress(), [&](D3D12_RESOURCE_DESC& prepared, D3D12_CLEAR_VALUE* prepared_clear) { return Detours::Report(&Handlers::PreCreatePlacedResource, device, prepared, prepared_clear, heap, offset); }, [&](const D3D12_RESOURCE_DESC* final_desc, const D3D12_CLEAR_VALUE* final_clear, bool) { return original<&CreatePlacedResource>(device, heap, offset, final_desc, state, final_clear, riid, resource); });
}

HRESULT STDMETHODCALLTYPE CreateReservedResource(ID3D12Device* device, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear,
   REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreateReservedResource>(device, desc, state, clear, riid, resource), resource, device, state, clear, true, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateCommittedResource1(ID3D12Device4* device, const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC* desc,
   D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear, ID3D12ProtectedResourceSession* session, REFIID riid, void** resource)
{
   return CreateResource(device, desc, clear, state, resource, _ReturnAddress(), [&](D3D12_RESOURCE_DESC& prepared, D3D12_CLEAR_VALUE* prepared_clear) { return Detours::Report(&Handlers::PreCreateCommittedResource, device, prepared, prepared_clear, heap, flags); }, [&](const D3D12_RESOURCE_DESC* final_desc, const D3D12_CLEAR_VALUE* final_clear, bool) { return original<&CreateCommittedResource1>(device, heap, flags, final_desc, state, final_clear, session, riid, resource); });
}

HRESULT STDMETHODCALLTYPE CreateReservedResource1(ID3D12Device4* device, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear,
   ID3D12ProtectedResourceSession* session, REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreateReservedResource1>(device, desc, state, clear, session, riid, resource), resource, device, state, clear, true, _ReturnAddress());
}

HRESULT STDMETHODCALLTYPE CreateCommittedResource2(ID3D12Device8* device, const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC1* desc,
   D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear, ID3D12ProtectedResourceSession* session, REFIID riid, void** resource)
{
   return CreateResource(device, desc, clear, state, resource, _ReturnAddress(), [&](D3D12_RESOURCE_DESC& prepared, D3D12_CLEAR_VALUE* prepared_clear) { return Detours::Report(&Handlers::PreCreateCommittedResource, device, prepared, prepared_clear, heap, flags); }, [&](const D3D12_RESOURCE_DESC1* final_desc, const D3D12_CLEAR_VALUE* final_clear, bool) { return original<&CreateCommittedResource2>(device, heap, flags, final_desc, state, final_clear, session, riid, resource); });
}

HRESULT STDMETHODCALLTYPE CreatePlacedResource1(ID3D12Device8* device, ID3D12Heap* heap, UINT64 offset, const D3D12_RESOURCE_DESC1* desc, D3D12_RESOURCE_STATES state,
   const D3D12_CLEAR_VALUE* clear, REFIID riid, void** resource)
{
   return CreateResource(device, desc, clear, state, resource, _ReturnAddress(), [&](D3D12_RESOURCE_DESC& prepared, D3D12_CLEAR_VALUE* prepared_clear) { return Detours::Report(&Handlers::PreCreatePlacedResource, device, prepared, prepared_clear, heap, offset); }, [&](const D3D12_RESOURCE_DESC1* final_desc, const D3D12_CLEAR_VALUE* final_clear, bool) { return original<&CreatePlacedResource1>(device, heap, offset, final_desc, state, final_clear, riid, resource); });
}

HRESULT STDMETHODCALLTYPE CreateCommittedResource3(ID3D12Device10* device, const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC1* desc,
   D3D12_BARRIER_LAYOUT layout, const D3D12_CLEAR_VALUE* clear, ID3D12ProtectedResourceSession* session, UINT32 castable_count, const DXGI_FORMAT* castable,
   REFIID riid, void** resource)
{
   return CreateResource(device, desc, clear, std::nullopt, resource, _ReturnAddress(), [&](D3D12_RESOURCE_DESC& prepared, D3D12_CLEAR_VALUE* prepared_clear) { return Detours::Report(&Handlers::PreCreateCommittedResource, device, prepared, prepared_clear, heap, flags); }, [&](const D3D12_RESOURCE_DESC1* final_desc, const D3D12_CLEAR_VALUE* final_clear, bool upgraded) { return original<&CreateCommittedResource3>(device, heap, flags, final_desc, layout, final_clear, session, upgraded ? 0 : castable_count, upgraded ? nullptr : castable, riid, resource); });
}

HRESULT STDMETHODCALLTYPE CreatePlacedResource2(ID3D12Device10* device, ID3D12Heap* heap, UINT64 offset, const D3D12_RESOURCE_DESC1* desc, D3D12_BARRIER_LAYOUT layout,
   const D3D12_CLEAR_VALUE* clear, UINT32 castable_count, const DXGI_FORMAT* castable, REFIID riid, void** resource)
{
   return CreateResource(device, desc, clear, std::nullopt, resource, _ReturnAddress(), [&](D3D12_RESOURCE_DESC& prepared, D3D12_CLEAR_VALUE* prepared_clear) { return Detours::Report(&Handlers::PreCreatePlacedResource, device, prepared, prepared_clear, heap, offset); }, [&](const D3D12_RESOURCE_DESC1* final_desc, const D3D12_CLEAR_VALUE* final_clear, bool upgraded) { return original<&CreatePlacedResource2>(device, heap, offset, final_desc, layout, final_clear, upgraded ? 0 : castable_count, upgraded ? nullptr : castable, riid, resource); });
}

HRESULT STDMETHODCALLTYPE CreateReservedResource2(ID3D12Device10* device, const D3D12_RESOURCE_DESC* desc, D3D12_BARRIER_LAYOUT layout, const D3D12_CLEAR_VALUE* clear,
   ID3D12ProtectedResourceSession* session, UINT32 castable_count, const DXGI_FORMAT* castable, REFIID riid, void** resource)
{
   DepthGuard guard(device_depth);
   return ReportResource(guard, original<&CreateReservedResource2>(device, desc, layout, clear, session, castable_count, castable, riid, resource), resource, device, std::nullopt, clear, true, _ReturnAddress());
}

// The game's allocator lays its heaps out with these: a desc the rules upgrade in place is answered for its upgraded size (and remembered
// for its creation). D3D12's C ABI: the info is returned through a pointer after `this`.
template <typename Desc>
const Desc* UpgradeAllocationDescs(ID3D12Device* device, UINT count, const Desc* descs, std::vector<Desc>& storage, std::vector<bool>* upgraded = nullptr)
{
   if (!descs || count == 0)
      return descs;
   if (upgraded)
      upgraded->assign(count, false);
   bool any = false;
   for (UINT i = 0; i < count; i++)
   {
      D3D12_RESOURCE_DESC base;
      std::memcpy(&base, &descs[i], sizeof(base));
      if (!Detours::Report(&Handlers::PreGetResourceAllocationInfo, device, base))
         continue;
      if (!any)
         storage.assign(descs, descs + count);
      any = true;
      std::memcpy(&storage[i], &base, sizeof(base));
      if (upgraded)
         (*upgraded)[i] = true;
   }
   return any ? storage.data() : descs;
}

// Versions 0 to 2: `infos` is version 1's and 2's per desc info
template <typename Device, typename Desc, typename... Infos>
D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE GetResourceAllocationInfo(Device* device, D3D12_RESOURCE_ALLOCATION_INFO* info, UINT mask, UINT count, const Desc* descs,
   Infos... infos)
{
   constexpr auto detour = &GetResourceAllocationInfo<Device, Desc, Infos...>;
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard, _ReturnAddress()))
      return original<detour>(device, info, mask, count, descs, infos...);
   std::vector<Desc> storage;
   return original<detour>(device, info, mask, count, UpgradeAllocationDescs(device, count, descs, storage), infos...);
}

D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE GetResourceAllocationInfo3(ID3D12Device12* device, D3D12_RESOURCE_ALLOCATION_INFO* info, UINT mask, UINT count,
   const D3D12_RESOURCE_DESC1* descs, const UINT32* castable_counts, const DXGI_FORMAT* const* castables, D3D12_RESOURCE_ALLOCATION_INFO1* infos)
{
   DepthGuard guard(device_depth);
   if (!Detours::IsGameCall(guard, _ReturnAddress()))
      return original<&GetResourceAllocationInfo3>(device, info, mask, count, descs, castable_counts, castables, infos);
   std::vector<D3D12_RESOURCE_DESC1> storage;
   std::vector<bool> upgraded;
   const D3D12_RESOURCE_DESC1* final_descs = UpgradeAllocationDescs(device, count, descs, storage, &upgraded);
   // An upgraded one is created without the game's castable formats
   std::vector<UINT32> counts;
   if (final_descs != descs && castable_counts)
   {
      counts.assign(castable_counts, castable_counts + count);
      for (UINT i = 0; i < count; i++)
      {
         if (upgraded[i])
            counts[i] = 0;
      }
   }
   return original<&GetResourceAllocationInfo3>(device, info, mask, count, final_descs, counts.empty() ? castable_counts : counts.data(), castables, infos);
}

// Textures upgraded in place report what the game asked for to the game when the rules spoof it (Upgrade::Rules::spoof_description).
// Returned through a pointer after `this`.
template <typename Resource, typename Desc>
Desc* STDMETHODCALLTYPE GetResourceDesc(Resource* resource, Desc* desc)
{
   Desc* result = original<&GetResourceDesc<Resource, Desc>>(resource, desc);
   if (result && Detours::IsGameCall(_ReturnAddress()))
   {
      Scope::Guard scope;
      Handlers::PostGetDesc(static_cast<ID3D12Resource*>(resource), *result);
   }
   return result;
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
   add(Function::GetResourceAllocationInfo, {
                                               MethodHook<&GetResourceAllocationInfo<ID3D12Device, D3D12_RESOURCE_DESC>>(Interface::D3D12Device, LumaSlot_ID3D12Device_GetResourceAllocationInfo, "ID3D12Device::GetResourceAllocationInfo"),
                                               MethodHook<&GetResourceAllocationInfo<ID3D12Device4, D3D12_RESOURCE_DESC, D3D12_RESOURCE_ALLOCATION_INFO1*>>(Interface::D3D12Device, LumaSlot_ID3D12Device4_GetResourceAllocationInfo1, "ID3D12Device4::GetResourceAllocationInfo1", __uuidof(ID3D12Device4)),
                                               MethodHook<&GetResourceAllocationInfo<ID3D12Device8, D3D12_RESOURCE_DESC1, D3D12_RESOURCE_ALLOCATION_INFO1*>>(Interface::D3D12Device, LumaSlot_ID3D12Device8_GetResourceAllocationInfo2, "ID3D12Device8::GetResourceAllocationInfo2", __uuidof(ID3D12Device8)),
                                               MethodHook<&GetResourceAllocationInfo3>(Interface::D3D12Device, LumaSlot_ID3D12Device12_GetResourceAllocationInfo3, "ID3D12Device12::GetResourceAllocationInfo3", __uuidof(ID3D12Device12)),
                                            });
   add(Function::GetResourceDesc, {
                                     MethodHook<&GetResourceDesc<ID3D12Resource, D3D12_RESOURCE_DESC>>(Interface::D3D12Resource, LumaSlot_ID3D12Resource_GetDesc, "ID3D12Resource::GetDesc"),
                                     MethodHook<&GetResourceDesc<ID3D12Resource2, D3D12_RESOURCE_DESC1>>(Interface::D3D12Resource, LumaSlot_ID3D12Resource2_GetDesc1, "ID3D12Resource2::GetDesc1", __uuidof(ID3D12Resource2)),
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
