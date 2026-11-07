#include "ui/d3d12_renderer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>

#include "utils/log.hpp"

using Microsoft::WRL::ComPtr;

// ImGui's DX12 backend calls CreateDXGIFactory1, but Luma can't link dxgi.lib: it is the dxgi.dll proxy. When nothing else
// defines the function, the linker uses this one.
extern "C" HRESULT WINAPI LumaCreateDXGIFactory1(REFIID riid, void** factory)
{
   static const auto function = [] {
      wchar_t path[MAX_PATH] = {};
      const UINT length = GetSystemDirectoryW(path, MAX_PATH);
      HMODULE dxgi = length ? LoadLibraryW((std::wstring(path, length) + L"\\dxgi.dll").c_str()) : nullptr;
      return dxgi ? reinterpret_cast<HRESULT(WINAPI*)(REFIID, void**)>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
   }();
   return function ? function(riid, factory) : E_FAIL;
}
#pragma comment(linker, "/ALTERNATENAME:CreateDXGIFactory1=LumaCreateDXGIFactory1")

namespace Luma {
namespace UI {
namespace D3D12 {
namespace {
constexpr uint32_t SRV_DESCRIPTOR_COUNT = 64;

struct Frame
{
   ComPtr<ID3D12CommandAllocator> allocator;
   uint64_t fence_value = 0;
};

struct Renderer
{
   ComPtr<ID3D12Device> device; // Real
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   uint32_t buffer_count = 0;

   ComPtr<ID3D12DescriptorHeap> srv_heap;
   uint32_t srv_increment = 0;
   std::vector<bool> srv_used;
   // Read when recorded, so one is enough for every frame
   ComPtr<ID3D12DescriptorHeap> rtv_heap;

   std::vector<Frame> frames;
   ComPtr<ID3D12GraphicsCommandList> list;
   ComPtr<ID3D12Fence> fence;
   uint64_t fence_value = 0;
   HANDLE event = nullptr;
};

Renderer* renderer = nullptr;

void Wait(uint64_t value)
{
   if (renderer->fence->GetCompletedValue() >= value)
      return;
   if (SUCCEEDED(renderer->fence->SetEventOnCompletion(value, renderer->event)) && WaitForSingleObject(renderer->event, 2000) == WAIT_TIMEOUT)
      Utils::Log::Warning("UI: the overlay's D3D12 work hasn't finished after 2s");
}

void AllocateSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
{
   for (uint32_t i = 0; i < SRV_DESCRIPTOR_COUNT; i++)
   {
      if (renderer->srv_used[i])
         continue;
      renderer->srv_used[i] = true;
      *cpu = renderer->srv_heap->GetCPUDescriptorHandleForHeapStart();
      *gpu = renderer->srv_heap->GetGPUDescriptorHandleForHeapStart();
      cpu->ptr += SIZE_T(i) * renderer->srv_increment;
      gpu->ptr += uint64_t(i) * renderer->srv_increment;
      return;
   }
   Utils::Log::Error("UI: the overlay's {} texture descriptors are all used", SRV_DESCRIPTOR_COUNT);
   *cpu = {};
   *gpu = {};
}

void FreeSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
{
   const SIZE_T start = renderer->srv_heap->GetCPUDescriptorHandleForHeapStart().ptr;
   if (cpu.ptr >= start)
   {
      const SIZE_T index = (cpu.ptr - start) / renderer->srv_increment;
      if (index < SRV_DESCRIPTOR_COUNT)
         renderer->srv_used[index] = false;
   }
}

bool Create(ID3D12CommandQueue* queue, DXGI_FORMAT format, uint32_t buffer_count)
{
   auto created = std::make_unique<Renderer>();
   Renderer& r = *created;
   if (FAILED(queue->GetDevice(IID_PPV_ARGS(&r.device))))
      return false;
   r.format = format;
   r.buffer_count = buffer_count;

   const D3D12_DESCRIPTOR_HEAP_DESC srv_desc = {
      .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
      .NumDescriptors = SRV_DESCRIPTOR_COUNT,
      .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
   };
   const D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .NumDescriptors = 1};
   if (FAILED(r.device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&r.srv_heap))) || FAILED(r.device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&r.rtv_heap))))
      return false;
   r.srv_increment = r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   r.srv_used.assign(SRV_DESCRIPTOR_COUNT, false);

   r.frames.resize(r.buffer_count);
   for (Frame& frame : r.frames)
   {
      if (FAILED(r.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator))))
         return false;
   }
   if (FAILED(r.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, r.frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&r.list))) ||
      FAILED(r.list->Close()) || FAILED(r.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r.fence))))
      return false;
   r.srv_heap->SetName(L"Luma overlay textures");
   r.list->SetName(L"Luma overlay");
   r.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

   renderer = created.release();
   ImGui_ImplDX12_InitInfo info;
   info.Device = r.device.Get();
   info.CommandQueue = queue;
   info.NumFramesInFlight = int(r.buffer_count);
   info.RTVFormat = r.format;
   info.DSVFormat = DXGI_FORMAT_UNKNOWN;
   info.SrvDescriptorHeap = r.srv_heap.Get();
   info.SrvDescriptorAllocFn = &AllocateSrv;
   info.SrvDescriptorFreeFn = &FreeSrv;
   if (!ImGui_ImplDX12_Init(&info))
   {
      if (renderer->event)
         CloseHandle(renderer->event);
      delete renderer;
      renderer = nullptr;
      return false;
   }
   return true;
}
} // namespace

