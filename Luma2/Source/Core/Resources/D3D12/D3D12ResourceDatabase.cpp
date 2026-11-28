#include "Resources/D3D12/D3D12ResourceDatabase.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <wrl/client.h>

#include <DDSTextureLoader12.h>

#include "Settings/SettingsRegistry.hpp"
#include "FrameState/FrameState.hpp"
#include "Game/GameRegistry.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Hooks/HookScope.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Resources/D3D12/D3D12HeapTail.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/TextureLoader.hpp"
#include "Shaders/D3D12/D3D12Database.hpp"
#include "Shaders/D3D12/D3D12RootSignatures.hpp"
#include "Utils/Debug.hpp"
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Database {
namespace D3D12 {
namespace {
constexpr uint64_t CHUNK_SIZE = 256 * 1024;
constexpr uint64_t CBV_ALIGNMENT = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
// Each layout has its own range of the tail's fixed section
constexpr uint32_t MAX_TABLES = HeapTail::D3D12::FIXED_SIZE / Bindings::D3D12_TEXTURE_REGISTERS;

// Registered layouts never change: read without the lock below `table_count`
std::mutex tables_mutex; // Never held with another
std::array<TableLayout, MAX_TABLES> tables;
std::atomic<uint32_t> table_count = 0;
std::atomic<uint32_t> texture_count = 0;

struct Texture
{
   ComPtr<ID3D12Resource> resource;
   D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
};

// Part of an upload heap buffer, mapped for its life. Lists that wrote into it hold the buffer until they've executed: a full chunk
// is dropped and the next one created.
struct Chunk
{
   Hooks::Lifetime::D3D12::Object object; // The buffer
   std::byte* mapped = nullptr;
   D3D12_GPU_VIRTUAL_ADDRESS address = 0;
   uint64_t used = 0;
};

struct RuntimeSlot
{
   ComPtr<ID3D12Resource> tagged; // Cleared at OnPresent
   Hooks::Lifetime::D3D12::Object managed;
   D3D12_RESOURCE_DESC managed_desc = {};
   D3D12_RESOURCE_STATES managed_state = D3D12_RESOURCE_STATE_COMMON;
   D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
   D3D12_CPU_DESCRIPTOR_HANDLE srv = {};
   D3D12_CPU_DESCRIPTOR_HANDLE uav = {};
};

constexpr uint32_t MAX_RUNTIME_SLOTS = 64;

// In the device's private data. Its chunk holds the device: released when the game releases it.
struct Store
{
   std::mutex mutex; // Never held with another
   std::unique_ptr<Chunk> chunk;
   bool released = false;
   // Per entry (null for other kinds), made before any list records: read without the lock
   std::vector<Texture> textures;
   // Per entry RuntimeTexture slots and their CPU descriptor heaps
   std::deque<RuntimeSlot> runtime;
   ComPtr<ID3D12DescriptorHeap> rtv_heap;
   ComPtr<ID3D12DescriptorHeap> srv_uav_heap;
   uint32_t rtv_increment = 0;
   uint32_t srv_uav_increment = 0;
   uint32_t next_rtv_slot = 0;
   uint32_t next_srv_uav_slot = 0;
};

std::atomic<uint64_t> ring_bytes = 0;
std::atomic<uint32_t> chunks = 0;

struct RootArg
{
   enum class Kind : uint8_t
   {
      None,
      Table,
      Descriptor,
   };
   Kind kind = Kind::None;
   uint64_t value = 0; // GPU descriptor handle ptr or GPU VA
};

struct List
{
   Store* store = nullptr;
   // The list's device and its state, found once: the entries' contents are written for them
   WriteTarget target;
   bool found = false;
   bool bundle = false; // Inherits its root arguments: the list that runs it binds Luma's
   // Per Luma root parameter (LumaCBuffers::GetRootIndex): what was uploaded for this list, at which address
   std::array<Database::Upload, Shaders::RootSignatures::D3D12::LUMA_PARAMETER_COUNT> uploads;
   std::array<D3D12_GPU_VIRTUAL_ADDRESS, Shaders::RootSignatures::D3D12::LUMA_PARAMETER_COUNT> addresses = {};
   std::vector<std::byte> written;
   // [compute]: the first of Luma's parameters in the bound signature (UINT32_MAX: not extended or none) and what's set there
   std::array<uint32_t, 2> luma_parameter = {UINT32_MAX, UINT32_MAX};
   std::array<std::array<D3D12_GPU_VIRTUAL_ADDRESS, Shaders::RootSignatures::D3D12::LUMA_PARAMETER_COUNT>, 2> set = {};
   // [compute]: whether the bound signature has Luma's texture table, and the table set there
   std::array<bool, 2> luma_table = {};
   std::array<UINT64, 2> table_set = {};
   ID3D12DescriptorHeap* heap = nullptr; // The CBV/SRV/UAV heap bound, when it has Luma's tail
   // Full bound state for bound-resource lookup and restoration after SR / custom passes
   std::array<ID3D12DescriptorHeap*, 2> bound_heaps = {};
   uint32_t bound_heap_count = 0;
   std::array<ID3D12RootSignature*, 2> root_signatures = {};
   std::array<const Shaders::Database::D3D12::RootLayout*, 2> root_layouts = {};
   std::array<std::vector<RootArg>, 2> root_args;
   ID3D12PipelineState* pipeline = nullptr;
   std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs = {};
   uint32_t rtv_count = 0;
   bool rtv_single_range = false;
   bool has_dsv = false;
   D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
   // The last table of views written into the ring (BindTableViews): reused while the views, the layout and the heap stay
   TableViews views = {};
   uint32_t views_table = NO_TABLE;
   ID3D12DescriptorHeap* views_heap = nullptr;
   UINT64 views_gpu = 0;
};

std::atomic<uint32_t> view_tables = 0;

bool Same(const TableViews& a, const TableViews& b)
{
   for (size_t i = 0; i < a.size(); i++)
   {
      if (a[i].resource != b[i].resource || (a[i].resource && std::memcmp(&a[i].desc, &b[i].desc, sizeof(a[i].desc)) != 0))
         return false;
   }
   return true;
}

List* GetList(ID3D12GraphicsCommandList* list)
{
   List* state = Hooks::Lifetime::GetOrCreateStateCached<List>(list);
   if (!state || state->found)
      return state;
   state->found = true;
   state->bundle = list->GetType() == D3D12_COMMAND_LIST_TYPE_BUNDLE;
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
      return state;
   state->store = Hooks::Lifetime::GetState<Store>(device.Get());
   state->target = {device.Get(), FrameState::Find(device.Get()), Game::GetGameDeviceData(device.Get())};
   return state;
}

// With the store locked
bool NewChunk(ID3D12Device* device, Store& store)
{
   const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_UPLOAD};
   const D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, CHUNK_SIZE, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
   ComPtr<ID3D12Resource> buffer;
   void* mapped = nullptr;
   Hooks::Scope::Guard scope;
   if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer))) ||
      FAILED(buffer->Map(0, nullptr, &mapped)))
   {
      Log::Error("D3D12: Luma's cbuffer memory ({} KB) couldn't be created", CHUNK_SIZE / 1024);
      return false;
   }
   auto chunk = std::make_unique<Chunk>();
   chunk->mapped = static_cast<std::byte*>(mapped);
   chunk->address = buffer->GetGPUVirtualAddress();
   Hooks::Lifetime::D3D12::Replace(chunk->object, std::move(buffer));
   store.chunk = std::move(chunk);
   chunks++;
   return true;
}

