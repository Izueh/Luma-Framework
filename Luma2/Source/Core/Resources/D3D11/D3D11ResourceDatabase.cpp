#include "Resources/D3D11/D3D11ResourceDatabase.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include <wrl/client.h>

#include <DDSTextureLoader11.h>

#include "Frame/Frame.hpp"
#include "Hooks/D3D11/D3D11Stages.hpp"
#include "Hooks/Lifetime.hpp"
#include "Luma.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Resources/Textures.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Log.hpp"
#include "Utils/Strings.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Database {
namespace D3D11 {
namespace {
struct Object
{
   Shaders::D3D11::Published shader;
   ComPtr<ID3D11Buffer> buffer;
   ComPtr<ID3D11ShaderResourceView> view; // Textures
};

// In the device's private data. Its objects hold the device: they're released when the game releases it.
struct Store
{
   // One per entry, the entries being fixed before any device
   uint32_t count = 0;
   std::unique_ptr<Object[]> objects;
   // Lock order: before Database's publication lock (GetShader)
   std::mutex mutex;
   Shaders::D3D11::Retired retired;
   uint64_t generation = UINT64_MAX; // Of the publication applied
   bool released = false;
};

struct Context
{
   const Store* store = nullptr;
   // The context's device and its state, which the entries' contents are written for, found with the store. Valid while the
   // context is.
   WriteTarget target;
   std::vector<Upload> uploads; // Per entry
   std::vector<std::byte> written; // An entry without a stamp's contents now, compared with its last upload
};

ComPtr<ID3D11Buffer> CreateBuffer(ID3D11Device* device, const Entry& entry)
{
   const D3D11_BUFFER_DESC desc = {entry.size, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
   ComPtr<ID3D11Buffer> buffer;
   if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer)))
      Utils::Log::Error("Resources: \"{}\" ({} bytes) couldn't be created", entry.name, entry.size);
   return buffer;
}

// A .dds file as a texture and its view. In the device's creation: the files are few and small (a noise texture), read once per device.
ComPtr<ID3D11ShaderResourceView> LoadTexture(ID3D11Device* device, uint32_t index)
{
   const Entry& entry = Get(index);
   const std::filesystem::path path = Textures::Find(index);
   ComPtr<ID3D11ShaderResourceView> view;
   const HRESULT result = path.empty() ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : DirectX::CreateDDSTextureFromFile(device, path.c_str(), nullptr, &view);
   if (FAILED(result))
      Utils::Log::Error("Resources: texture \"{}\" ({}) couldn't be loaded (0x{:08X})", entry.name, entry.file, uint32_t(result));
   return view;
}

// With the store locked
void UpdateShaders(ID3D11Device* device, Store& store)
{
   const uint64_t generation = GetGeneration();
   if (store.generation == generation)
      return;
   store.generation = generation;
   for (uint32_t index = 0; index < store.count; index++)
   {
      const Entry& entry = Get(index);
      if (entry.kind == Kind::Shader && !Shaders::D3D11::Publish(device, entry.stage, store.objects[index].shader, Database::GetShader(index).compiled, store.retired))
         Utils::Log::Error("Resources: the device refused shader \"{}\"", entry.name);
   }
}

const Store* GetStore(ID3D11DeviceContext* context, Context& state)
{
   if (!state.store)
   {
      ComPtr<ID3D11Device> device;
      context->GetDevice(&device);
      // The device's other states are created with it, before its store
      state.store = Hooks::Lifetime::GetState<Store>(device.Get());
      if (state.store)
         state.target = {device.Get(), Frame::Find(device.Get()), Game::GetGameDeviceData(device.Get())};
   }
   return state.store;
}

// `write` fills the mapped buffer. False if it couldn't be mapped.
template <typename Write>
bool WriteBuffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, uint32_t size, Write&& write)
{
   D3D11_MAPPED_SUBRESOURCE mapped = {};
   if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
      return false;
   write(std::span(static_cast<std::byte*>(mapped.pData), size));
   context->Unmap(buffer, 0);
   return true;
}

#if DEVELOPMENT && defined(_DEBUG)
// 1.0's check: the slot holds nothing or the entry's own buffer, otherwise Luma takes a slot the game uses
void CheckSlot(ID3D11DeviceContext* context, ShaderStage stage, uint32_t slot, ID3D11Buffer* buffer)
{
   ComPtr<ID3D11Buffer> bound;
   Hooks::D3D11::WithStage(stage, [&]<ShaderStage Stage>() { (context->*Hooks::D3D11::StageTraits<Stage>::GetConstantBuffers)(slot, 1, &bound); });
   ASSERT_ONCE_MSG(!bound || bound.Get() == buffer, "The game has a cbuffer bound at a slot Luma binds its own to");
}
#endif
} // namespace

void CreateDeviceObjects(ID3D11Device* device)
{
   Store* store = Hooks::Lifetime::SetState<Store>(device);
   if (!store)
      return;
   std::scoped_lock lock(store->mutex);
   store->count = GetCount();
   store->objects = std::make_unique<Object[]>(store->count);
   for (uint32_t index = 0; index < store->count; index++)
   {
      if (const Entry& entry = Get(index); entry.kind == Kind::ConstantBuffer)
         store->objects[index].buffer = CreateBuffer(device, entry);
      else if (entry.kind == Kind::Texture)
         store->objects[index].view = LoadTexture(device, index);
   }
   UpdateShaders(device, *store);
}

