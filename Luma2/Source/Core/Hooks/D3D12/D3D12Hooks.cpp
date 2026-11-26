#include "Hooks/HookManager.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

#include <d3d12.h>
#include <wrl/client.h>

#include "Handlers/D3D12/D3D12Handlers.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/VtableSlots.h"
#include "Hooks/DepthGuard.hpp"
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Hooks {
namespace {
PFN_D3D12_CREATE_DEVICE original_create_device = nullptr;
thread_local uint32_t create_depth = 0;
std::atomic<bool> vtables_resolved = false;

// Children return the real device: GetDevice hands the game its proxy instead. Kinds of children have different functions,
// and one may jump into another, so each has a detour and only the outermost call swaps.
using GetDeviceFunction = HRESULT(STDMETHODCALLTYPE*)(ID3D12DeviceChild* child, REFIID riid, void** device);

struct GetDeviceTarget
{
   Vtables::Interface vtable;
   const char* name;
};

constexpr std::array GET_DEVICE_TARGETS = {
   GetDeviceTarget{Vtables::Interface::D3D12CommandQueue, "ID3D12CommandQueue::GetDevice"},
   GetDeviceTarget{Vtables::Interface::D3D12CommandAllocator, "ID3D12CommandAllocator::GetDevice"},
   GetDeviceTarget{Vtables::Interface::D3D12GraphicsCommandList, "ID3D12GraphicsCommandList::GetDevice"},
   GetDeviceTarget{Vtables::Interface::D3D12DescriptorHeap, "ID3D12DescriptorHeap::GetDevice"},
   GetDeviceTarget{Vtables::Interface::D3D12Fence, "ID3D12Fence::GetDevice"},
};
std::array<GetDeviceFunction, GET_DEVICE_TARGETS.size()> original_get_device = {};
thread_local uint32_t get_device_depth = 0;

const std::array DEVICE_VERSIONS = {
   Vtables::GetVersion<ID3D12Device>("ID3D12Device", LumaMethodCount_ID3D12Device),
   Vtables::GetVersion<ID3D12Device1>("ID3D12Device1", LumaMethodCount_ID3D12Device1),
   Vtables::GetVersion<ID3D12Device2>("ID3D12Device2", LumaMethodCount_ID3D12Device2),
   Vtables::GetVersion<ID3D12Device3>("ID3D12Device3", LumaMethodCount_ID3D12Device3),
   Vtables::GetVersion<ID3D12Device4>("ID3D12Device4", LumaMethodCount_ID3D12Device4),
   Vtables::GetVersion<ID3D12Device5>("ID3D12Device5", LumaMethodCount_ID3D12Device5),
   Vtables::GetVersion<ID3D12Device6>("ID3D12Device6", LumaMethodCount_ID3D12Device6),
   Vtables::GetVersion<ID3D12Device7>("ID3D12Device7", LumaMethodCount_ID3D12Device7),
   Vtables::GetVersion<ID3D12Device8>("ID3D12Device8", LumaMethodCount_ID3D12Device8),
   Vtables::GetVersion<ID3D12Device9>("ID3D12Device9", LumaMethodCount_ID3D12Device9),
   Vtables::GetVersion<ID3D12Device10>("ID3D12Device10", LumaMethodCount_ID3D12Device10),
   Vtables::GetVersion<ID3D12Device11>("ID3D12Device11", LumaMethodCount_ID3D12Device11),
   Vtables::GetVersion<ID3D12Device12>("ID3D12Device12", LumaMethodCount_ID3D12Device12),
   Vtables::GetVersion<ID3D12Device13>("ID3D12Device13", LumaMethodCount_ID3D12Device13),
   Vtables::GetVersion<ID3D12Device14>("ID3D12Device14", LumaMethodCount_ID3D12Device14),
};
const std::array QUEUE_VERSIONS = {
   Vtables::GetVersion<ID3D12CommandQueue>("ID3D12CommandQueue", LumaMethodCount_ID3D12CommandQueue),
};
const std::array ALLOCATOR_VERSIONS = {
   Vtables::GetVersion<ID3D12CommandAllocator>("ID3D12CommandAllocator", LumaMethodCount_ID3D12CommandAllocator),
};
const std::array COMMAND_LIST_VERSIONS = {
   Vtables::GetVersion<ID3D12GraphicsCommandList>("ID3D12GraphicsCommandList", LumaMethodCount_ID3D12GraphicsCommandList),
   Vtables::GetVersion<ID3D12GraphicsCommandList1>("ID3D12GraphicsCommandList1", LumaMethodCount_ID3D12GraphicsCommandList1),
   Vtables::GetVersion<ID3D12GraphicsCommandList2>("ID3D12GraphicsCommandList2", LumaMethodCount_ID3D12GraphicsCommandList2),
   Vtables::GetVersion<ID3D12GraphicsCommandList3>("ID3D12GraphicsCommandList3", LumaMethodCount_ID3D12GraphicsCommandList3),
   Vtables::GetVersion<ID3D12GraphicsCommandList4>("ID3D12GraphicsCommandList4", LumaMethodCount_ID3D12GraphicsCommandList4),
   Vtables::GetVersion<ID3D12GraphicsCommandList5>("ID3D12GraphicsCommandList5", LumaMethodCount_ID3D12GraphicsCommandList5),
   Vtables::GetVersion<ID3D12GraphicsCommandList6>("ID3D12GraphicsCommandList6", LumaMethodCount_ID3D12GraphicsCommandList6),
   Vtables::GetVersion<ID3D12GraphicsCommandList7>("ID3D12GraphicsCommandList7", LumaMethodCount_ID3D12GraphicsCommandList7),
   Vtables::GetVersion<ID3D12GraphicsCommandList8>("ID3D12GraphicsCommandList8", LumaMethodCount_ID3D12GraphicsCommandList8),
   Vtables::GetVersion<ID3D12GraphicsCommandList9>("ID3D12GraphicsCommandList9", LumaMethodCount_ID3D12GraphicsCommandList9),
   Vtables::GetVersion<ID3D12GraphicsCommandList10>("ID3D12GraphicsCommandList10", LumaMethodCount_ID3D12GraphicsCommandList10),
};
const std::array DESCRIPTOR_HEAP_VERSIONS = {
   Vtables::GetVersion<ID3D12DescriptorHeap>("ID3D12DescriptorHeap", LumaMethodCount_ID3D12DescriptorHeap),
};
const std::array PIPELINE_LIBRARY_VERSIONS = {
   Vtables::GetVersion<ID3D12PipelineLibrary>("ID3D12PipelineLibrary", LumaMethodCount_ID3D12PipelineLibrary),
   Vtables::GetVersion<ID3D12PipelineLibrary1>("ID3D12PipelineLibrary1", LumaMethodCount_ID3D12PipelineLibrary1),
};
const std::array FENCE_VERSIONS = {
   Vtables::GetVersion<ID3D12Fence>("ID3D12Fence", LumaMethodCount_ID3D12Fence),
   Vtables::GetVersion<ID3D12Fence1>("ID3D12Fence1", LumaMethodCount_ID3D12Fence1),
};

template <typename T, size_t N>
void SetVtable(Vtables::Interface interface_type, const ComPtr<T>& object, const std::array<Vtables::InterfaceVersion, N>& versions, HRESULT result)
{
   if (SUCCEEDED(result) && object)
      Vtables::Set(interface_type, object.Get(), versions);
   else
      Log::Error("D3D12: failed to create a temporary {} to read its vtable (0x{:08X})", versions.front().name, uint32_t(result));
}

// From temporary objects on the game's device, so vtables come from the same implementation
void ResolveVtables(ID3D12Device* device)
{
   Scope::Guard scope;
   Vtables::Set(Vtables::Interface::D3D12Device, device, DEVICE_VERSIONS);

   const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
   ComPtr<ID3D12CommandQueue> queue;
   SetVtable(Vtables::Interface::D3D12CommandQueue, queue, QUEUE_VERSIONS, device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));

   ComPtr<ID3D12CommandAllocator> allocator;
   HRESULT result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
   SetVtable(Vtables::Interface::D3D12CommandAllocator, allocator, ALLOCATOR_VERSIONS, result);
   if (allocator)
   {
      ComPtr<ID3D12GraphicsCommandList> command_list;
      result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&command_list));
      SetVtable(Vtables::Interface::D3D12GraphicsCommandList, command_list, COMMAND_LIST_VERSIONS, result);
      if (command_list)
         command_list->Close();
   }

   const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = 1, .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
   ComPtr<ID3D12DescriptorHeap> heap;
   SetVtable(Vtables::Interface::D3D12DescriptorHeap, heap, DESCRIPTOR_HEAP_VERSIONS, device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)));

   ComPtr<ID3D12Fence> fence;
   SetVtable(Vtables::Interface::D3D12Fence, fence, FENCE_VERSIONS, device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

   // An empty library: what the game's pipeline library hooks need
   if (ComPtr<ID3D12Device1> device1; SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device1))))
   {
      ComPtr<ID3D12PipelineLibrary> library;
      SetVtable(Vtables::Interface::D3D12PipelineLibrary, library, PIPELINE_LIBRARY_VERSIONS, device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library)));
   }

   InstallPending();
   LogSummary();
}