// `write` fills the `size` bytes at the address returned, 0 if there's no memory. The list holds the chunk until it executed.
template <typename Write>
D3D12_GPU_VIRTUAL_ADDRESS UploadBytes(ID3D12GraphicsCommandList* list, const List& state, uint32_t size, Write&& write)
{
   Store& store = *state.store;
   const uint64_t aligned = (uint64_t(size) + CBV_ALIGNMENT - 1) / CBV_ALIGNMENT * CBV_ALIGNMENT;
   std::scoped_lock lock(store.mutex);
   if (store.released)
      return 0;
   if ((!store.chunk || store.chunk->used + aligned > CHUNK_SIZE) && !NewChunk(static_cast<ID3D12Device*>(state.target.device), store))
      return 0;
   Chunk& chunk = *store.chunk;
   if (!Hooks::Lifetime::D3D12::Reference(list, chunk.object))
      return 0;
   write(std::span(chunk.mapped + chunk.used, size));
   const D3D12_GPU_VIRTUAL_ADDRESS address = chunk.address + chunk.used;
   chunk.used += aligned;
   ring_bytes += aligned;
   return address;
}

void UploadEntry(ID3D12GraphicsCommandList* list, List& state, uint32_t index, const Entry& entry, const CustomData& custom)
{
   UploadIfDirty(entry, state.uploads[index], state.written, custom, state.target, [&](auto&& write) {
      state.addresses[index] = UploadBytes(list, state, entry.size, write);
      return state.addresses[index] != 0;
   });
}

void Bind(ID3D12GraphicsCommandList* list, List& state, bool compute, uint32_t index)
{
   const uint32_t first = state.luma_parameter[compute];
   const D3D12_GPU_VIRTUAL_ADDRESS address = state.addresses[index];
   if (first == UINT32_MAX || !address || state.set[compute][index] == address)
      return;
   Hooks::Scope::Guard scope;
   if (compute)
      list->SetComputeRootConstantBufferView(first + index, address);
   else
      list->SetGraphicsRootConstantBufferView(first + index, address);
   state.set[compute][index] = address;
}

