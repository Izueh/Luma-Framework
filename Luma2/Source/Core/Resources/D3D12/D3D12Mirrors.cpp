#include "Resources/D3D12/D3D12Mirrors.hpp"

#include <algorithm>
#include <atomic>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

#include <wrl/client.h>

#include "Frame/FrameState.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Hooks/HookScope.hpp"
#include "Resources/D3D12/D3D12HeapTail.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"
#include "Shaders/Shaders.hpp"
#include "Utils/Hash.hpp"
#include "Utils/Debug.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Utils/Format.hpp"
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D12 {
namespace {
// Luma's CPU heaps grow by this many descriptors
constexpr uint32_t POOL_CHUNK = 4096;

std::atomic<uint32_t> entry_count = 0;
std::atomic<uint32_t> sampler_count = 0;
std::atomic<uint32_t> swapped_copies = 0;
std::atomic<uint32_t> direct_writes = 0;
std::atomic<uint32_t> converted_copies = 0;
std::atomic<uint32_t> unhandled_copies = 0;
std::atomic<uint32_t> uav_clears = 0;
std::atomic<uint32_t> unhandled_clears = 0;
std::atomic<uint32_t> layout_resources = 0;
std::atomic<uint32_t> reserved_resources = 0;
std::atomic<uint32_t> aliasing_barriers = 0;
std::array<std::atomic<uint32_t>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> increments = {};

using OwnedResource = ComPtr<ID3D12Resource>;
using ResourceMirrorState = MirrorState<ID3D12Resource, OwnedResource>;

// Luma's CPU descriptor heaps of a device, per type, in its private data. A slot goes back to its pool when no game handle holds it.
struct Pool
{
   std::vector<ComPtr<ID3D12DescriptorHeap>> heaps;
   std::vector<uint64_t> free;
   uint32_t used_in_last = POOL_CHUNK;
   uint32_t increment = 0;
};

struct Pools
{
   std::mutex mutex; // After the handles' lock
   std::array<Pool, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> pools;
   bool released = false;
};

struct DeviceState
{
   std::shared_ptr<Pools> pools = std::make_shared<Pools>();
};

// One of Luma's descriptors, shared by every game handle that holds it (copies)
struct Slot
{
   std::shared_ptr<Pools> pools;
   D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   uint64_t handle = 0;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; // A view's
   bool sampler = false;
   bool counted = false; // A view the mirror counts
   // Samplers: the game's desc and the offset generation the mirror was written for
   D3D12_SAMPLER_DESC game_sampler = {};
   uint32_t generation = 0;
   ID3D12Device* device = nullptr; // Not held: the pools hold it

   ~Slot()
   {
      std::scoped_lock lock(pools->mutex);
      if (!pools->released)
         pools->pools[type].free.push_back(handle);
      if (sampler)
         sampler_count--;
      else if (counted)
         CountView(-1);
   }
};

// Game CPU handles (not shader-visible ones: those are never copied from) to the mirror's descriptor they stand for
std::shared_mutex handles_mutex; // Before a pool's lock
std::map<uint64_t, std::shared_ptr<Slot>> handles;

struct HeapRange
{
   uint64_t start = 0;
   uint64_t end = 0;
   bool shader_visible = false;
};

std::shared_mutex heaps_mutex; // Never held with another
std::vector<HeapRange> heaps; // By start

// In the game's heap's private data: its handles are forgotten with it
struct HeapState
{
   HeapRange range;

   ~HeapState()
   {
      {
         std::scoped_lock lock(heaps_mutex);
         std::erase_if(heaps, [&](const HeapRange& other) { return other.start == range.start; });
      }
      if (entry_count.load(std::memory_order_relaxed) == 0)
         return;
      std::unique_lock lock(handles_mutex);
      const auto first = handles.lower_bound(range.start);
      const auto last = handles.lower_bound(range.end);
      entry_count -= uint32_t(std::distance(first, last));
      handles.erase(first, last);
   }
};

bool IsShaderVisible(uint64_t handle)
{
   std::shared_lock lock(heaps_mutex);
   const auto it = std::ranges::upper_bound(heaps, handle, {}, &HeapRange::start);
   if (it == heaps.begin())
      return false;
   const HeapRange& range = *std::prev(it);
   return handle < range.end && range.shader_visible;
}

uint32_t GetIncrement(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   uint32_t increment = increments[type].load(std::memory_order_relaxed);
   if (!increment)
   {
      increment = device->GetDescriptorHandleIncrementSize(type);
      increments[type] = increment;
   }
   return increment;
}

std::shared_ptr<Slot> Allocate(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device);
   if (!state)
      state = Hooks::Lifetime::SetState<DeviceState>(device);
   if (!state)
      return nullptr;
   auto slot = std::make_shared<Slot>();
   slot->pools = state->pools;
   slot->type = type;
   slot->device = device;
   std::scoped_lock lock(state->pools->mutex);
   if (state->pools->released)
      return nullptr;
   Pool& pool = state->pools->pools[type];
   if (!pool.free.empty())
   {
      slot->handle = pool.free.back();
      pool.free.pop_back();
      return slot;
   }
   if (pool.used_in_last == POOL_CHUNK)
   {
      const D3D12_DESCRIPTOR_HEAP_DESC desc = {type, POOL_CHUNK, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
      ComPtr<ID3D12DescriptorHeap> heap;
      Hooks::Scope::Guard scope;
      if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap))))
         return nullptr;
      pool.heaps.push_back(std::move(heap));
      pool.used_in_last = 0;
      pool.increment = GetIncrement(device, type);
   }
   slot->handle = pool.heaps.back()->GetCPUDescriptorHandleForHeapStart().ptr + uint64_t(pool.used_in_last++) * pool.increment;
   return slot;
}

