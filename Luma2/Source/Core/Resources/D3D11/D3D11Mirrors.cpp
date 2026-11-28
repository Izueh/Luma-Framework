#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D11/D3D11MirrorsInternal.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "FrameState/FrameState.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Shaders/Shaders.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Format.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D11 {
namespace {
using namespace Utils::Format::DXGI;
using Internal::OwnedResource;
using Internal::ResourceMirrorState;

constexpr uint64_t RETIRE_PRESENTS = 2;
constexpr uint64_t POOL_TTL_FRAMES = 120;

struct PooledTexture
{
   ComPtr<ID3D11Texture2D> texture;
   D3D11_TEXTURE2D_DESC desc = {};
   uint64_t stored_frame = 0;
};

struct RetiredMirror
{
   ComPtr<ID3D11Resource> mirror;
   D3D11_TEXTURE2D_DESC desc = {};
   uint64_t retire_frame = 0;
   bool poolable = false;
};

struct PendingChainMirror
{
   ComPtr<ID3D11Resource> original;
   Upgrade::TextureInfo original_info;
   Upgrade::TextureInfo mirror_info;
   Chains::Origin origin = {};
};

// Per-device mirror registry, recycle pool and deferred-context pending chain queue. Never held with another lock.
struct DeviceMirrorRegistry
{
   std::mutex mutex;
   uint64_t scale_generation = 0;
   std::unordered_set<ID3D11Resource*> scaled_originals;
   std::unordered_set<const ID3D11Resource*> pending_set;
   std::vector<PendingChainMirror> pending_queue;
   std::vector<RetiredMirror> retired;
   std::vector<PooledTexture> pool;
   bool released = false;
};

struct DeviceMirrorState
{
   std::shared_ptr<DeviceMirrorRegistry> registry = std::make_shared<DeviceMirrorRegistry>();
};

DeviceMirrorState* GetOrCreateDeviceState(ID3D11Device* device)
{
   if (!device)
      return nullptr;
   if (DeviceMirrorState* existing = Hooks::Lifetime::GetState<DeviceMirrorState>(device))
      return existing;
   return Hooks::Lifetime::SetState<DeviceMirrorState>(device);
}

// Attached to an original resource whose mirror is scaled; removes the pointer on resource destruction
struct ScaledTracker
{
   std::shared_ptr<DeviceMirrorRegistry> registry;
   ID3D11Resource* original = nullptr;

