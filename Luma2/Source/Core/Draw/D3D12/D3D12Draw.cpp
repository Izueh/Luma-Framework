#include "Draw/D3D12/D3D12Draw.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <wrl/client.h>

#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Log/Log.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"
#include "Shaders/ShaderStore.hpp"
#include "Shaders/Shaders.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Utils/Format.hpp"
#include "Utils/Hash.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Draw {
namespace D3D12 {
namespace {
std::atomic<uint32_t> cached_rtv_increment = 0;

struct CopyPipelineEntry
{
   ID3D12RootSignature* root_signature = nullptr;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   bool scale = false;
   std::unique_ptr<Hooks::Lifetime::D3D12::Object> object;
};

struct CopyState
{
   std::mutex mutex; // Never held with another
   bool released = false;
   ComPtr<ID3D12DescriptorHeap> rtv_heap;
   std::shared_ptr<const Shaders::CompiledShader> copy_vs;
   std::shared_ptr<const Shaders::CompiledShader> copy_ps;
   std::shared_ptr<const Shaders::CompiledShader> scale_vs;
   std::shared_ptr<const Shaders::CompiledShader> scale_ps;
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

std::shared_ptr<const Shaders::CompiledShader> GetCompiled(ID3D12Device* device, uint64_t name_hash)
{
   const std::optional<uint32_t> index = Resources::Database::Find(name_hash);
   const std::shared_ptr<Shaders::Store> store = Shaders::GetStore(device);
   return index && store ? Resources::Database::GetShader(*store, *index).compiled : nullptr;
}

ID3D12PipelineState* GetCopyPipeline(ID3D12GraphicsCommandList* list, ID3D12Device* device, CopyState& copy, ID3D12RootSignature* root_signature, DXGI_FORMAT format,
   bool scale)
{
   std::shared_ptr<const Shaders::CompiledShader> vertex_shader = GetCompiled(device, scale ? "Luma_Scale_VS"_h : "Luma_Copy_VS"_h);
   std::shared_ptr<const Shaders::CompiledShader> pixel_shader = GetCompiled(device, scale ? "Luma_Scale_Table_PS"_h : "Luma_Copy_Table_PS"_h);
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
   auto& cached_vs = scale ? copy.scale_vs : copy.copy_vs;
   auto& cached_ps = scale ? copy.scale_ps : copy.copy_ps;
   if (cached_vs != vertex_shader || cached_ps != pixel_shader)
   {
      std::erase_if(copy.pipelines, [scale](const CopyPipelineEntry& entry) { return entry.scale == scale; });
      cached_vs = std::move(vertex_shader);
      cached_ps = std::move(pixel_shader);
   }
   for (const CopyPipelineEntry& entry : copy.pipelines)
   {
      if (entry.root_signature == root_signature && entry.format == format && entry.scale == scale)
         return static_cast<ID3D12PipelineState*>(Hooks::Lifetime::D3D12::Reference(list, *entry.object));
   }
   D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
   desc.pRootSignature = root_signature;
   desc.VS = {cached_vs->bytecode.data(), cached_vs->bytecode.size()};
   desc.PS = {cached_ps->bytecode.data(), cached_ps->bytecode.size()};
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
         Log::Warning("Draw: Luma's D3D12 {} pass couldn't be made for {}", scale ? "scale" : "copy", Utils::Format::GetName(format));
   }
   CopyPipelineEntry& entry = copy.pipelines.emplace_back(root_signature, format, scale, std::make_unique<Hooks::Lifetime::D3D12::Object>());
   Hooks::Lifetime::D3D12::Replace(*entry.object, std::move(pipeline));
   return static_cast<ID3D12PipelineState*>(Hooks::Lifetime::D3D12::Reference(list, *entry.object));
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, UINT subresource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
   D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
   barrier.Transition = {resource, subresource, before, after};
   return barrier;
}

DXGI_FORMAT GetCopyFormat(DXGI_FORMAT format)
{
   using namespace Utils::Format::DXGI;
   const DXGI_FORMAT typed = IsTypeless(format) ? GetDefaultTyped(format) : format;
   if (typed == DXGI_FORMAT_UNKNOWN || IsDepthStencil(typed) || IsBlockCompressed(typed))
      return DXGI_FORMAT_UNKNOWN;
   const Encoding encoding = GetEncoding(typed);
   return encoding == Encoding::Uint || encoding == Encoding::Sint || encoding == Encoding::Other ? DXGI_FORMAT_UNKNOWN : typed;
}

bool Blit(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source, const CopyOptions& options, bool scale)
{
   if (!list || !destination || !source)
      return false;
   const Recorded* state = Hooks::Lifetime::GetState<Recorded>(list);
   if (list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT || !state || !state->root_signature)
      return false;
   const D3D12_RESOURCE_DESC destination_desc = destination->GetDesc();
   const D3D12_RESOURCE_DESC source_desc = source->GetDesc();
   const auto single = [](const D3D12_RESOURCE_DESC& desc, UINT subresource) {
      return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.DepthOrArraySize == 1 && desc.SampleDesc.Count == 1 && subresource < desc.MipLevels;
   };
   if (!single(destination_desc, options.destination_subresource) || !single(source_desc, options.source_subresource) ||
      !(destination_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
      return false;
   const DXGI_FORMAT target_format = GetCopyFormat(destination_desc.Format);
   const DXGI_FORMAT source_format = GetCopyFormat(source_desc.Format);
   if (target_format == DXGI_FORMAT_UNKNOWN || source_format == DXGI_FORMAT_UNKNOWN)
      return false;
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
      return false;
   CopyState* copy = GetOrCreateCopyState(device.Get());
   ID3D12PipelineState* pipeline = copy ? GetCopyPipeline(list, device.Get(), *copy, state->root_signature, target_format, scale) : nullptr;
   if (!pipeline)
      return false;
   Resources::Database::D3D12::TableViews views = {};
   views[0].resource = source;
   views[0].desc = {source_format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
   views[0].desc.Texture2D = {options.source_subresource, 1};
   if (!Resources::Database::D3D12::BindTableViews(list, false, Resources::Database::D3D12::NO_TABLE, views))
      return false;

   const UINT dst_width = UINT(std::max<UINT64>(destination_desc.Width >> options.destination_subresource, 1));
   const UINT dst_height = std::max<UINT>(destination_desc.Height >> options.destination_subresource, 1);

   Hooks::Scope::Guard scope;
   {
      std::scoped_lock lock(copy->mutex);
      if (copy->released || !copy->rtv_heap)
         return false;
      std::array<D3D12_RESOURCE_BARRIER, 2> to_draw = {};
      UINT barrier_count = 0;
      if (options.source_state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
         to_draw[barrier_count++] = Transition(source, options.source_subresource, options.source_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      if (options.destination_state != D3D12_RESOURCE_STATE_RENDER_TARGET)
         to_draw[barrier_count++] = Transition(destination, options.destination_subresource, options.destination_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
      if (barrier_count)
         list->ResourceBarrier(barrier_count, to_draw.data());
      D3D12_RENDER_TARGET_VIEW_DESC view = {target_format, D3D12_RTV_DIMENSION_TEXTURE2D};
      view.Texture2D.MipSlice = options.destination_subresource;
      const D3D12_CPU_DESCRIPTOR_HANDLE handle = copy->rtv_heap->GetCPUDescriptorHandleForHeapStart();
      device->CreateRenderTargetView(destination, &view, handle);
      list->OMSetRenderTargets(1, &handle, FALSE, nullptr);
   }
   const D3D12_VIEWPORT viewport = {0.f, 0.f, float(dst_width), float(dst_height), 0.f, 1.f};
   const D3D12_RECT scissor = {0, 0, LONG(dst_width), LONG(dst_height)};
   list->RSSetViewports(1, &viewport);
   list->RSSetScissorRects(1, &scissor);
   list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   list->SetPipelineState(pipeline);
   list->DrawInstanced(4, 1, 0, 0);

   std::array<D3D12_RESOURCE_BARRIER, 2> back = {};
   UINT back_count = 0;
   if (options.source_state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
      back[back_count++] = Transition(source, options.source_subresource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, options.source_state);
   if (options.destination_state != D3D12_RESOURCE_STATE_RENDER_TARGET)
      back[back_count++] = Transition(destination, options.destination_subresource, D3D12_RESOURCE_STATE_RENDER_TARGET, options.destination_state);
   if (back_count)
      list->ResourceBarrier(back_count, back.data());

   Restore(list);
   return true;
}
} // namespace

const Recorded* GetRecorded(ID3D12GraphicsCommandList* list)
{
   return list ? Hooks::Lifetime::GetState<Recorded>(list) : nullptr;
}

void RecordRootSignature(ID3D12GraphicsCommandList* list, ID3D12RootSignature* root_signature)
{
   if (Recorded* state = list ? Hooks::Lifetime::GetOrCreateStateCached<Recorded>(list) : nullptr)
      state->root_signature = root_signature;
}

void RecordRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil)
{
   Recorded* state = list ? Hooks::Lifetime::GetOrCreateStateCached<Recorded>(list) : nullptr;
   if (!state)
      return;
   count = render_targets ? std::min<UINT>(count, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) : 0;
   uint32_t increment = cached_rtv_increment.load(std::memory_order_relaxed);
   if (single_range && count > 1 && !increment)
   {
      ComPtr<ID3D12Device> device;
      if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))))
      {
         increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
         cached_rtv_increment.store(increment, std::memory_order_relaxed);
      }
   }
   for (UINT i = 0; i < count; i++)
      state->render_targets[i] = single_range ? D3D12_CPU_DESCRIPTOR_HANDLE{SIZE_T(render_targets->ptr + i * increment)} : render_targets[i];
   state->render_target_count = count;
   state->has_depth = depth_stencil != nullptr;
   state->depth = depth_stencil ? *depth_stencil : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

void RecordViewports(ID3D12GraphicsCommandList* list, UINT count, const D3D12_VIEWPORT* viewports)
{
   Recorded* state = list ? Hooks::Lifetime::GetOrCreateStateCached<Recorded>(list) : nullptr;
   if (!state || !viewports)
      return;
   const UINT clamped = std::min<UINT>(count, UINT(state->viewports.size()));
   state->viewport_count = clamped;
   std::copy_n(viewports, clamped, state->viewports.begin());
}

void RecordScissorRects(ID3D12GraphicsCommandList* list, UINT count, const D3D12_RECT* rects)
{
   Recorded* state = list ? Hooks::Lifetime::GetOrCreateStateCached<Recorded>(list) : nullptr;
   if (!state || !rects)
      return;
   const UINT clamped = std::min<UINT>(count, UINT(state->scissors.size()));
   state->scissor_count = clamped;
   std::copy_n(rects, clamped, state->scissors.begin());
}

void RecordTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology)
{
   if (Recorded* state = list ? Hooks::Lifetime::GetOrCreateStateCached<Recorded>(list) : nullptr)
      state->topology = topology;
}

void ForgetList(ID3D12GraphicsCommandList* list)
{
   if (Recorded* state = list ? Hooks::Lifetime::GetOrCreateStateCached<Recorded>(list) : nullptr)
      *state = {};
}

void Restore(ID3D12GraphicsCommandList* list)
{
   if (!list)
      return;
   if (const Recorded* state = Hooks::Lifetime::GetState<Recorded>(list))
   {
      list->OMSetRenderTargets(state->render_target_count, state->render_target_count ? state->render_targets.data() : nullptr, FALSE,
         state->has_depth ? &state->depth : nullptr);
      if (state->viewport_count)
         list->RSSetViewports(state->viewport_count, state->viewports.data());
      if (state->scissor_count)
         list->RSSetScissorRects(state->scissor_count, state->scissors.data());
      if (state->topology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
         list->IASetPrimitiveTopology(state->topology);
   }
   Shaders::Variants::D3D12::RestorePipeline(list);
}

void ReleaseDeviceObjects(ID3D12Device* device)
{
   if (CopyState* copy = Hooks::Lifetime::GetState<CopyState>(device))
   {
      std::scoped_lock lock(copy->mutex);
      copy->released = true;
      copy->rtv_heap.Reset();
      copy->pipelines.clear();
   }
}

bool Copy(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source, const CopyOptions& options)
{
   return Blit(list, destination, source, options, false);
}

bool Scale(ID3D12GraphicsCommandList* list, ID3D12Resource* destination, ID3D12Resource* source, const CopyOptions& options)
{
   return Blit(list, destination, source, options, true);
}

ID3D12Resource* CreateTemporary(ID3D12GraphicsCommandList* list, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state)
{
   if (!list)
      return nullptr;
   ComPtr<ID3D12Device> device;
   if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))))
      return nullptr;
   const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
   ComPtr<ID3D12Resource> resource;
   {
      Hooks::Scope::Guard scope;
      if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource))))
         return nullptr;
   }
   Hooks::Lifetime::D3D12::Object holder;
   Hooks::Lifetime::D3D12::Replace(holder, resource);
   return static_cast<ID3D12Resource*>(Hooks::Lifetime::D3D12::Reference(list, holder));
}

ID3D12Resource* CreateTemporary2D(ID3D12GraphicsCommandList* list, DXGI_FORMAT format, uint32_t width, uint32_t height, D3D12_RESOURCE_FLAGS flags,
   D3D12_RESOURCE_STATES state)
{
   const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
      .Width = width,
      .Height = height,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = format,
      .SampleDesc = {1, 0},
      .Flags = flags};
   return CreateTemporary(list, desc, state);
}
} // namespace D3D12
} // namespace Draw
} // namespace Luma