// Copies `subresources` into `texture` (in COPY_DEST, as the DDS loader creates it) on a copy queue of Luma's, and waits. A texture
// used on a copy queue decays to COMMON, from which the game's queues promote it to a shader resource implicitly.
HRESULT Upload(ID3D12Device* device, ID3D12Resource* texture, std::span<const D3D12_SUBRESOURCE_DATA> subresources)
{
   const D3D12_RESOURCE_DESC desc = texture->GetDesc();
   const UINT count = UINT(subresources.size());
   std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(count);
   std::vector<UINT> rows(count);
   std::vector<UINT64> row_sizes(count);
   UINT64 total = 0;
   device->GetCopyableFootprints(&desc, 0, count, 0, footprints.data(), rows.data(), row_sizes.data(), &total);

   const D3D12_HEAP_PROPERTIES upload_heap = {D3D12_HEAP_TYPE_UPLOAD};
   const D3D12_RESOURCE_DESC buffer_desc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, total, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
   ComPtr<ID3D12Resource> buffer;
   HRESULT result = device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer));
   std::byte* mapped = nullptr;
   if (SUCCEEDED(result))
      result = buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
   if (FAILED(result))
      return result;
   for (UINT i = 0; i < count; i++)
   {
      const D3D12_SUBRESOURCE_FOOTPRINT& footprint = footprints[i].Footprint;
      const D3D12_SUBRESOURCE_DATA& source = subresources[i];
      for (UINT z = 0; z < footprint.Depth; z++)
      {
         for (UINT row = 0; row < rows[i]; row++)
         {
            std::byte* destination = mapped + footprints[i].Offset + (uint64_t(z) * rows[i] + row) * footprint.RowPitch;
            const auto* from = static_cast<const std::byte*>(source.pData) + z * source.SlicePitch + row * source.RowPitch;
            std::memcpy(destination, from, size_t(row_sizes[i]));
         }
      }
   }
   buffer->Unmap(0, nullptr);

   const D3D12_COMMAND_QUEUE_DESC queue_desc = {D3D12_COMMAND_LIST_TYPE_COPY};
   ComPtr<ID3D12CommandQueue> queue;
   ComPtr<ID3D12CommandAllocator> allocator;
   ComPtr<ID3D12GraphicsCommandList> list;
   ComPtr<ID3D12Fence> fence;
   if (FAILED(result = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) ||
      FAILED(result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&allocator))) ||
      FAILED(result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, allocator.Get(), nullptr, IID_PPV_ARGS(&list))) ||
      FAILED(result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
      return result;
   for (UINT i = 0; i < count; i++)
   {
      D3D12_TEXTURE_COPY_LOCATION destination = {texture, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
      destination.SubresourceIndex = i;
      D3D12_TEXTURE_COPY_LOCATION source = {buffer.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
      source.PlacedFootprint = footprints[i];
      list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
   }
   if (FAILED(result = list->Close()))
      return result;
   ID3D12CommandList* lists[] = {list.Get()};
   queue->ExecuteCommandLists(1, lists);
   if (FAILED(result = queue->Signal(fence.Get(), 1)))
      return result;
   const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
   if (!event)
      return HRESULT_FROM_WIN32(GetLastError());
   result = fence->SetEventOnCompletion(1, event);
   if (SUCCEEDED(result))
      WaitForSingleObject(event, INFINITE);
   CloseHandle(event);
   return result;
}

// A .dds file as a texture in a default heap and the view that reads all of it. In the device's creation: the files are few and
// small (a noise texture), read once per device.
Texture LoadTexture(ID3D12Device* device, uint32_t index)
{
   const Entry& entry = Get(index);
   const std::filesystem::path path = Textures::Find(index);
   Texture texture;
   std::unique_ptr<uint8_t[]> data;
   std::vector<D3D12_SUBRESOURCE_DATA> subresources;
   bool cube = false;
   Hooks::Scope::Guard scope;
   HRESULT result = path.empty() ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
                                 : DirectX::LoadDDSTextureFromFile(device, path.c_str(), &texture.resource, data, subresources, 0, nullptr, &cube);
   if (SUCCEEDED(result))
      result = Upload(device, texture.resource.Get(), subresources);
   if (FAILED(result))
   {
      Log::Error("Resources: texture \"{}\" ({}) couldn't be loaded on D3D12 (0x{:08X})", entry.name, entry.file, uint32_t(result));
      return {};
   }
   const D3D12_RESOURCE_DESC desc = texture.resource->GetDesc();
   D3D12_SHADER_RESOURCE_VIEW_DESC& view = texture.view;
   view.Format = desc.Format;
   view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
   switch (desc.Dimension)
   {
   case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
      view.ViewDimension = desc.DepthOrArraySize > 1 ? D3D12_SRV_DIMENSION_TEXTURE1DARRAY : D3D12_SRV_DIMENSION_TEXTURE1D;
      view.Texture1DArray = {0, desc.MipLevels, 0, desc.DepthOrArraySize, 0.f};
      break;
   case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
      view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      view.Texture3D = {0, desc.MipLevels, 0.f};
      break;
   default:
      if (cube)
      {
         view.ViewDimension = desc.DepthOrArraySize > 6 ? D3D12_SRV_DIMENSION_TEXTURECUBEARRAY : D3D12_SRV_DIMENSION_TEXTURECUBE;
         view.TextureCubeArray = {0, desc.MipLevels, 0, UINT(desc.DepthOrArraySize / 6), 0.f};
      }
      else
      {
         view.ViewDimension = desc.DepthOrArraySize > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
         view.Texture2DArray = {0, desc.MipLevels, 0, desc.DepthOrArraySize, 0, 0.f};
      }
      break;
   }
   texture_count++;
   return texture;
}

void WriteTable(ID3D12Device* device, const Store& store, const TableLayout* layout, D3D12_CPU_DESCRIPTOR_HANDLE start, uint32_t increment,
   const TableViews* views = nullptr)
{
   D3D12_SHADER_RESOURCE_VIEW_DESC null_view = {DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
   null_view.Texture2D.MipLevels = 1;
   Hooks::Scope::Guard scope;
   for (uint32_t i = 0; i < Bindings::D3D12_TEXTURE_REGISTERS; i++)
   {
      const D3D12_CPU_DESCRIPTOR_HANDLE handle = {start.ptr + SIZE_T(i) * increment};
      if (const TableView* view = views ? &(*views)[i] : nullptr; view && view->resource)
      {
         device->CreateShaderResourceView(view->resource, view->desc.ViewDimension == D3D12_SRV_DIMENSION_UNKNOWN ? nullptr : &view->desc, handle);
         continue;
      }
      const Texture* texture = layout && (*layout)[i] < store.textures.size() ? &store.textures[(*layout)[i]] : nullptr;
      if (texture && texture->resource)
         device->CreateShaderResourceView(texture->resource.Get(), &texture->view, handle);
      else
         device->CreateShaderResourceView(nullptr, &null_view, handle);
   }
}

// Sets Luma's table parameter to `gpu`, unless it's set there already
void SetTable(ID3D12GraphicsCommandList* list, List& state, bool compute, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
{
   if (state.table_set[compute] == gpu.ptr)
      return;
   const UINT parameter = state.luma_parameter[compute] + Shaders::RootSignatures::D3D12::LUMA_TABLE_PARAMETER;
   Hooks::Scope::Guard scope;
   if (compute)
      list->SetComputeRootDescriptorTable(parameter, gpu);
   else
      list->SetGraphicsRootDescriptorTable(parameter, gpu);
   state.table_set[compute] = gpu.ptr;
}

// The layout's textures are loaded on this device: a texture the device couldn't load runs the game's shaders (D3D11's UseOriginal)
bool HasTextures(const Store& store, const TableLayout& layout)
{
   for (const uint32_t entry : layout)
   {
      if (entry != UINT32_MAX && (entry >= store.textures.size() || !store.textures[entry].resource))
         return false;
   }
   return true;
}

// Luma's texture table at its parameter of the list's signature: false when it can't be (see BindRequested)
bool BindTable(ID3D12GraphicsCommandList* list, List& state, bool compute, uint32_t table)
{
   if (table == NO_TABLE)
      return true;
   if (table >= table_count.load(std::memory_order_acquire) || !state.luma_table[compute] || !state.heap)
      return false;
   const TableLayout& layout = tables[table];
   if (!HasTextures(*state.store, layout))
      return false;
   auto* device = static_cast<ID3D12Device*>(state.target.device);
   const D3D12_GPU_DESCRIPTOR_HANDLE gpu = HeapTail::D3D12::GetRange(state.heap, table * Bindings::D3D12_TEXTURE_REGISTERS, Bindings::D3D12_TEXTURE_REGISTERS, table,
      [&](D3D12_CPU_DESCRIPTOR_HANDLE start, uint32_t increment) { WriteTable(device, *state.store, &layout, start, increment); });
   if (!gpu.ptr)
      return false;
   SetTable(list, state, compute, gpu);
   return true;
}
} // namespace

uint32_t RegisterTable(const TableLayout& layout)
{
   std::scoped_lock lock(tables_mutex);
   const uint32_t count = table_count.load(std::memory_order_relaxed);
   for (uint32_t i = 0; i < count; i++)
   {
      if (tables[i] == layout)
         return i;
   }
   if (count == MAX_TABLES)
   {
      ASSERT_ONCE_MSG(false, "More layouts of Luma's texture table than the heap tail has room for");
      Log::Error("D3D12: more than {} layouts of Luma's texture table: patches with another one run the game's shaders", MAX_TABLES);
      return NO_TABLE;
   }
   tables[count] = layout;
   table_count.store(count + 1, std::memory_order_release);
   return count;
}

void CreateDeviceObjects(ID3D12Device* device)
{
   Store* store = Hooks::Lifetime::SetState<Store>(device);
   if (!store)
      return;
   store->textures.resize(GetCount());
   store->runtime.resize(GetCount());
   for (uint32_t index = 0; index < GetCount(); index++)
   {
      if (Get(index).kind == Kind::Texture)
         store->textures[index] = LoadTexture(device, index);
   }
   Hooks::Scope::Guard scope;
   const D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, MAX_RUNTIME_SLOTS, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
   if (SUCCEEDED(device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&store->rtv_heap))))
      store->rtv_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
   const D3D12_DESCRIPTOR_HEAP_DESC srv_uav_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, MAX_RUNTIME_SLOTS * 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
   if (SUCCEEDED(device->CreateDescriptorHeap(&srv_uav_desc, IID_PPV_ARGS(&store->srv_uav_heap))))
      store->srv_uav_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void ReleaseDeviceObjects(ID3D12Device* device)
{
   if (Store* store = Hooks::Lifetime::GetState<Store>(device))
   {
      std::scoped_lock lock(store->mutex);
      store->chunk.reset();
      store->released = true;
      // The game released the device: no list of it runs any more. The views in the heaps' tails go with the heaps.
      for (Texture& texture : store->textures)
      {
         texture_count -= texture.resource != nullptr;
         texture.resource.Reset();
      }
      for (RuntimeSlot& slot : store->runtime)
      {
         slot.tagged.Reset();
         Hooks::Lifetime::D3D12::Replace(slot.managed, nullptr);
         slot.rtv = {};
         slot.srv = {};
         slot.uav = {};
      }
      store->rtv_heap.Reset();
      store->srv_uav_heap.Reset();
   }
}

void SetRootSignature(ID3D12GraphicsCommandList* list, bool compute, ID3D12RootSignature* root_signature)
{
   List* state = GetList(list);
   if (!state)
      return;
   const Shaders::Database::D3D12::RootLayout* layout = Shaders::Database::D3D12::FindRootLayout(root_signature);
   state->root_signatures[compute] = root_signature;
   state->root_layouts[compute] = layout;
   state->root_args[compute].clear();
   state->luma_parameter[compute] = layout ? layout->luma_parameter : UINT32_MAX;
   state->luma_table[compute] = layout && layout->luma_table;
   state->set[compute] = {};
   state->table_set[compute] = 0;
}

void SetPipelineState(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pipeline)
{
   if (List* state = GetList(list))
      state->pipeline = pipeline;
}

void SetRootTable(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table)
{
   List* state = GetList(list);
   if (!state)
      return;
   auto& args = state->root_args[compute];
   if (index >= args.size())
      args.resize(size_t(index) + 1);
   args[index] = {RootArg::Kind::Table, table.ptr};
}

void SetRootDescriptor(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
   List* state = GetList(list);
   if (!state)
      return;
   auto& args = state->root_args[compute];
   if (index >= args.size())
      args.resize(size_t(index) + 1);
   args[index] = {RootArg::Kind::Descriptor, address};
}

void SetRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil)
{
   List* state = GetList(list);
   if (!state)
      return;
   state->rtv_count = std::min<UINT>(count, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
   state->rtv_single_range = single_range != FALSE;
   state->rtvs = {};
   if (render_targets && state->rtv_count > 0)
   {
      if (state->rtv_single_range)
      {
         const UINT increment = state->target.device
            ? static_cast<ID3D12Device*>(state->target.device)->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
            : 0;
         for (UINT i = 0; i < state->rtv_count; i++)
            state->rtvs[i] = {render_targets[0].ptr + SIZE_T(i) * increment};
      }
      else
      {
         for (UINT i = 0; i < state->rtv_count; i++)
            state->rtvs[i] = render_targets[i];
      }
   }
   state->has_dsv = depth_stencil != nullptr;
   state->dsv = depth_stencil ? *depth_stencil : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

void ForgetList(ID3D12GraphicsCommandList* list)
{
   if (List* state = GetList(list))
   {
      // A list starts empty, as a D3D11 command list boundary
      for (Database::Upload& upload : state->uploads)
         upload.done = false;
      state->addresses = {};
      state->luma_parameter = {UINT32_MAX, UINT32_MAX};
      state->set = {};
      state->luma_table = {};
      state->table_set = {};
      state->heap = nullptr;
      state->bound_heaps = {};
      state->bound_heap_count = 0;
      state->root_signatures = {};
      state->root_layouts = {};
      state->root_args[0].clear();
      state->root_args[1].clear();
      state->pipeline = nullptr;
      state->rtvs = {};
      state->rtv_count = 0;
      state->rtv_single_range = false;
      state->has_dsv = false;
      state->dsv = {};
      // Its ring descriptors are the queues' to free (HeapTail::D3D12::ForgetList)
      state->views_heap = nullptr;
      state->views_gpu = 0;
   }
}

void SetDescriptorHeaps(ID3D12GraphicsCommandList* list, UINT count, ID3D12DescriptorHeap* const* heaps)
{
   List* state = GetList(list);
   if (!state)
      return;
   // Tables set before point into the heaps unbound now
   state->table_set = {};
   state->heap = nullptr;
   state->bound_heaps = {};
   state->bound_heap_count = 0;
   for (UINT i = 0; heaps && i < count; i++)
   {
      if (heaps[i])
      {
         if (state->bound_heap_count < state->bound_heaps.size())
            state->bound_heaps[state->bound_heap_count++] = heaps[i];
         if (HeapTail::D3D12::GetGameSize(heaps[i]))
            state->heap = heaps[i];
      }
   }
}

void RestoreListState(ID3D12GraphicsCommandList* list)
{
   List* state = GetList(list);
   if (!state)
      return;
   Hooks::Scope::Guard scope;
   if (state->bound_heap_count > 0)
      list->SetDescriptorHeaps(state->bound_heap_count, state->bound_heaps.data());
   for (int compute = 0; compute < 2; compute++)
   {
      if (!state->root_signatures[compute])
         continue;
      if (compute)
         list->SetComputeRootSignature(state->root_signatures[compute]);
      else
         list->SetGraphicsRootSignature(state->root_signatures[compute]);
      const Shaders::Database::D3D12::RootLayout* layout = state->root_layouts[compute];
      const auto& args = state->root_args[compute];
      for (UINT i = 0; i < args.size(); i++)
      {
         if (args[i].kind == RootArg::Kind::Table && args[i].value != 0)
         {
            if (compute)
               list->SetComputeRootDescriptorTable(i, {args[i].value});
            else
               list->SetGraphicsRootDescriptorTable(i, {args[i].value});
         }
         else if (args[i].kind == RootArg::Kind::Descriptor && args[i].value != 0 && layout && i < layout->parameters.size())
         {
            const D3D12_ROOT_PARAMETER_TYPE type = layout->parameters[i].type;
            if (type == D3D12_ROOT_PARAMETER_TYPE_CBV)
               compute ? list->SetComputeRootConstantBufferView(i, args[i].value) : list->SetGraphicsRootConstantBufferView(i, args[i].value);
            else if (type == D3D12_ROOT_PARAMETER_TYPE_SRV)
               compute ? list->SetComputeRootShaderResourceView(i, args[i].value) : list->SetGraphicsRootShaderResourceView(i, args[i].value);
            else if (type == D3D12_ROOT_PARAMETER_TYPE_UAV)
               compute ? list->SetComputeRootUnorderedAccessView(i, args[i].value) : list->SetGraphicsRootUnorderedAccessView(i, args[i].value);
         }
      }
   }
   state->set = {};
   state->table_set = {};
   if (state->pipeline)
      list->SetPipelineState(state->pipeline);
   if (state->rtv_count > 0 || state->has_dsv)
      list->OMSetRenderTargets(state->rtv_count, state->rtv_count > 0 ? state->rtvs.data() : nullptr, FALSE, state->has_dsv ? &state->dsv : nullptr);
}

namespace {
bool VisibilityMatches(D3D12_SHADER_VISIBILITY visibility, ShaderStage stage)
{
   if (visibility == D3D12_SHADER_VISIBILITY_ALL)
      return true;
   switch (stage)
   {
   case ShaderStage::Vertex: return visibility == D3D12_SHADER_VISIBILITY_VERTEX;
   case ShaderStage::Hull: return visibility == D3D12_SHADER_VISIBILITY_HULL;
   case ShaderStage::Domain: return visibility == D3D12_SHADER_VISIBILITY_DOMAIN;
   case ShaderStage::Geometry: return visibility == D3D12_SHADER_VISIBILITY_GEOMETRY;
   case ShaderStage::Pixel: return visibility == D3D12_SHADER_VISIBILITY_PIXEL;
   case ShaderStage::Compute: return true;
   }
   return false;
}

ID3D12Resource* ResolveActiveResource(ID3D12Resource* resource)
{
   if (!resource)
      return nullptr;
   if (Mirrors::IsSwapping())
   {
      if (ID3D12Resource* mirror = Mirrors::D3D12::FindMirror(resource))
         return mirror;
   }
   return resource;
}
} // namespace

ID3D12Resource* FindBoundSRV(ID3D12GraphicsCommandList* list, ShaderStage stage, UINT slot)
{
   const List* state = GetList(list);
   const bool compute = stage == ShaderStage::Compute;
   if (!state || !state->root_layouts[compute])
      return nullptr;
   const auto& params = state->root_layouts[compute]->parameters;
   const auto& args = state->root_args[compute];
   for (size_t p = 0; p < params.size() && p < args.size(); p++)
   {
      const auto& param = params[p];
      if (!VisibilityMatches(param.visibility, stage))
         continue;
      if (param.type == D3D12_ROOT_PARAMETER_TYPE_SRV && param.space == 0 && param.shader_register == slot && args[p].kind == RootArg::Kind::Descriptor)
      {
         if (const std::optional<Descriptors::D3D12::ResourceInfo> info = Descriptors::D3D12::FindBuffer(args[p].value))
            return ResolveActiveResource(info->resource);
      }
      if (param.type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE && args[p].kind == RootArg::Kind::Table && args[p].value != 0)
      {
         for (const auto& range : param.ranges)
         {
            if (range.type != D3D12_DESCRIPTOR_RANGE_TYPE_SRV || range.space != 0)
               continue;
            if (slot >= range.base_register && (range.count == UINT_MAX || slot < range.base_register + range.count))
            {
               const uint32_t descriptor_index = range.offset + (slot - range.base_register);
               if (const std::optional<Descriptors::D3D12::Descriptor> desc = Descriptors::D3D12::Read({args[p].value}, descriptor_index);
                  desc && desc->kind == Descriptors::D3D12::Kind::ShaderResource && desc->resource)
                  return ResolveActiveResource(desc->resource);
            }
         }
      }
   }
   return nullptr;
}

ID3D12Resource* FindBoundRTV(ID3D12GraphicsCommandList* list, UINT slot)
{
   const List* state = GetList(list);
   if (!state || slot >= state->rtv_count)
      return nullptr;
   if (const std::optional<Descriptors::D3D12::Descriptor> desc = Descriptors::D3D12::Read(state->rtvs[slot]);
      desc && desc->kind == Descriptors::D3D12::Kind::RenderTarget && desc->resource)
      return ResolveActiveResource(desc->resource);
   return nullptr;
}

ID3D12Resource* FindBoundDSV(ID3D12GraphicsCommandList* list)
{
   const List* state = GetList(list);
   if (!state || !state->has_dsv)
      return nullptr;
   if (const std::optional<Descriptors::D3D12::Descriptor> desc = Descriptors::D3D12::Read(state->dsv);
      desc && desc->kind == Descriptors::D3D12::Kind::DepthStencil && desc->resource)
      return ResolveActiveResource(desc->resource);
   return nullptr;
}

std::optional<BoundCBuffer> FindBoundCBuffer(ID3D12GraphicsCommandList* list, ShaderStage stage, UINT slot)
{
   const List* state = GetList(list);
   const bool compute = stage == ShaderStage::Compute;
   if (!state || !state->root_layouts[compute])
      return std::nullopt;
   const auto& params = state->root_layouts[compute]->parameters;
   const auto& args = state->root_args[compute];
   for (size_t p = 0; p < params.size() && p < args.size(); p++)
   {
      const auto& param = params[p];
      if (!VisibilityMatches(param.visibility, stage))
         continue;
      if (param.type == D3D12_ROOT_PARAMETER_TYPE_CBV && param.space == 0 && param.shader_register == slot && args[p].kind == RootArg::Kind::Descriptor &&
         args[p].value != 0)
         return BoundCBuffer{args[p].value, 0};
      if (param.type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE && args[p].kind == RootArg::Kind::Table && args[p].value != 0)
      {
         for (const auto& range : param.ranges)
         {
            if (range.type != D3D12_DESCRIPTOR_RANGE_TYPE_CBV || range.space != 0)
               continue;
            if (slot >= range.base_register && (range.count == UINT_MAX || slot < range.base_register + range.count))
            {
               const uint32_t descriptor_index = range.offset + (slot - range.base_register);
               if (const std::optional<Descriptors::D3D12::Descriptor> desc = Descriptors::D3D12::Read({args[p].value}, descriptor_index);
                  desc && desc->kind == Descriptors::D3D12::Kind::ConstantBuffer && desc->has_desc && desc->desc.constant_buffer.BufferLocation != 0)
                  return BoundCBuffer{desc->desc.constant_buffer.BufferLocation, desc->desc.constant_buffer.SizeInBytes};
            }
         }
      }
   }
   return std::nullopt;
}

void TagResource(ID3D12Device* device, uint64_t name_hash, ID3D12Resource* resource)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->runtime.size())
      return;
   std::scoped_lock lock(store->mutex);
   if (!store->released)
      store->runtime[*index].tagged = ResolveActiveResource(resource);
}

ID3D12Resource* TagBoundSRV(ID3D12GraphicsCommandList* list, ShaderStage stage, UINT slot, uint64_t name_hash)
{
   ID3D12Resource* resource = FindBoundSRV(list, stage, slot);
   if (!resource)
      return nullptr;
   ComPtr<ID3D12Device> device;
   if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))))
      TagResource(device.Get(), name_hash, resource);
   return resource;
}

