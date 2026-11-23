#include "Display/D3D12/D3D12Composition.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include <wrl/client.h>

#include "Frame/FrameState.hpp"
#include "Game/Game.hpp"
#include "Resources/ResourceBindings.hpp"
#include "Resources/ResourceDatabase.hpp"
#if DEVELOPMENT
#include "Trace/DebugDraw.hpp"
#endif
#include "Utils/Debug.hpp"
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Display {
namespace Composition {
namespace D3D12 {
// Frames in flight: a frame's allocator, views and cbuffers are reused once its fence passed
constexpr uint32_t FRAMES = 3;
// t0 the game's image, t1 the UI, t2 to t8 the debug draw's (Luma_DisplayComposition.hlsl)
constexpr uint32_t VIEWS = 9;
// What one frame's cbuffers take: three entries at most, 256 byte aligned
constexpr uint32_t CBUFFER_SLICE = 4 * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT * 4;

struct FrameSlot
{
   ComPtr<ID3D12CommandAllocator> allocator;
   uint64_t fence_value = 0;
};

// The pipeline of one build of the shaders, for one target format
struct Pipeline
{
   std::shared_ptr<const Shaders::CompiledShader> vertex_shader;
   std::shared_ptr<const Shaders::CompiledShader> pixel_shader;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   ComPtr<ID3D12RootSignature> signature;
   ComPtr<ID3D12PipelineState> state;
   // The cbuffer entries, in the order of their root parameters (1 on)
   std::vector<uint32_t> cbuffers;
};

struct Renderer
{
   ComPtr<ID3D12Device> device;
   ComPtr<ID3D12CommandQueue> queue;
   std::array<FrameSlot, FRAMES> frames;
   uint32_t frame = 0;
   ComPtr<ID3D12GraphicsCommandList> list;
   ComPtr<ID3D12Fence> fence;
   uint64_t fence_value = 0;
   HANDLE event = nullptr;
   ComPtr<ID3D12DescriptorHeap> views; // Shader visible, VIEWS per frame
   ComPtr<ID3D12DescriptorHeap> targets; // One per frame
   uint32_t view_increment = 0;
   uint32_t target_increment = 0;
   ComPtr<ID3D12Resource> upload; // CBUFFER_SLICE per frame, mapped
   std::byte* upload_data = nullptr;
   Pipeline pipeline;
   // In place: the copy of the real buffer composition reads
   ComPtr<ID3D12Resource> copy;
   bool failed_logged = false;
};

namespace {
void Wait(Renderer& renderer, uint64_t value)
{
   if (renderer.fence->GetCompletedValue() >= value)
      return;
   if (SUCCEEDED(renderer.fence->SetEventOnCompletion(value, renderer.event)) && WaitForSingleObject(renderer.event, 2000) == WAIT_TIMEOUT)
      Log::Warning("Display: composition's D3D12 work hasn't finished after 2s");
}

PFN_D3D12_SERIALIZE_ROOT_SIGNATURE GetSerialize()
{
   // Loaded: the game presents with it
   static const auto serialize = [] {
      const HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
      return d3d12 ? reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(GetProcAddress(d3d12, "D3D12SerializeRootSignature")) : nullptr;
   }();
   return serialize;
}

// The views in space 0 (the shader's own registers), its cbuffers at the registers they request, in Luma's space as compiled
bool CreatePipeline(Renderer& renderer, Pipeline& pipeline)
{
   const D3D12_DESCRIPTOR_RANGE range = {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = VIEWS};
   std::vector<D3D12_ROOT_PARAMETER> parameters = {{.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
      .DescriptorTable = {1, &range},
      .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL}};
   pipeline.cbuffers.clear();
   const uint32_t space = Resources::Bindings::GetOptions().d3d12_space;
   for (const Shaders::CompiledShader* shader : {pipeline.vertex_shader.get(), pipeline.pixel_shader.get()})
   {
      for (const Shaders::Request& request : shader->requests)
      {
         if (Resources::Database::Get(request.entry).kind != Resources::Database::Kind::ConstantBuffer || std::ranges::contains(pipeline.cbuffers, request.entry))
            continue;
         pipeline.cbuffers.push_back(request.entry);
         parameters.push_back({.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {request.slot, space}, .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL});
      }
   }
   ASSERT_ONCE_MSG(pipeline.cbuffers.size() <= 4, "Display composition reads more cbuffers than its slices hold");
   const D3D12_STATIC_SAMPLER_DESC sampler = {
      .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
      .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      .ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
      .MaxLOD = D3D12_FLOAT32_MAX,
      .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
   };
   const D3D12_ROOT_SIGNATURE_DESC signature_desc = {
      .NumParameters = UINT(parameters.size()),
      .pParameters = parameters.data(),
      .NumStaticSamplers = 1,
      .pStaticSamplers = &sampler,
   };
   ComPtr<ID3DBlob> blob;
   ComPtr<ID3DBlob> errors;
   const PFN_D3D12_SERIALIZE_ROOT_SIGNATURE serialize = GetSerialize();
   if (!serialize || FAILED(serialize(&signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)) ||
      FAILED(renderer.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&pipeline.signature))))
   {
      Log::Error("Display: composition's D3D12 root signature couldn't be created{}", errors ? std::string(": ") + static_cast<const char*>(errors->GetBufferPointer()) : "");
      return false;
   }

   D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
   desc.pRootSignature = pipeline.signature.Get();
   desc.VS = {pipeline.vertex_shader->bytecode.data(), pipeline.vertex_shader->bytecode.size()};
   desc.PS = {pipeline.pixel_shader->bytecode.data(), pipeline.pixel_shader->bytecode.size()};
   desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
   desc.SampleMask = UINT_MAX;
   desc.RasterizerState = {D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE, FALSE, D3D12_DEFAULT_DEPTH_BIAS, D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
      D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
   desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
   desc.NumRenderTargets = 1;
   desc.RTVFormats[0] = pipeline.format;
   desc.SampleDesc = {1, 0};
   if (FAILED(renderer.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline.state))))
   {
      Log::Error("Display: composition's D3D12 pipeline couldn't be created for format {}", uint32_t(pipeline.format));
      return false;
   }
   return true;
}

std::shared_ptr<const Shaders::CompiledShader> GetCompiled(uint64_t name_hash)
{
   const std::optional<uint32_t> index = Resources::Database::Find(name_hash);
   return index ? Resources::Database::GetShader(*index).compiled : nullptr;
}

// The pipeline for the shaders published now and `format`, made again when either changed (a reload is rare: its old one goes once
// the queue is idle)
const Pipeline* GetPipeline(Renderer& renderer, DXGI_FORMAT format)
{
   std::shared_ptr<const Shaders::CompiledShader> vertex_shader = GetCompiled(VERTEX_SHADER);
   std::shared_ptr<const Shaders::CompiledShader> pixel_shader = GetCompiled(PIXEL_SHADER);
   if (!vertex_shader || !pixel_shader)
      return nullptr;
   Pipeline& pipeline = renderer.pipeline;
   if (pipeline.state && pipeline.vertex_shader == vertex_shader && pipeline.pixel_shader == pixel_shader && pipeline.format == format)
      return &pipeline;
   WaitIdle(renderer);
   pipeline = {.vertex_shader = std::move(vertex_shader), .pixel_shader = std::move(pixel_shader), .format = format};
   if (!CreatePipeline(renderer, pipeline))
   {
      pipeline.state.Reset();
      return nullptr;
   }
   return &pipeline;
}

// The copy an in place composition reads, as the real buffer
ID3D12Resource* GetCopy(Renderer& renderer, const D3D12_RESOURCE_DESC& desc)
{
   if (renderer.copy)
   {
      const D3D12_RESOURCE_DESC copy_desc = renderer.copy->GetDesc();
      if (copy_desc.Width == desc.Width && copy_desc.Height == desc.Height && copy_desc.Format == desc.Format)
         return renderer.copy.Get();
      WaitIdle(renderer);
      renderer.copy.Reset();
   }
   D3D12_RESOURCE_DESC copy_desc = desc;
   copy_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
   const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
   if (FAILED(renderer.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &copy_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&renderer.copy))))
      return nullptr;
   renderer.copy->SetName(L"Luma display composition source");
   return renderer.copy.Get();
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
   return {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, .Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after}};
}