Upgrade::TextureInfo Describe(const D3D12_RESOURCE_DESC& desc, bool gpu_only)
{
   Upgrade::TextureInfo described = {.width = uint32_t(desc.Width),
      .height = desc.Height,
      .depth_or_layers = desc.DepthOrArraySize,
      .levels = desc.MipLevels,
      .samples = desc.SampleDesc.Count,
      .format = desc.Format,
      .render_target = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0,
      .unordered_access = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0,
      .depth_stencil = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0,
      .gpu_only = gpu_only};
   switch (desc.Dimension)
   {
   case D3D12_RESOURCE_DIMENSION_TEXTURE1D: described.dimension = Upgrade::Dimension::Texture1D; break;
   case D3D12_RESOURCE_DIMENSION_TEXTURE3D: described.dimension = Upgrade::Dimension::Texture3D; break;
   default: described.dimension = Upgrade::Dimension::Texture2D; break;
   }
   return described;
}

Upgrade::FrameState GetFrameState(ID3D12Device* device)
{
   const Frame::Resolutions resolutions = Frame::Read(Frame::Find(device));
   return {.render = resolutions.render, .output = resolutions.output, .display = resolutions.display};
}

Upgrade::ViewKind ToUpgradeKind(ViewKind kind)
{
   switch (kind)
   {
   case ViewKind::RenderTarget: return Upgrade::ViewKind::RenderTarget;
   case ViewKind::UnorderedAccess: return Upgrade::ViewKind::UnorderedAccess;
   case ViewKind::DepthStencil: return Upgrade::ViewKind::DepthStencil;
   default: return Upgrade::ViewKind::ShaderResource;
   }
}

template <typename Desc>
Desc MapFormat(const void* desc, ViewKind kind, const ResourceMirrorState& state, DXGI_FORMAT& format)
{
   Desc mapped = *static_cast<const Desc*>(desc);
   const DXGI_FORMAT best = Upgrade::GetBestViewFormat(mapped.Format, ToUpgradeKind(kind), state.original_info.format, state.info.format);
   if (best != DXGI_FORMAT_UNKNOWN)
      mapped.Format = best;
   format = mapped.Format;
   return mapped;
}

DXGI_FORMAT WriteMirrorView(ID3D12Device* device, ID3D12Resource* mirror, ViewKind kind, const void* desc, const ResourceMirrorState& state, uint64_t handle)
{
   DXGI_FORMAT format = state.info.format;
   const D3D12_CPU_DESCRIPTOR_HANDLE destination = {SIZE_T(handle)};
   Hooks::Scope::Guard scope;
   switch (kind)
   {
   case ViewKind::ShaderResource:
   {
      const auto mapped = desc ? std::optional(MapFormat<D3D12_SHADER_RESOURCE_VIEW_DESC>(desc, kind, state, format)) : std::nullopt;
      device->CreateShaderResourceView(mirror, mapped ? &*mapped : nullptr, destination);
      break;
   }
   case ViewKind::UnorderedAccess:
   {
      const auto mapped = desc ? std::optional(MapFormat<D3D12_UNORDERED_ACCESS_VIEW_DESC>(desc, kind, state, format)) : std::nullopt;
      device->CreateUnorderedAccessView(mirror, nullptr, mapped ? &*mapped : nullptr, destination);
      break;
   }
   case ViewKind::RenderTarget:
   {
      const auto mapped = desc ? std::optional(MapFormat<D3D12_RENDER_TARGET_VIEW_DESC>(desc, kind, state, format)) : std::nullopt;
      device->CreateRenderTargetView(mirror, mapped ? &*mapped : nullptr, destination);
      break;
   }
   case ViewKind::DepthStencil:
   {
      const auto mapped = desc ? std::optional(MapFormat<D3D12_DEPTH_STENCIL_VIEW_DESC>(desc, kind, state, format)) : std::nullopt;
      device->CreateDepthStencilView(mirror, mapped ? &*mapped : nullptr, destination);
      break;
   }
   default: return DXGI_FORMAT_UNKNOWN;
   }
   return format;
}