ID3D12Resource* TagBoundRTV(ID3D12GraphicsCommandList* list, UINT slot, uint64_t name_hash)
{
   ID3D12Resource* resource = FindBoundRTV(list, slot);
   if (!resource)
      return nullptr;
   ComPtr<ID3D12Device> device;
   if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))))
      TagResource(device.Get(), name_hash, resource);
   return resource;
}

ID3D12Resource* TagBoundDSV(ID3D12GraphicsCommandList* list, uint64_t name_hash)
{
   ID3D12Resource* resource = FindBoundDSV(list);
   if (!resource)
      return nullptr;
   ComPtr<ID3D12Device> device;
   if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))))
      TagResource(device.Get(), name_hash, resource);
   return resource;
}

ID3D12Resource* EnsureTexture(ID3D12Device* device, uint64_t name_hash, UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
   D3D12_RESOURCE_STATES initial_state)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->runtime.size() || width == 0 || height == 0 || format == DXGI_FORMAT_UNKNOWN)
      return nullptr;
   std::scoped_lock lock(store->mutex);
   if (store->released)
      return nullptr;
   RuntimeSlot& slot = store->runtime[*index];
   if (slot.managed.object && slot.managed_desc.Width == width && slot.managed_desc.Height == height && slot.managed_desc.Format == format &&
      (slot.managed_desc.Flags & flags) == flags)
   {
      slot.tagged.Reset();
      return static_cast<ID3D12Resource*>(slot.managed.object.Get());
   }
   Hooks::Scope::Guard scope;
   const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
   const D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, width, height, 1, 1, format, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, flags};
   ComPtr<ID3D12Resource> resource;
   if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initial_state, nullptr, IID_PPV_ARGS(&resource))))
      return nullptr;
   slot.tagged.Reset();
   slot.managed_desc = desc;
   slot.managed_state = initial_state;
   if ((flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 && store->rtv_heap)
   {
      if (slot.rtv.ptr == 0 && store->next_rtv_slot < MAX_RUNTIME_SLOTS)
         slot.rtv = {store->rtv_heap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(store->next_rtv_slot++) * store->rtv_increment};
      if (slot.rtv.ptr != 0)
         device->CreateRenderTargetView(resource.Get(), nullptr, slot.rtv);
   }
   if (store->srv_uav_heap)
   {
      if (slot.srv.ptr == 0 && store->next_srv_uav_slot < MAX_RUNTIME_SLOTS * 2)
         slot.srv = {store->srv_uav_heap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(store->next_srv_uav_slot++) * store->srv_uav_increment};
      if (slot.srv.ptr != 0)
         device->CreateShaderResourceView(resource.Get(), nullptr, slot.srv);
      if ((flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
      {
         if (slot.uav.ptr == 0 && store->next_srv_uav_slot < MAX_RUNTIME_SLOTS * 2)
            slot.uav = {store->srv_uav_heap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(store->next_srv_uav_slot++) * store->srv_uav_increment};
         if (slot.uav.ptr != 0)
            device->CreateUnorderedAccessView(resource.Get(), nullptr, nullptr, slot.uav);
      }
   }
   ID3D12Resource* raw = resource.Get();
   Hooks::Lifetime::D3D12::Replace(slot.managed, std::move(resource));
   return raw;
}

ID3D12Resource* GetResource(ID3D12Device* device, uint64_t name_hash)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->runtime.size())
      return nullptr;
   std::scoped_lock lock(store->mutex);
   const RuntimeSlot& slot = store->runtime[*index];
   return slot.tagged ? slot.tagged.Get() : static_cast<ID3D12Resource*>(slot.managed.object.Get());
}