   ~ScaledTracker()
   {
      if (!registry || !original)
         return;
      std::scoped_lock lock(registry->mutex);
      registry->scaled_originals.erase(original);
   }
};

bool DescMatches(const D3D11_TEXTURE2D_DESC& a, const D3D11_TEXTURE2D_DESC& b)
{
   return a.Width == b.Width && a.Height == b.Height && a.MipLevels == b.MipLevels && a.ArraySize == b.ArraySize && a.Format == b.Format &&
      a.SampleDesc.Count == b.SampleDesc.Count && a.SampleDesc.Quality == b.SampleDesc.Quality && a.Usage == b.Usage && a.BindFlags == b.BindFlags &&
      a.CPUAccessFlags == b.CPUAccessFlags && a.MiscFlags == b.MiscFlags;
}

ComPtr<ID3D11Texture2D> AcquireFromPool(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& desc)
{
   DeviceMirrorState* dev = GetOrCreateDeviceState(device);
   if (!dev)
      return nullptr;
   std::scoped_lock lock(dev->registry->mutex);
   for (size_t i = 0; i < dev->registry->pool.size(); i++)
   {
      if (DescMatches(dev->registry->pool[i].desc, desc))
      {
         ComPtr<ID3D11Texture2D> texture = std::move(dev->registry->pool[i].texture);
         dev->registry->pool.erase(dev->registry->pool.begin() + ptrdiff_t(i));
         Scaling::CountPoolSize(-1);
         return texture;
      }
   }
   return nullptr;
}

template <typename Desc>
Upgrade::TextureInfo Describe(const Desc& desc)
{
   Upgrade::TextureInfo described = {.width = desc.Width, .levels = desc.MipLevels, .format = desc.Format};
   if constexpr (std::is_same_v<Desc, D3D11_TEXTURE1D_DESC>)
   {
      described.dimension = Upgrade::Dimension::Texture1D;
      described.depth_or_layers = desc.ArraySize;
   }
   else if constexpr (std::is_same_v<Desc, D3D11_TEXTURE2D_DESC1> || std::is_same_v<Desc, D3D11_TEXTURE2D_DESC>)
   {
      described.height = desc.Height;
      described.depth_or_layers = desc.ArraySize;
      described.samples = desc.SampleDesc.Count;
      described.cube = (desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0 && desc.ArraySize % 6 == 0;
   }
   else
   {
      described.dimension = Upgrade::Dimension::Texture3D;
      described.height = desc.Height;
      described.depth_or_layers = desc.Depth;
   }
   described.render_target = (desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
   described.unordered_access = (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
   described.depth_stencil = (desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0;
   described.shader_resource = (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0;
   described.gpu_only = desc.Usage == D3D11_USAGE_DEFAULT || desc.Usage == D3D11_USAGE_IMMUTABLE;
   return described;
}

struct ConvertedData
{
   std::vector<Converted> subresources;
   std::vector<D3D11_SUBRESOURCE_DATA> data;
};

std::optional<ConvertedData> ConvertInitialData(const Upgrade::TextureInfo& desc, DXGI_FORMAT mirror_format, const D3D11_SUBRESOURCE_DATA* initial_data)
{
   if (!initial_data || mirror_format != DXGI_FORMAT_R16G16B16A16_FLOAT)
      return std::nullopt;
   const bool volume = desc.dimension == Upgrade::Dimension::Texture3D;
   const uint32_t layers = volume ? 1 : desc.depth_or_layers;
   ConvertedData converted;
   for (uint32_t layer = 0; layer < layers; layer++)
   {
      for (uint32_t level = 0; level < desc.levels; level++)
      {
         const D3D11_SUBRESOURCE_DATA& source = initial_data[layer * desc.levels + level];
         std::optional<Converted> subresource = ConvertToRgba16Float(source.pSysMem, source.SysMemPitch, source.SysMemSlicePitch, desc.format,
            Upgrade::GetMipSize(desc.width, level), Upgrade::GetMipSize(desc.height, level), volume ? Upgrade::GetMipSize(desc.depth_or_layers, level) : 1);
         if (!subresource)
            return std::nullopt;
         converted.subresources.push_back(std::move(*subresource));
      }
   }
   for (const Converted& subresource : converted.subresources)
      converted.data.push_back({subresource.bytes.data(), subresource.row_pitch, subresource.depth_pitch});
   return converted;
}

template <typename Desc>
Desc MakeMirrorDesc(Desc desc, const Upgrade::TextureInfo& mirror_info)
{
   desc.Format = mirror_info.format;
   desc.Width = mirror_info.width;
   desc.MipLevels = mirror_info.levels;
   if constexpr (std::is_same_v<Desc, D3D11_TEXTURE1D_DESC>)
   {
      desc.ArraySize = mirror_info.depth_or_layers;
   }
   else if constexpr (std::is_same_v<Desc, D3D11_TEXTURE2D_DESC>)
   {
      desc.Height = mirror_info.height;
      desc.ArraySize = mirror_info.depth_or_layers;
      desc.SampleDesc.Count = mirror_info.samples;
      if (desc.SampleDesc.Count == 1)
         desc.SampleDesc.Quality = 0;
   }
   else
   {
      desc.Height = mirror_info.height;
      desc.Depth = mirror_info.depth_or_layers;
   }
   desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
   desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_RESOURCE_CLAMP;
   desc.CPUAccessFlags = 0;
   desc.Usage = D3D11_USAGE_DEFAULT;
   return desc;
}

void RegisterScaledOriginal(ID3D11Device* device, ID3D11Resource* original)
{
   DeviceMirrorState* dev = GetOrCreateDeviceState(device);
   if (!dev)
      return;
   {
      std::scoped_lock lock(dev->registry->mutex);
      dev->registry->scaled_originals.insert(original);
   }
   Hooks::Lifetime::SetState<ScaledTracker>(original, dev->registry, original);
}

uint64_t GetDeviceScaleGeneration(ID3D11Device* device)
{
   if (DeviceMirrorState* dev = GetOrCreateDeviceState(device))
   {
      std::scoped_lock lock(dev->registry->mutex);
      return dev->registry->scale_generation;
   }
   return 0;
}

bool CreateMirror2D(ID3D11Device* device, ID3D11Texture2D* texture, const D3D11_TEXTURE2D_DESC& desc, const Upgrade::TextureInfo& described,
   const Upgrade::TextureInfo& mirrored, const D3D11_SUBRESOURCE_DATA* initial_data, Chains::Origin origin)
{
   const std::optional<ConvertedData> data = ConvertInitialData(described, mirrored.format, initial_data);
   ASSERT_ONCE_MSG(!initial_data || data, "A texture matching the upgrade rules has initial data that can't be converted to its mirror's format");
   const D3D11_TEXTURE2D_DESC mirror_desc = MakeMirrorDesc(desc, mirrored);
   const bool scaled = mirrored.width != described.width || mirrored.height != described.height;
   ComPtr<ID3D11Texture2D> mirror;
   if (!initial_data && scaled)
      mirror = AcquireFromPool(device, mirror_desc);
   if (!mirror)
   {
      const HRESULT result = device->CreateTexture2D(&mirror_desc, data ? data->data.data() : nullptr, &mirror);
      if (FAILED(result) || !mirror)
      {
         LogCreationFailure(std::format("a {} mirror of a {} texture", Utils::Format::GetName(mirrored.format), Utils::Format::GetName(desc.Format)), result);
         return false;
      }
   }
   ComPtr<ID3D11Resource> mirror_res = mirror;
   const uint64_t generation = scaled ? GetDeviceScaleGeneration(device) : 0;
   AttachMirror<ID3D11Resource>(texture, std::move(mirror_res), described, mirrored, origin, generation);
   if (scaled)
      RegisterScaledOriginal(device, texture);
   return true;
}

template <typename Desc, typename Texture>
void Create(ID3D11Device* device, const Desc& desc, const D3D11_SUBRESOURCE_DATA* initial_data, Texture* texture)
{
   const Upgrade::TextureInfo described = Describe(desc);
   const std::optional<Upgrade::UpgradeInfo> upgrade = Upgrade::GetUpgradeInfo(described, Internal::GetFrameState(device),
      {.has_initial_data = initial_data != nullptr, .allow_writable_sr = Chains::GetMode() >= Chains::Mode::Direct, .mirror = true});
   if (!upgrade)
      return;
   const Upgrade::TextureInfo* mirrored = &upgrade->upgraded;
   const Chains::Origin origin = upgrade->resized ? Chains::Origin{Chains::OriginKind::CustomDimensions} : Chains::Origin{Chains::OriginKind::Rule};
   if constexpr (std::is_same_v<Desc, D3D11_TEXTURE2D_DESC1>)
   {
      D3D11_TEXTURE2D_DESC base_desc;
      std::memcpy(&base_desc, &desc, sizeof(base_desc));
      CreateMirror2D(device, texture, base_desc, described, *mirrored, initial_data, origin);
   }
   else
   {
      const std::optional<ConvertedData> data = ConvertInitialData(described, mirrored->format, initial_data);
      ASSERT_ONCE_MSG(!initial_data || data, "A texture matching the upgrade rules has initial data that can't be converted to its mirror's format");
      ComPtr<ID3D11Resource> mirror;
      HRESULT result = E_FAIL;
      if constexpr (std::is_same_v<Desc, D3D11_TEXTURE1D_DESC>)
      {
         const D3D11_TEXTURE1D_DESC mirror_desc = MakeMirrorDesc(desc, *mirrored);
         result = device->CreateTexture1D(&mirror_desc, data ? data->data.data() : nullptr, reinterpret_cast<ID3D11Texture1D**>(mirror.GetAddressOf()));
      }
      else
      {
         D3D11_TEXTURE3D_DESC mirror_desc;
         std::memcpy(&mirror_desc, &desc, sizeof(mirror_desc));
         mirror_desc = MakeMirrorDesc(mirror_desc, *mirrored);
         result = device->CreateTexture3D(&mirror_desc, data ? data->data.data() : nullptr, reinterpret_cast<ID3D11Texture3D**>(mirror.GetAddressOf()));
      }
      if (FAILED(result) || !mirror)
         return LogCreationFailure(std::format("a {} mirror of a {} texture", Utils::Format::GetName(mirrored->format), Utils::Format::GetName(desc.Format)), result);
      AttachMirror<ID3D11Resource>(texture, std::move(mirror), described, *mirrored, origin);
   }
}

// Between PrepareTexture and UpgradeTexture on the same thread (the game's creation call)
struct PendingInPlace
{
   bool active = false;
   Upgrade::TextureInfo original_info;
   Upgrade::TextureInfo info;
   std::optional<ConvertedData> data;
};

thread_local PendingInPlace pending_in_place;

// What changes in place: the format, and for a 2D texture its size and mips
template <typename Desc>
void WriteInPlaceDesc(Desc& desc, const Upgrade::TextureInfo& info)
{
   desc.Format = info.format;
   desc.Width = info.width;
   if constexpr (!std::is_same_v<Desc, D3D11_TEXTURE1D_DESC>)
      desc.Height = info.height;
   desc.MipLevels = info.levels;
}

template <typename Desc>
bool Prepare(ID3D11Device* device, Desc& desc, const D3D11_SUBRESOURCE_DATA*& initial_data)
{
   // A creation that failed left its own
   pending_in_place = {};
   // Opened or read by others in the game's format
   constexpr UINT SHARED = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
   if (desc.MiscFlags & SHARED)
      return false;
   const std::optional<Upgrade::UpgradeInfo> upgrade = Upgrade::GetUpgradeInfo(
      Describe(desc), Internal::GetFrameState(device), {.has_initial_data = initial_data != nullptr, .allow_writable_sr = Chains::GetMode() >= Chains::Mode::Direct});
   if (!upgrade || upgrade->method != Upgrade::Method::InPlace)
      return false;
   std::optional<ConvertedData> data;
   if (initial_data)
   {
      data = ConvertInitialData(upgrade->game, upgrade->upgraded.format, initial_data);
      // The mirror path asserts on it
      if (!data)
         return false;
   }
   WriteInPlaceDesc(desc, upgrade->upgraded);
   pending_in_place = {.active = true, .original_info = upgrade->game, .info = upgrade->upgraded, .data = std::move(data)};
   if (pending_in_place.data)
      initial_data = pending_in_place.data->data.data();
   return true;
}

bool IsPendingMirror(ID3D11Device* device, const ID3D11Resource* resource)
{
   if (!device || !resource)
      return false;
   DeviceMirrorState* dev = Hooks::Lifetime::GetState<DeviceMirrorState>(device);
   if (!dev)
      return false;
   std::scoped_lock lock(dev->registry->mutex);
   return dev->registry->pending_set.contains(resource);
}

// The texture prepared on this thread is upgraded in place, any other may get a mirror
template <typename Desc, typename Texture>
void UpgradeCreatedTexture(ID3D11Device* device, const Desc& desc, const D3D11_SUBRESOURCE_DATA* initial_data, Texture* texture)
{
   if (pending_in_place.active)
   {
      const PendingInPlace pending = std::exchange(pending_in_place, {});
      AttachInPlace(texture, pending.original_info, pending.info);
      return;
   }
   Create(device, desc, initial_data, texture);
}

template <typename Desc>
void SpoofInPlaceDesc(ID3D11Resource* texture, Desc& desc)
{
   if (const Upgrade::TextureInfo* game = FindSpoofedDesc(texture))
      WriteInPlaceDesc(desc, *game);
}
} // namespace

namespace Internal {
std::optional<Upgrade::TextureInfo> DescribeResource(ID3D11Resource* resource)
{
   if (!resource)
      return std::nullopt;
   D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   resource->GetType(&dimension);
   switch (dimension)
   {
   case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
   {
      D3D11_TEXTURE1D_DESC desc;
      static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
      return Describe(desc);
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
   {
      D3D11_TEXTURE2D_DESC desc;
      static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
      return Describe(desc);
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
   {
      D3D11_TEXTURE3D_DESC desc;
      static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
      return Describe(desc);
   }
   default: return std::nullopt;
   }
}

Upgrade::FrameState GetFrameState(ID3D11Device* device)
{
   const FrameState::Resolutions resolutions = FrameState::Read(FrameState::Find(device));
   return {.render = resolutions.render, .output = resolutions.output, .display = resolutions.display};
}

// Creates a chain/seed mirror for an existing 2D texture (immediate context or Present boundary)
bool MaterializeChainMirror(ID3D11Device* device, ID3D11Resource* resource, const Upgrade::TextureInfo& original_info, const Upgrade::TextureInfo& mirror_info,
   Chains::Origin origin)
{
   if (FindInPlace(resource))
      return false;
   if (!device || !resource || FindMirrorState<OwnedResource>(resource))
      return FindMirrorState<OwnedResource>(resource) != nullptr;
   ComPtr<ID3D11Texture2D> texture;
   if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture))))
      return false;
   D3D11_TEXTURE2D_DESC desc;
   texture->GetDesc(&desc);
   return CreateMirror2D(device, texture.Get(), desc, original_info, mirror_info, nullptr, origin);
}

void QueueDeferredChainMirror(ID3D11Device* device, ID3D11Resource* resource, const Upgrade::TextureInfo& original_info, const Upgrade::TextureInfo& mirror_info,
   Chains::Origin origin)
{
   DeviceMirrorState* dev = GetOrCreateDeviceState(device);
   if (!dev || !resource || FindInPlace(resource))
      return;
   std::scoped_lock lock(dev->registry->mutex);
   if (dev->registry->released || !dev->registry->pending_set.insert(resource).second)
      return;
   dev->registry->pending_queue.push_back({resource, original_info, mirror_info, origin});
   Chains::CountDeferredQueued(1);
}
} // namespace Internal

bool PrepareTexture(ID3D11Device* device, D3D11_TEXTURE1D_DESC& desc, const D3D11_SUBRESOURCE_DATA*& initial_data)
{
   return Prepare(device, desc, initial_data);
}

bool PrepareTexture(ID3D11Device* device, D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data)
{
   return Prepare(device, desc, initial_data);
}

bool PrepareTexture(ID3D11Device* device, D3D11_TEXTURE3D_DESC1& desc, const D3D11_SUBRESOURCE_DATA*& initial_data)
{
   return Prepare(device, desc, initial_data);
}

void UpgradeTexture(ID3D11Device* device, const D3D11_TEXTURE1D_DESC& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture1D* texture)
{
   UpgradeCreatedTexture(device, desc, initial_data, texture);
}

void UpgradeTexture(ID3D11Device* device, const D3D11_TEXTURE2D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture2D* texture)
{
   UpgradeCreatedTexture(device, desc, initial_data, texture);
}

void UpgradeTexture(ID3D11Device* device, const D3D11_TEXTURE3D_DESC1& desc, const D3D11_SUBRESOURCE_DATA* initial_data, ID3D11Texture3D* texture)
{
   UpgradeCreatedTexture(device, desc, initial_data, texture);
}

void InvalidateScaledMirrors(ID3D11Device* device)
{
   DeviceMirrorState* dev = Hooks::Lifetime::GetState<DeviceMirrorState>(device);
   if (!dev)
      return;
   std::vector<ID3D11Resource*> originals;
   uint64_t frame = Shaders::GetFrame();
   {
      std::scoped_lock lock(dev->registry->mutex);
      dev->registry->scale_generation++;
      originals.assign(dev->registry->scaled_originals.begin(), dev->registry->scaled_originals.end());
      dev->registry->scaled_originals.clear();
   }
   if (originals.empty())
      return;
   Scaling::CountInvalidation();
   std::vector<RetiredMirror> newly_retired;
   for (ID3D11Resource* original : originals)
   {
      const ResourceMirrorState* state = Hooks::Lifetime::GetState<ResourceMirrorState>(original);
      if (!state || !state->is_scaled)
         continue;
      RetiredMirror entry = {.mirror = state->mirror, .retire_frame = frame};
      ComPtr<ID3D11Texture2D> tex;
      if (SUCCEEDED(state->mirror->QueryInterface(IID_PPV_ARGS(&tex))))
      {
         tex->GetDesc(&entry.desc);
         entry.poolable = true;
      }
      newly_retired.push_back(std::move(entry));
      Hooks::Lifetime::RemoveState<ResourceMirrorState>(original);
      Hooks::Lifetime::RemoveState<ScaledTracker>(original);
   }
   if (!newly_retired.empty())
   {
      std::scoped_lock lock(dev->registry->mutex);
      for (RetiredMirror& entry : newly_retired)
         dev->registry->retired.push_back(std::move(entry));
   }
}

void FlushFrame(ID3D11Device* device)
{
   if (!device)
      return;
   const FrameState::Resolutions res = FrameState::Get(device);
   if (Scaling::IsNeeded() && Scaling::ConsumeResolutionChange(device, res.render, res.output))
      InvalidateScaledMirrors(device);
   Scaling::ResetFrame(device);

   DeviceMirrorState* dev = Hooks::Lifetime::GetState<DeviceMirrorState>(device);
   if (!dev)
      return;

   // Materialize deferred-context chain mirrors at the frame boundary before the next frame records
   std::vector<PendingChainMirror> pending;
   {
      std::scoped_lock lock(dev->registry->mutex);
      pending.swap(dev->registry->pending_queue);
      dev->registry->pending_set.clear();
   }
   if (!pending.empty())
   {
      Chains::CountDeferredQueued(-int32_t(pending.size()));
      for (const PendingChainMirror& req : pending)
      {
         if (Internal::MaterializeChainMirror(device, req.original.Get(), req.original_info, req.mirror_info, req.origin))
         {
            if (req.origin.kind == Chains::OriginKind::DirectChain)
               Chains::CountDirectPropagation();
            else if (req.origin.kind == Chains::OriginKind::IndirectChain)
               Chains::CountIndirectPropagation();
         }
      }
   }

   // Advance retired mirrors into the recycle pool and evict expired pool entries
   const uint64_t frame = Shaders::GetFrame();
   std::vector<ComPtr<ID3D11Resource>> to_release;
   {
      std::scoped_lock lock(dev->registry->mutex);
      for (auto it = dev->registry->retired.begin(); it != dev->registry->retired.end();)
      {
         if (frame >= it->retire_frame + RETIRE_PRESENTS)
         {
            if (it->poolable && !dev->registry->released)
            {
               ComPtr<ID3D11Texture2D> tex;
               if (SUCCEEDED(it->mirror->QueryInterface(IID_PPV_ARGS(&tex))))
               {
                  dev->registry->pool.push_back({std::move(tex), it->desc, frame});
                  Scaling::CountPoolSize(1);
               }
            }
            else
            {
               to_release.push_back(std::move(it->mirror));
            }
            it = dev->registry->retired.erase(it);
         }
         else
         {
            ++it;
         }
      }
      for (auto it = dev->registry->pool.begin(); it != dev->registry->pool.end();)
      {
         if (frame >= it->stored_frame + POOL_TTL_FRAMES)
         {
            to_release.push_back(std::move(it->texture));
            it = dev->registry->pool.erase(it);
            Scaling::CountPoolSize(-1);
         }
         else
         {
            ++it;
         }
      }
   }
}

void ReleaseDeviceObjects(ID3D11Device* device)
{
   DeviceMirrorState* dev = Hooks::Lifetime::GetState<DeviceMirrorState>(device);
   if (!dev)
      return;
   std::vector<PendingChainMirror> pending;
   std::vector<RetiredMirror> retired;
   std::vector<PooledTexture> pool;
   {
      std::scoped_lock lock(dev->registry->mutex);
      dev->registry->released = true;
      dev->registry->scaled_originals.clear();
      dev->registry->pending_set.clear();
      pending.swap(dev->registry->pending_queue);
      retired.swap(dev->registry->retired);
      pool.swap(dev->registry->pool);
   }
   if (!pending.empty())
      Chains::CountDeferredQueued(-int32_t(pending.size()));
   if (!pool.empty())
      Scaling::CountPoolSize(-int32_t(pool.size()));
}

ID3D11Resource* FindMirror(ID3D11Resource* resource)
{
   return Mirrors::FindMirror<OwnedResource>(resource);
}

ID3D11Resource* FindOriginal(ID3D11Resource* mirror)
{
   return Mirrors::FindOriginal(mirror);
}

ID3D11Resource* GetEffectiveResource(ID3D11Resource* resource)
{
   if (!resource || !IsSwapping())
      return resource;
   if (ID3D11Resource* mirror = FindMirror(resource))
      return mirror;
   return resource;
}

std::optional<MirrorSummary> FindMirrorSummary(ID3D11Resource* resource)
{
   if (!resource)
      return std::nullopt;
   if (const ResourceMirrorState* state = FindMirrorState<OwnedResource>(resource))
   {
      return MirrorSummary{
         .original_info = state->original_info,
         .mirror_info = state->info,
         .origin = state->origin,
         .is_scaled = state->is_scaled,
         .pending = false,
      };
   }
   ComPtr<ID3D11Device> device;
   resource->GetDevice(&device);
   if (IsPendingMirror(device.Get(), resource))
   {
      if (const std::optional<Upgrade::TextureInfo> info = Internal::DescribeResource(resource))
         return MirrorSummary{.original_info = *info, .mirror_info = *info, .pending = true};
   }
   return std::nullopt;
}

std::optional<Upgrade::UpgradeInfo> FindUpgrade(ID3D11Resource* resource)
{
   if (const InPlaceState* state = FindInPlace(resource))
      return Upgrade::UpgradeInfo{state->original_info, state->info, Upgrade::Method::InPlace, state->resized};
   if (const ResourceMirrorState* state = FindMirrorState<OwnedResource>(resource))
      return Upgrade::UpgradeInfo{state->original_info, state->info, Upgrade::Method::Mirror,
         state->is_scaled || state->info.levels != state->original_info.levels};
   return std::nullopt;
}

void SpoofDesc(ID3D11Resource* texture, D3D11_TEXTURE1D_DESC& desc)
{
   SpoofInPlaceDesc(texture, desc);
}

void SpoofDesc(ID3D11Resource* texture, D3D11_TEXTURE2D_DESC& desc)
{
   SpoofInPlaceDesc(texture, desc);
}

void SpoofDesc(ID3D11Resource* texture, D3D11_TEXTURE2D_DESC1& desc)
{
   SpoofInPlaceDesc(texture, desc);
}

void SpoofDesc(ID3D11Resource* texture, D3D11_TEXTURE3D_DESC& desc)
{
   SpoofInPlaceDesc(texture, desc);
}

void SpoofDesc(ID3D11Resource* texture, D3D11_TEXTURE3D_DESC1& desc)
{
   SpoofInPlaceDesc(texture, desc);
}

void WriteGameDesc(ID3D11Resource* texture, D3D11_TEXTURE2D_DESC1& desc)
{
   if (const InPlaceState* state = FindInPlace(texture))
      WriteInPlaceDesc(desc, state->original_info);
}
} // namespace D3D11
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
