#include "Shaders/D3D12/D3D12RootSignatures.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <mutex>
#include <type_traits>
#include <unordered_map>

#include <d3d12.h>
#include <wrl/client.h>

#include "Hooks/Lifetime.hpp"
#include "Hooks/Scope.hpp"
#include "Resources/Bindings.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/Samplers.hpp"
#include "Shaders/Container.hpp"
#include "Utils/Hash.hpp"
#include "System/Modules.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Shaders {
namespace RootSignatures {
namespace D3D12 {
namespace {
constexpr uint32_t MAX_SIZE = 64; // DWORDs
constexpr uint32_t LUMA_SIZE = LUMA_PARAMETER_COUNT * 2; // Root descriptors are 2 DWORDs
constexpr uint32_t LUMA_TABLE_SIZE = 1; // A table is 1 DWORD

template <typename T>
T GetExport(const char* name)
{
   return reinterpret_cast<T>(System::Modules::FindExport(System::Modules::FindSystemModule(L"d3d12.dll"), name));
}

template <typename Parameter>
uint32_t GetSize(const Parameter& parameter)
{
   switch (parameter.ParameterType)
   {
   case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: return 1;
   case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: return parameter.Constants.Num32BitValues;
   default: return 2;
   }
}

bool Covers(uint32_t base, uint32_t count, uint32_t slot)
{
   return base <= slot && (count == UINT_MAX || slot < uint64_t(base) + count);
}

// Whether the game already uses Luma's registers. A space of Luma's own (not 0) is all Luma's: any register of any type in it is a
// conflict, so the views and samplers added there later never clash either. In space 0 only Luma's cbuffer slots are.
template <typename Parameter>
bool UsesLumaRegisters(const Parameter& parameter, uint32_t space, uint32_t settings_slot, uint32_t data_slot)
{
   const auto conflicts = [&](bool cbuffer, uint32_t register_space, uint32_t base, uint32_t count) {
      if (register_space != space)
         return false;
      return space != 0 || (cbuffer && (Covers(base, count, settings_slot) || Covers(base, count, data_slot)));
   };
   switch (parameter.ParameterType)
   {
   case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
      for (UINT i = 0; i < parameter.DescriptorTable.NumDescriptorRanges; i++)
      {
         const auto& range = parameter.DescriptorTable.pDescriptorRanges[i];
         if (conflicts(range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV, range.RegisterSpace, range.BaseShaderRegister, range.NumDescriptors))
            return true;
      }
      return false;
   case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: return conflicts(true, parameter.Constants.RegisterSpace, parameter.Constants.ShaderRegister, 1);
   case D3D12_ROOT_PARAMETER_TYPE_CBV: return conflicts(true, parameter.Descriptor.RegisterSpace, parameter.Descriptor.ShaderRegister, 1);
   default: return conflicts(false, parameter.Descriptor.RegisterSpace, parameter.Descriptor.ShaderRegister, 1);
   }
}

template <typename Parameter>
Parameter MakeConstantBuffer(uint32_t slot, uint32_t space)
{
   Parameter parameter = {};
   parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
   parameter.Descriptor.ShaderRegister = slot;
   parameter.Descriptor.RegisterSpace = space;
   // Version 1.1's default flags: Luma's ring memory isn't written after it's set
   parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
   return parameter;
}

// Luma's texture table: one SRV range in its space. Its descriptors are written once per heap and never change; the ones no patch
// reads are null views (version 1.1's volatile flag keeps validation quiet about them either way).
template <typename Parameter, typename Range>
Parameter MakeTable(const Range& range)
{
   Parameter parameter = {};
   parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
   parameter.DescriptorTable.NumDescriptorRanges = 1;
   parameter.DescriptorTable.pDescriptorRanges = &range;
   parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
   return parameter;
}

template <typename Range>
Range MakeTextureRange(uint32_t space)
{
   Range range = {};
   range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
   range.NumDescriptors = Resources::Bindings::D3D12_TEXTURE_REGISTERS;
   range.BaseShaderRegister = Resources::Bindings::D3D12_FIRST_TEXTURE_REGISTER;
   range.RegisterSpace = space;
   range.OffsetInDescriptorsFromTableStart = 0;
   if constexpr (requires { range.Flags; })
      range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
   return range;
}

// `desc` is the game's at its version (D3D12_ROOT_SIGNATURE_DESC, DESC1 or DESC2), with the parameter type of that version
template <typename Desc, typename Parameter>
Extended Append(const Desc& desc, D3D_ROOT_SIGNATURE_VERSION version, Desc D3D12_VERSIONED_ROOT_SIGNATURE_DESC::* member)
{
   const Resources::LumaCBuffers::Options& options = Resources::LumaCBuffers::GetOptions();
   const uint32_t space = Resources::Bindings::GetOptions().d3d12_space;
   uint32_t size = 0;
   bool conflict = false;
   for (UINT i = 0; i < desc.NumParameters; i++)
   {
      size += GetSize(desc.pParameters[i]);
      conflict |= UsesLumaRegisters(desc.pParameters[i], space, options.settings_slot, options.data_slot);
   }
   // Static samplers cost nothing, but take registers. Sampler upgrades can't reach them: a signature's samplers are fixed at its
   // creation (a mirror signature per offset would be the way).
   bool anisotropic = false;
   for (UINT i = 0; i < desc.NumStaticSamplers; i++)
   {
      conflict |= space != 0 && desc.pStaticSamplers[i].RegisterSpace == space;
      anisotropic |= D3D12_DECODE_IS_ANISOTROPIC_FILTER(desc.pStaticSamplers[i].Filter);
   }
   if (anisotropic && Resources::Samplers::IsNeeded())
      LUMA_UNIMPLEMENTED("Sampler upgrades of anisotropic static samplers in D3D12 root signatures");
   if (conflict)
   {
      // A game shader reading a slot Luma binds its own to is a conflict. The mod picks another space (Game::Options::bindings).
      ASSERT_ONCE_MSG(false, std::format("A game root signature uses register space {}, Luma's: pick another in Game::Options::bindings", space));
      if (space != 0)
         Utils::Log::Warning("D3D12: a root signature uses register space {}, Luma's: not extended, its pipelines get no variants", space);
      else
         Utils::Log::Warning("D3D12: a root signature declares b{} or b{} in space 0: not extended, its pipelines get no variants", options.settings_slot, options.data_slot);
      return {};
   }
   if (size + LUMA_SIZE > MAX_SIZE)
   {
      ASSERT_ONCE_MSG(false, "A game root signature has no room for Luma's parameters");
      Utils::Log::Warning("D3D12: a root signature of {} DWORDs has no room for Luma's {}: not extended, its pipelines get no variants", size, LUMA_SIZE);
      return {};
   }

   // In space 0 the table's registers could be the game's
   const bool table = space != 0 && size + LUMA_SIZE + LUMA_TABLE_SIZE <= MAX_SIZE;
   if (!table && space != 0)
      Utils::Log::Warning("D3D12: a root signature of {} DWORDs has no room for Luma's texture table: patches that add textures fall back with its pipelines", size);

   std::vector<Parameter> parameters(desc.pParameters, desc.pParameters + desc.NumParameters);
   const uint32_t luma_parameter = uint32_t(parameters.size());
   parameters.push_back(MakeConstantBuffer<Parameter>(options.settings_slot, space));
   parameters.push_back(MakeConstantBuffer<Parameter>(options.data_slot, space));
   using Range = std::remove_cvref_t<decltype(*Parameter{}.DescriptorTable.pDescriptorRanges)>;
   const Range texture_range = MakeTextureRange<Range>(space);
   if (table)
      parameters.push_back(MakeTable<Parameter>(texture_range));
   Desc extended_desc = desc;
   extended_desc.NumParameters = UINT(parameters.size());
   extended_desc.pParameters = parameters.data();
   D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned = {.Version = version};
   versioned.*member = extended_desc;

   static const auto serialize = GetExport<PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE>("D3D12SerializeVersionedRootSignature");
   ComPtr<ID3DBlob> blob;
   ComPtr<ID3DBlob> errors;
   if (!serialize || FAILED(serialize(&versioned, &blob, &errors)))
   {
      Utils::Log::Error("D3D12: an extended root signature couldn't be serialized: {}", errors ? std::string_view(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "");
      return {};
   }
   const auto* bytes = static_cast<const uint8_t*>(blob->GetBufferPointer());
   return {{bytes, bytes + blob->GetBufferSize()}, luma_parameter, table};
}

// Signatures Luma created for pipelines that brought theirs, in the device's private data. They hold the device: released with
// ReleaseDeviceObjects when the game releases it.
struct Created
{
   std::mutex mutex; // Never held with another
   std::unordered_map<uint32_t, ComPtr<ID3D12RootSignature>> by_blob; // By the game blob's CRC32
};

// The extended signature for a game blob that came with a pipeline, created once per device and blob
ID3D12RootSignature* GetForBlob(ID3D12Device* device, std::span<const uint8_t> blob)
{
   Created* created = Hooks::Lifetime::GetState<Created>(device);
   if (!created)
      created = Hooks::Lifetime::SetState<Created>(device);
   if (!created)
      return nullptr;
   const uint32_t key = Utils::Hash::CRC32(blob);
   std::scoped_lock lock(created->mutex);
   if (const auto it = created->by_blob.find(key); it != created->by_blob.end())
      return it->second.Get();
   const Extended extended = Extend(blob);
   if (extended.blob.empty())
      return nullptr;
   ComPtr<ID3D12RootSignature> root_signature;
   {
      Hooks::Scope::Guard scope;
      if (FAILED(device->CreateRootSignature(0, extended.blob.data(), extended.blob.size(), IID_PPV_ARGS(&root_signature))))
      {
         Utils::Log::Error("D3D12: an extended root signature for a pipeline couldn't be created");
         return nullptr;
      }
   }
   Database::D3D12::TrackRootSignature(blob, root_signature.Get(), extended.luma_parameter, extended.table);
   return created->by_blob.emplace(key, std::move(root_signature)).first->second.Get();
}

// The first root signature chunk of a desc's shaders
std::span<const uint8_t> FindEmbedded(std::span<const D3D12_SHADER_BYTECODE> shaders)
{
   for (const D3D12_SHADER_BYTECODE& shader : shaders)
   {
      if (!shader.pShaderBytecode)
         continue;
      const std::span<const uint8_t> chunk = Container::GetRootSignature({static_cast<const uint8_t*>(shader.pShaderBytecode), shader.BytecodeLength});
      if (!chunk.empty())
         return chunk;
   }
   return {};
}

template <typename Desc>
std::optional<RewrittenDesc> RewriteStruct(ID3D12Device* device, const Desc& desc, std::span<const D3D12_SHADER_BYTECODE> shaders)
{
   if (desc.pRootSignature)
      return std::nullopt;
   const std::span<const uint8_t> embedded = FindEmbedded(shaders);
   ID3D12RootSignature* root_signature = embedded.empty() ? nullptr : GetForBlob(device, embedded);
   if (!root_signature)
      return std::nullopt;
   Desc rewritten = desc;
   rewritten.pRootSignature = root_signature;
   RewrittenDesc result;
   result.bytes.resize(sizeof(Desc));
   std::memcpy(result.bytes.data(), &rewritten, sizeof(Desc));
   return result;
}

template <typename T>
void AppendSubobject(std::vector<std::byte>& stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type, const T& value)
{
   const Database::D3D12::SubobjectLayout layout = *Database::D3D12::GetSubobjectLayout(type);
   const size_t offset = stream.size();
   stream.resize(offset + layout.size);
   std::memcpy(stream.data() + offset, &type, sizeof(type));
   std::memcpy(stream.data() + offset + layout.value_offset, &value, sizeof(T));
}

// A stream: a serialized root signature subobject is replaced by the extended signature; with none, one embedded in a shader is
// named by a root signature subobject
std::optional<RewrittenDesc> RewriteStream(ID3D12Device* device, const D3D12_PIPELINE_STATE_STREAM_DESC& desc)
{
   const auto* bytes = static_cast<const std::byte*>(desc.pPipelineStateSubobjectStream);
   if (!bytes)
      return std::nullopt;
   std::vector<D3D12_SHADER_BYTECODE> shaders;
   std::span<const uint8_t> serialized;
   bool named = false;
   size_t offset = 0;
   while (offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= desc.SizeInBytes)
   {
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
      std::memcpy(&type, bytes + offset, sizeof(type));
      const std::optional<Database::D3D12::SubobjectLayout> layout = Database::D3D12::GetSubobjectLayout(type);
      if (!layout)
         return std::nullopt;
      const std::byte* value = bytes + offset + layout->value_offset;
      switch (type)
      {
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
      {
         ID3D12RootSignature* root_signature = nullptr;
         std::memcpy(&root_signature, value, sizeof(root_signature));
         named |= root_signature != nullptr;
         break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SERIALIZED_ROOT_SIGNATURE:
      {
         D3D12_SERIALIZED_ROOT_SIGNATURE_DESC serialized_desc;
         std::memcpy(&serialized_desc, value, sizeof(serialized_desc));
         serialized = {static_cast<const uint8_t*>(serialized_desc.pSerializedBlob), serialized_desc.SerializedBlobSizeInBytes};
         break;
      }
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
      {
         D3D12_SHADER_BYTECODE shader;
         std::memcpy(&shader, value, sizeof(shader));
         shaders.push_back(shader);
         break;
      }
      default: break;
      }
      offset += layout->size;
   }
   if (named)
      return std::nullopt;
   const std::span<const uint8_t> blob = !serialized.empty() ? serialized : FindEmbedded(shaders);
   ID3D12RootSignature* root_signature = blob.empty() ? nullptr : GetForBlob(device, blob);
   if (!root_signature)
      return std::nullopt;

   RewrittenDesc result;
   offset = 0;
   while (offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= desc.SizeInBytes)
   {
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
      std::memcpy(&type, bytes + offset, sizeof(type));
      const Database::D3D12::SubobjectLayout layout = *Database::D3D12::GetSubobjectLayout(type);
      // An empty root signature subobject and the serialized one make way for the extended signature
      if (type != D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SERIALIZED_ROOT_SIGNATURE && type != D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE)
         result.stream.insert(result.stream.end(), bytes + offset, bytes + std::min<size_t>(offset + layout.size, desc.SizeInBytes));
      offset += layout.size;
   }
   result.stream.resize((result.stream.size() + alignof(void*) - 1) / alignof(void*) * alignof(void*));
   AppendSubobject(result.stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, root_signature);
   const D3D12_PIPELINE_STATE_STREAM_DESC rewritten = {result.stream.size(), result.stream.data()};
   result.bytes.resize(sizeof(rewritten));
   std::memcpy(result.bytes.data(), &rewritten, sizeof(rewritten));
   return result;
}
} // namespace

std::optional<RewrittenDesc> RewritePipelineDesc(ID3D12Device* device, Database::D3D12::PipelineKind kind, const void* desc)
{
   switch (kind)
   {
   case Database::D3D12::PipelineKind::Graphics:
   {
      const auto& graphics = *static_cast<const D3D12_GRAPHICS_PIPELINE_STATE_DESC*>(desc);
      const D3D12_SHADER_BYTECODE shaders[] = {graphics.VS, graphics.PS, graphics.DS, graphics.HS, graphics.GS};
      return RewriteStruct(device, graphics, shaders);
   }
   case Database::D3D12::PipelineKind::Compute:
   {
      const auto& compute = *static_cast<const D3D12_COMPUTE_PIPELINE_STATE_DESC*>(desc);
      return RewriteStruct(device, compute, std::span(&compute.CS, 1));
   }
   default: return RewriteStream(device, *static_cast<const D3D12_PIPELINE_STATE_STREAM_DESC*>(desc));
   }
}

void ReleaseDeviceObjects(ID3D12Device* device)
{
   if (Created* created = Hooks::Lifetime::GetState<Created>(device))
   {
      std::scoped_lock lock(created->mutex);
      created->by_blob.clear();
   }
}

Extended Extend(std::span<const uint8_t> blob)
{
   static const auto create_deserializer = GetExport<PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER>("D3D12CreateVersionedRootSignatureDeserializer");
   ComPtr<ID3D12VersionedRootSignatureDeserializer> deserializer;
   if (!create_deserializer || FAILED(create_deserializer(blob.data(), blob.size(), IID_PPV_ARGS(&deserializer))))
      return {};
   const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc = deserializer->GetUnconvertedRootSignatureDesc();
   if (!desc)
      return {};
   // DXR's local root signatures are bound through shader tables, not as a command list's signature
   const D3D12_ROOT_SIGNATURE_FLAGS flags = desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_0 ? desc->Desc_1_0.Flags
      : desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_1                                     ? desc->Desc_1_1.Flags
                                                                                            : desc->Desc_1_2.Flags;
   if (flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE)
      return {};
   switch (desc->Version)
   {
   case D3D_ROOT_SIGNATURE_VERSION_1_0: return Append<D3D12_ROOT_SIGNATURE_DESC, D3D12_ROOT_PARAMETER>(desc->Desc_1_0, desc->Version, &D3D12_VERSIONED_ROOT_SIGNATURE_DESC::Desc_1_0);
   case D3D_ROOT_SIGNATURE_VERSION_1_1: return Append<D3D12_ROOT_SIGNATURE_DESC1, D3D12_ROOT_PARAMETER1>(desc->Desc_1_1, desc->Version, &D3D12_VERSIONED_ROOT_SIGNATURE_DESC::Desc_1_1);
   case D3D_ROOT_SIGNATURE_VERSION_1_2: return Append<D3D12_ROOT_SIGNATURE_DESC2, D3D12_ROOT_PARAMETER1>(desc->Desc_1_2, desc->Version, &D3D12_VERSIONED_ROOT_SIGNATURE_DESC::Desc_1_2);
   default:
      Utils::Log::Warning("D3D12: a root signature of unknown version {} isn't extended", int(desc->Version));
      return {};
   }
}
} // namespace D3D12
} // namespace RootSignatures
} // namespace Shaders
} // namespace Luma
