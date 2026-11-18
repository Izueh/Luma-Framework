#include "Trace/D3D12/D3D12Capture.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Windows.h>
#include <wrl/client.h>

#include "Resources/Bindings.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/Mirrors.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Utils/Format.hpp"
#include "Hooks/Lifetime.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Shaders/D3D12/D3D12Database.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"
#include "Shaders/Database.hpp"
#include "Shaders/Variants.hpp"
#include "Trace/StateNames.hpp"
#include "Trace/Track.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Trace {
namespace Capture {
namespace D3D12 {
namespace {
using Resources::Descriptors::D3D12::Descriptor;
using Resources::Descriptors::D3D12::Kind;
using Resources::Descriptors::D3D12::ResourceInfo;
using Shaders::Database::D3D12::RootLayout;
using Shaders::Database::D3D12::RootParameter;

struct RootArgument
{
   enum class Type : uint8_t
   {
      None,
      Table, // `value` is the GPU descriptor handle
      Descriptor, // `value` is the GPU address
      Constants,
   };
   Type type = Type::None;
   uint64_t value = 0;
   std::vector<uint32_t> constants;
};

// What the game set on a list. A list is recorded by one thread at a time, and executed only once closed.
struct ListState
{
   std::array<ID3D12RootSignature*, 2> signatures = {}; // Graphics, compute
   std::array<const RootLayout*, 2> layouts = {};
   std::array<std::vector<RootArgument>, 2> arguments;
   std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> render_targets;
   std::optional<D3D12_CPU_DESCRIPTOR_HANDLE> depth_stencil;
   UINT render_target_increment = 0;
   std::vector<D3D12_VIEWPORT> viewports;
   std::vector<D3D12_RECT> scissor_rects;
   D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
   std::array<float, 4> blend_factor = {1.f, 1.f, 1.f, 1.f};
   UINT stencil_ref = 0;
   // The state each resource was last moved to on this list by a whole resource (or first subresource) transition: what a readback's
   // barriers start from. A ClearState keeps them.
   std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> resource_states;
   // Recorded while capturing, until the list is reset
   std::vector<Event> events;
};

// What a capture may copy of cbuffer contents, so a frame of thousands of draws doesn't run out of memory
constexpr uint64_t MAX_COPIED_BYTES = 64ull << 20;
std::atomic<uint64_t> copied_bytes = 0;
// A root CBV has no size: what's copied of one the reflection doesn't size (a cbuffer's most)
constexpr uint32_t MAX_CONSTANT_BUFFER_BYTES = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
// Registers read of a range without the shader's declarations (an unbounded range has UINT_MAX), and with them (bindless arrays)
constexpr uint32_t MAX_UNREFLECTED_REGISTERS = 128;
constexpr uint32_t MAX_DECLARED_REGISTERS = 1024;

// WKPDID_D3DDebugObjectNameW and WKPDID_D3DDebugObjectName, without linking dxguid
constexpr GUID DEBUG_OBJECT_NAME_W = {0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x13, 0x2c, 0x87, 0x3d, 0x93, 0x1c, 0x9a}};
constexpr GUID DEBUG_OBJECT_NAME = {0x429b8c22, 0x9188, 0x4b0c, {0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00}};

// Never held with another lock
std::mutex tracked_mutex;
std::optional<Track::Reading> tracked_reading;

ListState* GetState(ID3D12GraphicsCommandList* list)
{
   return Hooks::Lifetime::GetOrCreateStateCached<ListState>(list);
}

std::string GetId(const void* object)
{
   return std::format("0x{:X}", reinterpret_cast<uintptr_t>(object));
}

std::string GetDebugName(ID3D12Resource* resource)
{
   std::array<wchar_t, 256> wide = {};
   UINT size = UINT(sizeof(wide) - sizeof(wchar_t));
   if (SUCCEEDED(resource->GetPrivateData(DEBUG_OBJECT_NAME_W, &size, wide.data())))
      return Utils::Strings::ToUtf8(std::wstring_view(wide.data(), wcsnlen(wide.data(), size / sizeof(wchar_t))));
   std::array<char, 256> name = {};
   size = UINT(name.size() - 1);
   if (SUCCEEDED(resource->GetPrivateData(DEBUG_OBJECT_NAME, &size, name.data())))
      return std::string(name.data(), strnlen(name.data(), size));
   return {};
}

// `expected_id` is the one the descriptor saw written (0: unknown). A resource Luma doesn't know, or one destroyed since, is named
// by address only and never touched.
Resource DescribeResource(ID3D12Resource* resource, uint64_t expected_id)
{
   Resource described = {.id = GetId(resource)};
   const std::optional<ResourceInfo> info = Resources::Descriptors::D3D12::FindResource(resource);
   if (!info || (expected_id && info->id != expected_id))
   {
      described.type = info ? "Stale" : "Unknown";
      return described;
   }
   const D3D12_RESOURCE_DESC& desc = info->desc;
   switch (desc.Dimension)
   {
   case D3D12_RESOURCE_DIMENSION_BUFFER:
      described.type = "Buffer";
      described.size = {uint32_t(desc.Width), 1, 1, 1};
      break;
   case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
      described.type = "Texture1D";
      described.size = {uint32_t(desc.Width), 1, desc.DepthOrArraySize, desc.MipLevels};
      break;
   case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
      described.type = desc.SampleDesc.Count > 1 ? std::format("Texture2D ({}x MSAA)", desc.SampleDesc.Count) : std::string("Texture2D");
      described.size = {uint32_t(desc.Width), desc.Height, desc.DepthOrArraySize, desc.MipLevels};
      break;
   case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
      described.type = "Texture3D";
      described.size = {uint32_t(desc.Width), desc.Height, desc.DepthOrArraySize, desc.MipLevels};
      break;
   default: described.type = "Unknown"; break;
   }
   described.format = desc.Format;
   described.debug_name = GetDebugName(resource);
   // What the call read or wrote while the mirrors are swapped in
   if (ID3D12Resource* mirror = Resources::Mirrors::IsSwapping() ? Resources::Mirrors::D3D12::GetMirror(resource) : nullptr)
   {
      const D3D12_RESOURCE_DESC mirror_desc = mirror->GetDesc();
      described.mirror = std::format("{} {}x{}", Utils::Format::GetName(mirror_desc.Format), uint32_t(mirror_desc.Width), mirror_desc.Height);
   }
   return described;
}

UINT GetMip(const Descriptor& descriptor)
{
   if (!descriptor.has_desc)
      return 0;
   switch (descriptor.kind)
   {
   case Kind::ShaderResource:
   {
      const D3D12_SHADER_RESOURCE_VIEW_DESC& desc = descriptor.desc.shader_resource;
      switch (desc.ViewDimension)
      {
      case D3D12_SRV_DIMENSION_TEXTURE1D: return desc.Texture1D.MostDetailedMip;
      case D3D12_SRV_DIMENSION_TEXTURE1DARRAY: return desc.Texture1DArray.MostDetailedMip;
      case D3D12_SRV_DIMENSION_TEXTURE2D: return desc.Texture2D.MostDetailedMip;
      case D3D12_SRV_DIMENSION_TEXTURE2DARRAY: return desc.Texture2DArray.MostDetailedMip;
      case D3D12_SRV_DIMENSION_TEXTURE3D: return desc.Texture3D.MostDetailedMip;
      case D3D12_SRV_DIMENSION_TEXTURECUBE: return desc.TextureCube.MostDetailedMip;
      case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY: return desc.TextureCubeArray.MostDetailedMip;
      default: return 0;
      }
   }
   case Kind::UnorderedAccess:
   {
      const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc = descriptor.desc.unordered_access;
      switch (desc.ViewDimension)
      {
      case D3D12_UAV_DIMENSION_TEXTURE1D: return desc.Texture1D.MipSlice;
      case D3D12_UAV_DIMENSION_TEXTURE1DARRAY: return desc.Texture1DArray.MipSlice;
      case D3D12_UAV_DIMENSION_TEXTURE2D: return desc.Texture2D.MipSlice;
      case D3D12_UAV_DIMENSION_TEXTURE2DARRAY: return desc.Texture2DArray.MipSlice;
      case D3D12_UAV_DIMENSION_TEXTURE3D: return desc.Texture3D.MipSlice;
      default: return 0;
      }
   }
   case Kind::RenderTarget:
   {
      const D3D12_RENDER_TARGET_VIEW_DESC& desc = descriptor.desc.render_target;
      switch (desc.ViewDimension)
      {
      case D3D12_RTV_DIMENSION_TEXTURE1D: return desc.Texture1D.MipSlice;
      case D3D12_RTV_DIMENSION_TEXTURE1DARRAY: return desc.Texture1DArray.MipSlice;
      case D3D12_RTV_DIMENSION_TEXTURE2D: return desc.Texture2D.MipSlice;
      case D3D12_RTV_DIMENSION_TEXTURE2DARRAY: return desc.Texture2DArray.MipSlice;
      case D3D12_RTV_DIMENSION_TEXTURE3D: return desc.Texture3D.MipSlice;
      default: return 0;
      }
   }
   case Kind::DepthStencil:
   {
      const D3D12_DEPTH_STENCIL_VIEW_DESC& desc = descriptor.desc.depth_stencil;
      switch (desc.ViewDimension)
      {
      case D3D12_DSV_DIMENSION_TEXTURE1D: return desc.Texture1D.MipSlice;
      case D3D12_DSV_DIMENSION_TEXTURE1DARRAY: return desc.Texture1DArray.MipSlice;
      case D3D12_DSV_DIMENSION_TEXTURE2D: return desc.Texture2D.MipSlice;
      case D3D12_DSV_DIMENSION_TEXTURE2DARRAY: return desc.Texture2DArray.MipSlice;
      default: return 0;
      }
   }
   default: return 0;
   }
}

DXGI_FORMAT GetViewFormat(const Descriptor& descriptor)
{
   if (!descriptor.has_desc)
      return DXGI_FORMAT_UNKNOWN;
   switch (descriptor.kind)
   {
   case Kind::ShaderResource: return descriptor.desc.shader_resource.Format;
   case Kind::UnorderedAccess: return descriptor.desc.unordered_access.Format;
   case Kind::RenderTarget: return descriptor.desc.render_target.Format;
   case Kind::DepthStencil: return descriptor.desc.depth_stencil.Format;
   default: return DXGI_FORMAT_UNKNOWN;
   }
}

View DescribeView(const Descriptor& descriptor, uint32_t slot)
{
   View described = {.slot = slot};
   if (descriptor.resource)
      described.resource = DescribeResource(descriptor.resource, descriptor.resource_id);
   else
      described.resource.type = "Null";
   const DXGI_FORMAT format = GetViewFormat(descriptor);
   described.format = format != DXGI_FORMAT_UNKNOWN ? format : described.resource.format;
   described.mip = GetMip(descriptor);
   const std::array<uint32_t, 4>& size = described.resource.size;
   described.size = {std::max(size[0] >> described.mip, 1u), std::max(size[1] >> described.mip, 1u), size[2]};
   return described;
}

// A root SRV or UAV: a buffer by address
View DescribeBufferView(D3D12_GPU_VIRTUAL_ADDRESS address, uint32_t slot)
{
   View described = {.slot = slot};
   if (const std::optional<ResourceInfo> info = Resources::Descriptors::D3D12::FindBuffer(address))
   {
      described.resource = DescribeResource(info->resource, info->id);
      described.size = {uint32_t(info->desc.Width - (address - info->address)), 1, 1};
   }
   else
      described.resource = {.id = std::format("0x{:X} (GPU address)", address), .type = "Unknown"};
   return described;
}

Sampler DescribeSampler(uint32_t slot, D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE u, D3D12_TEXTURE_ADDRESS_MODE v, D3D12_TEXTURE_ADDRESS_MODE w,
   float mip_lod_bias)
{
   return {slot, StateNames::Filter(filter), uint32_t(filter), {uint32_t(u), uint32_t(v), uint32_t(w)},
      std::format("{} {} {}", StateNames::Address(u), StateNames::Address(v), StateNames::Address(w)), mip_lod_bias};
}

// The parameters a stage sees: compute sees all of its signature's
bool IsVisible(const RootParameter& parameter, bool compute, ShaderStage stage)
{
   if (compute || parameter.visibility == D3D12_SHADER_VISIBILITY_ALL)
      return true;
   // Vertex, hull, domain, geometry, pixel: D3D12_SHADER_VISIBILITY_VERTEX (1) on, in ShaderStage's order
   return uint32_t(parameter.visibility) == uint32_t(stage) + 1;
}

const Shaders::Database::Reflection* GetReflection(const Shaders::Database::Shader* shader)
{
   return shader && shader->reflection.found ? &shader->reflection : nullptr;
}

using Shaders::Container::BindingType;

template <size_t N>
bool IsSet(const std::bitset<N>& slots, uint32_t reg)
{
   return reg < N && slots[reg];
}

// Whether `shader` reads the register: its declarations, in any space (SM5.1 and SM6 too); without them its reflection, which
// only knows space 0; without either, every register of the game's
bool Reads(const Shaders::Database::Shader* shader, BindingType type, uint32_t space, uint32_t reg)
{
   if (shader && shader->bindings.found)
      return shader->bindings.Declares(type, space, reg);
   const Shaders::Database::Reflection* reflection = GetReflection(shader);
   if (!reflection)
      return true;
   if (space != 0)
      return false;
   switch (type)
   {
   case BindingType::ConstantBuffer: return IsSet(reflection->constant_buffers, reg);
   case BindingType::ShaderResource: return IsSet(reflection->shader_resources, reg);
   case BindingType::UnorderedAccess: return IsSet(reflection->unordered_access, reg);
   default: return IsSet(reflection->samplers, reg);
   }
}

// The registers of a range looked at: more when the shader's declarations say which it reads
uint32_t GetReadCount(const Shaders::Database::D3D12::RootRange& range, const Shaders::Database::Shader* shader)
{
   return std::min(range.count, shader && shader->bindings.found ? MAX_DECLARED_REGISTERS : MAX_UNREFLECTED_REGISTERS);
}

BindingType GetBindingType(D3D12_DESCRIPTOR_RANGE_TYPE type)
{
   switch (type)
   {
   case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: return BindingType::ConstantBuffer;
   case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: return BindingType::ShaderResource;
   case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: return BindingType::UnorderedAccess;
   default: return BindingType::Sampler;
   }
}

// Luma's own parameters, after the game's in an extended signature: the game's shader doesn't read them
bool IsLumaParameter(const RootLayout& layout, size_t index)
{
   return index >= layout.luma_parameter;
}

// The cbuffer size the shader declares at `slot`, 0 when its reflection doesn't say
uint32_t GetDeclaredSize(const Shaders::Database::Shader* shader, uint32_t slot)
{
   if (!shader)
      return 0;
   for (const Shaders::Compiler::ConstantBuffer& buffer : shader->reflection.constant_buffer_layouts)
   {
      if (buffer.slot == slot)
         return buffer.size;
   }
   return 0;
}

std::vector<std::byte> CopyContents(const std::byte* source, size_t size)
{
   if (copied_bytes.fetch_add(size, std::memory_order_relaxed) + size > MAX_COPIED_BYTES)
      return {};
   return std::vector<std::byte>(source, source + size);
}

// A cbuffer at a GPU address, `size` bytes when the binding says (a CBV), otherwise what the shader declares
ConstantBuffer DescribeConstantBuffer(ShaderStage stage, uint32_t slot, uint32_t space, D3D12_GPU_VIRTUAL_ADDRESS address, uint32_t size, uint32_t declared)
{
   ConstantBuffer buffer = {.stage = stage, .slot = slot, .contents_ranged = true, .space = space};
   const std::optional<ResourceInfo> info = Resources::Descriptors::D3D12::FindBuffer(address);
   if (!info)
   {
      buffer.id = std::format("0x{:X} (GPU address)", address);
      buffer.size = size;
      return buffer;
   }
   buffer.id = GetId(info->resource);
   buffer.size = uint32_t(info->desc.Width);
   const uint64_t offset = address - info->address;
   const uint64_t available = info->desc.Width - offset;
   const uint64_t bound = std::min<uint64_t>(size ? size : declared ? declared
                                                                    : MAX_CONSTANT_BUFFER_BYTES,
      available);
   buffer.first_constant = uint32_t(offset / 16);
   buffer.constant_count = uint32_t((bound + 15) / 16);
   if (info->mapped)
      buffer.contents = CopyContents(info->mapped + offset, size_t(bound));
   return buffer;
}

// The cbuffers `stage` reads (all its parameters' without reflection), `only_slot` alone when set
void AddConstantBuffers(std::vector<ConstantBuffer>& buffers, const ListState& state, bool compute, ShaderStage stage, const Shaders::Database::Shader* shader,
   std::optional<uint32_t> only_slot = std::nullopt)
{
   const RootLayout* layout = state.layouts[compute];
   if (!layout)
      return;
   const std::vector<RootArgument>& arguments = state.arguments[compute];
   // Space 0 only for the slot the trace's Track names, and for the declared sizes (reflection's bind points)
   const auto wanted = [&](uint32_t space, uint32_t reg) { return (!only_slot || (*only_slot == reg && space == 0)) && Reads(shader, BindingType::ConstantBuffer, space, reg); };
   const auto declared = [&](uint32_t space, uint32_t reg) { return space == 0 ? GetDeclaredSize(shader, reg) : 0u; };
   for (size_t index = 0; index < layout->parameters.size() && index < arguments.size(); index++)
   {
      const RootParameter& parameter = layout->parameters[index];
      const RootArgument& argument = arguments[index];
      if (IsLumaParameter(*layout, index) || !IsVisible(parameter, compute, stage) || argument.type == RootArgument::Type::None)
         continue;
      switch (parameter.type)
      {
      case D3D12_ROOT_PARAMETER_TYPE_CBV:
         if (wanted(parameter.space, parameter.shader_register))
            buffers.push_back(DescribeConstantBuffer(stage, parameter.shader_register, parameter.space, argument.value, 0, declared(parameter.space, parameter.shader_register)));
         break;
      case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
         if (wanted(parameter.space, parameter.shader_register))
         {
            std::vector<uint32_t> values = argument.constants;
            values.resize(parameter.constant_count);
            const auto* bytes = reinterpret_cast<const std::byte*>(values.data());
            buffers.push_back({.stage = stage, .slot = parameter.shader_register, .id = "root constants", .size = parameter.constant_count * 4, .contents = std::vector<std::byte>(bytes, bytes + values.size() * 4), .contents_ranged = true, .space = parameter.space});
         }
         break;
      case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
         for (const Shaders::Database::D3D12::RootRange& range : parameter.ranges)
         {
            if (range.type != D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
               continue;
            for (uint32_t i = 0; i < GetReadCount(range, shader); i++)
            {
               const uint32_t reg = range.base_register + i;
               if (!wanted(range.space, reg))
                  continue;
               const std::optional<Descriptor> descriptor = Resources::Descriptors::D3D12::Read(D3D12_GPU_DESCRIPTOR_HANDLE{argument.value}, range.offset + i);
               if (descriptor && descriptor->kind == Kind::ConstantBuffer && descriptor->desc.constant_buffer.BufferLocation)
                  buffers.push_back(DescribeConstantBuffer(stage, reg, range.space, descriptor->desc.constant_buffer.BufferLocation,
                     descriptor->desc.constant_buffer.SizeInBytes, declared(range.space, reg)));
            }
         }
         break;
      default: break;
      }
   }
}

// The views and samplers `stage` reads
void AddViews(Event& event, const ListState& state, bool compute, ShaderStage stage, const Shaders::Database::Shader* shader)
{
   const RootLayout* layout = state.layouts[compute];
   if (!layout)
      return;
   const std::vector<RootArgument>& arguments = state.arguments[compute];
   const auto with_space = [](auto binding, uint32_t space) {
      binding.space = space;
      return binding;
   };
   for (size_t index = 0; index < layout->parameters.size() && index < arguments.size(); index++)
   {
      const RootParameter& parameter = layout->parameters[index];
      const RootArgument& argument = arguments[index];
      if (IsLumaParameter(*layout, index) || !IsVisible(parameter, compute, stage) || argument.type == RootArgument::Type::None)
         continue;
      if (parameter.type == D3D12_ROOT_PARAMETER_TYPE_SRV && Reads(shader, BindingType::ShaderResource, parameter.space, parameter.shader_register))
         event.shader_resources.push_back(with_space(DescribeBufferView(argument.value, parameter.shader_register), parameter.space));
      else if (parameter.type == D3D12_ROOT_PARAMETER_TYPE_UAV && Reads(shader, BindingType::UnorderedAccess, parameter.space, parameter.shader_register))
         event.unordered_access.push_back(with_space(DescribeBufferView(argument.value, parameter.shader_register), parameter.space));
      if (parameter.type != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
         continue;
      for (const Shaders::Database::D3D12::RootRange& range : parameter.ranges)
      {
         if (range.type == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
            continue;
         for (uint32_t i = 0; i < GetReadCount(range, shader); i++)
         {
            const uint32_t reg = range.base_register + i;
            if (!Reads(shader, GetBindingType(range.type), range.space, reg))
               continue;
            const std::optional<Descriptor> descriptor = Resources::Descriptors::D3D12::Read(D3D12_GPU_DESCRIPTOR_HANDLE{argument.value}, range.offset + i);
            if (!descriptor || descriptor->kind == Kind::None)
               continue;
            if (descriptor->kind == Kind::ShaderResource)
               event.shader_resources.push_back(with_space(DescribeView(*descriptor, reg), range.space));
            else if (descriptor->kind == Kind::UnorderedAccess)
               event.unordered_access.push_back(with_space(DescribeView(*descriptor, reg), range.space));
            else if (descriptor->kind == Kind::Sampler)
            {
               const D3D12_SAMPLER_DESC& desc = descriptor->desc.sampler;
               event.samplers.push_back(with_space(DescribeSampler(reg, desc.Filter, desc.AddressU, desc.AddressV, desc.AddressW, desc.MipLODBias), range.space));
            }
         }
      }
   }
   for (const D3D12_STATIC_SAMPLER_DESC& sampler : layout->static_sampler_descs)
   {
      const bool visible = compute || sampler.ShaderVisibility == D3D12_SHADER_VISIBILITY_ALL || uint32_t(sampler.ShaderVisibility) == uint32_t(stage) + 1;
      if (visible && Reads(shader, BindingType::Sampler, sampler.RegisterSpace, sampler.ShaderRegister))
         event.samplers.push_back(
            with_space(DescribeSampler(sampler.ShaderRegister, sampler.Filter, sampler.AddressU, sampler.AddressV, sampler.AddressW, sampler.MipLODBias), sampler.RegisterSpace));
   }
   const auto by_register = [](const auto& binding) { return std::pair(binding.space, binding.slot); };
   std::ranges::sort(event.shader_resources, {}, by_register);
   std::ranges::sort(event.unordered_access, {}, by_register);
   std::ranges::sort(event.samplers, {}, by_register);
}

// Render targets, blend, depth, rasterizer from the pipeline's desc; viewports, topology from the list
void AddOutputState(Event& event, ID3D12GraphicsCommandList* list, ListState& state, const Shaders::Database::D3D12::Pipeline* pipeline)
{
   std::optional<Shaders::Database::D3D12::GraphicsState> graphics;
   if (pipeline && pipeline->desc)
      graphics = Shaders::Database::D3D12::GetGraphicsState(*pipeline->desc);
   for (uint32_t slot = 0; slot < state.render_targets.size(); slot++)
   {
      const std::optional<Descriptor> descriptor = Resources::Descriptors::D3D12::Read(state.render_targets[slot]);
      if (!descriptor || descriptor->kind != Kind::RenderTarget)
         continue;
      View& view = event.render_targets.emplace_back(DescribeView(*descriptor, slot));
      if (graphics)
      {
         const D3D12_RENDER_TARGET_BLEND_DESC& blend = graphics->blend.RenderTarget[graphics->blend.IndependentBlendEnable ? slot : 0];
         view.blend = StateNames::DescribeBlend(blend);
         view.write_mask = blend.RenderTargetWriteMask;
      }
   }
   if (state.depth_stencil)
   {
      if (const std::optional<Descriptor> descriptor = Resources::Descriptors::D3D12::Read(*state.depth_stencil); descriptor && descriptor->kind == Kind::DepthStencil)
         event.depth_stencil_view = DescribeView(*descriptor, 0);
   }
   event.blend_factor = state.blend_factor;
   event.depth_stencil.stencil_ref = state.stencil_ref;
   if (graphics)
   {
      event.alpha_to_coverage = graphics->blend.AlphaToCoverageEnable;
      const D3D12_DEPTH_STENCIL_DESC& depth = graphics->depth_stencil;
      event.depth_stencil.state_bound = true;
      event.depth_stencil.depth_enable = depth.DepthEnable;
      event.depth_stencil.depth_write = depth.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL;
      event.depth_stencil.depth_func = StateNames::Comparison(depth.DepthFunc);
      event.depth_stencil.stencil_enable = depth.StencilEnable;
      event.depth_stencil.read_mask = depth.StencilReadMask;
      event.depth_stencil.write_mask = depth.StencilWriteMask;
      event.depth_stencil.front = StateNames::DescribeStencilFace(depth.FrontFace);
      event.depth_stencil.back = StateNames::DescribeStencilFace(depth.BackFace);
      const D3D12_RASTERIZER_DESC& rasterizer = graphics->rasterizer;
      event.rasterizer = {true, StateNames::Cull(rasterizer.CullMode), StateNames::Fill(rasterizer.FillMode), bool(rasterizer.FrontCounterClockwise),
         int32_t(graphics->depth_bias), rasterizer.SlopeScaledDepthBias, bool(rasterizer.DepthClipEnable), true, bool(rasterizer.MultisampleEnable)};
   }
   for (const D3D12_VIEWPORT& viewport : state.viewports)
      event.viewports.push_back({viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height, viewport.MinDepth, viewport.MaxDepth});
   for (const D3D12_RECT& rect : state.scissor_rects)
      event.scissor_rects.push_back({rect.left, rect.top, rect.right, rect.bottom});
   event.topology = uint32_t(state.topology);
   (void)list;
}

// A game call: its shaders from the pipeline bound, its main stage's views
// What Luma binds for a variant at the call, after the trace recorded the game's state: its cbuffers at its root parameters and its
// textures in its table, in its space, described from the variant's requests
void AddLumaBindings(Event& event, const Shaders::Variants::D3D12::Built* built)
{
   if (!built)
      return;
   const uint32_t space = Resources::Bindings::GetOptions().d3d12_space;
   for (size_t stage = 0; stage < built->compiled.size(); stage++)
   {
      if (!built->compiled[stage])
         continue;
      for (const Shaders::Request& request : built->compiled[stage]->requests)
      {
         const Resources::Database::Entry& entry = Resources::Database::Get(request.entry);
         if (entry.kind == Resources::Database::Kind::ConstantBuffer)
            event.constant_buffers.push_back({.stage = ShaderStage(stage), .slot = request.slot, .id = entry.name, .size = entry.size, .space = space});
         else if (entry.kind == Resources::Database::Kind::Texture)
            event.shader_resources.push_back({.slot = request.slot, .resource = {.id = entry.name, .type = "Luma texture", .debug_name = entry.name}, .space = space});
      }
   }
}

Recording BeginGameCall(ID3D12GraphicsCommandList* list, EventType type, Shaders::StageSet stages)
{
   Recording recording = {.event = {.type = type}, .list = list};
   Event& event = recording.event;
   event.thread = GetCurrentThreadId();
   ListState* state = GetState(list);
   if (!state)
      return recording;
   const bool compute = type == EventType::Dispatch;
   const Shaders::Variants::D3D12::BoundPipeline bound = Shaders::Variants::D3D12::GetBoundPipeline(list);
   ShaderStage main_stage = compute ? ShaderStage::Compute : ShaderStage::Vertex;
   for (uint32_t index = 0; index <= uint32_t(ShaderStage::Compute); index++)
   {
      const ShaderStage stage = ShaderStage(index);
      const Shaders::Database::Shader* shader = bound.tracked ? bound.tracked->shaders[index] : nullptr;
      if (!stages.Has(stage) || !shader)
         continue;
      Shader& recorded = event.shaders.emplace_back(Shader{.stage = stage, .hash = shader->hash, .name = std::string(shader->name)});
      for (const Shaders::Variants::Variant& variant : Shaders::Variants::GetVariants(shader->hash))
      {
         if (variant.compiled)
            recorded.variants.push_back(variant.source);
      }
      recorded.forced = shader->forced_variant.load(std::memory_order_relaxed);
      if (bound.built && bound.built->variant_stages.Has(stage))
         recorded.variant = bound.built->sources[index];
      AddConstantBuffers(event.constant_buffers, *state, compute, stage, shader);
      if (stage == ShaderStage::Pixel)
         main_stage = stage;
   }
   const Shaders::Database::Shader* main_shader = bound.tracked ? bound.tracked->shaders[size_t(main_stage)] : nullptr;
   AddViews(event, *state, compute, main_stage, main_shader);
   if (!compute)
      AddOutputState(event, list, *state, bound.tracked);
   AddLumaBindings(event, bound.built);
   return recording;
}

void SetIndirect(Event& event, const Game::D3D12::IndirectArguments& indirect)
{
   event.indirect = true;
   event.instance_count = indirect.max_count; // The most it runs: the arguments are on the GPU
}

std::vector<RootArgument>& GetArguments(ListState& state, bool compute, UINT index)
{
   std::vector<RootArgument>& arguments = state.arguments[compute];
   if (index >= arguments.size())
      arguments.resize(index + 1);
   return arguments;
}
} // namespace

void OnReset(ID3D12GraphicsCommandList* list)
{
   if (ListState* state = GetState(list))
      *state = {};
}

void OnClearState(ID3D12GraphicsCommandList* list)
{
   if (ListState* state = GetState(list))
   {
      std::vector<Event> events = std::move(state->events);
      std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> resource_states = std::move(state->resource_states);
      *state = {};
      state->events = std::move(events);
      state->resource_states = std::move(resource_states);
   }
}

namespace {
void AddResourceEvent(ID3D12GraphicsCommandList* list, Event event)
{
   if (ListState* state = GetState(list))
   {
      event.thread = GetCurrentThreadId();
      state->events.push_back(std::move(event));
   }
}
} // namespace

void AddCopy(ID3D12GraphicsCommandList* list, std::string_view name, ID3D12Resource* destination, ID3D12Resource* source, const Copy& copy)
{
   Event event = {.type = EventType::Copy, .name = std::string(name)};
   if (source)
      event.source = DescribeResource(source, 0);
   if (destination)
      event.target = DescribeResource(destination, 0);
   event.copy = copy;
   AddResourceEvent(list, std::move(event));
}

void AddClear(ID3D12GraphicsCommandList* list, std::string_view name, ID3D12Resource* resource, const std::array<float, 4>& values)
{
   Event event = {.type = EventType::Clear, .name = std::string(name)};
   if (resource)
      event.target = DescribeResource(resource, 0);
   event.clear_values = values;
   AddResourceEvent(list, std::move(event));
}

void AddClear(ID3D12GraphicsCommandList* list, std::string_view name, D3D12_CPU_DESCRIPTOR_HANDLE view, const std::array<float, 4>& values)
{
   const std::optional<Descriptor> descriptor = Resources::Descriptors::D3D12::Read(view);
   Event event = {.type = EventType::Clear, .name = std::string(name)};
   if (descriptor && descriptor->resource)
      event.target = DescribeResource(descriptor->resource, descriptor->resource_id);
   event.clear_values = values;
   AddResourceEvent(list, std::move(event));
}

void OnResourceBarrier(ID3D12GraphicsCommandList* list, UINT count, const D3D12_RESOURCE_BARRIER* barriers)
{
   ListState* state = GetState(list);
   if (!state || !barriers)
      return;
   for (UINT i = 0; i < count; i++)
   {
      const D3D12_RESOURCE_BARRIER& barrier = barriers[i];
      if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION || barrier.Flags == D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY ||
         (barrier.Transition.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES && barrier.Transition.Subresource != 0))
         continue;
      const auto it = std::ranges::find(state->resource_states, barrier.Transition.pResource, &std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>::first);
      if (it != state->resource_states.end())
         it->second = barrier.Transition.StateAfter;
      else
         state->resource_states.emplace_back(barrier.Transition.pResource, barrier.Transition.StateAfter);
   }
}

std::optional<BoundView> FindBoundView(ID3D12GraphicsCommandList* list, Readback::ViewKind view, ShaderStage stage, uint32_t slot)
{
   const ListState* state = GetState(list);
   if (!state)
      return std::nullopt;
   const auto found = [&](const Descriptor& descriptor, std::optional<D3D12_RESOURCE_STATES> required) -> std::optional<BoundView> {
      if (!descriptor.resource)
         return std::nullopt;
      // A resource destroyed since the view was written, or another at its address, is never touched
      const std::optional<ResourceInfo> info = Resources::Descriptors::D3D12::FindResource(descriptor.resource);
      if (!info || (descriptor.resource_id && info->id != descriptor.resource_id))
         return std::nullopt;
      BoundView bound = {.resource = descriptor.resource, .format = GetViewFormat(descriptor), .state = required};
      if (!bound.state)
      {
         const auto it = std::ranges::find(state->resource_states, descriptor.resource, &std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>::first);
         if (it != state->resource_states.end())
            bound.state = it->second;
      }
      return bound;
   };
   switch (view)
   {
   case Readback::ViewKind::RenderTarget:
   {
      if (slot >= state->render_targets.size())
         return std::nullopt;
      const std::optional<Descriptor> descriptor = Resources::Descriptors::D3D12::Read(state->render_targets[slot]);
      // A render target being drawn to is in that state alone
      return descriptor && descriptor->kind == Kind::RenderTarget ? found(*descriptor, D3D12_RESOURCE_STATE_RENDER_TARGET) : std::nullopt;
   }
   case Readback::ViewKind::Depth:
   case Readback::ViewKind::Stencil:
   {
      if (!state->depth_stencil)
         return std::nullopt;
      const std::optional<Descriptor> descriptor = Resources::Descriptors::D3D12::Read(*state->depth_stencil);
      return descriptor && descriptor->kind == Kind::DepthStencil ? found(*descriptor, std::nullopt) : std::nullopt;
   }
   case Readback::ViewKind::ShaderResource:
   case Readback::ViewKind::UnorderedAccess:
   {
      const bool compute = stage == ShaderStage::Compute;
      const bool uav = view == Readback::ViewKind::UnorderedAccess;
      const RootLayout* layout = state->layouts[compute];
      if (!layout)
         return std::nullopt;
      const std::vector<RootArgument>& arguments = state->arguments[compute];
      for (size_t index = 0; index < layout->parameters.size() && index < arguments.size(); index++)
      {
         const RootParameter& parameter = layout->parameters[index];
         const RootArgument& argument = arguments[index];
         if (IsLumaParameter(*layout, index) || !IsVisible(parameter, compute, stage) || parameter.type != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
            argument.type != RootArgument::Type::Table)
            continue;
         for (const Shaders::Database::D3D12::RootRange& range : parameter.ranges)
         {
            if (range.space != 0 || range.type != (uav ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV) || slot < range.base_register ||
               slot - range.base_register >= range.count)
               continue;
            const std::optional<Descriptor> descriptor = Resources::Descriptors::D3D12::Read(D3D12_GPU_DESCRIPTOR_HANDLE{argument.value}, range.offset + (slot - range.base_register));
            if (!descriptor || descriptor->kind != (uav ? Kind::UnorderedAccess : Kind::ShaderResource))
               return std::nullopt;
            // A UAV being written is in that state alone
            return found(*descriptor, uav ? std::optional(D3D12_RESOURCE_STATE_UNORDERED_ACCESS) : std::nullopt);
         }
      }
      return std::nullopt;
   }
   default: return std::nullopt;
   }
}

void OnSetRootSignature(ID3D12GraphicsCommandList* list, bool compute, ID3D12RootSignature* root_signature)
{
   ListState* state = GetState(list);
   if (!state || state->signatures[compute] == root_signature)
      return;
   // Another signature: the arguments set before are gone
   state->signatures[compute] = root_signature;
   state->layouts[compute] = root_signature ? Shaders::Database::D3D12::FindRootLayout(root_signature) : nullptr;
   state->arguments[compute].assign(state->layouts[compute] ? state->layouts[compute]->parameters.size() : 0, {});
}

void OnSetRenderTargets(ID3D12GraphicsCommandList* list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, BOOL single_range,
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil)
{
   ListState* state = GetState(list);
   if (!state)
      return;
   state->render_targets.clear();
   if (render_targets && count)
   {
      if (single_range && !state->render_target_increment)
      {
         Microsoft::WRL::ComPtr<ID3D12Device> device;
         if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))))
            state->render_target_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
      }
      for (UINT i = 0; i < count; i++)
         state->render_targets.push_back(single_range ? D3D12_CPU_DESCRIPTOR_HANDLE{render_targets[0].ptr + size_t(i) * state->render_target_increment} : render_targets[i]);
   }
   state->depth_stencil = depth_stencil ? std::optional(*depth_stencil) : std::nullopt;
}