void OnDeviceReleased(IUnknown* device)
{
   Handlers::OnDeviceDestroyed(static_cast<ID3D12Device*>(device));
}

void OnDeviceCreated(HRESULT result, REFIID riid, void** device_pointer)
{
   // Callers that only check support pass no device
   if (FAILED(result) || !device_pointer || !*device_pointer || Scope::IsActive())
      return;
   ComPtr<ID3D12Device> device;
   if (FAILED(static_cast<IUnknown*>(*device_pointer)->QueryInterface(IID_PPV_ARGS(&device))))
      return;
   if (!vtables_resolved.exchange(true))
      ResolveVtables(device.Get());
   else
      Vtables::Set(Vtables::Interface::D3D12Device, device.Get(), DEVICE_VERSIONS);

   // D3D12CreateDevice returns the adapter's existing device while one is alive
   bool created = false;
   if (!DeviceProxy::Wrap(device_pointer, riid, device.Get(), &OnDeviceReleased, created))
   {
      Log::Warning("D3D12: device {} was returned as an interface proxies don't cover, Luma ignores it", static_cast<void*>(device.Get()));
      return;
   }
   if (created)
   {
      Scope::Guard scope;
      Handlers::PostCreateDevice(device.Get());
   }
}

HRESULT WINAPI CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID riid, void** device)
{
   DepthGuard guard(create_depth);
   if (guard.IsOutermost() && !Scope::IsActive())
      Handlers::PreCreateDevice();
   HRESULT result;
   {
      // What the runtime does inside this isn't the game's
      Scope::Guard scope;
      result = original_create_device(adapter, minimum_feature_level, riid, device);
   }
   if (guard.IsOutermost())
      OnDeviceCreated(result, riid, device);
   return result;
}