ID3D12Resource* ReferenceResource(ID3D12GraphicsCommandList* list, uint64_t name_hash)
{
   if (!list)
      return nullptr;
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
      return nullptr;
   Store* store = Hooks::Lifetime::GetState<Store>(device.Get());
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->runtime.size())
      return nullptr;
   std::scoped_lock lock(store->mutex);
   RuntimeSlot& slot = store->runtime[*index];
   if (slot.tagged)
      return slot.tagged.Get();
   return static_cast<ID3D12Resource*>(Hooks::Lifetime::D3D12::Reference(list, slot.managed));
}

D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(ID3D12Device* device, uint64_t name_hash)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->runtime.size())
      return {};
   std::scoped_lock lock(store->mutex);
   return store->runtime[*index].rtv;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(ID3D12Device* device, uint64_t name_hash)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->runtime.size())
      return {};
   std::scoped_lock lock(store->mutex);
   return store->runtime[*index].srv;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetUAV(ID3D12Device* device, uint64_t name_hash)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->runtime.size())
      return {};
   std::scoped_lock lock(store->mutex);
   return store->runtime[*index].uav;
}

D3D12_RESOURCE_STATES GetManagedState(ID3D12Device* device, uint64_t name_hash)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->runtime.size())
      return D3D12_RESOURCE_STATE_COMMON;
   std::scoped_lock lock(store->mutex);
   return store->runtime[*index].managed_state;
}