void OnSetRootTable(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table)
{
   if (ListState* state = GetState(list))
      GetArguments(*state, compute, index)[index] = {RootArgument::Type::Table, table.ptr};
}

void OnSetRootDescriptor(ID3D12GraphicsCommandList* list, bool compute, UINT index, D3D12_GPU_VIRTUAL_ADDRESS address)
{
   if (ListState* state = GetState(list))
      GetArguments(*state, compute, index)[index] = {RootArgument::Type::Descriptor, address};
}

void OnSetRootConstants(ID3D12GraphicsCommandList* list, bool compute, UINT index, UINT count, const void* values, UINT offset)
{
   ListState* state = GetState(list);
   if (!state || !values)
      return;
   RootArgument& argument = GetArguments(*state, compute, index)[index];
   argument.type = RootArgument::Type::Constants;
   if (argument.constants.size() < size_t(offset) + count)
      argument.constants.resize(size_t(offset) + count);
   std::memcpy(argument.constants.data() + offset, values, size_t(count) * 4);
}

void OnSetViewports(ID3D12GraphicsCommandList* list, UINT count, const D3D12_VIEWPORT* viewports)
{
   if (ListState* state = GetState(list))
      state->viewports.assign(viewports, viewports + (viewports ? count : 0));
}

void OnSetScissorRects(ID3D12GraphicsCommandList* list, UINT count, const D3D12_RECT* rects)
{
   if (ListState* state = GetState(list))
      state->scissor_rects.assign(rects, rects + (rects ? count : 0));
}

