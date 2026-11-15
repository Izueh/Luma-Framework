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
   const size_t size = layout == Layout::Base ? 24 : layout == Layout::Stream ? 28
                                                                              : 32;
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

// DXBC's checksum: MD5 rounds over the container after its checksum field, finished DXBC's own way (from dxbc-spirv by Philip
// Rebohle, MIT, as 1.0's CalcDXBCHash)
class Md5
{
public:
   void Update(const uint8_t* data, size_t size)
   {
      size_t offset = size_t(total % 64);
      total += size;
      if (offset)
      {
         const size_t n = std::min(size, 64 - offset);
         std::memcpy(block.data() + offset, data, n);
         data += n;
         size -= n;
         if (offset + n < 64)
            return;
         Process(block.data());
      }
      for (; size >= 64; data += 64, size -= 64)
         Process(data);
      if (size)
         std::memcpy(block.data(), data, size);
   }

   std::array<uint32_t, 4> state = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};

private:
   std::array<uint8_t, 64> block = {};
   uint64_t total = 0;

   void Process(const uint8_t* data)
   {
      static constexpr std::array<uint8_t, 64> SHIFTS = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14,
         20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
      static constexpr std::array<uint32_t, 64> CONSTANTS = {0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u,
         0xfd469501u, 0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u, 0xf61e2562u, 0xc040b340u,
         0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u, 0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u,
         0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
         0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u, 0xf4292244u, 0x432aff97u, 0xab9423a7u,
         0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u, 0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u,
         0x2ad7d2bbu, 0xeb86d391u};
      std::array<uint32_t, 4> s = state;
      for (uint32_t i = 0; i < 64; i++)
      {
         uint32_t f = 0;
         uint32_t g = 0;
         if (i < 16)
            f = (s[1] & s[2]) | (~s[1] & s[3]), g = i;
         else if (i < 32)
            f = (s[3] & s[1]) | (~s[3] & s[2]), g = (5 * i + 1) % 16;
         else if (i < 48)
            f = s[1] ^ s[2] ^ s[3], g = (3 * i + 5) % 16;
         else
            f = s[2] ^ (s[1] | ~s[3]), g = (7 * i) % 16;
         uint32_t word = 0;
         std::memcpy(&word, data + 4 * g, 4);
         f = f + s[0] + CONSTANTS[i] + word;
         s[0] = s[3];
         s[3] = s[2];
         s[2] = s[1];
         s[1] += (f << SHIFTS[i]) | (f >> (32 - SHIFTS[i]));
      }
      for (size_t i = 0; i < 4; i++)
         state[i] += s[i];
   }
};

std::array<uint32_t, 4> GetChecksum(std::span<const uint8_t> container)
{
   // After "DXBC" and the checksum itself
   constexpr size_t START = 20;
   const uint8_t* bytes = container.data() + START;
   const size_t size = container.size() - START;
   const uint32_t bits = uint32_t(size) * 8;
   const uint32_t magic = (bits >> 2) | 1;
   const size_t remainder = size % 64;
   const size_t padding = 64 - remainder;
   static constexpr std::array<uint8_t, 64> PADDING = {0x80};
   Md5 md5;
   md5.Update(bytes, size - remainder);
   // Not MD5's own finish: the padding depends on the size
   if (remainder >= 56)
   {
      md5.Update(bytes + size - remainder, remainder);
      md5.Update(PADDING.data(), padding);
      md5.Update(reinterpret_cast<const uint8_t*>(&bits), 4);
      md5.Update(PADDING.data() + 4, 64 - 8);
      md5.Update(reinterpret_cast<const uint8_t*>(&magic), 4);
   }
   else
   {
      md5.Update(reinterpret_cast<const uint8_t*>(&bits), 4);
      if (remainder)
         md5.Update(bytes + size - remainder, remainder);
      md5.Update(PADDING.data(), padding - 8);
      md5.Update(reinterpret_cast<const uint8_t*>(&magic), 4);
   }
   return md5.state;
}

// The offset of the program chunk's header ("SHEX" or "SHDR"), 0 without one
size_t FindProgramChunk(std::span<const uint8_t> bytecode)
{
   if (bytecode.size() < 32 || std::memcmp(bytecode.data(), "DXBC", 4) != 0)
      return 0;
   const uint32_t chunk_count = Read32(bytecode, 28);
   for (uint32_t i = 0; i < chunk_count && 32 + size_t(i + 1) * 4 <= bytecode.size(); i++)
   {
      const size_t chunk = Read32(bytecode, 32 + size_t(i) * 4);
      if (chunk + 16 <= bytecode.size() && (std::memcmp(bytecode.data() + chunk, "SHEX", 4) == 0 || std::memcmp(bytecode.data() + chunk, "SHDR", 4) == 0) &&
         chunk + 8 + Read32(bytecode, chunk + 4) <= bytecode.size())
         return chunk;
   }
   return 0;
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

std::span<const uint8_t> GetProgramCode(std::span<const uint8_t> bytecode)
{
   const size_t chunk = FindProgramChunk(bytecode);
   if (!chunk)
      return {};
   // The chunk is the version token, the length in tokens (both counted), then the instructions
   const size_t tokens = Read32(bytecode, chunk + 12);
   const size_t size = Read32(bytecode, chunk + 4);
   if (tokens < 2 || tokens * 4 > size)
      return {};
   return bytecode.subspan(chunk + 16, tokens * 4 - 8);
}

std::vector<uint8_t> ReplaceProgramCode(std::span<const uint8_t> bytecode, std::span<const uint8_t> code)
{
   const size_t chunk = FindProgramChunk(bytecode);
   if (!chunk || code.size() % 4 != 0)
      return {};
   const uint32_t chunk_count = Read32(bytecode, 28);
   const size_t old_size = Read32(bytecode, chunk + 4);
   const size_t new_size = 8 + code.size();
   const size_t old_end = chunk + 8 + old_size;
   std::vector<uint8_t> result;
   result.reserve(bytecode.size() - old_size + new_size);
   result.insert(result.end(), bytecode.begin(), bytecode.begin() + chunk + 16);
   result.insert(result.end(), code.begin(), code.end());
   result.insert(result.end(), bytecode.begin() + old_end, bytecode.end());
   const auto write = [&](size_t offset, uint32_t value) { std::memcpy(result.data() + offset, &value, 4); };
   const int64_t delta = int64_t(new_size) - int64_t(old_size);
   write(24, uint32_t(result.size()));
   write(chunk + 4, uint32_t(new_size));
   write(chunk + 12, uint32_t(new_size / 4));
   for (uint32_t i = 0; i < chunk_count; i++)
   {
      const uint32_t offset = Read32(bytecode, 32 + size_t(i) * 4);
      if (offset > chunk)
         write(32 + size_t(i) * 4, uint32_t(int64_t(offset) + delta));
   }
   Sign(result);
   return result;
}

void Sign(std::span<uint8_t> bytecode)
{
   if (bytecode.size() < 32)
      return;
   const std::array<uint32_t, 4> checksum = GetChecksum(bytecode);
   std::memcpy(bytecode.data() + 4, checksum.data(), 16);
}
} // namespace Container
} // namespace Shaders
} // namespace Luma