// A null view of each kind the shader declares at t1 to t8, so reading one gives zero
void WriteNullViews(Renderer& renderer, D3D12_CPU_DESCRIPTOR_HANDLE first)
{
   constexpr std::array<D3D12_SRV_DIMENSION, VIEWS - 1> DIMENSIONS = {D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_SRV_DIMENSION_TEXTURE2DMS,
      D3D12_SRV_DIMENSION_TEXTURE2DARRAY, D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY, D3D12_SRV_DIMENSION_TEXTURE3D, D3D12_SRV_DIMENSION_TEXTURE1D,
      D3D12_SRV_DIMENSION_TEXTURE1DARRAY};
   for (uint32_t i = 0; i < DIMENSIONS.size(); i++)
   {
      const D3D12_SHADER_RESOURCE_VIEW_DESC desc = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM, .ViewDimension = DIMENSIONS[i], .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
      renderer.device->CreateShaderResourceView(nullptr, &desc, {first.ptr + SIZE_T(i + 1) * renderer.view_increment});
   }
}
} // namespace

void RendererDeleter::operator()(Renderer* renderer) const
{
   WaitIdle(*renderer);
   if (renderer->event)
      CloseHandle(renderer->event);
   delete renderer;
}

RendererPtr CreateRenderer(ID3D12CommandQueue* queue)
{
   RendererPtr renderer(new Renderer{.queue = queue});
   Renderer& r = *renderer;
   if (FAILED(queue->GetDevice(IID_PPV_ARGS(&r.device))))
      return nullptr;
   const D3D12_DESCRIPTOR_HEAP_DESC views_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = VIEWS * FRAMES, .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
   const D3D12_DESCRIPTOR_HEAP_DESC targets_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .NumDescriptors = FRAMES};
   const D3D12_HEAP_PROPERTIES upload_heap = {.Type = D3D12_HEAP_TYPE_UPLOAD};
   const D3D12_RESOURCE_DESC upload_desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Width = CBUFFER_SLICE * FRAMES,
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .SampleDesc = {1, 0},
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
   if (FAILED(r.device->CreateDescriptorHeap(&views_desc, IID_PPV_ARGS(&r.views))) || FAILED(r.device->CreateDescriptorHeap(&targets_desc, IID_PPV_ARGS(&r.targets))) ||
      FAILED(r.device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&r.upload))) ||
      FAILED(r.upload->Map(0, nullptr, reinterpret_cast<void**>(&r.upload_data))))
      return nullptr;
   for (FrameSlot& frame : r.frames)
   {
      if (FAILED(r.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator))))
         return nullptr;
   }
   if (FAILED(r.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, r.frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&r.list))) || FAILED(r.list->Close()) ||
      FAILED(r.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r.fence))))
      return nullptr;
   r.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
   r.view_increment = r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   r.target_increment = r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
   for (uint32_t i = 0; i < FRAMES; i++)
      WriteNullViews(r, {r.views->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(i) * VIEWS * r.view_increment});
   r.views->SetName(L"Luma display composition views");
   r.list->SetName(L"Luma display composition");
   return renderer;
}