void OnSetTopology(ID3D12GraphicsCommandList* list, D3D12_PRIMITIVE_TOPOLOGY topology)
{
   if (ListState* state = GetState(list))
      state->topology = topology;
}

void OnSetBlendFactor(ID3D12GraphicsCommandList* list, const FLOAT factor[4])
{
   if (ListState* state = GetState(list))
      state->blend_factor = factor ? std::array<float, 4>{factor[0], factor[1], factor[2], factor[3]} : std::array<float, 4>{1.f, 1.f, 1.f, 1.f};
}

void OnSetStencilRef(ID3D12GraphicsCommandList* list, UINT stencil_ref)
{
   if (ListState* state = GetState(list))
      state->stencil_ref = stencil_ref;
}

Recording BeginDraw(ID3D12GraphicsCommandList* list, const Game::D3D12::DrawCall& draw)
{
   using Game::D3D12::DrawFunction;
   Recording recording = BeginGameCall(list, EventType::Draw, Shaders::GRAPHICS_STAGES);
   Event& event = recording.event;
   constexpr std::array<std::string_view, 4> NAMES = {"DrawInstanced", "DrawIndexedInstanced", "DispatchMesh", "ExecuteIndirect"};
   event.name = NAMES[size_t(draw.function)];
   event.vertex_or_index_count = draw.vertex_or_index_count;
   event.instance_count = draw.instance_count;
   event.start_vertex_or_index = draw.start_vertex_or_index;
   event.base_vertex = draw.base_vertex;
   event.start_instance = draw.start_instance;
   event.indexed = draw.function == DrawFunction::DrawIndexedInstanced;
   if (draw.function == DrawFunction::DispatchMesh)
      event.dispatch = {draw.mesh_groups[0], draw.mesh_groups[1], draw.mesh_groups[2]};
   if (draw.function == DrawFunction::ExecuteIndirect)
      SetIndirect(event, draw.indirect);
   return recording;
}

