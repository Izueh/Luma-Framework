#include "UI/D3D12/D3D12Renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>

#include "Utils/Log.hpp"

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

   // ImGui's pipeline with the encoding pixel shader, created once, over Luma's root signature: ImGui's with the encoding's
   // constants added
   ComPtr<ID3D12RootSignature> encoding_signature;
   ComPtr<ID3D12PipelineState> encoding_pipeline;
   bool encoding_failed = false;
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

// ImGui's vertex shader and input layout, as its backend creates them, and its root signature with a third parameter: the encoding's
// constants. Parameters 0 (the projection) and 1 (the texture) keep their places, so the backend's per command texture binds still
// land; the projection is set again when the encoding's root signature is bound.
constexpr const char* VERTEX_SHADER = R"(
cbuffer vertexBuffer : register(b0)
{
   float4x4 ProjectionMatrix;
};
struct VS_INPUT
{
   float2 pos : POSITION;
   float4 col : COLOR0;
   float2 uv : TEXCOORD0;
};
struct PS_INPUT
{
   float4 pos : SV_POSITION;
   float4 col : COLOR0;
   float2 uv : TEXCOORD0;
};
PS_INPUT main(VS_INPUT input)
{
   PS_INPUT output;
   output.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.0f, 1.0f));
   output.col = input.col;
   output.uv = input.uv;
   return output;
}
)";

ComPtr<ID3D12RootSignature> CreateEncodingRootSignature(ID3D12Device* device)
{
   const D3D12_DESCRIPTOR_RANGE range = {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = 1};
   D3D12_ROOT_PARAMETER parameters[3] = {};
   parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
   parameters[0].Constants.Num32BitValues = 16;
   parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
   parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
   parameters[1].DescriptorTable = {1, &range};
   parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
   parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
   parameters[2].Constants = {Encoding::CONSTANTS_REGISTER, 0, sizeof(Encoding::Constants) / 4};
   parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
   const D3D12_STATIC_SAMPLER_DESC sampler = {
      .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
      .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
      .BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
      .MaxLOD = D3D12_FLOAT32_MAX,
      .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
   };
   const D3D12_ROOT_SIGNATURE_DESC desc = {
      .NumParameters = 3,
      .pParameters = parameters,
      .NumStaticSamplers = 1,
      .pStaticSamplers = &sampler,
      .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
         D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS,
   };
   // Loaded: the game presents with it
   const HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
   const auto serialize = d3d12 ? reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(GetProcAddress(d3d12, "D3D12SerializeRootSignature")) : nullptr;
   ComPtr<ID3DBlob> blob;
   ComPtr<ID3D12RootSignature> signature;
   if (serialize && SUCCEEDED(serialize(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr)))
      device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&signature));
   return signature;
}

ComPtr<ID3D12PipelineState> CreateEncodingPipeline(ID3D12Device* device, ID3D12RootSignature* signature, DXGI_FORMAT format)
{
   const ComPtr<ID3DBlob> pixel_shader = Encoding::CompilePixelShader("ps_5_0");
   ComPtr<ID3DBlob> vertex_shader;
   if (!signature || !pixel_shader ||
      FAILED(D3DCompile(VERTEX_SHADER, std::strlen(VERTEX_SHADER), "LumaOverlay", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vertex_shader, nullptr)))
      return nullptr;
   const std::array<D3D12_INPUT_ELEMENT_DESC, 3> layout = {{
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, UINT(offsetof(ImDrawVert, pos)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, UINT(offsetof(ImDrawVert, uv)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, UINT(offsetof(ImDrawVert, col)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
   }};
   D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
   desc.pRootSignature = signature;
   desc.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
   desc.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
   desc.SampleMask = UINT_MAX;
   desc.InputLayout = {layout.data(), UINT(layout.size())};
   desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
   desc.NumRenderTargets = 1;
   desc.RTVFormats[0] = format;
   desc.SampleDesc = {1, 0};
   desc.NodeMask = 1;
   desc.BlendState.RenderTarget[0] = {TRUE, FALSE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE,
      D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL};
   desc.RasterizerState = {D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE, FALSE, D3D12_DEFAULT_DEPTH_BIAS, D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
      D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
   desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
   desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
   ComPtr<ID3D12PipelineState> pipeline;
   if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline))))
      Utils::Log::Error("UI: the overlay's HDR pipeline couldn't be created");
   return pipeline;
}

// What the encoding binds, set before the frame's commands are recorded
std::array<float, 16> projection = {};
Encoding::Constants encoding_constants;

// ImGui's backend projection (its SetupRenderState)
std::array<float, 16> GetProjection(const ImDrawData* data)
{
   const float left = data->DisplayPos.x;
   const float right = data->DisplayPos.x + data->DisplaySize.x;
   const float top = data->DisplayPos.y;
   const float bottom = data->DisplayPos.y + data->DisplaySize.y;
   return {2.f / (right - left), 0.f, 0.f, 0.f, 0.f, 2.f / (top - bottom), 0.f, 0.f, 0.f, 0.f, 0.5f, 0.f, (right + left) / (left - right),
      (top + bottom) / (bottom - top), 0.5f, 1.f};
}

void BindEncodingPipeline(const ImDrawList*, const ImDrawCmd*)
{
   ID3D12GraphicsCommandList* list = ImGui_ImplDX12_GetRenderState()->CommandList;
   list->SetGraphicsRootSignature(renderer->encoding_signature.Get());
   list->SetPipelineState(renderer->encoding_pipeline.Get());
   // A new root signature drops the root arguments: the backend binds the texture (parameter 1) at each command after this one
   list->SetGraphicsRoot32BitConstants(0, 16, projection.data(), 0);
   list->SetGraphicsRoot32BitConstants(2, sizeof(encoding_constants) / 4, &encoding_constants, 0);
}

// False if the pipeline isn't available
bool PrepareEncoding(const ImDrawData* data, const Encoding::Output& output)
{
   if (!renderer->encoding_pipeline && !renderer->encoding_failed)
   {
      renderer->encoding_signature = CreateEncodingRootSignature(renderer->device.Get());
      if (renderer->encoding_signature)
         renderer->encoding_pipeline = CreateEncodingPipeline(renderer->device.Get(), renderer->encoding_signature.Get(), renderer->format);
      renderer->encoding_failed = !renderer->encoding_pipeline;
   }
   projection = GetProjection(data);
   encoding_constants = Encoding::GetConstants(output);
   return renderer->encoding_pipeline != nullptr;
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

bool NeedsRecreation(const DXGI_SWAP_CHAIN_DESC& desc)
{
   return renderer && (renderer->format != desc.BufferDesc.Format || renderer->buffer_count != desc.BufferCount);
}

bool NewFrame(ID3D12CommandQueue* queue, const DXGI_SWAP_CHAIN_DESC& desc)
{
   if (!renderer && !Create(queue, desc.BufferDesc.Format, desc.BufferCount))
   {
      Utils::Log::Error("UI: ImGui's D3D12 renderer failed to initialize on queue {}", static_cast<void*>(queue));
      return false;
   }
   ImGui_ImplDX12_NewFrame();
   return true;
}

void Render(ID3D12CommandQueue* queue, IDXGISwapChain* swapchain, ImDrawData* data, const Encoding::Output& output)
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
   if (output.kind != Encoding::Kind::Sdr && PrepareEncoding(data, output))
      Encoding::InsertCallback(data, &BindEncodingPipeline);
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
