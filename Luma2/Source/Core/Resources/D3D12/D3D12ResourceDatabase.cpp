#include "Resources/D3D12/D3D12ResourceDatabase.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <wrl/client.h>

#include "Frame/Frame.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/Lifetime.hpp"
#include "Hooks/Scope.hpp"
#include "Luma.hpp"
#include "Resources/LumaCBuffers.hpp"
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
} // namespace

void CreateDeviceObjects(ID3D12Device* device)
{
   Hooks::Lifetime::SetState<Store>(device);
   // Luma's views need its descriptor heap tail, built with the D3D12 passes
   for (uint32_t index = 0; index < GetCount(); index++)
   {
      if (Get(index).kind == Kind::Texture)
      {
         LUMA_UNIMPLEMENTED("Resources::Database textures on D3D12 (the heap tail, Step 5)");
         break;
      }
   }
}

void ReleaseDeviceObjects(ID3D12Device* device)
{
   if (Store* store = Hooks::Lifetime::GetState<Store>(device))
   {
      std::scoped_lock lock(store->mutex);
      store->chunk.reset();
      store->released = true;
   }
}

void SetRootSignature(ID3D12GraphicsCommandList* list, bool compute, ID3D12RootSignature* root_signature)
{
   List* state = GetList(list);
   if (!state)
      return;
   const Shaders::Database::D3D12::RootLayout* layout = Shaders::Database::D3D12::FindRootLayout(root_signature);
   state->luma_parameter[compute] = layout ? layout->luma_parameter : UINT32_MAX;
   state->set[compute] = {};
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
   }
}

void BindRequested(ID3D12GraphicsCommandList* list, bool compute, std::span<const uint32_t> entries, const CustomData& custom)
{
   if (entries.empty() || !IsEnabled())
      return;
   List* state = GetList(list);
   if (!state || !state->store || state->bundle)
      return;
   if (state->luma_parameter[compute] == UINT32_MAX)
   {
      ASSERT_ONCE_MSG(false, "A variant runs with a root signature Luma didn't extend");
      return;
   }
   for (const uint32_t entry_index : entries)
   {
      const std::optional<uint32_t> index = LumaCBuffers::GetRootIndex(entry_index);
      if (!index)
      {
         LUMA_UNIMPLEMENTED("Resources::Database entries other than Luma's cbuffers on D3D12");
         continue;
      }
      UploadEntry(list, *state, *index, Get(entry_index), custom);
      Bind(list, *state, compute, *index);
   }
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
   return {ring_bytes.load(), chunks.load()};
}
} // namespace D3D12
} // namespace Database
} // namespace Resources
} // namespace Luma