Recording BeginDispatch(ID3D12GraphicsCommandList* list, const Game::D3D12::DispatchCall& dispatch)
{
   Recording recording = BeginGameCall(list, EventType::Dispatch, Shaders::COMPUTE_STAGES);
   recording.event.name = dispatch.indirect_call ? "ExecuteIndirect" : "Dispatch";
   recording.event.dispatch = {dispatch.x, dispatch.y, dispatch.z};
   if (dispatch.indirect_call)
      SetIndirect(recording.event, dispatch.indirect);
   return recording;
}

void End(Recording recording, Outcome outcome)
{
   recording.event.outcome = outcome;
   if (ListState* state = GetState(recording.list))
      state->events.push_back(std::move(recording.event));
}

void OnExecuteBundle(ID3D12GraphicsCommandList* list, ID3D12GraphicsCommandList* bundle)
{
   ListState* to = GetState(list);
   const ListState* from = bundle ? GetState(bundle) : nullptr;
   if (to && from)
      to->events.insert(to->events.end(), from->events.begin(), from->events.end());
}

void OnExecute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
   if (!IsCapturing())
      return;
   const std::string queue_id = GetId(queue);
   for (UINT i = 0; i < count; i++)
   {
      Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
      if (!lists[i] || FAILED(lists[i]->QueryInterface(IID_PPV_ARGS(&list))))
         continue;
      const ListState* state = Hooks::Lifetime::GetState<ListState>(list.Get());
      if (!state)
         continue;
      const std::string list_id = GetId(list.Get());
      // A list can be executed again: its events are copied
      for (Event event : state->events)
      {
         event.queue = queue_id;
         event.list = list_id;
         Add(std::move(event));
      }
   }
}