template <size_t Index>
HRESULT STDMETHODCALLTYPE GetDevice(ID3D12DeviceChild* child, REFIID riid, void** device)
{
   DepthGuard guard(get_device_depth);
   const HRESULT result = original_get_device[Index](child, riid, device);
   if (SUCCEEDED(result) && guard.IsOutermost() && !Scope::IsActive())
      DeviceProxy::Replace(device);
   return result;
}

using ExecuteCommandListsFunction = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);
ExecuteCommandListsFunction original_execute_command_lists = nullptr;

// Luma's own submissions too: they may use objects released after use
void STDMETHODCALLTYPE ExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
   original_execute_command_lists(queue, count, lists);
   Handlers::PostExecuteCommandLists(queue, count, lists);
}

template <size_t... Indices>
constexpr std::array<void*, sizeof...(Indices)> GetDeviceDetours(std::index_sequence<Indices...>)
{
   return {reinterpret_cast<void*>(&GetDevice<Indices>)...};
}
} // namespace

void InstallD3D12(const D3D12::Functions& functions)
{
   // Devices from ID3D12DeviceFactory bypass this export, and aren't seen yet
   const std::array hooks = {
      Hook{ExportTarget{L"d3d12.dll", "D3D12CreateDevice"}, reinterpret_cast<void*>(&CreateDevice), reinterpret_cast<void**>(&original_create_device)},
   };
   Declare(hooks);

   const auto detours = GetDeviceDetours(std::make_index_sequence<GET_DEVICE_TARGETS.size()>());
   std::array<Hook, GET_DEVICE_TARGETS.size()> get_device_hooks;
   for (size_t i = 0; i < GET_DEVICE_TARGETS.size(); i++)
   {
      get_device_hooks[i] = Hook{VtableTarget{GET_DEVICE_TARGETS[i].vtable, LumaSlot_ID3D12DeviceChild_GetDevice, GET_DEVICE_TARGETS[i].name}, detours[i],
         reinterpret_cast<void**>(&original_get_device[i]), true};
   }
   Declare(get_device_hooks);

   // Always: the release-after-use of Luma's objects needs it (Hooks/D3D12Lifetime), and it runs once per submission
   const std::array queue_hooks = {
      Hook{VtableTarget{Vtables::Interface::D3D12CommandQueue, LumaSlot_ID3D12CommandQueue_ExecuteCommandLists, "ID3D12CommandQueue::ExecuteCommandLists"},
         reinterpret_cast<void*>(&ExecuteCommandLists), reinterpret_cast<void**>(&original_execute_command_lists)},
   };
   Declare(queue_hooks);

   InstallD3D12Device(functions);
   InstallD3D12CommandList(functions);
}
} // namespace Hooks
} // namespace Luma
