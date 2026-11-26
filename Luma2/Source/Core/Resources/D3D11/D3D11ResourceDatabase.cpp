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

#include "Settings/SettingsRegistry.hpp"
#include "Frame/FrameState.hpp"
#include "Game/Game.hpp"
#include "Hooks/D3D11/D3D11Stages.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/TextureLoader.hpp"
#include "Shaders/ShaderStore.hpp"
#include "Utils/Debug.hpp"
#include "Log/Log.hpp"
#include "Utils/Strings.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Database {
namespace D3D11 {
namespace {
struct RuntimeSlot
{
   ComPtr<ID3D11Resource> tagged; // Cleared at OnPresent
   ComPtr<ID3D11Texture2D> managed;
   ComPtr<ID3D11ShaderResourceView> managed_srv;
   ComPtr<ID3D11RenderTargetView> managed_rtv;
   ComPtr<ID3D11UnorderedAccessView> managed_uav;
   D3D11_TEXTURE2D_DESC managed_desc = {};
};

struct Object
{
   Shaders::D3D11::Published shader;
   ComPtr<ID3D11Buffer> buffer;
   ComPtr<ID3D11ShaderResourceView> view; // Textures
   RuntimeSlot runtime; // RuntimeTexture
};

// In the device's private data. Its objects hold the device: they're released when the game releases it.
struct Store
{
   // Null for a device created inside Luma's scope: Luma's shaders aren't made on it
   std::shared_ptr<Shaders::Store> shaders;
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
      Log::Error("Resources: \"{}\" ({} bytes) couldn't be created", entry.name, entry.size);
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
      Log::Error("Resources: texture \"{}\" ({}) couldn't be loaded (0x{:08X})", entry.name, entry.file, uint32_t(result));
   return view;
}

// With the store locked
void UpdateShaders(ID3D11Device* device, Store& store)
{
   if (!store.shaders)
      return;
   const uint64_t generation = GetGeneration(*store.shaders);
   if (store.generation == generation)
      return;
   store.generation = generation;
   for (uint32_t index = 0; index < store.count; index++)
   {
      const Entry& entry = Get(index);
      if (entry.kind == Kind::Shader && !Shaders::D3D11::Publish(device, entry.stage, store.objects[index].shader, Database::GetShader(*store.shaders, index).compiled, store.retired))
         Log::Error("Resources: the device refused shader \"{}\"", entry.name);
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
// The slot holds nothing or the entry's own buffer, otherwise Luma takes a slot the game uses
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
   store->shaders = Shaders::GetStore(device);
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
      store->objects[index].runtime = {};
   }
   store->retired.lives.clear();
   store->shaders.reset();
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
   const Object& object = store->objects[*index];
   return object.view ? object.view.Get() : object.runtime.managed_srv.Get();
}

namespace {
ID3D11Resource* ResolveActiveResource(ID3D11Resource* resource)
{
   if (!resource)
      return nullptr;
   if (Mirrors::IsSwapping())
   {
      if (ID3D11Resource* mirror = Mirrors::D3D11::FindMirror(resource))
         return mirror;
   }
   return resource;
}

ID3D11Texture2D* CopyResourceIntoSlot(ID3D11DeviceContext* context, ID3D11Resource* source, uint64_t name_hash, const TextureCopyOptions& options)
{
   if (!context || !source)
      return nullptr;
   ComPtr<ID3D11Texture2D> source_texture;
   if (FAILED(source->QueryInterface(IID_PPV_ARGS(&source_texture))))
      return nullptr;
   D3D11_TEXTURE2D_DESC desc = {};
   source_texture->GetDesc(&desc);
   const DXGI_FORMAT format = options.format_override != DXGI_FORMAT_UNKNOWN ? options.format_override : desc.Format;
   const UINT bind_flags = D3D11_BIND_SHADER_RESOURCE | options.extra_bind_flags;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   ID3D11Texture2D* managed = EnsureTexture(device.Get(), name_hash, desc.Width, desc.Height, format, bind_flags);
   if (!managed)
      return nullptr;
   context->CopyResource(managed, source);
   return managed;
}
} // namespace

void TagResource(ID3D11Device* device, uint64_t name_hash, ID3D11Resource* resource)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->count)
      return;
   std::scoped_lock lock(store->mutex);
   if (!store->released)
      store->objects[*index].runtime.tagged = ResolveActiveResource(resource);
}

