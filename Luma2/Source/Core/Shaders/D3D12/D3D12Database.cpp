#include "Shaders/D3D12/D3D12Database.hpp"

#include <cstddef>
#include <algorithm>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>

#include "Hooks/Lifetime.hpp"
#include "Utils/Hash.hpp"
#include "System/Modules.hpp"
#include "Utils/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Shaders {
namespace Database {
namespace D3D12 {
namespace {
std::atomic<uint32_t> live_pipelines = 0;
std::atomic<uint32_t> extended_signatures = 0;
std::atomic<uint32_t> game_signatures = 0;

// In the private data of each of the game's pipelines
struct PipelineState
{
   Pipeline pipeline;

   explicit PipelineState(Pipeline pipeline_) : pipeline(std::move(pipeline_))
   {
      live_pipelines++;
      ForEachShader([](Shader& shader) { shader.objects++; });
      if (pipeline.root_layout)
         const_cast<RootLayout*>(pipeline.root_layout)->pipelines++;
   }

   ~PipelineState()
   {
      live_pipelines--;
      ForEachShader([](Shader& shader) { shader.objects--; });
      if (pipeline.root_layout)
         const_cast<RootLayout*>(pipeline.root_layout)->pipelines--;
   }

   PipelineState(const PipelineState&) = delete;
   PipelineState& operator=(const PipelineState&) = delete;

   template <typename Callback>
   void ForEachShader(Callback callback)
   {
      for (Shader* shader : pipeline.shaders)
      {
         if (shader)
            callback(*shader);
      }
   }
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
// Fills a DescCopy: pointed-to data moved into its storage, pointer fields noted so the content hash leaves them out
class Copier
{
public:
   explicit Copier(DescCopy& copy) : copy_(copy) {}

   template <typename T>
   T& At(size_t offset)
   {
      return *reinterpret_cast<T*>(copy_.bytes.data() + offset);
   }

   template <typename T>
   T* Store(const T* data, size_t count)
   {
      if (!data || count == 0)
         return nullptr;
      auto block = std::make_unique<std::byte[]>(sizeof(T) * count);
      std::memcpy(block.get(), data, sizeof(T) * count);
      T* stored = reinterpret_cast<T*>(block.get());
      copy_.storage.push_back(std::move(block));
      return stored;
   }

   const char* StoreString(const char* text) { return text ? Store(text, std::strlen(text) + 1) : nullptr; }

   void Shader(size_t offset, std::optional<ShaderStage> stage)
   {
      auto& bytecode = At<D3D12_SHADER_BYTECODE>(offset);
      bytecode.pShaderBytecode = Store(static_cast<const std::byte*>(bytecode.pShaderBytecode), bytecode.BytecodeLength);
      if (!bytecode.pShaderBytecode)
         bytecode.BytecodeLength = 0;
      if (stage && bytecode.pShaderBytecode)
         copy_.stages[size_t(*stage)] = offset;
   }

   void InputLayout(size_t offset)
   {
      auto& layout = At<D3D12_INPUT_LAYOUT_DESC>(offset);
      D3D12_INPUT_ELEMENT_DESC* elements = Store(layout.pInputElementDescs, layout.NumElements);
      for (UINT i = 0; elements && i < layout.NumElements; i++)
         elements[i].SemanticName = StoreString(elements[i].SemanticName);
      layout.pInputElementDescs = elements;
   }

   void StreamOutput(size_t offset)
   {
      auto& output = At<D3D12_STREAM_OUTPUT_DESC>(offset);
      D3D12_SO_DECLARATION_ENTRY* entries = Store(output.pSODeclaration, output.NumEntries);
      for (UINT i = 0; entries && i < output.NumEntries; i++)
         entries[i].SemanticName = StoreString(entries[i].SemanticName);
      output.pSODeclaration = entries;
      output.pBufferStrides = Store(output.pBufferStrides, output.NumStrides);
   }

   // The game's cached blob is for its pipeline, not a variant's
   void CachedPipeline(size_t offset) { At<D3D12_CACHED_PIPELINE_STATE>(offset) = {}; }

   void ViewInstancing(size_t offset)
   {
      auto& instancing = At<D3D12_VIEW_INSTANCING_DESC>(offset);
      instancing.pViewInstanceLocations = Store(instancing.pViewInstanceLocations, instancing.ViewInstanceCount);
   }

