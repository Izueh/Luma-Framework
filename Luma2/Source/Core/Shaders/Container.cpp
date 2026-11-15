#include "Shaders/Container.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <format>
#include <initializer_list>
#include <utility>

namespace Luma {
namespace Shaders {
namespace Container {
namespace {
uint32_t Read32(std::span<const uint8_t> bytes, size_t offset)
{
   uint32_t value = 0;
   if (offset + sizeof(value) <= bytes.size())
      std::memcpy(&value, bytes.data() + offset, sizeof(value));
   return value;
}

// The version token of SM4/SM5 programs and the DXIL program header alike: the kind, then the major and minor versions
Program DecodeVersion(uint32_t version, bool dxil)
{
   return {.kind = version >> 16, .major = (version >> 4) & 0xF, .minor = version & 0xF, .dxil = dxil};
}

enum class Layout
{
   Base, // ISGN, OSGN, PCSG: 24 byte elements
   Stream, // OSG5: the stream first, 28 bytes
   Precision, // ISG1, OSG1, PSG1: the stream first and the minimum precision last, 32 bytes
};

std::vector<Element> ReadElements(std::span<const uint8_t> chunk, Layout layout)
{
   // The element count and where the elements start; names are offsets from the chunk's start too
   const uint32_t count = Read32(chunk, 0);
   const size_t start = Read32(chunk, 4);
   const size_t size = layout == Layout::Base ? 24 : layout == Layout::Stream ? 28 : 32;
   if (chunk.size() < 8 || start + size_t(count) * size > chunk.size())
      return {};
   std::vector<Element> elements;
   elements.reserve(count);
   for (uint32_t i = 0; i < count; i++)
   {
      size_t offset = start + size_t(i) * size;
      Element element;
      if (layout != Layout::Base)
      {
         element.stream = Read32(chunk, offset);
         offset += 4;
      }
      const size_t name = Read32(chunk, offset);
      element.index = Read32(chunk, offset + 4);
      element.system_value = Read32(chunk, offset + 8);
      element.component_type = Read32(chunk, offset + 12);
      element.reg = Read32(chunk, offset + 16);
      element.mask = chunk[offset + 20];
      element.used = chunk[offset + 21];
      if (layout == Layout::Precision)
         element.min_precision = Read32(chunk, offset + 24);
      for (size_t c = name; c < chunk.size() && chunk[c] != 0; c++)
         element.semantic += char(std::toupper(chunk[c]));
      elements.push_back(std::move(element));
   }
   return elements;
}

// SFI0's feature flags (D3D_SHADER_FEATURE_*_DESCRIPTOR_HEAP_INDEXING)
constexpr uint64_t RESOURCE_HEAP_INDEXING = 0x2000000;
constexpr uint64_t SAMPLER_HEAP_INDEXING = 0x4000000;

// D3D10_SB_OPCODE_* and D3D11_SB_OPCODE_*: the declarations of a register
std::optional<BindingType> GetDeclaredType(uint32_t opcode)
{
   switch (opcode)
   {
   case 0x59: return BindingType::ConstantBuffer; // dcl_constantbuffer
   case 0x5A: return BindingType::Sampler; // dcl_sampler
   case 0x58: // dcl_resource
   case 0xA1: // dcl_resource_raw
   case 0xA2: return BindingType::ShaderResource; // dcl_resource_structured
   case 0x9C: // dcl_uav_typed
   case 0x9D: // dcl_uav_raw
   case 0x9E: return BindingType::UnorderedAccess; // dcl_uav_structured
   default: return std::nullopt;
   }
}

// One register declaration: the opcode token (and its extended tokens), the operand token (and its), its immediate indices, then
// the declaration's own tokens. SM5.0 and earlier: [register] ([register][size] for a cbuffer), space 0. SM5.1: [id][first][last],
// and the space is the last token.
std::optional<Binding> ReadDeclaration(std::span<const uint32_t> instruction, BindingType type)
{
   size_t position = 0;
   while (position < instruction.size() && (instruction[position] & 0x80000000u))
      position++;
   position++;
   if (position >= instruction.size())
      return std::nullopt;
   const uint32_t operand = instruction[position];
   while (position < instruction.size() && (instruction[position] & 0x80000000u))
      position++;
   position++;
   const uint32_t dimensions = (operand >> 20) & 0x3;
   std::array<uint32_t, 3> indices = {};
   for (uint32_t i = 0; i < dimensions; i++)
   {
      // Immediate 32 bit indices only: declarations have no relative ones
      if (((operand >> (22 + i * 3)) & 0x7) != 0 || position >= instruction.size())
         return std::nullopt;
      indices[i] = instruction[position++];
   }
   Binding binding = {.type = type};
   if (dimensions == 3)
   {
      binding.base = indices[1];
      binding.count = indices[2] == UINT32_MAX ? UINT32_MAX : indices[2] - indices[1] + 1;
      binding.space = instruction.back();
   }
   else if (dimensions >= 1)
      binding.base = indices[0];
   else
      return std::nullopt;
   return binding;
}

bool ReadDeclarations(std::span<const uint8_t> chunk, std::vector<Binding>& bindings)
{
   // The version token, the length in tokens, the instructions
   if (chunk.size() < 8)
      return false;
   const size_t length = std::min<size_t>(Read32(chunk, 4), chunk.size() / 4);
   size_t position = 2;
   while (position < length)
   {
      const uint32_t token = Read32(chunk, position * 4);
      const uint32_t opcode = token & 0x7FF;
      // Custom data (immediate constant buffers, comments) has its length in the next token
      const size_t size = opcode == 0x35 ? Read32(chunk, (position + 1) * 4) : (token >> 24) & 0x7F;
      if (size == 0 || position + size > length)
         return false;
      if (const std::optional<BindingType> type = GetDeclaredType(opcode))
      {
         std::vector<uint32_t> instruction(size);
         std::memcpy(instruction.data(), chunk.data() + position * 4, size * 4);
         if (const std::optional<Binding> binding = ReadDeclaration(instruction, *type))
            bindings.push_back(*binding);
      }
      position += size;
   }
   return true;
}

// PSV0: the runtime info's size and the info, the resource count, then (when there are resources) each one's size and the resources,
// each starting with its type, space, first and last register (UINT32_MAX for unbounded)
bool ReadValidationBindings(std::span<const uint8_t> chunk, std::vector<Binding>& bindings)
{
   const size_t info_size = Read32(chunk, 0);
   size_t offset = 4 + info_size;
   if (offset + 4 > chunk.size())
      return false;
   const uint32_t count = Read32(chunk, offset);
   offset += 4;
   if (count == 0)
      return true;
   const size_t stride = Read32(chunk, offset);
   offset += 4;
   if (stride < 16 || offset + size_t(count) * stride > chunk.size())
      return false;
   for (uint32_t i = 0; i < count; i++, offset += stride)
   {
      // PSVResourceType: sampler, CBV, then three SRV and four UAV types
      const uint32_t kind = Read32(chunk, offset);
      Binding binding;
      if (kind == 1)
         binding.type = BindingType::Sampler;
      else if (kind == 2)
         binding.type = BindingType::ConstantBuffer;
      else if (kind >= 3 && kind <= 5)
         binding.type = BindingType::ShaderResource;
      else if (kind >= 6 && kind <= 9)
         binding.type = BindingType::UnorderedAccess;
      else
         continue;
      binding.space = Read32(chunk, offset + 4);
      binding.base = Read32(chunk, offset + 8);
      const uint32_t last = Read32(chunk, offset + 12);
      binding.count = last == UINT32_MAX ? UINT32_MAX : last - binding.base + 1;
      bindings.push_back(binding);
   }
   return true;
}

// The first of these chunks the container has, newest layout first
std::vector<Element> ReadSignature(std::span<const uint8_t> bytecode, std::initializer_list<std::pair<std::string_view, Layout>> chunks)
{
   for (const auto& [fourcc, layout] : chunks)
   {
      if (const std::span<const uint8_t> chunk = GetChunk(bytecode, fourcc); !chunk.empty())
         return ReadElements(chunk, layout);
   }
   return {};
}
} // namespace

std::span<const uint8_t> GetChunk(std::span<const uint8_t> bytecode, std::string_view fourcc)
{
   // "DXBC", a 16 byte checksum, a version, the total size, the chunk count and their offsets
   if (bytecode.size() < 32 || fourcc.size() != 4 || std::memcmp(bytecode.data(), "DXBC", 4) != 0)
      return {};
   const uint32_t chunk_count = Read32(bytecode, 28);
   for (uint32_t i = 0; i < chunk_count && 32 + size_t(i + 1) * 4 <= bytecode.size(); i++)
   {
      const uint32_t chunk = Read32(bytecode, 32 + size_t(i) * 4);
      if (size_t(chunk) + 8 > bytecode.size() || std::memcmp(bytecode.data() + chunk, fourcc.data(), 4) != 0)
         continue;
      const uint32_t size = Read32(bytecode, size_t(chunk) + 4);
      if (size_t(chunk) + 8 + size <= bytecode.size())
         return bytecode.subspan(size_t(chunk) + 8, size);
   }
   return {};
}

bool HasChunk(std::span<const uint8_t> bytecode, std::string_view fourcc)
{
   return !GetChunk(bytecode, fourcc).empty();
}

std::optional<Program> GetProgram(std::span<const uint8_t> bytecode)
{
   for (const std::string_view fourcc : {"SHEX", "SHDR"})
   {
      if (const std::span<const uint8_t> chunk = GetChunk(bytecode, fourcc); chunk.size() >= 4)
         return DecodeVersion(Read32(chunk, 0), false);
   }
   if (const std::span<const uint8_t> chunk = GetChunk(bytecode, "DXIL"); chunk.size() >= 4)
      return DecodeVersion(Read32(chunk, 0), true);
   return std::nullopt;
}

std::string GetTarget(std::span<const uint8_t> bytecode)
{
   const std::optional<Program> program = GetProgram(bytecode);
   if (!program)
      return {};
   // By D3D12_SHADER_VERSION_TYPE; the ray tracing kinds only live in libraries
   constexpr std::array<std::string_view, 15> PREFIXES = {"ps", "vs", "gs", "hs", "ds", "cs", "lib", "lib", "lib", "lib", "lib", "lib", "lib", "ms", "as"};
   if (program->kind >= PREFIXES.size())
      return {};
   return std::format("{}_{}_{}", PREFIXES[program->kind], program->major, program->minor);
}

std::optional<ShaderStage> GetStage(const Program& program)
{
   constexpr std::array<ShaderStage, 6> STAGES = {ShaderStage::Pixel, ShaderStage::Vertex, ShaderStage::Geometry, ShaderStage::Hull, ShaderStage::Domain, ShaderStage::Compute};
   if (program.kind >= STAGES.size())
      return std::nullopt;
   return STAGES[program.kind];
}

std::span<const uint8_t> GetRootSignature(std::span<const uint8_t> bytecode)
{
   return GetChunk(bytecode, "RTS0");
}

Signature GetSignature(std::span<const uint8_t> bytecode)
{
   return {
      .inputs = ReadSignature(bytecode, {{"ISG1", Layout::Precision}, {"ISGN", Layout::Base}}),
      .outputs = ReadSignature(bytecode, {{"OSG1", Layout::Precision}, {"OSG5", Layout::Stream}, {"OSGN", Layout::Base}}),
      .patch_constants = ReadSignature(bytecode, {{"PSG1", Layout::Precision}, {"PCSG", Layout::Base}}),
   };
}

std::string GetName(const Element& element)
{
   return std::format("{}{}", element.semantic, element.index);
}

bool Binding::Covers(BindingType type_, uint32_t space_, uint32_t reg) const
{
   return type == type_ && space == space_ && reg >= base && (count == UINT32_MAX || reg - base < count);
}

bool Bindings::Declares(BindingType type, uint32_t space, uint32_t reg) const
{
   return std::ranges::any_of(bindings, [&](const Binding& binding) { return binding.Covers(type, space, reg); });
}

bool Bindings::UsesSpace(uint32_t space) const
{
   return std::ranges::any_of(bindings, [&](const Binding& binding) { return binding.space == space; });
}

bool Bindings::HasUnbounded() const
{
   return std::ranges::any_of(bindings, [](const Binding& binding) { return binding.count == UINT32_MAX; });
}

Bindings GetBindings(std::span<const uint8_t> bytecode)
{
   Bindings result;
   if (const std::span<const uint8_t> features = GetChunk(bytecode, "SFI0"); features.size() >= 8)
   {
      const uint64_t flags = Read32(features, 0) | (uint64_t(Read32(features, 4)) << 32);
      result.resource_heap_indexed = (flags & RESOURCE_HEAP_INDEXING) != 0;
      result.sampler_heap_indexed = (flags & SAMPLER_HEAP_INDEXING) != 0;
   }
   for (const std::string_view fourcc : {"SHEX", "SHDR"})
   {
      if (const std::span<const uint8_t> chunk = GetChunk(bytecode, fourcc); !chunk.empty())
      {
         result.found = ReadDeclarations(chunk, result.bindings);
         return result;
      }
   }
   if (const std::span<const uint8_t> chunk = GetChunk(bytecode, "PSV0"); !chunk.empty())
      result.found = ReadValidationBindings(chunk, result.bindings);
   return result;
}
} // namespace Container
} // namespace Shaders
} // namespace Luma
