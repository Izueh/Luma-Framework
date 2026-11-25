#include "Trace/D3D11/D3D11Capture.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <string>

#include <Windows.h>

#include "Draw/D3D11/D3D11Draw.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Utils/Format.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderVariants.hpp"
#include "Trace/StateNames.hpp"
#include "Trace/CbufferTracker.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Trace {
namespace Capture {
namespace D3D11 {
namespace {
struct Pending
{
   Added event;
   size_t constant_buffer;
   ComPtr<ID3D11Buffer> copy;
   ComPtr<ID3D11DeviceContext> context;
};

// A copy of the tracked slot. One made on a deferred context holds its contents only once the command list recording it was
// executed on the immediate context.
struct TrackedCopy
{
   Track::Target target;
   ComPtr<ID3D11Buffer> copy;
   ComPtr<ID3D11Device> device;
   std::string buffer;
   uint32_t first_constant = 0;
   uint32_t constant_count = 0;
   // Compared only: the deferred context recording the copy, then the command list it ended in (both null on the immediate context)
   const void* deferred_context = nullptr;
   const void* command_list = nullptr;
   bool executed = true;
};

// Deferred copies a frame doesn't execute wait for a later one, up to this many
constexpr size_t MAX_TRACKED_COPIES = 8;

// Never held with another lock
std::mutex tracked_mutex;
std::vector<TrackedCopy> tracked;

// Never held with another lock
std::mutex pending_mutex;
std::vector<Pending> pending;
constexpr uint64_t MAX_COPIED_BYTES = 64ull << 20;
std::atomic<uint64_t> copied_bytes = 0;

Resource DescribeOwn(ID3D11Resource* resource)
{
   Resource described = {.id = GetObjectId(resource)};
   D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   resource->GetType(&dimension);
   switch (dimension)
   {
   case D3D11_RESOURCE_DIMENSION_BUFFER:
   {
      D3D11_BUFFER_DESC desc;
      static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
      described.type = "Buffer";
      described.size = {desc.ByteWidth, 1, 1, 1};
      break;
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
   {
      D3D11_TEXTURE1D_DESC desc;
      static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
      described.type = "Texture1D";
      described.format = desc.Format;
      described.size = {desc.Width, 1, desc.ArraySize, desc.MipLevels};
      break;
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
   {
      D3D11_TEXTURE2D_DESC desc;
      static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
      described.type = desc.SampleDesc.Count > 1 ? std::format("Texture2D ({}x MSAA)", desc.SampleDesc.Count) : std::string("Texture2D");
      described.format = desc.Format;
      described.size = {desc.Width, desc.Height, desc.ArraySize, desc.MipLevels};
      break;
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
   {
      D3D11_TEXTURE3D_DESC desc;
      static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
      described.type = "Texture3D";
      described.format = desc.Format;
      described.size = {desc.Width, desc.Height, desc.Depth, desc.MipLevels};
      break;
   }
   default: described.type = "Unknown"; break;
   }
   described.debug_name = GetD3DDebugName(resource);
   return described;
}

// A mirror is bound in place of the game's texture: described as the game's, with the mirror's format, size, scale and origin
Resource DescribeResource(ID3D11Resource* resource)
{
   ID3D11Resource* original = Resources::Mirrors::D3D11::FindOriginal(resource);
   ID3D11Resource* target = original ? original : resource;
   Resource described = DescribeOwn(target);
   if (const std::optional<Resources::Mirrors::D3D11::MirrorSummary> summary = Resources::Mirrors::D3D11::FindMirrorSummary(target))
   {
      const auto& mi = summary->mirror_info;
      const auto& oi = summary->original_info;
      described.mirror_info = MirrorInfo{
         .format = uint32_t(mi.format),
         .size = {mi.width, mi.height, mi.depth_or_layers, mi.levels},
         .is_scaled = summary->is_scaled,
         .scale = {float(mi.width) / float(std::max(oi.width, 1u)), float(mi.height) / float(std::max(oi.height, 1u))},
         .origin = summary->origin,
         .pending = summary->pending,
      };
      if (original || !summary->pending)
      {
         described.mirror = summary->is_scaled
            ? std::format("{} {}x{} ({:.2f}x)", Utils::Format::GetName(mi.format), mi.width, mi.height, described.mirror_info->scale[0])
            : std::format("{} {}x{}", Utils::Format::GetName(mi.format), mi.width, mi.height);
      }
   }
   return described;
}

UINT GetMip(const D3D11_SHADER_RESOURCE_VIEW_DESC& desc)
{
   switch (desc.ViewDimension)
   {
   case D3D11_SRV_DIMENSION_TEXTURE1D: return desc.Texture1D.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURE1DARRAY: return desc.Texture1DArray.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURE2D: return desc.Texture2D.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURE2DARRAY: return desc.Texture2DArray.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURE3D: return desc.Texture3D.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURECUBE: return desc.TextureCube.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY: return desc.TextureCubeArray.MostDetailedMip;
   default: return 0;
   }
}

UINT GetMip(const D3D11_RENDER_TARGET_VIEW_DESC& desc)
{
   switch (desc.ViewDimension)
   {
   case D3D11_RTV_DIMENSION_TEXTURE1D: return desc.Texture1D.MipSlice;
   case D3D11_RTV_DIMENSION_TEXTURE1DARRAY: return desc.Texture1DArray.MipSlice;
   case D3D11_RTV_DIMENSION_TEXTURE2D: return desc.Texture2D.MipSlice;
   case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: return desc.Texture2DArray.MipSlice;
   case D3D11_RTV_DIMENSION_TEXTURE3D: return desc.Texture3D.MipSlice;
   default: return 0;
   }
}

UINT GetMip(const D3D11_UNORDERED_ACCESS_VIEW_DESC& desc)
{
   switch (desc.ViewDimension)
   {
   case D3D11_UAV_DIMENSION_TEXTURE1D: return desc.Texture1D.MipSlice;
   case D3D11_UAV_DIMENSION_TEXTURE1DARRAY: return desc.Texture1DArray.MipSlice;
   case D3D11_UAV_DIMENSION_TEXTURE2D: return desc.Texture2D.MipSlice;
   case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: return desc.Texture2DArray.MipSlice;
   case D3D11_UAV_DIMENSION_TEXTURE3D: return desc.Texture3D.MipSlice;
   default: return 0;
   }
}

UINT GetMip(const D3D11_DEPTH_STENCIL_VIEW_DESC& desc)
{
   switch (desc.ViewDimension)
   {
   case D3D11_DSV_DIMENSION_TEXTURE1D: return desc.Texture1D.MipSlice;
   case D3D11_DSV_DIMENSION_TEXTURE1DARRAY: return desc.Texture1DArray.MipSlice;
   case D3D11_DSV_DIMENSION_TEXTURE2D: return desc.Texture2D.MipSlice;
   case D3D11_DSV_DIMENSION_TEXTURE2DARRAY: return desc.Texture2DArray.MipSlice;
   default: return 0;
   }
}

template <typename ViewType>
struct ViewDesc;

template <>
struct ViewDesc<ID3D11ShaderResourceView>
{
   using Type = D3D11_SHADER_RESOURCE_VIEW_DESC;
};

template <>
struct ViewDesc<ID3D11RenderTargetView>
{
   using Type = D3D11_RENDER_TARGET_VIEW_DESC;
};

template <>
struct ViewDesc<ID3D11UnorderedAccessView>
{
   using Type = D3D11_UNORDERED_ACCESS_VIEW_DESC;
};

template <>
struct ViewDesc<ID3D11DepthStencilView>
{
   using Type = D3D11_DEPTH_STENCIL_VIEW_DESC;
};

template <typename ViewType>
View DescribeView(ViewType* view, uint32_t slot)
{
   View described = {.slot = slot};
   ComPtr<ID3D11Resource> resource;
   view->GetResource(&resource);
   if (resource)
      described.resource = DescribeResource(resource.Get());
   typename ViewDesc<ViewType>::Type desc;
   view->GetDesc(&desc);
   described.format = desc.Format;
   described.mip = GetMip(desc);
   const std::array<uint32_t, 4>& size = described.resource.size;
   described.size = {std::max(size[0] >> described.mip, 1u), std::max(size[1] >> described.mip, 1u), size[2]};
   return described;
}

ComPtr<ID3D11DeviceChild> GetShader(ID3D11DeviceContext* context, ShaderStage stage)
{
   ComPtr<ID3D11DeviceChild> shader;
   switch (stage)
   {
   case ShaderStage::Vertex: context->VSGetShader(reinterpret_cast<ID3D11VertexShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   case ShaderStage::Hull: context->HSGetShader(reinterpret_cast<ID3D11HullShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   case ShaderStage::Domain: context->DSGetShader(reinterpret_cast<ID3D11DomainShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   case ShaderStage::Geometry: context->GSGetShader(reinterpret_cast<ID3D11GeometryShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   case ShaderStage::Pixel: context->PSGetShader(reinterpret_cast<ID3D11PixelShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   case ShaderStage::Compute: context->CSGetShader(reinterpret_cast<ID3D11ComputeShader**>(shader.GetAddressOf()), nullptr, nullptr); break;
   }
   return shader;
}

void GetConstantBuffers(ID3D11DeviceContext* context, ID3D11DeviceContext1* context1, ShaderStage stage, ID3D11Buffer** buffers, UINT* first, UINT* counts)
{
   constexpr UINT N = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
   if (context1)
   {
      switch (stage)
      {
      case ShaderStage::Vertex: context1->VSGetConstantBuffers1(0, N, buffers, first, counts); break;
      case ShaderStage::Hull: context1->HSGetConstantBuffers1(0, N, buffers, first, counts); break;
      case ShaderStage::Domain: context1->DSGetConstantBuffers1(0, N, buffers, first, counts); break;
      case ShaderStage::Geometry: context1->GSGetConstantBuffers1(0, N, buffers, first, counts); break;
      case ShaderStage::Pixel: context1->PSGetConstantBuffers1(0, N, buffers, first, counts); break;
      case ShaderStage::Compute: context1->CSGetConstantBuffers1(0, N, buffers, first, counts); break;
      }
      return;
   }
   switch (stage)
   {
   case ShaderStage::Vertex: context->VSGetConstantBuffers(0, N, buffers); break;
   case ShaderStage::Hull: context->HSGetConstantBuffers(0, N, buffers); break;
   case ShaderStage::Domain: context->DSGetConstantBuffers(0, N, buffers); break;
   case ShaderStage::Geometry: context->GSGetConstantBuffers(0, N, buffers); break;
   case ShaderStage::Pixel: context->PSGetConstantBuffers(0, N, buffers); break;
   case ShaderStage::Compute: context->CSGetConstantBuffers(0, N, buffers); break;
   }
}

// The constant buffers a stage's shader reads (all bound ones when its reflection didn't tell), copied for their contents on the
// immediate context. Also includes Luma cbuffers requested by a replaced/patched shader.
void AddConstantBuffers(Recording& recording, ID3D11DeviceContext1* context1, ShaderStage stage, const Shaders::Database::Shader* shader,
   const Shaders::CompiledShader* compiled = nullptr)
{
   ID3D11DeviceContext* context = recording.context.Get();
   std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers = {};
   std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> first = {};
   std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> counts = {};
   GetConstantBuffers(context, context1, stage, buffers.data(), first.data(), counts.data());
   const bool known = shader && shader->reflection.found;
   const bool immediate = context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);

   // Slots that a custom/replaced shader binds to Luma's own cbuffers (e.g. LumaSettings, LumaData)
   std::array<const Resources::Database::Entry*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> luma_slots = {};
   if (compiled)
   {
      for (const Shaders::Request& request : compiled->requests)
      {
         if (request.slot >= luma_slots.size())
            continue;
         const Resources::Database::Entry& entry = Resources::Database::Get(request.entry);
         if (entry.kind == Resources::Database::Kind::ConstantBuffer)
            luma_slots[request.slot] = &entry;
      }
   }

   for (uint32_t slot = 0; slot < buffers.size(); slot++)
   {
      ComPtr<ID3D11Buffer> buffer;
      buffer.Attach(buffers[slot]);
      if (const Resources::Database::Entry* luma_entry = luma_slots[slot])
      {
         ConstantBuffer cb = {stage, slot, "Luma", luma_entry->size, 0, 0};
         cb.luma_name = luma_entry->name;
         if (luma_entry->write && luma_entry->size > 0)
         {
            cb.contents.resize(luma_entry->size);
            luma_entry->write(cb.contents, {}, {device.Get(), {}});
         }
         recording.event.constant_buffers.push_back(std::move(cb));
         continue;
      }
      if (!buffer || (known && !shader->reflection.constant_buffers[slot]))
         continue;
      D3D11_BUFFER_DESC desc;
      buffer->GetDesc(&desc);
      // The whole buffer, as the D3D11.0 binds report it (4096 constants), is no range
      const bool whole = first[slot] == 0 && uint64_t(counts[slot]) * 16 >= desc.ByteWidth;
      recording.event.constant_buffers.push_back({stage, slot, GetObjectId(buffer.Get()), desc.ByteWidth, whole ? 0 : first[slot], whole ? 0 : counts[slot]});
      if (!immediate || copied_bytes.fetch_add(desc.ByteWidth, std::memory_order_relaxed) + desc.ByteWidth > MAX_COPIED_BYTES)
         continue;
      const D3D11_BUFFER_DESC copy_desc = {desc.ByteWidth, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
      ComPtr<ID3D11Buffer> copy;
      if (FAILED(device->CreateBuffer(&copy_desc, nullptr, &copy)))
         continue;
      context->CopyResource(copy.Get(), buffer.Get());
      recording.copies.emplace_back(recording.event.constant_buffers.size() - 1, std::move(copy));
   }
}

// The views and samplers of `stage`, the targets, the rasterizer and topology (always live), the viewports, blend and depth from the
// bound-state cache for a game call (`recorded`), live for Luma's own passes
void DescribeState(Recording& recording, ShaderStage stage, bool recorded)
{
   ID3D11DeviceContext* context = recording.context.Get();
   Event& event = recording.event;
   event.thread = GetCurrentThreadId();
   event.deferred = context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED;

   std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> resources = {};
   std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers = {};
   std::array<ID3D11UnorderedAccessView*, D3D11_1_UAV_SLOT_COUNT> unordered_access = {};
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   const UINT uav_slots = device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT;
   switch (stage)
   {
   case ShaderStage::Vertex:
      context->VSGetShaderResources(0, UINT(resources.size()), resources.data());
      context->VSGetSamplers(0, UINT(samplers.size()), samplers.data());
      break;
   case ShaderStage::Pixel:
      context->PSGetShaderResources(0, UINT(resources.size()), resources.data());
      context->PSGetSamplers(0, UINT(samplers.size()), samplers.data());
      break;
   case ShaderStage::Compute:
      context->CSGetShaderResources(0, UINT(resources.size()), resources.data());
      context->CSGetSamplers(0, UINT(samplers.size()), samplers.data());
      context->CSGetUnorderedAccessViews(0, uav_slots, unordered_access.data());
      break;
   default: break;
   }
   for (uint32_t slot = 0; slot < resources.size(); slot++)
   {
      ComPtr<ID3D11ShaderResourceView> view;
      view.Attach(resources[slot]);
      if (view)
         event.shader_resources.push_back(DescribeView(view.Get(), slot));
   }
   for (uint32_t slot = 0; slot < samplers.size(); slot++)
   {
      ComPtr<ID3D11SamplerState> sampler;
      sampler.Attach(samplers[slot]);
      if (!sampler)
         continue;
      D3D11_SAMPLER_DESC desc;
      sampler->GetDesc(&desc);
      event.samplers.push_back({slot, StateNames::GetFilterName(desc.Filter), uint32_t(desc.Filter), {uint32_t(desc.AddressU), uint32_t(desc.AddressV), uint32_t(desc.AddressW)},
         std::format("{} {} {}", StateNames::GetAddressName(desc.AddressU), StateNames::GetAddressName(desc.AddressV), StateNames::GetAddressName(desc.AddressW)),
         desc.MipLODBias});
   }

   if (stage != ShaderStage::Compute)
   {
      std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets = {};
      ID3D11DepthStencilView* depth_stencil = nullptr;
      context->OMGetRenderTargets(UINT(targets.size()), targets.data(), &depth_stencil);
      UINT target_count = 0;
      for (UINT i = 0; i < targets.size(); i++)
         target_count = targets[i] ? i + 1 : target_count;
      context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, target_count, uav_slots - target_count, unordered_access.data() + target_count);

      ComPtr<ID3D11BlendState> blend;
      D3D11_BLEND_DESC blend_desc = CD3D11_BLEND_DESC(D3D11_DEFAULT);
      if (recorded)
      {
         if (const Draw::D3D11::Bound* bound = Draw::D3D11::GetBound(context))
         {
            blend = bound->blend;
            event.blend_factor = bound->blend_factor;
         }
      }
      else
      {
         UINT sample_mask = 0;
         context->OMGetBlendState(&blend, event.blend_factor.data(), &sample_mask);
      }
      if (blend)
         blend->GetDesc(&blend_desc);
      event.alpha_to_coverage = blend_desc.AlphaToCoverageEnable;
      for (uint32_t slot = 0; slot < targets.size(); slot++)
      {
         ComPtr<ID3D11RenderTargetView> view;
         view.Attach(targets[slot]);
         if (!view)
            continue;
         View& described = event.render_targets.emplace_back(DescribeView(view.Get(), slot));
         const D3D11_RENDER_TARGET_BLEND_DESC& target_blend = blend_desc.RenderTarget[blend_desc.IndependentBlendEnable ? slot : 0];
         described.blend = StateNames::DescribeBlend(target_blend);
         described.write_mask = target_blend.RenderTargetWriteMask;
      }
      if (depth_stencil)
      {
         event.depth_stencil_view = DescribeView(depth_stencil, 0);
         depth_stencil->Release();
      }

      ComPtr<ID3D11DepthStencilState> depth;
      if (recorded)
      {
         if (const Draw::D3D11::Bound* bound = Draw::D3D11::GetBound(context))
         {
            depth = bound->depth;
            event.depth_stencil.stencil_ref = bound->stencil_ref;
         }
      }
      else
         context->OMGetDepthStencilState(&depth, &event.depth_stencil.stencil_ref);
      D3D11_DEPTH_STENCIL_DESC depth_desc = CD3D11_DEPTH_STENCIL_DESC(D3D11_DEFAULT);
      if (depth)
         depth->GetDesc(&depth_desc);
      event.depth_stencil.state_bound = depth != nullptr;
      event.depth_stencil.depth_enable = depth_desc.DepthEnable;
      event.depth_stencil.depth_write = depth_desc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL;
      event.depth_stencil.depth_func = StateNames::GetComparisonName(depth_desc.DepthFunc);
      event.depth_stencil.stencil_enable = depth_desc.StencilEnable;
      event.depth_stencil.read_mask = depth_desc.StencilReadMask;
      event.depth_stencil.write_mask = depth_desc.StencilWriteMask;
      event.depth_stencil.front = StateNames::DescribeStencilFace(depth_desc.FrontFace);
      event.depth_stencil.back = StateNames::DescribeStencilFace(depth_desc.BackFace);

      ComPtr<ID3D11RasterizerState> rasterizer;
      context->RSGetState(&rasterizer);
      D3D11_RASTERIZER_DESC rasterizer_desc = CD3D11_RASTERIZER_DESC(D3D11_DEFAULT);
      if (rasterizer)
         rasterizer->GetDesc(&rasterizer_desc);
      event.rasterizer = {rasterizer != nullptr, StateNames::GetCullName(rasterizer_desc.CullMode), StateNames::GetFillName(rasterizer_desc.FillMode),
         bool(rasterizer_desc.FrontCounterClockwise), rasterizer_desc.DepthBias, rasterizer_desc.SlopeScaledDepthBias, bool(rasterizer_desc.DepthClipEnable),
         bool(rasterizer_desc.ScissorEnable), bool(rasterizer_desc.MultisampleEnable)};

      std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
      std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> rects = {};
      UINT viewport_count = UINT(viewports.size());
      UINT rect_count = UINT(rects.size());
      const std::optional<Resources::Mirrors::D3D11::ContextScalingSnapshot> scaling = Resources::Mirrors::D3D11::GetContextScalingSnapshot(context);
      const Draw::D3D11::Bound* bound = recorded ? Draw::D3D11::GetBound(context) : nullptr;
      if (scaling && (scaling->viewports_scaled || scaling->scissors_scaled))
      {
         // Read live viewports/scissors (Luma's rescaled ones) and keep the game's original values alongside
         context->RSGetViewports(&viewport_count, viewports.data());
         context->RSGetScissorRects(&rect_count, rects.data());
         event.viewports_scaled = scaling->viewports_scaled;
         if (scaling->viewports_scaled)
         {
            for (UINT i = 0; i < scaling->game_viewport_count; i++)
            {
               const D3D11_VIEWPORT& vp = scaling->game_viewports[i];
               event.original_viewports.push_back({vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height, vp.MinDepth, vp.MaxDepth});
            }
         }
         event.scissors_scaled = scaling->scissors_scaled;
         if (scaling->scissors_scaled)
         {
            for (UINT i = 0; i < scaling->game_scissor_count; i++)
            {
               const D3D11_RECT& rc = scaling->game_scissors[i];
               event.original_scissor_rects.push_back({rc.left, rc.top, rc.right, rc.bottom});
            }
         }
      }
      else if (bound)
      {
         viewports = bound->viewports;
         viewport_count = bound->viewport_count;
         rects = bound->scissor_rects;
         rect_count = bound->scissor_count;
      }
      else
      {
         context->RSGetViewports(&viewport_count, viewports.data());
         context->RSGetScissorRects(&rect_count, rects.data());
      }
      if (scaling)
      {
         event.chain_seed = scaling->chain_seed;
         event.chain_propagated = scaling->chain_propagated;
         event.cross_thread_scaling_overlap = scaling->cross_thread_overlap;
      }
      for (UINT i = 0; i < viewport_count; i++)
         event.viewports.push_back({viewports[i].TopLeftX, viewports[i].TopLeftY, viewports[i].Width, viewports[i].Height, viewports[i].MinDepth, viewports[i].MaxDepth});
      for (UINT i = 0; i < rect_count; i++)
         event.scissor_rects.push_back({rects[i].left, rects[i].top, rects[i].right, rects[i].bottom});

      D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      context->IAGetPrimitiveTopology(&topology);
      event.topology = uint32_t(topology);
   }
   for (uint32_t slot = 0; slot < unordered_access.size(); slot++)
   {
      ComPtr<ID3D11UnorderedAccessView> view;
      view.Attach(unordered_access[slot]);
      if (view)
         event.unordered_access.push_back(DescribeView(view.Get(), slot));
   }
}

// A game call: its shaders from Variants' record, its main stage's state
Recording BeginGameCall(ID3D11DeviceContext* context, EventType type, Shaders::StageSet stages)
{
   Recording recording = {.event = {.type = type}, .context = context};
   const Shaders::Variants::D3D11::BoundShaders* bound = Shaders::Variants::D3D11::GetBoundShaders(context);
   ComPtr<ID3D11DeviceContext1> context1;
   context->QueryInterface(IID_PPV_ARGS(&context1));
   ShaderStage main_stage = type == EventType::Dispatch ? ShaderStage::Compute : ShaderStage::Vertex;
   for (uint32_t index = 0; index <= uint32_t(ShaderStage::Compute); index++)
   {
      const ShaderStage stage = ShaderStage(index);
      if (!stages.Has(stage) || !GetShader(context, stage))
         continue;
      const Shaders::Database::Shader* shader = bound ? bound->shaders[index] : nullptr;
      Shader& recorded = recording.event.shaders.emplace_back(Shader{
         .stage = stage,
         .hash = shader ? shader->hash : 0,
         .name = shader ? Shaders::Database::GetDisplayName(shader->hash) : std::string(),
      });
      if (shader)
      {
         for (const Shaders::Variants::Variant& variant : Shaders::Variants::GetVariants(shader->hash))
         {
            if (variant.compiled)
               recorded.variants.push_back(variant.source);
         }
         recorded.forced = shader->forced_variant.load(std::memory_order_relaxed);
      }
      const Shaders::CompiledShader* compiled = (bound && bound->variant_stages.Has(stage)) ? bound->compiled[index].get() : nullptr;
      if (compiled)
         recorded.variant = bound->sources[index];
      AddConstantBuffers(recording, context1.Get(), stage, shader, compiled);
      if (stage == ShaderStage::Pixel)
         main_stage = stage;
   }
   DescribeState(recording, main_stage, true);
   return recording;
}
} // namespace

Recording BeginDraw(ID3D11DeviceContext* context, const Game::D3D11::DrawCall& draw)
{
   using Function = Hooks::D3D11::Function;
   Recording recording = BeginGameCall(context, EventType::Draw, Shaders::GRAPHICS_STAGES);
   Event& event = recording.event;
   // In Function's order
   constexpr std::array<std::string_view, 7> NAMES = {"Draw", "DrawInstanced", "DrawIndexed", "DrawIndexedInstanced", "DrawAuto", "DrawInstancedIndirect",
      "DrawIndexedInstancedIndirect"};
   const size_t function = size_t(draw.function) - size_t(Function::Draw);
   event.name = function < NAMES.size() ? NAMES[function] : "Draw";
   event.vertex_or_index_count = draw.vertex_or_index_count;
   event.instance_count = draw.instance_count;
   event.start_vertex_or_index = draw.start_vertex_or_index;
   event.base_vertex = draw.base_vertex;
   event.start_instance = draw.start_instance;
   event.indexed = draw.function == Function::DrawIndexed || draw.function == Function::DrawIndexedInstanced || draw.function == Function::DrawIndexedInstancedIndirect;
   event.indirect = draw.arguments != nullptr;
   return recording;
}

Recording BeginDispatch(ID3D11DeviceContext* context, const Game::D3D11::DispatchCall& dispatch)
{
   Recording recording = BeginGameCall(context, EventType::Dispatch, Shaders::COMPUTE_STAGES);
   recording.event.name = dispatch.arguments ? "DispatchIndirect" : "Dispatch";
   recording.event.dispatch = {dispatch.x, dispatch.y, dispatch.z};
   recording.event.indirect = dispatch.arguments != nullptr;
   return recording;
}

Recording BeginPass(ID3D11DeviceContext* context, std::string_view name, ShaderStage stage, uint64_t shader)
{
   Recording recording = {.event = {.type = EventType::LumaPass, .name = std::string(name)}, .context = context};
   std::string luma_shader;
   if (const std::optional<uint32_t> entry = Resources::Database::Find(shader))
      luma_shader = Resources::Database::Get(*entry).name;
   recording.event.shaders.push_back({.stage = stage, .luma_shader = std::move(luma_shader)});
   ComPtr<ID3D11DeviceContext1> context1;
   context->QueryInterface(IID_PPV_ARGS(&context1));
   AddConstantBuffers(recording, context1.Get(), stage, nullptr);
   DescribeState(recording, stage, false);
   recording.event.vertex_or_index_count = 4;
   recording.event.instance_count = 1;
   return recording;
}

void End(Recording recording, Outcome outcome)
{
   recording.event.outcome = outcome;
   const Added added = Add(std::move(recording.event));
   if (recording.copies.empty())
      return;
   std::scoped_lock lock(pending_mutex);
   for (auto& [constant_buffer, copy] : recording.copies)
      pending.push_back({added, constant_buffer, std::move(copy), recording.context});
}

namespace {
Event MakeResourceEvent(ID3D11DeviceContext* context, EventType type, std::string_view name)
{
   return {.type = type, .thread = GetCurrentThreadId(), .deferred = context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED, .name = std::string(name)};
}
} // namespace

void AddCopy(ID3D11DeviceContext* context, std::string_view name, ID3D11Resource* destination, ID3D11Resource* source, const Copy& copy)
{
   Event event = MakeResourceEvent(context, EventType::Copy, name);
   if (source)
      event.source = DescribeResource(source);
   if (destination)
      event.target = DescribeResource(destination);
   event.copy = copy;
   Add(std::move(event));
}

void AddClear(ID3D11DeviceContext* context, std::string_view name, ID3D11View* view, const std::array<float, 4>& values)
{
   Event event = MakeResourceEvent(context, EventType::Clear, name);
   if (view)
   {
      ComPtr<ID3D11Resource> resource;
      view->GetResource(&resource);
      if (resource)
         event.target = DescribeResource(resource.Get());
   }
   event.clear_values = values;
   Add(std::move(event));
}

void AddWrite(ID3D11DeviceContext* context, std::string_view name, ID3D11Resource* resource, const CpuAccess& access, EventType type)
{
   Event event = MakeResourceEvent(context, type, name);
   if (resource)
      event.target = DescribeResource(resource);
   CpuAccess details = access;
   if (details.api_function.empty())
      details.api_function = std::string(name);
   event.cpu_access = std::move(details);
   Add(std::move(event));
}

bool IsSkipped(ID3D11DeviceContext* context, Shaders::StageSet stages)
{
   const Shaders::Variants::D3D11::BoundShaders* bound = Shaders::Variants::D3D11::GetBoundShaders(context);
   return bound && IsAnyStageSkipped(bound->shaders, stages);
}

void TrackCall(ID3D11DeviceContext* context, Shaders::StageSet stages)
{
   const Shaders::Variants::D3D11::BoundShaders* bound = Shaders::Variants::D3D11::GetBoundShaders(context);
   const std::optional<Track::MatchedCall> matched = bound ? Track::MatchCall(bound->shaders, stages) : std::nullopt;
   if (!matched)
      return;
   const Track::Target& target = matched->target;
   std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers = {};
   std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> first = {};
   std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> counts = {};
   ComPtr<ID3D11DeviceContext1> context1;
   context->QueryInterface(IID_PPV_ARGS(&context1));
   GetConstantBuffers(context, context1.Get(), target.stage, buffers.data(), first.data(), counts.data());
   ComPtr<ID3D11Buffer> buffer;
   for (uint32_t slot = 0; slot < buffers.size(); slot++)
   {
      if (slot == target.slot)
         buffer.Attach(buffers[slot]);
      else if (buffers[slot])
         buffers[slot]->Release();
   }
   if (!buffer || target.slot >= buffers.size())
      return;
   D3D11_BUFFER_DESC desc;
   buffer->GetDesc(&desc);
   const bool whole = first[target.slot] == 0 && uint64_t(counts[target.slot]) * 16 >= desc.ByteWidth;
   const bool deferred = context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED;
   TrackedCopy copy = {target, nullptr, nullptr, GetObjectId(buffer.Get()), whole ? 0 : first[target.slot], whole ? 0 : counts[target.slot],
      deferred ? context : nullptr, nullptr, !deferred};
   context->GetDevice(&copy.device);
   const D3D11_BUFFER_DESC copy_desc = {desc.ByteWidth, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
   if (FAILED(copy.device->CreateBuffer(&copy_desc, nullptr, &copy.copy)))
      return;
   // On a deferred context the copy runs when its command list does (OnFinishCommandList, OnExecuteCommandList)
   context->CopyResource(copy.copy.Get(), buffer.Get());
   std::scoped_lock lock(tracked_mutex);
   tracked.push_back(std::move(copy));
   if (tracked.size() > MAX_TRACKED_COPIES)
      tracked.erase(tracked.begin());
}

void OnFinishCommandList(const ID3D11DeviceContext* context, const ID3D11CommandList* list)
{
   std::scoped_lock lock(tracked_mutex);
   for (TrackedCopy& copy : tracked)
   {
      if (copy.deferred_context == context && !copy.command_list)
         copy.command_list = list;
   }
}

void OnExecuteCommandList(ID3D11DeviceContext* context, const ID3D11CommandList* list)
{
   // A list executed into another deferred context is recorded by that one now
   const bool immediate = context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
   std::scoped_lock lock(tracked_mutex);
   for (TrackedCopy& copy : tracked)
   {
      if (copy.command_list != list || copy.executed)
         continue;
      if (immediate)
         copy.executed = true;
      else
      {
         copy.deferred_context = context;
         copy.command_list = nullptr;
      }
   }
}

void ReadBackTracked()
{
   // The newest copy whose contents are there, older ones dropped; the others keep waiting for their execution
   std::optional<TrackedCopy> copy;
   {
      std::scoped_lock lock(tracked_mutex);
      const std::optional<Track::Target> target = Track::GetTarget();
      std::erase_if(tracked, [&](const TrackedCopy& entry) { return entry.target != target; });
      const auto newest = std::ranges::find_last_if(tracked, &TrackedCopy::executed);
      if (!newest.empty())
      {
         copy = std::move(*newest.begin());
         tracked.erase(tracked.begin(), newest.begin() + 1);
      }
   }
   if (!Track::IsActive())
      return;
   if (!copy)
      return Track::SetReading(std::nullopt);
   ComPtr<ID3D11DeviceContext> immediate;
   copy->device->GetImmediateContext(&immediate);
   D3D11_BUFFER_DESC desc;
   copy->copy->GetDesc(&desc);
   D3D11_MAPPED_SUBRESOURCE mapped = {};
   if (FAILED(immediate->Map(copy->copy.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
      return Track::SetReading(std::nullopt);
   // The range the shader sees (16 bytes a constant)
   const size_t begin = std::min<size_t>(size_t(copy->first_constant) * 16, desc.ByteWidth);
   const size_t end = copy->constant_count ? std::min<size_t>(begin + size_t(copy->constant_count) * 16, desc.ByteWidth) : desc.ByteWidth;
   const auto* bytes = static_cast<const std::byte*>(mapped.pData);
   Track::Reading reading = {.buffer = std::move(copy->buffer), .first_constant = copy->first_constant, .constant_count = copy->constant_count, .deferred = copy->deferred_context != nullptr, .contents = std::vector<std::byte>(bytes + begin, bytes + end)};
   immediate->Unmap(copy->copy.Get(), 0);
   Track::SetReading(std::move(reading));
}

void ReadBackConstantBuffers()
{
   std::vector<Pending> read;
   {
      std::scoped_lock lock(pending_mutex);
      read.swap(pending);
   }
   copied_bytes = 0;
   for (Pending& copy : read)
   {
      D3D11_BUFFER_DESC desc;
      copy.copy->GetDesc(&desc);
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(copy.context->Map(copy.copy.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
         continue;
      const auto* bytes = static_cast<const std::byte*>(mapped.pData);
      SetContents(copy.event, copy.constant_buffer, std::vector<std::byte>(bytes, bytes + desc.ByteWidth));
      copy.context->Unmap(copy.copy.Get(), 0);
   }
}
} // namespace D3D11
} // namespace Capture
} // namespace Trace
} // namespace Luma
#endif