   void SerializedRootSignature(size_t offset)
   {
      auto& serialized = At<D3D12_SERIALIZED_ROOT_SIGNATURE_DESC>(offset);
      serialized.pSerializedBlob = Store(static_cast<const std::byte*>(serialized.pSerializedBlob), serialized.SerializedBlobSizeInBytes);
   }

private:
   DescCopy& copy_;
};

// The content hash of a copied desc, field by field: structs and streams have padding the game never initializes, so their bytes
// differ between boots
class ContentHasher
{
public:
   template <typename T>
      requires std::is_arithmetic_v<T> || std::is_enum_v<T>
   void Add(T value)
   {
      AddBytes(&value, sizeof(value));
   }

   void AddBytes(const void* data, size_t size)
   {
      const auto* bytes = static_cast<const uint8_t*>(data);
      for (size_t i = 0; i < size; i++)
      {
         hash_ ^= bytes[i];
         hash_ *= 0x100000001B3ull;
      }
   }

   void AddString(const char* text) { text ? AddBytes(text, std::strlen(text) + 1) : Add(uint8_t(0)); }

   // A struct of 32 bit members only, which has no padding
   template <typename T>
   void AddPacked(const T& value)
   {
      static_assert(sizeof(T) % 4 == 0);
      AddBytes(&value, sizeof(T));
   }

   // The game's blob: the same signature on every boot
   void Add(ID3D12RootSignature* root_signature)
   {
      const RootLayout* layout = FindRootLayout(root_signature);
      Add(uint32_t(layout ? Utils::Hash::CRC32(layout->blob) : 0));
   }

   void Add(const D3D12_BLEND_DESC& desc)
   {
      Add(desc.AlphaToCoverageEnable);
      Add(desc.IndependentBlendEnable);
      for (const D3D12_RENDER_TARGET_BLEND_DESC& target : desc.RenderTarget)
      {
         Add(target.BlendEnable);
         Add(target.LogicOpEnable);
         Add(target.SrcBlend);
         Add(target.DestBlend);
         Add(target.BlendOp);
         Add(target.SrcBlendAlpha);
         Add(target.DestBlendAlpha);
         Add(target.BlendOpAlpha);
         Add(target.LogicOp);
         Add(target.RenderTargetWriteMask);
      }
   }

   template <typename Face>
   void AddFace(const Face& face)
   {
      Add(face.StencilFailOp);
      Add(face.StencilDepthFailOp);
      Add(face.StencilPassOp);
      Add(face.StencilFunc);
      if constexpr (std::is_same_v<Face, D3D12_DEPTH_STENCILOP_DESC1>)
      {
         Add(face.StencilReadMask);
         Add(face.StencilWriteMask);
      }
   }

   template <typename Desc>
   void AddDepthStencil(const Desc& desc)
   {
      Add(desc.DepthEnable);
      Add(desc.DepthWriteMask);
      Add(desc.DepthFunc);
      Add(desc.StencilEnable);
      if constexpr (!std::is_same_v<Desc, D3D12_DEPTH_STENCIL_DESC2>)
      {
         Add(desc.StencilReadMask);
         Add(desc.StencilWriteMask);
      }
      AddFace(desc.FrontFace);
      AddFace(desc.BackFace);
      if constexpr (!std::is_same_v<Desc, D3D12_DEPTH_STENCIL_DESC>)
         Add(desc.DepthBoundsTestEnable);
   }

   void Add(const D3D12_INPUT_LAYOUT_DESC& desc)
   {
      Add(desc.NumElements);
      for (UINT i = 0; desc.pInputElementDescs && i < desc.NumElements; i++)
      {
         const D3D12_INPUT_ELEMENT_DESC& element = desc.pInputElementDescs[i];
         AddString(element.SemanticName);
         Add(element.SemanticIndex);
         Add(element.Format);
         Add(element.InputSlot);
         Add(element.AlignedByteOffset);
         Add(element.InputSlotClass);
         Add(element.InstanceDataStepRate);
      }
   }

   void Add(const D3D12_STREAM_OUTPUT_DESC& desc)
   {
      Add(desc.NumEntries);
      for (UINT i = 0; desc.pSODeclaration && i < desc.NumEntries; i++)
      {
         const D3D12_SO_DECLARATION_ENTRY& entry = desc.pSODeclaration[i];
         Add(entry.Stream);
         AddString(entry.SemanticName);
         Add(entry.SemanticIndex);
         Add(entry.StartComponent);
         Add(entry.ComponentCount);
         Add(entry.OutputSlot);
      }
      Add(desc.NumStrides);
      if (desc.pBufferStrides)
         AddBytes(desc.pBufferStrides, sizeof(UINT) * desc.NumStrides);
      Add(desc.RasterizedStream);
   }