bool NeedsRecreation(DXGI_FORMAT format, uint32_t buffer_count)
{
   return renderer && (renderer->format != format || renderer->buffer_count != buffer_count);
}

bool NewFrame(ID3D12CommandQueue* queue, DXGI_FORMAT format, uint32_t buffer_count)
{
   if (!renderer && !Create(queue, format, buffer_count))
   {
      Utils::Log::Error("UI: ImGui's D3D12 renderer failed to initialize on queue {}", static_cast<void*>(queue));
      return false;
   }
   ImGui_ImplDX12_NewFrame();
   return true;
}

void Render(ID3D12CommandQueue* queue, IDXGISwapChain* swapchain, ImDrawData* data)
{
   ComPtr<IDXGISwapChain3> swapchain3;
   if (!renderer || FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3))))
      return;
   const uint32_t index = swapchain3->GetCurrentBackBufferIndex();
   ComPtr<ID3D12Resource> buffer;
   if (FAILED(swapchain3->GetBuffer(index, IID_PPV_ARGS(&buffer))))
      return;

   Frame& frame = renderer->frames[index % renderer->frames.size()];
   Wait(frame.fence_value);
   frame.allocator->Reset();
   ID3D12GraphicsCommandList* list = renderer->list.Get();
   list->Reset(frame.allocator.Get(), nullptr);

   D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
      .Transition = {
         .pResource = buffer.Get(),
         .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
         .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
         .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
      }};
   list->ResourceBarrier(1, &barrier);
   const D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer->rtv_heap->GetCPUDescriptorHandleForHeapStart();
   renderer->device->CreateRenderTargetView(buffer.Get(), nullptr, rtv);
   list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
   ID3D12DescriptorHeap* heaps[] = {renderer->srv_heap.Get()};
   list->SetDescriptorHeaps(1, heaps);
   ImGui_ImplDX12_RenderDrawData(data, list);
   std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
   list->ResourceBarrier(1, &barrier);
   list->Close();

   ID3D12CommandList* lists[] = {list};
   queue->ExecuteCommandLists(1, lists);
   queue->Signal(renderer->fence.Get(), ++renderer->fence_value);
   frame.fence_value = renderer->fence_value;
}

void Shutdown()
{
   if (!renderer)
      return;
   Wait(renderer->fence_value);
   ImGui_ImplDX12_Shutdown();
   if (renderer->event)
      CloseHandle(renderer->event);
   delete renderer;
   renderer = nullptr;
}
} // namespace D3D12
} // namespace UI
} // namespace Luma