ID3D11Resource* TagBoundSRV(ID3D11DeviceContext* context, ShaderStage stage, UINT slot, uint64_t name_hash)
{
   if (!context)
      return nullptr;
   ComPtr<ID3D11ShaderResourceView> srv;
   Hooks::D3D11::WithStage(stage, [&]<ShaderStage Stage>() { (context->*Hooks::D3D11::StageTraits<Stage>::GetShaderResources)(slot, 1, &srv); });
   if (!srv)
      return nullptr;
   ComPtr<ID3D11Resource> resource;
   srv->GetResource(&resource);
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   TagResource(device.Get(), name_hash, resource.Get());
   return ResolveActiveResource(resource.Get());
}

ID3D11Resource* TagBoundRTV(ID3D11DeviceContext* context, UINT slot, uint64_t name_hash)
{
   if (!context || slot >= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT)
      return nullptr;
   ComPtr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
   context->OMGetRenderTargets(slot + 1, &rtvs[0], nullptr);
   if (!rtvs[slot])
      return nullptr;
   ComPtr<ID3D11Resource> resource;
   rtvs[slot]->GetResource(&resource);
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   TagResource(device.Get(), name_hash, resource.Get());
   return ResolveActiveResource(resource.Get());
}

ID3D11Resource* TagBoundDSV(ID3D11DeviceContext* context, uint64_t name_hash)
{
   if (!context)
      return nullptr;
   ComPtr<ID3D11DepthStencilView> dsv;
   context->OMGetRenderTargets(0, nullptr, &dsv);
   if (!dsv)
      return nullptr;
   ComPtr<ID3D11Resource> resource;
   dsv->GetResource(&resource);
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   TagResource(device.Get(), name_hash, resource.Get());
   return ResolveActiveResource(resource.Get());
}

ID3D11Resource* TagBoundUAV(ID3D11DeviceContext* context, UINT slot, uint64_t name_hash)
{
   if (!context)
      return nullptr;
   ComPtr<ID3D11UnorderedAccessView> uav;
   context->CSGetUnorderedAccessViews(slot, 1, &uav);
   if (!uav)
      return nullptr;
   ComPtr<ID3D11Resource> resource;
   uav->GetResource(&resource);
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   TagResource(device.Get(), name_hash, resource.Get());
   return ResolveActiveResource(resource.Get());
}

ID3D11Texture2D* CopyBoundSRV(ID3D11DeviceContext* context, ShaderStage stage, UINT slot, uint64_t name_hash, const TextureCopyOptions& options)
{
   if (!context)
      return nullptr;
   ComPtr<ID3D11ShaderResourceView> srv;
   Hooks::D3D11::WithStage(stage, [&]<ShaderStage Stage>() { (context->*Hooks::D3D11::StageTraits<Stage>::GetShaderResources)(slot, 1, &srv); });
   if (!srv)
      return nullptr;
   ComPtr<ID3D11Resource> resource;
   srv->GetResource(&resource);
   return CopyResourceIntoSlot(context, ResolveActiveResource(resource.Get()), name_hash, options);
}

ID3D11Texture2D* CopyBoundRTV(ID3D11DeviceContext* context, UINT slot, uint64_t name_hash, const TextureCopyOptions& options)
{
   if (!context || slot >= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT)
      return nullptr;
   ComPtr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
   context->OMGetRenderTargets(slot + 1, &rtvs[0], nullptr);
   if (!rtvs[slot])
      return nullptr;
   ComPtr<ID3D11Resource> resource;
   rtvs[slot]->GetResource(&resource);
   return CopyResourceIntoSlot(context, ResolveActiveResource(resource.Get()), name_hash, options);
}

