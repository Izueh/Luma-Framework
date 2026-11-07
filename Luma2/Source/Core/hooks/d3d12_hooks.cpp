#include "hooks/hooks.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

#include <d3d12.h>
#include <wrl/client.h>

#include "handlers/d3d12_handlers.hpp"
#include "hooks/device_proxy.hpp"
#include "hooks/scope.hpp"
#include "hooks/slots.h"
#include "utils/depth_guard.hpp"
#include "utils/log.hpp"
#include "validation/d3d12_validation.hpp"

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
   LUMA_INTERFACE_VERSION(ID3D12Device),
   LUMA_INTERFACE_VERSION(ID3D12Device1),
   LUMA_INTERFACE_VERSION(ID3D12Device2),
   LUMA_INTERFACE_VERSION(ID3D12Device3),
   LUMA_INTERFACE_VERSION(ID3D12Device4),
   LUMA_INTERFACE_VERSION(ID3D12Device5),
   LUMA_INTERFACE_VERSION(ID3D12Device6),
   LUMA_INTERFACE_VERSION(ID3D12Device7),
   LUMA_INTERFACE_VERSION(ID3D12Device8),
   LUMA_INTERFACE_VERSION(ID3D12Device9),
   LUMA_INTERFACE_VERSION(ID3D12Device10),
   LUMA_INTERFACE_VERSION(ID3D12Device11),
   LUMA_INTERFACE_VERSION(ID3D12Device12),
   LUMA_INTERFACE_VERSION(ID3D12Device13),
   LUMA_INTERFACE_VERSION(ID3D12Device14),
};
const std::array QUEUE_VERSIONS = {
   LUMA_INTERFACE_VERSION(ID3D12CommandQueue),
};
const std::array ALLOCATOR_VERSIONS = {
   LUMA_INTERFACE_VERSION(ID3D12CommandAllocator),
};
const std::array COMMAND_LIST_VERSIONS = {
   LUMA_INTERFACE_VERSION(ID3D12GraphicsCommandList),
   LUMA_INTERFACE_VERSION(ID3D12GraphicsCommandList1),
   LUMA_INTERFACE_VERSION(ID3D12GraphicsCommandList2),
   LUMA_INTERFACE_VERSION(ID3D12GraphicsCommandList3),
   LUMA_INTERFACE_VERSION(ID3D12GraphicsCommandList4),
   LUMA_INTERFACE_VERSION(ID3D12GraphicsCommandList5),
   LUMA_INTERFACE_VERSION(ID3D12GraphicsCommandList6),
   LUMA_INTERFACE_VERSION(ID3D12GraphicsCommandList7),
   LUMA_INTERFACE_VERSION(ID3D12GraphicsCommandList8),
   LUMA_INTERFACE_VERSION(ID3D12GraphicsCommandList9),
   LUMA_INTERFACE_VERSION(ID3D12GraphicsCommandList10),
};
const std::array DESCRIPTOR_HEAP_VERSIONS = {
   LUMA_INTERFACE_VERSION(ID3D12DescriptorHeap),
};
const std::array FENCE_VERSIONS = {
   LUMA_INTERFACE_VERSION(ID3D12Fence),
   LUMA_INTERFACE_VERSION(ID3D12Fence1),
};

template <typename T, size_t N>
void SetVtable(Vtables::Interface interface_type, const ComPtr<T>& object, const std::array<Vtables::InterfaceVersion, N>& versions, HRESULT result)
{
   if (SUCCEEDED(result) && object)
      Vtables::Set(interface_type, object.Get(), versions);
   else
      Utils::Log::Error("D3D12: failed to create a temporary {} to read its vtable (0x{:08X})", versions.front().name, uint32_t(result));
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

   InstallPending();
   LogSummary();
}

void OnDeviceReleased(IUnknown* device)
{
   Handlers::OnDeviceReleased(static_cast<ID3D12Device*>(device));
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
      Utils::Log::Warning("D3D12: device {} was returned as an interface proxies don't cover, Luma ignores it", static_cast<void*>(device.Get()));
      return;
   }
   if (created)
      Handlers::OnDeviceCreated(device.Get());
}

HRESULT WINAPI CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID riid, void** device)
{
   Utils::DepthGuard guard(create_depth);
   if (guard.IsOutermost() && !Scope::IsActive())
      Validation::D3D12::BeforeCreateDevice();
   const HRESULT result = original_create_device(adapter, minimum_feature_level, riid, device);
   if (guard.IsOutermost())
      OnDeviceCreated(result, riid, device);
   return result;
}

template <size_t Index>
HRESULT STDMETHODCALLTYPE GetDevice(ID3D12DeviceChild* child, REFIID riid, void** device)
{
   Utils::DepthGuard guard(get_device_depth);
   const HRESULT result = original_get_device[Index](child, riid, device);
   if (SUCCEEDED(result) && guard.IsOutermost() && !Scope::IsActive())
      DeviceProxy::Replace(device);
   return result;
}

template <size_t... Indices>
constexpr std::array<void*, sizeof...(Indices)> GetDeviceDetours(std::index_sequence<Indices...>)
{
   return {reinterpret_cast<void*>(&GetDevice<Indices>)...};
}
} // namespace

void InstallD3D12()
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
}
} // namespace Hooks
} // namespace Luma