void SetManagedState(ID3D12Device* device, uint64_t name_hash, D3D12_RESOURCE_STATES state)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->runtime.size())
      return;
   std::scoped_lock lock(store->mutex);
   store->runtime[*index].managed_state = state;
}

void ClearFrameTags(ID3D12Device* device)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   if (!store)
      return;
   std::scoped_lock lock(store->mutex);
   for (RuntimeSlot& slot : store->runtime)
      slot.tagged.Reset();
}

void Invalidate(ID3D12Device* device, Invalidation reason)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   if (!store || reason == Invalidation::None)
      return;
   std::scoped_lock lock(store->mutex);
   for (uint32_t index = 0; index < store->runtime.size(); index++)
   {
      if (Get(index).kind == Kind::RuntimeTexture && HasInvalidation(Get(index).invalidation, reason))
      {
         store->runtime[index].tagged.Reset();
         Hooks::Lifetime::D3D12::Replace(store->runtime[index].managed, nullptr);
         store->runtime[index].managed_desc = {};
      }
   }
}

ID3D12DescriptorHeap* GetBoundHeap(ID3D12GraphicsCommandList* list)
{
   const List* state = GetList(list);
   return state ? state->heap : nullptr;
}

bool BindRequested(ID3D12GraphicsCommandList* list, bool compute, std::span<const uint32_t> entries, const CustomData& custom, uint32_t table)
{
   if (entries.empty() || !Settings::IsEnabled())
      return true;
   List* state = GetList(list);
   if (!state || !state->store || state->bundle)
      return table == NO_TABLE;
   if (state->luma_parameter[compute] == UINT32_MAX)
   {
      ASSERT_ONCE_MSG(false, "A variant runs with a root signature Luma didn't extend");
      return false;
   }
   if (!BindTable(list, *state, compute, table))
      return false;
   for (const uint32_t entry_index : entries)
   {
      // Textures are in the table
      if (Get(entry_index).kind == Kind::Texture)
         continue;
      // Other kinds were reported when the variant was made
      const std::optional<uint32_t> index = LumaCBuffers::GetRootIndex(entry_index);
      if (!index)
         continue;
      UploadEntry(list, *state, *index, Get(entry_index), custom);
      Bind(list, *state, compute, *index);
   }
   return true;
}