void UpdateShaderObjects(ID3D11Device* device)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   if (!store)
      return;
   std::scoped_lock lock(store->mutex);
   if (store->released)
      return;
   Shaders::D3D11::ReleaseRetired(store->retired);
   UpdateShaders(device, *store);
}

void ReleaseDeviceObjects(ID3D11Device* device)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   if (!store)
      return;
   std::scoped_lock lock(store->mutex);
   for (uint32_t index = 0; index < store->count; index++)
   {
      Shaders::D3D11::Release(store->objects[index].shader);
      store->objects[index].buffer.Reset();
      store->objects[index].view.Reset();
   }
   store->retired.lives.clear();
   store->released = true;
}

const Shaders::D3D11::Live* GetShader(ID3D11Device* device, uint64_t name_hash)
{
   const Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->count)
      return nullptr;
   return store->objects[*index].shader.current.load(std::memory_order_acquire);
}

ID3D11ShaderResourceView* GetTexture(ID3D11Device* device, uint64_t name_hash)
{
   const Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->count)
      return nullptr;
   return store->objects[*index].view.Get();
}

bool HasRequested(ID3D11DeviceContext* context, const Shaders::CompiledShader& compiled)
{
   if (compiled.requests.empty())
      return true;
   Context* state = Hooks::Lifetime::GetOrCreateStateCached<Context>(context);
   const Store* store = state ? GetStore(context, *state) : nullptr;
   if (!store)
      return false;
   return std::ranges::all_of(compiled.requests, [store](const Shaders::Request& request) {
      if (request.entry >= store->count)
         return false;
      const Object& object = store->objects[request.entry];
      switch (Get(request.entry).kind)
      {
      case Kind::ConstantBuffer: return object.buffer != nullptr;
      case Kind::Texture: return object.view != nullptr;
      default: return false;
      }
   });
}

bool BindRequested(ID3D11DeviceContext* context, ShaderStage stage, const Shaders::CompiledShader& compiled, const CustomData& custom)
{
   if (compiled.requests.empty() || !IsEnabled())
      return false;
   Context* state = Hooks::Lifetime::GetOrCreateStateCached<Context>(context);
   const Store* store = state ? GetStore(context, *state) : nullptr;
   if (!store)
      return false;
   state->uploads.resize(store->count);
   bool bound = false;
   for (const Shaders::Request& request : compiled.requests)
   {
      if (request.entry >= store->count)
         continue;
      const Entry& entry = Get(request.entry);
      if (entry.kind == Kind::Texture)
      {
         if (ID3D11ShaderResourceView* view = store->objects[request.entry].view.Get())
         {
            Hooks::D3D11::WithStage(stage, [&]<ShaderStage Stage>() { (context->*Hooks::D3D11::StageTraits<Stage>::SetShaderResources)(request.slot, 1, &view); });
            bound = true;
         }
         continue;
      }
      ID3D11Buffer* buffer = store->objects[request.entry].buffer.Get();
      if (entry.kind != Kind::ConstantBuffer || !buffer)
         continue;
      // Uploaded again only when the contents changed for this context (1.0): LumaSettings when its stamp moved (its dirty flag),
      // LumaData when what it writes now differs from its last upload
      Upload& upload = state->uploads[request.entry];
      if (entry.get_stamp)
      {
         const uint64_t stamp = entry.get_stamp();
         if (NeedsUpload(upload, stamp))
         {
            upload.done = WriteBuffer(context, buffer, entry.size, [&](std::span<std::byte> destination) { entry.write(destination, custom, state->target); });
            upload.stamp = stamp;
         }
      }
      else
      {
         state->written.resize(entry.size);
         entry.write(state->written, custom, state->target);
         if (ReplaceIfChanged(upload, state->written))
            upload.done = WriteBuffer(context, buffer, entry.size, [&](std::span<std::byte> destination) { std::ranges::copy(upload.bytes, destination.begin()); });
      }
#if DEVELOPMENT && defined(_DEBUG)
      // With the game's buffers restored after the draw, Luma's in a slot of the game's is expected
      if (!LumaCBuffers::GetOptions().restore_after_draw)
         CheckSlot(context, stage, request.slot, buffer);
#endif
      Hooks::D3D11::WithStage(stage, [&]<ShaderStage Stage>() { (context->*Hooks::D3D11::StageTraits<Stage>::SetConstantBuffers)(request.slot, 1, &buffer); });
      bound = true;
   }
   return bound;
}

void ForgetUploads(ID3D11DeviceContext* context)
{
   if (Context* state = Hooks::Lifetime::GetOrCreateStateCached<Context>(context))
   {
      for (Upload& upload : state->uploads)
         upload.done = false;
   }
}
} // namespace D3D11
} // namespace Database
} // namespace Resources
} // namespace Luma