void WaitIdle(Renderer& renderer)
{
   Wait(renderer, renderer.fence_value);
}

bool Compose(Renderer& renderer, ID3D12Resource* source, ID3D12Resource* target, const Pass& pass)
{
   const D3D12_RESOURCE_DESC target_desc = target->GetDesc();
   const Pipeline* pipeline = GetPipeline(renderer, target_desc.Format);
   if (!pipeline)
      return false;
   const bool in_place = source == target;
   if (in_place && !(source = GetCopy(renderer, target_desc)))
      return false;

   const uint32_t index = renderer.frame++ % FRAMES;
   FrameSlot& frame = renderer.frames[index];
   Wait(renderer, frame.fence_value);
   frame.allocator->Reset();
   ID3D12GraphicsCommandList* list = renderer.list.Get();
   list->Reset(frame.allocator.Get(), nullptr);

   // The game's image at t0, null views at t1..t8 (then debug draw if active)
   const D3D12_CPU_DESCRIPTOR_HANDLE views = {renderer.views->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(index) * VIEWS * renderer.view_increment};
   WriteNullViews(renderer, views);
   const D3D12_RESOURCE_DESC source_desc = source->GetDesc();
   const D3D12_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = source_desc.Format,
      .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
      .Texture2D = {.MipLevels = 1}};
   renderer.device->CreateShaderResourceView(source, &view_desc, views);
   Resources::Database::CustomData custom = pass.custom;