ID3D11Texture2D* EnsureTexture(ID3D11Device* device, uint64_t name_hash, UINT width, UINT height, DXGI_FORMAT format, UINT bind_flags)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->count || width == 0 || height == 0 || format == DXGI_FORMAT_UNKNOWN)
      return nullptr;
   std::scoped_lock lock(store->mutex);
   if (store->released)
      return nullptr;
   RuntimeSlot& slot = store->objects[*index].runtime;
   if (slot.managed && slot.managed_desc.Width == width && slot.managed_desc.Height == height && slot.managed_desc.Format == format &&
      (slot.managed_desc.BindFlags & bind_flags) == bind_flags)
   {
      slot.tagged.Reset();
      return slot.managed.Get();
   }
   slot = {};
   D3D11_TEXTURE2D_DESC desc = {};
   desc.Width = width;
   desc.Height = height;
   desc.MipLevels = 1;
   desc.ArraySize = 1;
   desc.Format = format;
   desc.SampleDesc.Count = 1;
   desc.Usage = D3D11_USAGE_DEFAULT;
   desc.BindFlags = bind_flags;
   if (FAILED(device->CreateTexture2D(&desc, nullptr, &slot.managed)))
      return nullptr;
   slot.managed_desc = desc;
   if ((bind_flags & D3D11_BIND_SHADER_RESOURCE) != 0)
      device->CreateShaderResourceView(slot.managed.Get(), nullptr, &slot.managed_srv);
   if ((bind_flags & D3D11_BIND_RENDER_TARGET) != 0)
      device->CreateRenderTargetView(slot.managed.Get(), nullptr, &slot.managed_rtv);
   if ((bind_flags & D3D11_BIND_UNORDERED_ACCESS) != 0)
      device->CreateUnorderedAccessView(slot.managed.Get(), nullptr, &slot.managed_uav);
   return slot.managed.Get();
}

ID3D11Resource* GetResource(ID3D11Device* device, uint64_t name_hash)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->count)
      return nullptr;
   std::scoped_lock lock(store->mutex);
   const RuntimeSlot& slot = store->objects[*index].runtime;
   return slot.tagged ? slot.tagged.Get() : slot.managed.Get();
}

ID3D11ShaderResourceView* GetSRV(ID3D11Device* device, uint64_t name_hash)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->count)
      return nullptr;
   std::scoped_lock lock(store->mutex);
   const Object& object = store->objects[*index];
   return object.runtime.managed_srv ? object.runtime.managed_srv.Get() : object.view.Get();
}

ID3D11RenderTargetView* GetRTV(ID3D11Device* device, uint64_t name_hash)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->count)
      return nullptr;
   std::scoped_lock lock(store->mutex);
   return store->objects[*index].runtime.managed_rtv.Get();
}

ID3D11UnorderedAccessView* GetUAV(ID3D11Device* device, uint64_t name_hash)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   const std::optional<uint32_t> index = Find(name_hash);
   if (!store || !index || *index >= store->count)
      return nullptr;
   std::scoped_lock lock(store->mutex);
   return store->objects[*index].runtime.managed_uav.Get();
}

void OnPresent(ID3D11Device* device)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   if (!store)
      return;
   std::scoped_lock lock(store->mutex);
   for (uint32_t index = 0; index < store->count; index++)
      store->objects[index].runtime.tagged.Reset();
}

void Invalidate(ID3D11Device* device, Invalidation reason)
{
   Store* store = Hooks::Lifetime::GetState<Store>(device);
   if (!store || reason == Invalidation::None)
      return;
   std::scoped_lock lock(store->mutex);
   for (uint32_t index = 0; index < store->count; index++)
   {
      if (Get(index).kind == Kind::RuntimeTexture && HasInvalidation(Get(index).invalidation, reason))
         store->objects[index].runtime = {};
   }
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

bool BindRequested(ID3D11DeviceContext* context, ShaderStage stage, const Shaders::CompiledShader& compiled, const CustomData& custom, bool while_off)
{
   if (compiled.requests.empty() || (!Settings::IsEnabled() && !while_off))
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
      UploadIfDirty(entry, state->uploads[request.entry], state->written, custom, state->target,
         [&](auto&& write) { return WriteBuffer(context, buffer, entry.size, write); });
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
