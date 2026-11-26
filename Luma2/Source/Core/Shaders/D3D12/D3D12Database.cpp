#include "Shaders/D3D12/D3D12Database.hpp"

#include <cstddef>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include "Hooks/ObjectLifetime.hpp"
#include "Resources/ResourceBindings.hpp"
#include "Shaders/D3D12/D3D12RootSignatures.hpp"
#include "Utils/Hash.hpp"
#include "System/Modules.hpp"
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Shaders {
namespace Database {
namespace D3D12 {
namespace {
std::atomic<uint32_t> live_pipelines = 0;
std::atomic<uint32_t> extended_signatures = 0;
std::atomic<uint32_t> game_signatures = 0;
std::atomic<uint32_t> largest_extended = 0;
// The game's size of each signature left unextended, for the status. Never held with another lock.
std::mutex unextended_mutex;
std::vector<uint32_t> unextended_dwords;
constexpr size_t MAX_UNEXTENDED_LISTED = 64;

template <typename Callback>
void ForEachPipelineShader(const Pipeline& pipeline, Callback callback)
{
   for (Shader* shader : pipeline.shaders)
   {
      if (shader)
         callback(*shader);
   }
}

// In the private data of each of the game's pipelines
struct PipelineState
{
   Pipeline pipeline;

   explicit PipelineState(Pipeline pipeline_) : pipeline(std::move(pipeline_))
   {
      live_pipelines++;
      ForEachPipelineShader(pipeline, [](Shader& shader) { shader.objects++; });
      if (pipeline.root_layout)
         const_cast<RootLayout*>(pipeline.root_layout)->pipelines++;
   }

   ~PipelineState()
   {
      live_pipelines--;
      ForEachPipelineShader(pipeline, [](Shader& shader) { shader.objects--; });
      if (pipeline.root_layout)
         const_cast<RootLayout*>(pipeline.root_layout)->pipelines--;
   }

   PipelineState(const PipelineState&) = delete;
   PipelineState& operator=(const PipelineState&) = delete;
};

std::span<const uint8_t> ToSpan(const D3D12_SHADER_BYTECODE& bytecode)
{
   if (!bytecode.pShaderBytecode || bytecode.BytecodeLength == 0)
      return {};
   return {static_cast<const uint8_t*>(bytecode.pShaderBytecode), bytecode.BytecodeLength};
}

template <typename T>
constexpr SubobjectLayout GetLayout()
{
   const size_t value_offset = (sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) + alignof(T) - 1) / alignof(T) * alignof(T);
   const size_t end = value_offset + sizeof(T);
   return {value_offset, (end + alignof(void*) - 1) / alignof(void*) * alignof(void*)};
}

size_t AlignUp(size_t value, size_t alignment)
{
   return (value + alignment - 1) / alignment * alignment;
}

std::optional<ShaderStage> GetStage(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type)
{
   switch (type)
   {
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: return ShaderStage::Vertex;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: return ShaderStage::Hull;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: return ShaderStage::Domain;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: return ShaderStage::Geometry;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: return ShaderStage::Pixel;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: return ShaderStage::Compute;
   default: return std::nullopt;
   }
}

uint32_t GetSize(const D3D12_ROOT_PARAMETER1& parameter)
{
   switch (parameter.ParameterType)
   {
   case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: return 1;
   case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: return parameter.Constants.Num32BitValues;
   default: return 2;
   }
}

// From d3d12.dll's export, as the debug layer's: Luma links no D3D12 import library
PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER GetDeserializerFactory()
{
   static const auto factory = reinterpret_cast<PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER>(
      System::Modules::FindExport(System::Modules::FindSystemModule(L"d3d12.dll"), "D3D12CreateVersionedRootSignatureDeserializer"));
   return factory;
}

bool ReadLayout(std::span<const uint8_t> blob, RootLayout& layout)
{
   const auto factory = GetDeserializerFactory();
   ComPtr<ID3D12VersionedRootSignatureDeserializer> deserializer;
   if (!factory || FAILED(factory(blob.data(), blob.size(), IID_PPV_ARGS(&deserializer))))
      return false;
   const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* unconverted = deserializer->GetUnconvertedRootSignatureDesc();
   const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* converted = nullptr;
   if (!unconverted || FAILED(deserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &converted)) || !converted)
      return false;
   layout.version = unconverted->Version;
   const D3D12_ROOT_SIGNATURE_DESC1& desc = converted->Desc_1_1;
   layout.flags = desc.Flags;
   layout.static_samplers = desc.NumStaticSamplers;
   layout.static_sampler_descs.assign(desc.pStaticSamplers, desc.pStaticSamplers + desc.NumStaticSamplers);
   layout.parameters.reserve(desc.NumParameters);
   for (UINT i = 0; i < desc.NumParameters; i++)
   {
      const D3D12_ROOT_PARAMETER1& source = desc.pParameters[i];
      RootParameter& parameter = layout.parameters.emplace_back();
      parameter.type = source.ParameterType;
      parameter.visibility = source.ShaderVisibility;
      layout.size += GetSize(source);
      switch (source.ParameterType)
      {
      case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
      {
         uint32_t offset = 0;
         for (UINT r = 0; r < source.DescriptorTable.NumDescriptorRanges; r++)
         {
            const D3D12_DESCRIPTOR_RANGE1& range = source.DescriptorTable.pDescriptorRanges[r];
            if (range.OffsetInDescriptorsFromTableStart != D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
               offset = range.OffsetInDescriptorsFromTableStart;
            parameter.ranges.push_back({range.RangeType, range.NumDescriptors, range.BaseShaderRegister, range.RegisterSpace, offset});
            if (range.NumDescriptors != UINT_MAX)
               offset += range.NumDescriptors;
         }
         break;
      }
      case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
         parameter.shader_register = source.Constants.ShaderRegister;
         parameter.space = source.Constants.RegisterSpace;
         parameter.constant_count = source.Constants.Num32BitValues;
         break;
      default:
         parameter.shader_register = source.Descriptor.ShaderRegister;
         parameter.space = source.Descriptor.RegisterSpace;
         break;
      }
   }
   return true;
}

constexpr uint64_t FNV_OFFSET = 0xCBF29CE484222325ull;
constexpr uint64_t FNV_PRIME = 0x100000001B3ull;

// Helpers filling a DescCopy: pointed-to data moved into its storage so the desc stays valid after creation
template <typename T>
T& DescAt(DescCopy& copy, size_t offset)
{
   return *reinterpret_cast<T*>(copy.bytes.data() + offset);
}

template <typename T>
T* DescStore(DescCopy& copy, const T* data, size_t count)
{
   if (!data || count == 0)
      return nullptr;
   auto block = std::make_unique<std::byte[]>(sizeof(T) * count);
   std::memcpy(block.get(), data, sizeof(T) * count);
   T* stored = reinterpret_cast<T*>(block.get());
   copy.storage.push_back(std::move(block));
   return stored;
}

const char* DescStoreString(DescCopy& copy, const char* text)
{
   return text ? DescStore(copy, text, std::strlen(text) + 1) : nullptr;
}

void CopyShaderBytecode(DescCopy& copy, size_t offset, std::optional<ShaderStage> stage)
{
   auto& bytecode = DescAt<D3D12_SHADER_BYTECODE>(copy, offset);
   bytecode.pShaderBytecode = DescStore(copy, static_cast<const std::byte*>(bytecode.pShaderBytecode), bytecode.BytecodeLength);
   if (!bytecode.pShaderBytecode)
      bytecode.BytecodeLength = 0;
   if (stage && bytecode.pShaderBytecode)
      copy.stages[size_t(*stage)] = offset;
}

void CopyInputLayout(DescCopy& copy, size_t offset)
{
   auto& layout = DescAt<D3D12_INPUT_LAYOUT_DESC>(copy, offset);
   D3D12_INPUT_ELEMENT_DESC* elements = DescStore(copy, layout.pInputElementDescs, layout.NumElements);
   for (UINT i = 0; elements && i < layout.NumElements; i++)
      elements[i].SemanticName = DescStoreString(copy, elements[i].SemanticName);
   layout.pInputElementDescs = elements;
}

void CopyStreamOutput(DescCopy& copy, size_t offset)
{
   auto& output = DescAt<D3D12_STREAM_OUTPUT_DESC>(copy, offset);
   D3D12_SO_DECLARATION_ENTRY* entries = DescStore(copy, output.pSODeclaration, output.NumEntries);
   for (UINT i = 0; entries && i < output.NumEntries; i++)
      entries[i].SemanticName = DescStoreString(copy, entries[i].SemanticName);
   output.pSODeclaration = entries;
   output.pBufferStrides = DescStore(copy, output.pBufferStrides, output.NumStrides);
}

// The game's cached blob is for its pipeline, not a variant's
void ClearCachedPipeline(DescCopy& copy, size_t offset)
{
   DescAt<D3D12_CACHED_PIPELINE_STATE>(copy, offset) = {};
}

void CopyViewInstancing(DescCopy& copy, size_t offset)
{
   auto& instancing = DescAt<D3D12_VIEW_INSTANCING_DESC>(copy, offset);
   instancing.pViewInstanceLocations = DescStore(copy, instancing.pViewInstanceLocations, instancing.ViewInstanceCount);
}

void CopySerializedRootSignature(DescCopy& copy, size_t offset)
{
   auto& serialized = DescAt<D3D12_SERIALIZED_ROOT_SIGNATURE_DESC>(copy, offset);
   serialized.pSerializedBlob = DescStore(copy, static_cast<const std::byte*>(serialized.pSerializedBlob), serialized.SerializedBlobSizeInBytes);
}

// Content hash of a copied desc, field by field: structs and streams have padding the game never initializes, so their bytes differ between boots
void HashBytes(uint64_t& hash, const void* data, size_t size)
{
   const auto* bytes = static_cast<const uint8_t*>(data);
   for (size_t i = 0; i < size; i++)
   {
      hash ^= bytes[i];
      hash *= FNV_PRIME;
   }
}

template <typename T>
   requires std::is_arithmetic_v<T> || std::is_enum_v<T>
void HashValue(uint64_t& hash, T value)
{
   HashBytes(hash, &value, sizeof(value));
}

void HashString(uint64_t& hash, const char* text)
{
   text ? HashBytes(hash, text, std::strlen(text) + 1) : HashValue(hash, uint8_t(0));
}

// A struct of 32 bit members only, which has no padding
template <typename T>
void HashPacked(uint64_t& hash, const T& value)
{
   static_assert(sizeof(T) % 4 == 0);
   HashBytes(hash, &value, sizeof(T));
}

// Includes Luma's extension so pipelines cached with a different extension won't collide
void HashRootSignature(uint64_t& hash, ID3D12RootSignature* root_signature)
{
   const RootLayout* layout = FindRootLayout(root_signature);
   HashValue(hash, uint32_t(layout ? Utils::Hash::CRC32(layout->blob) : 0));
   if (!layout)
      return;
   HashValue(hash, layout->luma_parameter);
   HashValue(hash, layout->luma_table);
   HashValue(hash, uint32_t(layout->static_sampler_descs.size()));
   HashValue(hash, Resources::Bindings::GetOptions().d3d12_space);
}

void HashBlend(uint64_t& hash, const D3D12_BLEND_DESC& desc)
{
   HashValue(hash, desc.AlphaToCoverageEnable);
   HashValue(hash, desc.IndependentBlendEnable);
   for (const D3D12_RENDER_TARGET_BLEND_DESC& target : desc.RenderTarget)
   {
      HashValue(hash, target.BlendEnable);
      HashValue(hash, target.LogicOpEnable);
      HashValue(hash, target.SrcBlend);
      HashValue(hash, target.DestBlend);
      HashValue(hash, target.BlendOp);
      HashValue(hash, target.SrcBlendAlpha);
      HashValue(hash, target.DestBlendAlpha);
      HashValue(hash, target.BlendOpAlpha);
      HashValue(hash, target.LogicOp);
      HashValue(hash, target.RenderTargetWriteMask);
   }
}

template <typename Face>
void HashStencilFace(uint64_t& hash, const Face& face)
{
   HashValue(hash, face.StencilFailOp);
   HashValue(hash, face.StencilDepthFailOp);
   HashValue(hash, face.StencilPassOp);
   HashValue(hash, face.StencilFunc);
   if constexpr (std::is_same_v<Face, D3D12_DEPTH_STENCILOP_DESC1>)
   {
      HashValue(hash, face.StencilReadMask);
      HashValue(hash, face.StencilWriteMask);
   }
}

template <typename Desc>
void HashDepthStencil(uint64_t& hash, const Desc& desc)
{
   HashValue(hash, desc.DepthEnable);
   HashValue(hash, desc.DepthWriteMask);
   HashValue(hash, desc.DepthFunc);
   HashValue(hash, desc.StencilEnable);
   if constexpr (!std::is_same_v<Desc, D3D12_DEPTH_STENCIL_DESC2>)
   {
      HashValue(hash, desc.StencilReadMask);
      HashValue(hash, desc.StencilWriteMask);
   }
   HashStencilFace(hash, desc.FrontFace);
   HashStencilFace(hash, desc.BackFace);
   if constexpr (!std::is_same_v<Desc, D3D12_DEPTH_STENCIL_DESC>)
      HashValue(hash, desc.DepthBoundsTestEnable);
}

void HashInputLayout(uint64_t& hash, const D3D12_INPUT_LAYOUT_DESC& desc)
{
   HashValue(hash, desc.NumElements);
   for (UINT i = 0; desc.pInputElementDescs && i < desc.NumElements; i++)
   {
      const D3D12_INPUT_ELEMENT_DESC& element = desc.pInputElementDescs[i];
      HashString(hash, element.SemanticName);
      HashValue(hash, element.SemanticIndex);
      HashValue(hash, element.Format);
      HashValue(hash, element.InputSlot);
      HashValue(hash, element.AlignedByteOffset);
      HashValue(hash, element.InputSlotClass);
      HashValue(hash, element.InstanceDataStepRate);
   }
}

void HashStreamOutput(uint64_t& hash, const D3D12_STREAM_OUTPUT_DESC& desc)
{
   HashValue(hash, desc.NumEntries);
   for (UINT i = 0; desc.pSODeclaration && i < desc.NumEntries; i++)
   {
      const D3D12_SO_DECLARATION_ENTRY& entry = desc.pSODeclaration[i];
      HashValue(hash, entry.Stream);
      HashString(hash, entry.SemanticName);
      HashValue(hash, entry.SemanticIndex);
      HashValue(hash, entry.StartComponent);
      HashValue(hash, entry.ComponentCount);
      HashValue(hash, entry.OutputSlot);
   }
   HashValue(hash, desc.NumStrides);
   if (desc.pBufferStrides)
      HashBytes(hash, desc.pBufferStrides, sizeof(UINT) * desc.NumStrides);
   HashValue(hash, desc.RasterizedStream);
}

void HashViewInstancing(uint64_t& hash, const D3D12_VIEW_INSTANCING_DESC& desc)
{
   HashValue(hash, desc.ViewInstanceCount);
   if (desc.pViewInstanceLocations)
      HashBytes(hash, desc.pViewInstanceLocations, sizeof(D3D12_VIEW_INSTANCE_LOCATION) * desc.ViewInstanceCount);
   HashValue(hash, desc.Flags);
}

// A stream subobject's value. Shaders and the cached blob aren't part of the content.
void HashSubobject(uint64_t& hash, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type, const std::byte* value)
{
   HashValue(hash, type);
   const auto as = [&]<typename T>() -> const T& { return *reinterpret_cast<const T*>(value); };
   switch (type)
   {
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: HashRootSignature(hash, as.operator()<ID3D12RootSignature*>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: HashBlend(hash, as.operator()<D3D12_BLEND_DESC>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: HashDepthStencil(hash, as.operator()<D3D12_DEPTH_STENCIL_DESC>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: HashDepthStencil(hash, as.operator()<D3D12_DEPTH_STENCIL_DESC1>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2: HashDepthStencil(hash, as.operator()<D3D12_DEPTH_STENCIL_DESC2>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: HashInputLayout(hash, as.operator()<D3D12_INPUT_LAYOUT_DESC>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: HashStreamOutput(hash, as.operator()<D3D12_STREAM_OUTPUT_DESC>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: HashViewInstancing(hash, as.operator()<D3D12_VIEW_INSTANCING_DESC>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SERIALIZED_ROOT_SIGNATURE:
   {
      const auto& serialized = as.operator()<D3D12_SERIALIZED_ROOT_SIGNATURE_DESC>();
      if (serialized.pSerializedBlob)
         HashBytes(hash, serialized.pSerializedBlob, serialized.SerializedBlobSizeInBytes);
      break;
   }
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK: HashPacked(hash, as.operator()<UINT>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: HashPacked(hash, as.operator()<D3D12_RASTERIZER_DESC>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1: HashPacked(hash, as.operator()<D3D12_RASTERIZER_DESC1>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2: HashPacked(hash, as.operator()<D3D12_RASTERIZER_DESC2>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE: HashPacked(hash, as.operator()<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: HashPacked(hash, as.operator()<D3D12_PRIMITIVE_TOPOLOGY_TYPE>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: HashPacked(hash, as.operator()<D3D12_RT_FORMAT_ARRAY>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: HashPacked(hash, as.operator()<DXGI_FORMAT>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: HashPacked(hash, as.operator()<DXGI_SAMPLE_DESC>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: HashPacked(hash, as.operator()<UINT>()); break;
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: HashPacked(hash, as.operator()<D3D12_PIPELINE_STATE_FLAGS>()); break;
   default: break; // Shaders, the cached blob
   }
}

void HashGraphicsDesc(uint64_t& hash, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
{
   HashRootSignature(hash, desc.pRootSignature);
   HashStreamOutput(hash, desc.StreamOutput);
   HashBlend(hash, desc.BlendState);
   HashPacked(hash, desc.SampleMask);
   HashPacked(hash, desc.RasterizerState);
   HashDepthStencil(hash, desc.DepthStencilState);
   HashInputLayout(hash, desc.InputLayout);
   HashValue(hash, desc.IBStripCutValue);
   HashValue(hash, desc.PrimitiveTopologyType);
   HashValue(hash, desc.NumRenderTargets);
   HashBytes(hash, desc.RTVFormats, sizeof(desc.RTVFormats));
   HashValue(hash, desc.DSVFormat);
   HashPacked(hash, desc.SampleDesc);
   HashValue(hash, desc.NodeMask);
   HashValue(hash, desc.Flags);
}

void HashComputeDesc(uint64_t& hash, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc)
{
   HashRootSignature(hash, desc.pRootSignature);
   HashValue(hash, desc.NodeMask);
   HashValue(hash, desc.Flags);
}

template <typename Desc>
void CopyBytes(DescCopy& copy, const Desc& desc)
{
   copy.bytes.resize(sizeof(Desc));
   std::memcpy(copy.bytes.data(), &desc, sizeof(Desc));
}
} // namespace

std::optional<SubobjectLayout> GetSubobjectLayout(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type)
{
   switch (type)
   {
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: return GetLayout<ID3D12RootSignature*>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: return GetLayout<D3D12_SHADER_BYTECODE>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: return GetLayout<D3D12_STREAM_OUTPUT_DESC>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: return GetLayout<D3D12_BLEND_DESC>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK: return GetLayout<UINT>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: return GetLayout<D3D12_RASTERIZER_DESC>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: return GetLayout<D3D12_DEPTH_STENCIL_DESC>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: return GetLayout<D3D12_INPUT_LAYOUT_DESC>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE: return GetLayout<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: return GetLayout<D3D12_PRIMITIVE_TOPOLOGY_TYPE>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: return GetLayout<D3D12_RT_FORMAT_ARRAY>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: return GetLayout<DXGI_FORMAT>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: return GetLayout<DXGI_SAMPLE_DESC>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: return GetLayout<UINT>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: return GetLayout<D3D12_CACHED_PIPELINE_STATE>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: return GetLayout<D3D12_PIPELINE_STATE_FLAGS>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: return GetLayout<D3D12_DEPTH_STENCIL_DESC1>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: return GetLayout<D3D12_VIEW_INSTANCING_DESC>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2: return GetLayout<D3D12_DEPTH_STENCIL_DESC2>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1: return GetLayout<D3D12_RASTERIZER_DESC1>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2: return GetLayout<D3D12_RASTERIZER_DESC2>();
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SERIALIZED_ROOT_SIGNATURE: return GetLayout<D3D12_SERIALIZED_ROOT_SIGNATURE_DESC>();
   default: return std::nullopt;
   }
}

std::shared_ptr<const DescCopy> CopyDesc(PipelineKind kind, const void* native)
{
   auto copy = std::make_shared<DescCopy>();
   copy->kind = kind;
   switch (kind)
   {
   case PipelineKind::Graphics:
   {
      using Desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC;
      CopyBytes(*copy, *static_cast<const Desc*>(native));
      CopyShaderBytecode(*copy, offsetof(Desc, VS), ShaderStage::Vertex);
      CopyShaderBytecode(*copy, offsetof(Desc, HS), ShaderStage::Hull);
      CopyShaderBytecode(*copy, offsetof(Desc, DS), ShaderStage::Domain);
      CopyShaderBytecode(*copy, offsetof(Desc, GS), ShaderStage::Geometry);
      CopyShaderBytecode(*copy, offsetof(Desc, PS), ShaderStage::Pixel);
      CopyStreamOutput(*copy, offsetof(Desc, StreamOutput));
      CopyInputLayout(*copy, offsetof(Desc, InputLayout));
      ClearCachedPipeline(*copy, offsetof(Desc, CachedPSO));
      break;
   }
   case PipelineKind::Compute:
   {
      using Desc = D3D12_COMPUTE_PIPELINE_STATE_DESC;
      CopyBytes(*copy, *static_cast<const Desc*>(native));
      CopyShaderBytecode(*copy, offsetof(Desc, CS), ShaderStage::Compute);
      ClearCachedPipeline(*copy, offsetof(Desc, CachedPSO));
      break;
   }
   case PipelineKind::Stream:
   {
      const auto& desc = *static_cast<const D3D12_PIPELINE_STATE_STREAM_DESC*>(native);
      const auto* stream = static_cast<const std::byte*>(desc.pPipelineStateSubobjectStream);
      if (!stream)
         return nullptr;
      copy->bytes.assign(stream, stream + desc.SizeInBytes);
      size_t offset = 0;
      while (offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= copy->bytes.size())
      {
         D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
         std::memcpy(&type, copy->bytes.data() + offset, sizeof(type));
         const std::optional<SubobjectLayout> layout = GetSubobjectLayout(type);
         if (!layout || offset + layout->value_offset > copy->bytes.size())
            return nullptr;
         const size_t value = offset + layout->value_offset;
         switch (type)
         {
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: CopyInputLayout(*copy, value); break;
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: CopyStreamOutput(*copy, value); break;
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: ClearCachedPipeline(*copy, value); break;
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: CopyViewInstancing(*copy, value); break;
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SERIALIZED_ROOT_SIGNATURE: CopySerializedRootSignature(*copy, value); break;
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: CopyShaderBytecode(*copy, value, std::nullopt); break;
         default:
            if (const std::optional<ShaderStage> stage = GetStage(type))
               CopyShaderBytecode(*copy, value, *stage);
            break;
         }
         offset += layout->size;
      }
      break;
   }
   }
   uint64_t hash = FNV_OFFSET;
   HashValue(hash, uint8_t(kind));
   switch (kind)
   {
   case PipelineKind::Graphics: HashGraphicsDesc(hash, *reinterpret_cast<const D3D12_GRAPHICS_PIPELINE_STATE_DESC*>(copy->bytes.data())); break;
   case PipelineKind::Compute: HashComputeDesc(hash, *reinterpret_cast<const D3D12_COMPUTE_PIPELINE_STATE_DESC*>(copy->bytes.data())); break;
   case PipelineKind::Stream:
      for (size_t offset = 0; offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= copy->bytes.size();)
      {
         D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
         std::memcpy(&type, copy->bytes.data() + offset, sizeof(type));
         const SubobjectLayout layout = *GetSubobjectLayout(type);
         HashSubobject(hash, type, copy->bytes.data() + offset + layout.value_offset);
         offset += layout.size;
      }
      break;
   }
   copy->content_hash = hash;
   return copy;
}

PipelineDesc DescribePipeline(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
{
   PipelineDesc result{.kind = PipelineKind::Graphics, .root_signature = desc.pRootSignature};
   result.stages[size_t(ShaderStage::Vertex)] = ToSpan(desc.VS);
   result.stages[size_t(ShaderStage::Hull)] = ToSpan(desc.HS);
   result.stages[size_t(ShaderStage::Domain)] = ToSpan(desc.DS);
   result.stages[size_t(ShaderStage::Geometry)] = ToSpan(desc.GS);
   result.stages[size_t(ShaderStage::Pixel)] = ToSpan(desc.PS);
   return result;
}

PipelineDesc DescribePipeline(const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc)
{
   PipelineDesc result{.kind = PipelineKind::Compute, .root_signature = desc.pRootSignature};
   result.stages[size_t(ShaderStage::Compute)] = ToSpan(desc.CS);
   return result;
}

PipelineDesc DescribePipeline(const D3D12_PIPELINE_STATE_STREAM_DESC& desc)
{
   PipelineDesc result{.kind = PipelineKind::Stream};
   const auto* bytes = static_cast<const uint8_t*>(desc.pPipelineStateSubobjectStream);
   size_t offset = 0;
   while (bytes && offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= desc.SizeInBytes)
   {
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
      std::memcpy(&type, bytes + offset, sizeof(type));
      const std::optional<SubobjectLayout> layout = GetSubobjectLayout(type);
      if (!layout || offset + layout->size > AlignUp(desc.SizeInBytes, alignof(void*)))
      {
         result.unknown_subobject = true;
         break;
      }
      const uint8_t* value = bytes + offset + layout->value_offset;
      if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE)
         std::memcpy(&result.root_signature, value, sizeof(result.root_signature));
      else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS || type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS)
         result.mesh_shaders = true;
      else if (const std::optional<ShaderStage> stage = GetStage(type))
      {
         D3D12_SHADER_BYTECODE bytecode;
         std::memcpy(&bytecode, value, sizeof(bytecode));
         result.stages[size_t(*stage)] = ToSpan(bytecode);
      }
      offset += layout->size;
   }
   return result;
}

std::optional<GraphicsState> GetGraphicsState(const DescCopy& desc)
{
   GraphicsState state;
   // d3dx12's defaults, which a stream without the subobject gets
   for (D3D12_RENDER_TARGET_BLEND_DESC& target : state.blend.RenderTarget)
      target = {FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP,
         D3D12_COLOR_WRITE_ENABLE_ALL};
   state.rasterizer = {D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_BACK, FALSE, D3D12_DEFAULT_DEPTH_BIAS, D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
      D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
   const D3D12_DEPTH_STENCILOP_DESC keep = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
   state.depth_stencil = {TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS, FALSE, D3D12_DEFAULT_STENCIL_READ_MASK, D3D12_DEFAULT_STENCIL_WRITE_MASK, keep, keep};
   if (desc.kind == PipelineKind::Compute)
      return std::nullopt;
   if (desc.kind == PipelineKind::Graphics)
   {
      const auto& graphics = *reinterpret_cast<const D3D12_GRAPHICS_PIPELINE_STATE_DESC*>(desc.bytes.data());
      state.blend = graphics.BlendState;
      state.rasterizer = graphics.RasterizerState;
      state.depth_bias = float(graphics.RasterizerState.DepthBias);
      state.depth_stencil = graphics.DepthStencilState;
      state.targets.NumRenderTargets = graphics.NumRenderTargets;
      std::memcpy(state.targets.RTFormats, graphics.RTVFormats, sizeof(state.targets.RTFormats));
      state.depth_format = graphics.DSVFormat;
      state.topology = graphics.PrimitiveTopologyType;
      state.sample_count = graphics.SampleDesc.Count;
      return state;
   }
   const std::byte* bytes = desc.bytes.data();
   size_t offset = 0;
   bool compute = false;
   while (offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= desc.bytes.size())
   {
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
      std::memcpy(&type, bytes + offset, sizeof(type));
      const std::optional<SubobjectLayout> layout = GetSubobjectLayout(type);
      if (!layout || offset + layout->value_offset > desc.bytes.size())
         break;
      const std::byte* value = bytes + offset + layout->value_offset;
      const auto read = [&](auto& field) { std::memcpy(&field, value, std::min(sizeof(field), desc.bytes.size() - (offset + layout->value_offset))); };
      switch (type)
      {
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: compute = true; break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: read(state.blend); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
         read(state.rasterizer);
         state.depth_bias = float(state.rasterizer.DepthBias);
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1:
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2:
      {
         // The same fields up to the bias, a float; then the bias clamp, slope, depth clip; 1 continues as the original, 2 as well
         D3D12_RASTERIZER_DESC1 rasterizer = {};
         read(rasterizer);
         state.rasterizer.FillMode = rasterizer.FillMode;
         state.rasterizer.CullMode = rasterizer.CullMode;
         state.rasterizer.FrontCounterClockwise = rasterizer.FrontCounterClockwise;
         state.depth_bias = rasterizer.DepthBias;
         state.rasterizer.DepthBias = INT(rasterizer.DepthBias);
         state.rasterizer.DepthBiasClamp = rasterizer.DepthBiasClamp;
         state.rasterizer.SlopeScaledDepthBias = rasterizer.SlopeScaledDepthBias;
         state.rasterizer.DepthClipEnable = rasterizer.DepthClipEnable;
         if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1)
            state.rasterizer.MultisampleEnable = rasterizer.MultisampleEnable;
         break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: read(state.depth_stencil); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
      {
         D3D12_DEPTH_STENCIL_DESC1 depth = {};
         read(depth);
         state.depth_stencil = {depth.DepthEnable, depth.DepthWriteMask, depth.DepthFunc, depth.StencilEnable, depth.StencilReadMask, depth.StencilWriteMask,
            depth.FrontFace, depth.BackFace};
         break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2:
      {
         // Masks per face: the front face's are shown
         D3D12_DEPTH_STENCIL_DESC2 depth = {};
         read(depth);
         const auto face = [](const D3D12_DEPTH_STENCILOP_DESC1& op) {
            return D3D12_DEPTH_STENCILOP_DESC{op.StencilFailOp, op.StencilDepthFailOp, op.StencilPassOp, op.StencilFunc};
         };
         state.depth_stencil = {depth.DepthEnable, depth.DepthWriteMask, depth.DepthFunc, depth.StencilEnable, UINT8(depth.FrontFace.StencilReadMask),
            UINT8(depth.FrontFace.StencilWriteMask), face(depth.FrontFace), face(depth.BackFace)};
         break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: read(state.targets); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: read(state.depth_format); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: read(state.topology); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
      {
         DXGI_SAMPLE_DESC sample = {1, 0};
         read(sample);
         state.sample_count = sample.Count;
         break;
      }
      default: break;
      }
      offset += layout->size;
   }
   if (compute)
      return std::nullopt;
   return state;
}

bool TargetFormats::Any() const
{
   return depth != DXGI_FORMAT_UNKNOWN || std::ranges::any_of(render_targets, [](DXGI_FORMAT format) { return format != DXGI_FORMAT_UNKNOWN; });
}

const RootLayout* TrackRootSignature(std::span<const uint8_t> blob, ID3D12RootSignature* root_signature, uint32_t luma_parameter, bool luma_table,
   bool luma_samplers)
{
   // The driver returns the same object for the same blob
   if (const RootLayout* existing = FindRootLayout(root_signature))
      return existing;
   auto* layout = Hooks::Lifetime::SetState<RootLayout>(root_signature);
   if (!layout)
      return nullptr;
   layout->blob.assign(blob.begin(), blob.end());
   layout->luma_parameter = luma_parameter;
   layout->luma_table = luma_parameter != UINT32_MAX && luma_table;
   if (!ReadLayout(blob, *layout))
   {
      Log::Warning("D3D12: root signature {} ({} bytes) couldn't be read", static_cast<void*>(root_signature), blob.size());
      return nullptr;
   }
   if (luma_parameter != UINT32_MAX && luma_samplers)
      layout->static_sampler_descs.append_range(RootSignatures::D3D12::GetStaticSamplers());
   if (luma_parameter != UINT32_MAX)
   {
      extended_signatures++;
      uint32_t largest = largest_extended.load(std::memory_order_relaxed);
      while (layout->size > largest && !largest_extended.compare_exchange_weak(largest, layout->size, std::memory_order_relaxed))
      {
      }
   }
   else
   {
      game_signatures++;
      std::scoped_lock lock(unextended_mutex);
      if (unextended_dwords.size() < MAX_UNEXTENDED_LISTED)
         unextended_dwords.push_back(layout->size);
   }
   return layout;
}

Stats GetStats()
{
   Stats stats = {live_pipelines.load(), extended_signatures.load(), game_signatures.load(), largest_extended.load()};
   std::scoped_lock lock(unextended_mutex);
   stats.unextended_dwords = unextended_dwords;
   return stats;
}

const RootLayout* FindRootLayout(ID3D12RootSignature* root_signature)
{
   return root_signature ? Hooks::Lifetime::GetState<RootLayout>(root_signature) : nullptr;
}

const Pipeline* TrackPipeline(ID3D12Device* device, const PipelineDesc& desc, std::shared_ptr<const DescCopy> copy, bool from_library, ID3D12PipelineState* pipeline)
{
   const std::shared_ptr<Store> store = GetStore(device);
   Pipeline tracked{.kind = desc.kind, .from_library = from_library, .root_signature = desc.root_signature, .desc = std::move(copy)};
   tracked.root_layout = FindRootLayout(desc.root_signature);
   tracked.embedded_root_signature = !desc.root_signature;
   for (size_t i = 0; i < desc.stages.size(); i++)
   {
      if (!desc.stages[i].empty())
         tracked.shaders[i] = &Track(store.get(), ShaderStage(i), desc.stages[i]);
   }
   const PipelineState* state = Hooks::Lifetime::SetState<PipelineState>(pipeline, std::move(tracked));
   return state ? &state->pipeline : nullptr;
}

const Pipeline* FindPipeline(ID3D12PipelineState* pipeline)
{
   const PipelineState* state = pipeline ? Hooks::Lifetime::GetState<PipelineState>(pipeline) : nullptr;
   return state ? &state->pipeline : nullptr;
}
} // namespace D3D12
} // namespace Database
} // namespace Shaders
} // namespace Luma