D3D12_DESCRIPTOR_HEAP_TYPE GetHeapType(ViewKind kind)
{
   switch (kind)
   {
   case ViewKind::RenderTarget: return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
   case ViewKind::DepthStencil: return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
   default: return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
   }
}

void RefreshSampler(Slot& slot)
{
   const uint32_t generation = Samplers::GetGeneration();
   if (slot.generation == generation)
      return;
   slot.generation = generation;
   const D3D12_SAMPLER_DESC mirror = Samplers::GetMirrorDesc(slot.game_sampler).value_or(slot.game_sampler);
   Hooks::Scope::Guard scope;
   slot.device->CreateSampler(&mirror, {SIZE_T(slot.handle)});
}

void Forget(uint64_t handle)
{
   if (entry_count.load(std::memory_order_relaxed) == 0)
      return;
   std::unique_lock lock(handles_mutex);
   if (handles.erase(handle))
      entry_count--;
}

void Remember(uint64_t handle, std::shared_ptr<Slot> slot)
{
   std::unique_lock lock(handles_mutex);
   auto [it, added] = handles.insert_or_assign(handle, std::move(slot));
   entry_count += added;
}

std::shared_ptr<Slot> Find(uint64_t handle)
{
   if (entry_count.load(std::memory_order_relaxed) == 0)
      return nullptr;
   std::shared_lock lock(handles_mutex);
   const auto it = handles.find(handle);
   return it != handles.end() ? it->second : nullptr;
}

DXGI_FORMAT GetFormat(ID3D12Resource* resource)
{
   return resource->GetDesc().Format;
}

// What Luma's converting copy changes on a list, as the game set it
struct ListState
{
   ID3D12RootSignature* root_signature = nullptr; // Graphics
   std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets = {};
   UINT render_target_count = 0;
   D3D12_CPU_DESCRIPTOR_HANDLE depth = {};
   bool has_depth = false;
   std::array<D3D12_VIEWPORT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
   UINT viewport_count = 0;
   std::array<D3D12_RECT, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors = {};
   UINT scissor_count = 0;
   D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

struct CopyPipelineEntry
{
   ID3D12RootSignature* root_signature = nullptr;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   std::unique_ptr<Hooks::Lifetime::D3D12::Object> object;
};

struct CopyState
{
   std::mutex mutex; // Never held with another
   bool released = false;
   ComPtr<ID3D12DescriptorHeap> rtv_heap;
   std::shared_ptr<const Shaders::CompiledShader> vertex_shader;
   std::shared_ptr<const Shaders::CompiledShader> pixel_shader;
   std::vector<CopyPipelineEntry> pipelines;
};

std::mutex copy_init_mutex; // Never held with another

CopyState* GetOrCreateCopyState(ID3D12Device* device)
{
   if (CopyState* state = Hooks::Lifetime::GetState<CopyState>(device))
      return state;
   std::scoped_lock lock(copy_init_mutex);
   if (CopyState* state = Hooks::Lifetime::GetState<CopyState>(device))
      return state;
   return Hooks::Lifetime::SetState<CopyState>(device);
}

std::shared_ptr<const Shaders::CompiledShader> GetCompiled(uint64_t name_hash)
{
   const std::optional<uint32_t> index = Database::Find(name_hash);
   return index ? Database::GetShader(*index).compiled : nullptr;
}

// Uses the game's root signature so its root arguments stay valid around the copy
ID3D12PipelineState* GetCopyPipeline(ID3D12GraphicsCommandList* list, ID3D12Device* device, CopyState& copy, ID3D12RootSignature* root_signature, DXGI_FORMAT format)
{
   std::shared_ptr<const Shaders::CompiledShader> vertex_shader = GetCompiled("Luma_Copy_VS"_h);
   std::shared_ptr<const Shaders::CompiledShader> pixel_shader = GetCompiled("Luma_Copy_Table_PS"_h);
   if (!vertex_shader || !pixel_shader)
      return nullptr;
   std::scoped_lock lock(copy.mutex);
   if (copy.released)
      return nullptr;
   if (!copy.rtv_heap)
   {
      Hooks::Scope::Guard scope;
      const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
      if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&copy.rtv_heap))))
         return nullptr;
   }
   if (copy.vertex_shader != vertex_shader || copy.pixel_shader != pixel_shader)
   {
      copy.pipelines.clear();
      copy.vertex_shader = std::move(vertex_shader);
      copy.pixel_shader = std::move(pixel_shader);
   }
   for (const CopyPipelineEntry& entry : copy.pipelines)
   {
      if (entry.root_signature == root_signature && entry.format == format)
         return static_cast<ID3D12PipelineState*>(Hooks::Lifetime::D3D12::Reference(list, *entry.object));
   }
   D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
   desc.pRootSignature = root_signature;
   desc.VS = {copy.vertex_shader->bytecode.data(), copy.vertex_shader->bytecode.size()};
   desc.PS = {copy.pixel_shader->bytecode.data(), copy.pixel_shader->bytecode.size()};
   desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
   desc.SampleMask = UINT_MAX;
   desc.RasterizerState = {D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE, FALSE, D3D12_DEFAULT_DEPTH_BIAS, D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
      D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
   desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
   desc.NumRenderTargets = 1;
   desc.RTVFormats[0] = format;
   desc.SampleDesc = {1, 0};
   ComPtr<ID3D12PipelineState> pipeline;
   {
      Hooks::Scope::Guard scope;
      if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline))))
         Log::Warning("Mirrors: Luma's D3D12 copy pass couldn't be made for {}", Utils::Format::GetName(format));
   }
   CopyPipelineEntry& entry = copy.pipelines.emplace_back(root_signature, format, std::make_unique<Hooks::Lifetime::D3D12::Object>());
   Hooks::Lifetime::D3D12::Replace(*entry.object, std::move(pipeline));
   return static_cast<ID3D12PipelineState*>(Hooks::Lifetime::D3D12::Reference(list, *entry.object));
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, UINT subresource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
   D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
   barrier.Transition = {resource, subresource, before, after};
   return barrier;
}

