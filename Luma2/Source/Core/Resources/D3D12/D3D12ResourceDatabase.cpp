#include "Resources/D3D12/D3D12ResourceDatabase.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <wrl/client.h>

#include <DDSTextureLoader12.h>

#include "Frame/Frame.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/Lifetime.hpp"
#include "Hooks/Scope.hpp"
#include "Luma.hpp"
#include "Resources/D3D12/D3D12HeapTail.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/Textures.hpp"
#include "Shaders/D3D12/D3D12Database.hpp"
#include "Shaders/D3D12/D3D12RootSignatures.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Database {
namespace D3D12 {
namespace {
constexpr uint64_t CHUNK_SIZE = 256 * 1024;
constexpr uint64_t CBV_ALIGNMENT = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
// Each layout has its own range of the tail
constexpr uint32_t MAX_TABLES = HeapTail::D3D12::TAIL_SIZE / Bindings::D3D12_TEXTURE_REGISTERS;

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

// In the device's private data. Its chunk holds the device: released when the game releases it.
struct Store
{
   std::mutex mutex; // Never held with another
   std::unique_ptr<Chunk> chunk;
   bool released = false;
   // Per entry (null for other kinds), made before any list records: read without the lock
   std::vector<Texture> textures;
};

std::atomic<uint64_t> ring_bytes = 0;
std::atomic<uint32_t> chunks = 0;

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
};

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
   state->target = {device.Get(), Frame::Find(device.Get()), Game::GetGameDeviceData(device.Get())};
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
      Utils::Log::Error("D3D12: Luma's cbuffer memory ({} KB) couldn't be created", CHUNK_SIZE / 1024);
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

// The entry's contents now for the list, uploaded again only when they changed (1.0): LumaSettings when its stamp moved, LumaData
// when its bytes differ from the list's last upload
void UploadEntry(ID3D12GraphicsCommandList* list, List& state, uint32_t index, const Entry& entry, const CustomData& custom)
{
   Database::Upload& upload = state.uploads[index];
   if (entry.get_stamp)
   {
      const uint64_t stamp = entry.get_stamp();
      if (!NeedsUpload(upload, stamp))
         return;
      state.addresses[index] = UploadBytes(list, state, entry.size, [&](std::span<std::byte> destination) { entry.write(destination, custom, state.target); });
      upload.stamp = stamp;
      upload.done = state.addresses[index] != 0;
      return;
   }
   state.written.resize(entry.size);
   entry.write(state.written, custom, state.target);
   if (ReplaceIfChanged(upload, state.written))
   {
      state.addresses[index] = UploadBytes(list, state, entry.size, [&](std::span<std::byte> destination) { std::ranges::copy(upload.bytes, destination.begin()); });
      upload.done = state.addresses[index] != 0;
   }
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
      Utils::Log::Error("Resources: texture \"{}\" ({}) couldn't be loaded on D3D12 (0x{:08X})", entry.name, entry.file, uint32_t(result));
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

// The table's views into the tail range at `start`: a null view where the layout has no texture
void WriteTable(ID3D12Device* device, const Store& store, const TableLayout& layout, D3D12_CPU_DESCRIPTOR_HANDLE start, uint32_t increment)
{
   D3D12_SHADER_RESOURCE_VIEW_DESC null_view = {DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
   null_view.Texture2D.MipLevels = 1;
   Hooks::Scope::Guard scope;
   for (uint32_t i = 0; i < layout.size(); i++)
   {
      const D3D12_CPU_DESCRIPTOR_HANDLE handle = {start.ptr + SIZE_T(i) * increment};
      const Texture* texture = layout[i] < store.textures.size() ? &store.textures[layout[i]] : nullptr;
      if (texture && texture->resource)
         device->CreateShaderResourceView(texture->resource.Get(), &texture->view, handle);
      else
         device->CreateShaderResourceView(nullptr, &null_view, handle);
   }
}

// Luma's texture table at its parameter of the list's signature: false when it can't be (see BindRequested)
bool BindTable(ID3D12GraphicsCommandList* list, List& state, bool compute, uint32_t table)
{
   if (table == NO_TABLE)
      return true;
   if (table >= table_count.load(std::memory_order_acquire) || !state.luma_table[compute] || !state.heap)
      return false;
   const TableLayout& layout = tables[table];
   // A texture the device couldn't load: the game's shaders run (D3D11's UseOriginal)
   for (const uint32_t entry : layout)
   {
      if (entry != UINT32_MAX && (entry >= state.store->textures.size() || !state.store->textures[entry].resource))
         return false;
   }
   auto* device = static_cast<ID3D12Device*>(state.target.device);
   const D3D12_GPU_DESCRIPTOR_HANDLE gpu = HeapTail::D3D12::GetRange(state.heap, table * Bindings::D3D12_TEXTURE_REGISTERS, Bindings::D3D12_TEXTURE_REGISTERS, table,
      [&](D3D12_CPU_DESCRIPTOR_HANDLE start, uint32_t increment) { WriteTable(device, *state.store, layout, start, increment); });
   if (!gpu.ptr)
      return false;
   if (state.table_set[compute] == gpu.ptr)
      return true;
   const UINT parameter = state.luma_parameter[compute] + Shaders::RootSignatures::D3D12::LUMA_TABLE_PARAMETER;
   Hooks::Scope::Guard scope;
   if (compute)
      list->SetComputeRootDescriptorTable(parameter, gpu);
   else
      list->SetGraphicsRootDescriptorTable(parameter, gpu);
   state.table_set[compute] = gpu.ptr;
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
      Utils::Log::Error("D3D12: more than {} layouts of Luma's texture table: patches with another one run the game's shaders", MAX_TABLES);
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
   for (uint32_t index = 0; index < GetCount(); index++)
   {
      if (Get(index).kind == Kind::Texture)
         store->textures[index] = LoadTexture(device, index);
   }
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
   }
}

void SetRootSignature(ID3D12GraphicsCommandList* list, bool compute, ID3D12RootSignature* root_signature)
{
   List* state = GetList(list);
   if (!state)
      return;
   const Shaders::Database::D3D12::RootLayout* layout = Shaders::Database::D3D12::FindRootLayout(root_signature);
   state->luma_parameter[compute] = layout ? layout->luma_parameter : UINT32_MAX;
   state->luma_table[compute] = layout && layout->luma_table;
   state->set[compute] = {};
   state->table_set[compute] = 0;
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
   for (UINT i = 0; heaps && i < count; i++)
   {
      if (heaps[i] && HeapTail::D3D12::GetGameSize(heaps[i]))
         state->heap = heaps[i];
   }
}

bool BindRequested(ID3D12GraphicsCommandList* list, bool compute, std::span<const uint32_t> entries, const CustomData& custom, uint32_t table)
{
   if (entries.empty() || !IsEnabled())
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
      const std::optional<uint32_t> index = LumaCBuffers::GetRootIndex(entry_index);
      if (!index)
      {
         LUMA_UNIMPLEMENTED("Resources::Database shaders as D3D12 bindings");
         continue;
      }
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

Stats GetStats()
{
   return {ring_bytes.load(), chunks.load(), texture_count.load(), table_count.load()};
}
} // namespace D3D12
} // namespace Database
} // namespace Resources
} // namespace Luma