   void Add(const D3D12_VIEW_INSTANCING_DESC& desc)
   {
      Add(desc.ViewInstanceCount);
      if (desc.pViewInstanceLocations)
         AddBytes(desc.pViewInstanceLocations, sizeof(D3D12_VIEW_INSTANCE_LOCATION) * desc.ViewInstanceCount);
      Add(desc.Flags);
   }

   // A stream subobject's value. Shaders and the cached blob aren't part of the content.
   void AddSubobject(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type, const std::byte* value)
   {
      Add(type);
      const auto as = [&]<typename T>() -> const T& { return *reinterpret_cast<const T*>(value); };
      switch (type)
      {
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: Add(as.operator()<ID3D12RootSignature*>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: Add(as.operator()<D3D12_BLEND_DESC>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: AddDepthStencil(as.operator()<D3D12_DEPTH_STENCIL_DESC>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: AddDepthStencil(as.operator()<D3D12_DEPTH_STENCIL_DESC1>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2: AddDepthStencil(as.operator()<D3D12_DEPTH_STENCIL_DESC2>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: Add(as.operator()<D3D12_INPUT_LAYOUT_DESC>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: Add(as.operator()<D3D12_STREAM_OUTPUT_DESC>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: Add(as.operator()<D3D12_VIEW_INSTANCING_DESC>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SERIALIZED_ROOT_SIGNATURE:
      {
         const auto& serialized = as.operator()<D3D12_SERIALIZED_ROOT_SIGNATURE_DESC>();
         if (serialized.pSerializedBlob)
            AddBytes(serialized.pSerializedBlob, serialized.SerializedBlobSizeInBytes);
         break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK: AddPacked(as.operator()<UINT>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: AddPacked(as.operator()<D3D12_RASTERIZER_DESC>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1: AddPacked(as.operator()<D3D12_RASTERIZER_DESC1>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2: AddPacked(as.operator()<D3D12_RASTERIZER_DESC2>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE: AddPacked(as.operator()<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: AddPacked(as.operator()<D3D12_PRIMITIVE_TOPOLOGY_TYPE>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: AddPacked(as.operator()<D3D12_RT_FORMAT_ARRAY>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: AddPacked(as.operator()<DXGI_FORMAT>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: AddPacked(as.operator()<DXGI_SAMPLE_DESC>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: AddPacked(as.operator()<UINT>()); break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: AddPacked(as.operator()<D3D12_PIPELINE_STATE_FLAGS>()); break;
      default: break; // Shaders, the cached blob
      }
   }

   void Add(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
   {
      Add(desc.pRootSignature);
      Add(desc.StreamOutput);
      Add(desc.BlendState);
      AddPacked(desc.SampleMask);
      AddPacked(desc.RasterizerState);
      AddDepthStencil(desc.DepthStencilState);
      Add(desc.InputLayout);
      Add(desc.IBStripCutValue);
      Add(desc.PrimitiveTopologyType);
      Add(desc.NumRenderTargets);
      AddBytes(desc.RTVFormats, sizeof(desc.RTVFormats));
      Add(desc.DSVFormat);
      AddPacked(desc.SampleDesc);
      Add(desc.NodeMask);
      Add(desc.Flags);
   }

   void Add(const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc)
   {
      Add(desc.pRootSignature);
      Add(desc.NodeMask);
      Add(desc.Flags);
   }

   uint64_t Get() const { return hash_; }

private:
   uint64_t hash_ = 0xCBF29CE484222325ull;
};

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
   Copier copier(*copy);
   switch (kind)
   {
   case PipelineKind::Graphics:
   {
      using Desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC;
      CopyBytes(*copy, *static_cast<const Desc*>(native));
      copier.Shader(offsetof(Desc, VS), ShaderStage::Vertex);
      copier.Shader(offsetof(Desc, HS), ShaderStage::Hull);
      copier.Shader(offsetof(Desc, DS), ShaderStage::Domain);
      copier.Shader(offsetof(Desc, GS), ShaderStage::Geometry);
      copier.Shader(offsetof(Desc, PS), ShaderStage::Pixel);
      copier.StreamOutput(offsetof(Desc, StreamOutput));
      copier.InputLayout(offsetof(Desc, InputLayout));
      copier.CachedPipeline(offsetof(Desc, CachedPSO));
      break;
   }
   case PipelineKind::Compute:
   {
      using Desc = D3D12_COMPUTE_PIPELINE_STATE_DESC;
      CopyBytes(*copy, *static_cast<const Desc*>(native));
      copier.Shader(offsetof(Desc, CS), ShaderStage::Compute);
      copier.CachedPipeline(offsetof(Desc, CachedPSO));
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
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: copier.InputLayout(value); break;
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: copier.StreamOutput(value); break;
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: copier.CachedPipeline(value); break;
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: copier.ViewInstancing(value); break;
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SERIALIZED_ROOT_SIGNATURE: copier.SerializedRootSignature(value); break;
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
         case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: copier.Shader(value, std::nullopt); break;
         default:
            if (const std::optional<ShaderStage> stage = GetStage(type))
               copier.Shader(value, *stage);
            break;
         }
         offset += layout->size;
      }
      break;
   }
   }
   ContentHasher hasher;
   hasher.Add(uint8_t(kind));
   switch (kind)
   {
   case PipelineKind::Graphics: hasher.Add(*reinterpret_cast<const D3D12_GRAPHICS_PIPELINE_STATE_DESC*>(copy->bytes.data())); break;
   case PipelineKind::Compute: hasher.Add(*reinterpret_cast<const D3D12_COMPUTE_PIPELINE_STATE_DESC*>(copy->bytes.data())); break;
   case PipelineKind::Stream:
      for (size_t offset = 0; offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= copy->bytes.size();)
      {
         D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
         std::memcpy(&type, copy->bytes.data() + offset, sizeof(type));
         const SubobjectLayout layout = *GetSubobjectLayout(type);
         hasher.AddSubobject(type, copy->bytes.data() + offset + layout.value_offset);
         offset += layout.size;
      }
      break;
   }
   copy->content_hash = hasher.Get();
   return copy;
}

PipelineDesc Describe(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
{
   PipelineDesc result{.kind = PipelineKind::Graphics, .root_signature = desc.pRootSignature};
   result.stages[size_t(ShaderStage::Vertex)] = ToSpan(desc.VS);
   result.stages[size_t(ShaderStage::Hull)] = ToSpan(desc.HS);
   result.stages[size_t(ShaderStage::Domain)] = ToSpan(desc.DS);
   result.stages[size_t(ShaderStage::Geometry)] = ToSpan(desc.GS);
   result.stages[size_t(ShaderStage::Pixel)] = ToSpan(desc.PS);
   return result;
}

PipelineDesc Describe(const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc)
{
   PipelineDesc result{.kind = PipelineKind::Compute, .root_signature = desc.pRootSignature};
   result.stages[size_t(ShaderStage::Compute)] = ToSpan(desc.CS);
   return result;
}

PipelineDesc Describe(const D3D12_PIPELINE_STATE_STREAM_DESC& desc)
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

const RootLayout* TrackRootSignature(std::span<const uint8_t> blob, ID3D12RootSignature* root_signature, uint32_t luma_parameter)
{
   // The driver returns the same object for the same blob
   if (const RootLayout* existing = FindRootLayout(root_signature))
      return existing;
   auto* layout = Hooks::Lifetime::SetState<RootLayout>(root_signature);
   if (!layout)
      return nullptr;
   layout->blob.assign(blob.begin(), blob.end());
   layout->luma_parameter = luma_parameter;
   if (!ReadLayout(blob, *layout))
   {
      Utils::Log::Warning("D3D12: root signature {} ({} bytes) couldn't be read", static_cast<void*>(root_signature), blob.size());
      return nullptr;
   }
   (luma_parameter != UINT32_MAX ? extended_signatures : game_signatures)++;
   return layout;
}

Stats GetStats()
{
   return {live_pipelines.load(), extended_signatures.load(), game_signatures.load()};
}

const RootLayout* FindRootLayout(ID3D12RootSignature* root_signature)
{
   return root_signature ? Hooks::Lifetime::GetState<RootLayout>(root_signature) : nullptr;
}

const Pipeline* TrackPipeline(const PipelineDesc& desc, std::shared_ptr<const DescCopy> copy, bool from_library, ID3D12PipelineState* pipeline)
{
   Pipeline tracked{.kind = desc.kind, .from_library = from_library, .root_signature = desc.root_signature, .desc = std::move(copy)};
   tracked.root_layout = FindRootLayout(desc.root_signature);
   tracked.embedded_root_signature = !desc.root_signature;
   for (size_t i = 0; i < desc.stages.size(); i++)
   {
      if (!desc.stages[i].empty())
         tracked.shaders[i] = &Track(ShaderStage(i), desc.stages[i]);
   }
   const PipelineState* state = Hooks::Lifetime::SetState<PipelineState>(pipeline, std::move(tracked));
   return state ? &state->pipeline : nullptr;
}

const Pipeline* Find(ID3D12PipelineState* pipeline)
{
   const PipelineState* state = pipeline ? Hooks::Lifetime::GetState<PipelineState>(pipeline) : nullptr;
   return state ? &state->pipeline : nullptr;
}
} // namespace D3D12
} // namespace Database
} // namespace Shaders
} // namespace Luma