// A format the copy pass can write (it outputs floats) and read
DXGI_FORMAT GetCopyFormat(DXGI_FORMAT format)
{
   using namespace Utils::Format::DXGI;
   const DXGI_FORMAT typed = IsTypeless(format) ? GetDefaultTyped(format) : format;
   if (typed == DXGI_FORMAT_UNKNOWN || IsDepthStencil(typed) || IsBlockCompressed(typed))
      return DXGI_FORMAT_UNKNOWN;
   const Encoding encoding = GetEncoding(typed);
   return encoding == Encoding::Uint || encoding == Encoding::Sint || encoding == Encoding::Other ? DXGI_FORMAT_UNKNOWN : typed;
}

// Assumes COPY_SOURCE/COPY_DEST (no implicit promotion from COMMON)
bool ConvertingCopy(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source, const CopyRegion& region)
{
   const UINT destination_subresource = region.destination_subresource;
   const UINT source_subresource = region.source_subresource;
   const ListState* state = Hooks::Lifetime::GetState<ListState>(list);
   if (list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT || !state || !state->root_signature)
      return false;
   const D3D12_RESOURCE_DESC destination_desc = destination->GetDesc();
   const D3D12_RESOURCE_DESC source_desc = source->GetDesc();
   const auto single = [](const D3D12_RESOURCE_DESC& desc, UINT subresource) {
      return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.DepthOrArraySize == 1 && desc.SampleDesc.Count == 1 && subresource < desc.MipLevels;
   };
   if (!single(destination_desc, destination_subresource) || !single(source_desc, source_subresource) || !(destination_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
      return false;
   const UINT width = UINT(std::max<UINT64>(destination_desc.Width >> destination_subresource, 1));
   const UINT height = std::max<UINT>(destination_desc.Height >> destination_subresource, 1);
   if (width != std::max<UINT64>(source_desc.Width >> source_subresource, 1) || height != std::max<UINT>(source_desc.Height >> source_subresource, 1))
      return false;
   if (region.whole_resource && (destination_desc.MipLevels != 1 || source_desc.MipLevels != 1))
      return false;
   if (region.offset != std::array<UINT, 3>{} ||
      (region.box && (region.box->left != 0 || region.box->top != 0 || region.box->front != 0 || region.box->right != width || region.box->bottom != height || region.box->back != 1)))
      return false;
   const DXGI_FORMAT target_format = GetCopyFormat(destination_desc.Format);
   const DXGI_FORMAT source_format = GetCopyFormat(source_desc.Format);
   if (target_format == DXGI_FORMAT_UNKNOWN || source_format == DXGI_FORMAT_UNKNOWN)
      return false;
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
      return false;
   CopyState* copy = GetOrCreateCopyState(device.Get());
   ID3D12PipelineState* pipeline = copy ? GetCopyPipeline(list, device.Get(), *copy, state->root_signature, target_format) : nullptr;
   if (!pipeline)
      return false;
   Database::D3D12::TableViews views = {};
   views[0].resource = source;
   views[0].desc = {source_format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
   views[0].desc.Texture2D = {source_subresource, 1};
   if (!Database::D3D12::BindTableViews(list, false, Database::D3D12::NO_TABLE, views))
      return false;

   Hooks::Scope::Guard scope;
   {
      std::scoped_lock lock(copy->mutex);
      if (copy->released || !copy->rtv_heap)
         return false;
      const D3D12_RESOURCE_BARRIER to_draw[] = {Transition(source, source_subresource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
         Transition(destination, destination_subresource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET)};
      list->ResourceBarrier(UINT(std::size(to_draw)), to_draw);
      D3D12_RENDER_TARGET_VIEW_DESC view = {target_format, D3D12_RTV_DIMENSION_TEXTURE2D};
      view.Texture2D.MipSlice = destination_subresource;
      const D3D12_CPU_DESCRIPTOR_HANDLE handle = copy->rtv_heap->GetCPUDescriptorHandleForHeapStart();
      device->CreateRenderTargetView(destination, &view, handle);
      list->OMSetRenderTargets(1, &handle, FALSE, nullptr);
   }
   const D3D12_VIEWPORT viewport = {0.f, 0.f, float(width), float(height), 0.f, 1.f};
   const D3D12_RECT scissor = {0, 0, LONG(width), LONG(height)};
   list->RSSetViewports(1, &viewport);
   list->RSSetScissorRects(1, &scissor);
   list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   list->SetPipelineState(pipeline);
   list->DrawInstanced(4, 1, 0, 0);
   const D3D12_RESOURCE_BARRIER back[] = {Transition(source, source_subresource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
      Transition(destination, destination_subresource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST)};
   list->ResourceBarrier(UINT(std::size(back)), back);

   list->OMSetRenderTargets(state->render_target_count, state->render_target_count ? state->render_targets.data() : nullptr, FALSE,
      state->has_depth ? &state->depth : nullptr);
   if (state->viewport_count)
      list->RSSetViewports(state->viewport_count, state->viewports.data());
   if (state->scissor_count)
      list->RSSetScissorRects(state->scissor_count, state->scissors.data());
   if (state->topology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
      list->IASetPrimitiveTopology(state->topology);
   Shaders::Variants::D3D12::RestorePipeline(list);
   return true;
}
} // namespace

void CreateMirror(ID3D12Device* device, ID3D12Resource* resource, std::optional<D3D12_RESOURCE_STATES> state, const D3D12_CLEAR_VALUE* clear, bool reserved)
{
   const D3D12_RESOURCE_DESC desc = resource->GetDesc();
   if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER || desc.Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
      return;
   // Placed resources have no heap properties of their own: they're GPU only unless their heap says otherwise, which nothing renders into
   D3D12_HEAP_PROPERTIES properties = {D3D12_HEAP_TYPE_DEFAULT};
   resource->GetHeapProperties(&properties, nullptr);
   const Upgrade::TextureInfo described = Describe(desc, properties.Type == D3D12_HEAP_TYPE_DEFAULT);
   const std::optional<Upgrade::TextureInfo> mirrored = Upgrade::Match(described, GetFrameState(device), false);
   if (!mirrored)
      return;
   if (reserved)
   {
      reserved_resources++;
      LUMA_UNIMPLEMENTED("Mirrors of D3D12 reserved resources");
      return;
   }
   if (!state)
   {
      layout_resources++;
      LUMA_UNIMPLEMENTED("Mirrors of D3D12 resources created with an enhanced barrier layout");
      return;
   }
   D3D12_RESOURCE_DESC mirror_desc = desc;
   mirror_desc.Format = mirrored->format;
   mirror_desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
   std::optional<D3D12_CLEAR_VALUE> mirror_clear;
   if (clear && !mirrored->depth_stencil)
   {
      mirror_clear = *clear;
      mirror_clear->Format = Utils::Format::DXGI::IsTypeless(mirrored->format) ? Utils::Format::DXGI::GetDefaultTyped(mirrored->format) : mirrored->format;
   }
   const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
   ComPtr<ID3D12Resource> mirror;
   HRESULT result = E_FAIL;
   {
      Hooks::Scope::Guard scope;
      result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &mirror_desc, *state, mirror_clear ? &*mirror_clear : nullptr, IID_PPV_ARGS(&mirror));
   }
   if (FAILED(result) || !mirror)
      return LogCreationFailure(std::format("a {} mirror of a {} texture", Utils::Format::GetName(mirrored->format), Utils::Format::GetName(desc.Format)), result);
   AttachMirror(resource, std::move(mirror), described, *mirrored);
}

ID3D12Resource* FindMirror(ID3D12Resource* resource)
{
   return Mirrors::FindMirror<OwnedResource>(resource);
}

ID3D12Resource* FindOriginal(ID3D12Resource* mirror)
{
   return Mirrors::FindOriginal(mirror);
}

void ReleaseDeviceObjects(ID3D12Device* device)
{
   if (DeviceState* state = Hooks::Lifetime::GetState<DeviceState>(device))
   {
      std::scoped_lock lock(state->pools->mutex);
      state->pools->released = true;
      for (Pool& pool : state->pools->pools)
      {
         pool.heaps.clear();
         pool.free.clear();
      }
   }
   if (CopyState* copy = Hooks::Lifetime::GetState<CopyState>(device))
   {
      std::scoped_lock lock(copy->mutex);
      copy->released = true;
      copy->rtv_heap.Reset();
      copy->pipelines.clear();
   }
}

bool HasMirrors()
{
   return HasAnyMirrors() || entry_count.load(std::memory_order_relaxed) != 0;
}

void TrackHeap(ID3D12Device* device, ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC& desc)
{
   const uint64_t start = heap->GetCPUDescriptorHandleForHeapStart().ptr;
   const HeapRange range = {start, start + uint64_t(desc.NumDescriptors) * GetIncrement(device, desc.Type), (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0};
   if (!Hooks::Lifetime::SetState<HeapState>(heap, range))
      return;
   std::scoped_lock lock(heaps_mutex);
   heaps.insert(std::ranges::upper_bound(heaps, range.start, {}, &HeapRange::start), range);
}

void TrackView(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, ViewKind kind, ID3D12Resource* resource, const void* desc, bool swapping)
{
   const ResourceMirrorState* state = kind != ViewKind::ConstantBuffer ? FindMirrorState<OwnedResource>(resource) : nullptr;
   if (!state)
      return Forget(destination.ptr);
   if (IsShaderVisible(destination.ptr))
   {
      if (swapping)
      {
         WriteMirrorView(device, state->mirror.Get(), kind, desc, *state, destination.ptr);
         if (direct_writes++ == 0)
            ASSERT_ONCE_MSG(false, "A view of a mirrored texture was written straight into a shader-visible heap: it won't follow a toggle");
      }
      return;
   }
   std::shared_ptr<Slot> slot = Allocate(device, GetHeapType(kind));
   if (!slot)
      return Forget(destination.ptr);
   slot->format = WriteMirrorView(device, state->mirror.Get(), kind, desc, *state, slot->handle);
   slot->counted = true;
   CountView(1);
   Remember(destination.ptr, std::move(slot));
}

void TrackSampler(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, const D3D12_SAMPLER_DESC& desc, bool swapping)
{
   const std::optional<D3D12_SAMPLER_DESC> mirror = Samplers::GetMirrorDesc(desc);
   if (!mirror)
      return Forget(destination.ptr);
   if (IsShaderVisible(destination.ptr))
   {
      if (swapping)
      {
         Hooks::Scope::Guard scope;
         device->CreateSampler(&*mirror, destination);
         direct_writes++;
      }
      return;
   }
   std::shared_ptr<Slot> slot = Allocate(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
   if (!slot)
      return Forget(destination.ptr);
   slot->sampler = true;
   slot->game_sampler = desc;
   slot->generation = Samplers::GetGeneration();
   sampler_count++;
   {
      Hooks::Scope::Guard scope;
      device->CreateSampler(&*mirror, {SIZE_T(slot->handle)});
   }
   Remember(destination.ptr, std::move(slot));
}

void SwapCopiedDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type,
   bool swap_views, bool swap_samplers)
{
   if (entry_count.load(std::memory_order_relaxed) == 0 || !count)
      return;
   const uint64_t increment = GetIncrement(device, type);
   const uint64_t source_end = source.ptr + count * increment;
   const uint64_t destination_end = destination.ptr + count * increment;
   const bool shader_visible = IsShaderVisible(destination.ptr);
   thread_local std::vector<std::pair<uint64_t, std::shared_ptr<Slot>>> copied;
   copied.clear();
   {
      std::shared_lock lock(handles_mutex);
      for (auto it = handles.lower_bound(source.ptr); it != handles.end() && it->first < source_end; ++it)
         copied.emplace_back(destination.ptr + (it->first - source.ptr), it->second);
      // A shader-visible heap is never copied from: only what it shows matters
      if (!shader_visible && copied.empty())
      {
         const auto first = handles.lower_bound(destination.ptr);
         if (first == handles.end() || first->first >= destination_end)
            return;
      }
   }
   if (shader_visible)
   {
      Hooks::Scope::Guard scope;
      for (auto& [handle, slot] : copied)
      {
         if (!(slot->sampler ? swap_samplers : swap_views))
            continue;
         if (slot->sampler)
            RefreshSampler(*slot);
         device->CopyDescriptorsSimple(1, {SIZE_T(handle)}, {SIZE_T(slot->handle)}, type);
         swapped_copies++;
      }
      copied.clear();
      return;
   }
   std::unique_lock lock(handles_mutex);
   const auto first = handles.lower_bound(destination.ptr);
   const auto last = handles.lower_bound(destination_end);
   entry_count -= uint32_t(std::distance(first, last));
   handles.erase(first, last);
   for (auto& [handle, slot] : copied)
      handles.emplace(handle, std::move(slot));
   entry_count += uint32_t(copied.size());
   copied.clear();
}

std::span<const D3D12_RESOURCE_BARRIER> AddMirrorBarriers(std::span<const D3D12_RESOURCE_BARRIER> barriers)
{
   if (!HasAnyMirrors())
      return barriers;
   thread_local std::vector<D3D12_RESOURCE_BARRIER> extended;
   extended.clear();
   for (const D3D12_RESOURCE_BARRIER& barrier : barriers)
   {
      D3D12_RESOURCE_BARRIER mirrored = barrier;
      switch (barrier.Type)
      {
      case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
         if ((mirrored.Transition.pResource = FindMirror(barrier.Transition.pResource)))
            extended.push_back(mirrored);
         break;
      case D3D12_RESOURCE_BARRIER_TYPE_UAV:
         if ((mirrored.UAV.pResource = FindMirror(barrier.UAV.pResource)))
            extended.push_back(mirrored);
         break;
      default:
         // Mirrors are committed: they alias nothing
         if (FindMirror(barrier.Aliasing.pResourceBefore) || FindMirror(barrier.Aliasing.pResourceAfter))
            aliasing_barriers++;
         break;
      }
   }
   if (extended.empty())
      return barriers;
   extended.insert(extended.begin(), barriers.begin(), barriers.end());
   return extended;
}

void SwapRenderTargets(UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE*& render_targets, BOOL& single_range, const D3D12_CPU_DESCRIPTOR_HANDLE*& depth_stencil,
   Shaders::Database::D3D12::TargetFormats& formats)
{
   formats = {};
   if (entry_count.load(std::memory_order_relaxed) == 0)
      return;
   if (depth_stencil)
   {
      if (const std::shared_ptr<Slot> slot = Find(depth_stencil->ptr))
      {
         thread_local D3D12_CPU_DESCRIPTOR_HANDLE depth;
         depth.ptr = SIZE_T(slot->handle);
         depth_stencil = &depth;
         formats.depth = slot->format;
      }
   }
   if (!render_targets)
      return;
   thread_local std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> swapped;
   const uint64_t increment = increments[D3D12_DESCRIPTOR_HEAP_TYPE_RTV].load(std::memory_order_relaxed);
   bool any = false;
   count = std::min<UINT>(count, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
   for (UINT i = 0; i < count; i++)
   {
      swapped[i] = single_range ? D3D12_CPU_DESCRIPTOR_HANDLE{SIZE_T(render_targets->ptr + i * increment)} : render_targets[i];
      if (const std::shared_ptr<Slot> slot = Find(swapped[i].ptr))
      {
         swapped[i].ptr = SIZE_T(slot->handle);
         formats.render_targets[i] = slot->format;
         any = true;
      }
   }
   if (!any)
      return;
   render_targets = swapped.data();
   single_range = FALSE;
}

bool SwapHandle(D3D12_CPU_DESCRIPTOR_HANDLE& handle)
{
   const std::shared_ptr<Slot> slot = Find(handle.ptr);
   if (!slot || slot->sampler)
      return false;
   handle.ptr = SIZE_T(slot->handle);
   return true;
}

bool SwapCopy(ID3D12GraphicsCommandList* list, ID3D12Resource*& destination, ID3D12Resource*& source, const CopyRegion& region)
{
   const CopyPlan<ID3D12Resource> plan = PlanCopy(destination, source, &FindMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return false;
   if (plan.action == CopyAction::DirectMirrorCopy)
   {
      destination = plan.destination;
      source = plan.source;
      return false;
   }
   // A footprint side (an upload or a readback) is a buffer
   if (!source || !destination)
      return false;
   if (region.convertible && ConvertingCopy(list, plan.destination, plan.source, region))
   {
      converted_copies++;
      return true;
   }
   if (unhandled_copies++ == 0)
      Log::Warning("Mirrors: a copy between a {} mirror and a {} texture without one couldn't run through Luma's copy pass on D3D12, the game's textures are copied",
         Utils::Format::GetName(GetFormat(plan.destination_mirrored ? plan.destination : plan.source)),
         Utils::Format::GetName(GetFormat(plan.destination_mirrored ? source : destination)));
   return false;
}

void SwapClearUnorderedAccess(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE& gpu, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, ID3D12Resource*& resource)
{
   ID3D12Resource* mirror = FindMirror(resource);
   if (!mirror)
      return;
   // The CPU handle must be in a heap that isn't shader-visible, the GPU one in the bound heap: the mirror's view goes into its ring
   const std::shared_ptr<Slot> slot = Find(cpu.ptr);
   ID3D12DescriptorHeap* heap = Database::D3D12::GetBoundHeap(list);
   const std::optional<HeapTail::D3D12::RingRange> range = slot && heap ? HeapTail::D3D12::AllocateRing(list, heap, 1) : std::nullopt;
   if (!range)
   {
      unhandled_clears++;
      LUMA_UNIMPLEMENTED("UAV clears of mirrored textures on D3D12 without the mirror's view or a ring in the bound heap");
      return;
   }
   {
      Hooks::Scope::Guard scope;
      slot->device->CopyDescriptorsSimple(1, range->cpu, {SIZE_T(slot->handle)}, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   }
   gpu = range->gpu;
   cpu.ptr = SIZE_T(slot->handle);
   resource = mirror;
   uav_clears++;
}

void RecordRootSignature(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root_signature)
{
   if (ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list))
      state->root_signature = root_signature;
}

void RecordRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil)
{
   ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
   if (!state)
      return;
   count = render_targets ? std::min<UINT>(count, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) : 0;
   uint64_t increment = increments[D3D12_DESCRIPTOR_HEAP_TYPE_RTV].load(std::memory_order_relaxed);
   if (single_range && count > 1 && !increment)
   {
      ComPtr<ID3D12Device> device;
      if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))))
         increment = GetIncrement(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
   }
   for (UINT i = 0; i < count; i++)
      state->render_targets[i] = single_range ? D3D12_CPU_DESCRIPTOR_HANDLE{SIZE_T(render_targets->ptr + i * increment)} : render_targets[i];
   state->render_target_count = count;
   state->has_depth = depth_stencil != nullptr;
   state->depth = depth_stencil ? *depth_stencil : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

void RecordViewports(ID3D12GraphicsCommandList* list, UINT count, const D3D12_VIEWPORT* viewports)
{
   ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
   if (!state || !viewports)
      return;
   state->viewport_count = std::min<UINT>(count, UINT(state->viewports.size()));
   std::copy_n(viewports, state->viewport_count, state->viewports.begin());
}

void RecordScissorRects(ID3D12GraphicsCommandList* list, UINT count, const D3D12_RECT* rects)
{
   ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
   if (!state || !rects)
      return;
   state->scissor_count = std::min<UINT>(count, UINT(state->scissors.size()));
   std::copy_n(rects, state->scissor_count, state->scissors.begin());
}

void RecordTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology)
{
   if (ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list))
      state->topology = topology;
}

void ForgetListState(ID3D12GraphicsCommandList* list)
{
   if (ListState* state = Hooks::Lifetime::GetOrCreateStateCached<ListState>(list))
      *state = {};
}

Stats GetStats()
{
   return {entry_count.load(), sampler_count.load(), swapped_copies.load(), direct_writes.load(), converted_copies.load(), unhandled_copies.load(), uav_clears.load(),
      unhandled_clears.load(), layout_resources.load(), reserved_resources.load(), aliasing_barriers.load()};
}
} // namespace D3D12
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