#if DEVELOPMENT
   Trace::DebugDraw::BindD3D12(renderer.device.Get(), views, renderer.view_increment, custom.custom_data_2, custom.custom_data_3);
#endif
   const D3D12_CPU_DESCRIPTOR_HANDLE target_view = {renderer.targets->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(index) * renderer.target_increment};
   const D3D12_RENDER_TARGET_VIEW_DESC target_view_desc = {.Format = target_desc.Format, .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D};
   renderer.device->CreateRenderTargetView(target, &target_view_desc, target_view);

   // The cbuffers, written as a pass's (LumaData with this pass's values)
   SetCurrent(pass);
   const Resources::Database::WriteTarget write_target = {renderer.device.Get(), Luma::Frame::Find(renderer.device.Get()), Game::GetGameDeviceData(renderer.device.Get())};
   std::byte* slice = renderer.upload_data + size_t(index) * CBUFFER_SLICE;
   const D3D12_GPU_VIRTUAL_ADDRESS slice_address = renderer.upload->GetGPUVirtualAddress() + uint64_t(index) * CBUFFER_SLICE;
   std::vector<D3D12_GPU_VIRTUAL_ADDRESS> addresses;
   uint32_t offset = 0;
   for (const uint32_t entry_index : pipeline->cbuffers)
   {
      const Resources::Database::Entry& entry = Resources::Database::Get(entry_index);
      const uint32_t size = (entry.size + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
      if (offset + size > CBUFFER_SLICE)
         break;
      std::memset(slice + offset, 0, size);
      entry.write(std::span(slice + offset, entry.size), custom, write_target);
      addresses.push_back(slice_address + offset);
      offset += size;
   }

   std::vector<D3D12_RESOURCE_BARRIER> barriers;
   if (in_place)
   {
      barriers = {Transition(target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE), Transition(source, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)};
      list->ResourceBarrier(UINT(barriers.size()), barriers.data());
      list->CopyResource(source, target);
      barriers = {Transition(target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
         Transition(source, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
   }
   else
   {
      barriers = {Transition(target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
         Transition(source, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
   }
   list->ResourceBarrier(UINT(barriers.size()), barriers.data());

   ID3D12DescriptorHeap* heaps[] = {renderer.views.Get()};
   list->SetDescriptorHeaps(1, heaps);
   list->SetGraphicsRootSignature(pipeline->signature.Get());
   list->SetPipelineState(pipeline->state.Get());
   list->SetGraphicsRootDescriptorTable(0, {renderer.views->GetGPUDescriptorHandleForHeapStart().ptr + uint64_t(index) * VIEWS * renderer.view_increment});
   for (uint32_t i = 0; i < addresses.size(); i++)
      list->SetGraphicsRootConstantBufferView(i + 1, addresses[i]);
   list->OMSetRenderTargets(1, &target_view, FALSE, nullptr);
   const D3D12_VIEWPORT viewport = {0.f, 0.f, float(target_desc.Width), float(target_desc.Height), 0.f, 1.f};
   const D3D12_RECT scissor = {0, 0, LONG(target_desc.Width), LONG(target_desc.Height)};
   list->RSSetViewports(1, &viewport);
   list->RSSetScissorRects(1, &scissor);
   list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   list->DrawInstanced(4, 1, 0, 0);

   for (D3D12_RESOURCE_BARRIER& barrier : barriers)
      std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
   // Back to how they were: the real buffer to present, the game's image (or the copy) to common
   barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
   barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
   list->ResourceBarrier(UINT(barriers.size()), barriers.data());
   list->Close();

   ID3D12CommandList* lists[] = {list};
   renderer.queue->ExecuteCommandLists(1, lists);
   renderer.queue->Signal(renderer.fence.Get(), ++renderer.fence_value);
   frame.fence_value = renderer.fence_value;
   return true;
}
} // namespace D3D12
} // namespace Composition
} // namespace Display
} // namespace Luma