bool IsSkipped(ID3D12GraphicsCommandList* list, Shaders::StageSet stages)
{
   const Shaders::Variants::D3D12::BoundPipeline bound = Shaders::Variants::D3D12::GetBoundPipeline(list);
   if (!bound.tracked)
      return false;
   for (size_t stage = 0; stage < bound.tracked->shaders.size(); stage++)
   {
      const Shaders::Database::Shader* shader = bound.tracked->shaders[stage];
      if (stages.Has(ShaderStage(stage)) && shader && shader->skip_draws.load(std::memory_order_relaxed))
         return true;
   }
   return false;
}

void TrackCall(ID3D12GraphicsCommandList* list, Shaders::StageSet stages)
{
   const std::optional<Track::Target> target = Track::Get();
   if (!target || !stages.Has(target->stage))
      return;
   const Shaders::Variants::D3D12::BoundPipeline bound = Shaders::Variants::D3D12::GetBoundPipeline(list);
   const Shaders::Database::Shader* shader = bound.tracked ? bound.tracked->shaders[size_t(target->stage)] : nullptr;
   if (!shader || shader->hash != target->hash || Track::CountCall() != target->instance)
      return;
   const ListState* state = GetState(list);
   if (!state)
      return;
   std::vector<ConstantBuffer> buffers;
   AddConstantBuffers(buffers, *state, target->stage == ShaderStage::Compute, target->stage, shader, target->slot);
   if (buffers.empty() || buffers.front().contents.empty())
      return;
   ConstantBuffer& buffer = buffers.front();
   Track::Reading reading = {.buffer = std::move(buffer.id), .first_constant = buffer.first_constant, .constant_count = buffer.constant_count, .contents = std::move(buffer.contents)};
   std::scoped_lock lock(tracked_mutex);
   tracked_reading = std::move(reading);
}

void ReadBackTracked()
{
   // The copies of the next capture start from nothing
   copied_bytes = 0;
   std::optional<Track::Reading> reading;
   {
      std::scoped_lock lock(tracked_mutex);
      reading = std::exchange(tracked_reading, std::nullopt);
   }
   if (Track::IsActive())
      Track::SetReading(std::move(reading));
}
} // namespace D3D12
} // namespace Capture
} // namespace Trace
} // namespace Luma
#endif