void BindForBundle(ID3D12GraphicsCommandList* list)
{
   std::vector<uint32_t> entries;
   for (const Shaders::Request& request : LumaCBuffers::GetRequests())
      entries.push_back(request.entry);
   List* state = GetList(list);
   if (state && state->luma_parameter[0] != UINT32_MAX)
      BindRequested(list, false, entries);
}

bool BindTableViews(ID3D12GraphicsCommandList* list, bool compute, uint32_t table, const TableViews& views)
{
   List* state = GetList(list);
   if (!state || !state->store || state->bundle || state->luma_parameter[compute] == UINT32_MAX || !state->luma_table[compute] || !state->heap)
      return false;
   const TableLayout* layout = nullptr;
   if (table != NO_TABLE)
   {
      if (table >= table_count.load(std::memory_order_acquire) || !HasTextures(*state->store, tables[table]))
         return false;
      layout = &tables[table];
   }
   if (state->views_heap != state->heap || state->views_table != table || !Same(state->views, views))
   {
      const std::optional<HeapTail::D3D12::RingRange> range = HeapTail::D3D12::AllocateRing(list, state->heap, Bindings::D3D12_TEXTURE_REGISTERS);
      if (!range)
         return false;
      WriteTable(static_cast<ID3D12Device*>(state->target.device), *state->store, layout, range->cpu, range->increment, &views);
      state->views = views;
      state->views_table = table;
      state->views_heap = state->heap;
      state->views_gpu = range->gpu.ptr;
      view_tables++;
   }
   SetTable(list, *state, compute, {state->views_gpu});
   return true;
}

Stats GetStats()
{
   return {ring_bytes.load(), chunks.load(), texture_count.load(), table_count.load(), view_tables.load()};
}
} // namespace D3D12
} // namespace Database
} // namespace Resources
} // namespace Luma
