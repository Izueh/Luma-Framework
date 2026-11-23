#include "Trace/Cbuffers.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <cstring>
#include <format>

#include "Resources/ResourceDatabase.hpp"
#include "Shaders/ShaderCompiler.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderVariants.hpp"

namespace Luma {
namespace Trace {
namespace Cbuffers {
namespace {
using Shaders::Compiler::Variable;

constexpr uint32_t REGISTER = 16;
// Long arrays are cut, the rest is in the raw rows
constexpr uint32_t MAX_ELEMENTS = 64;

std::string FormatWord(std::span<const std::byte> data, uint32_t offset, Variable::Kind kind)
{
   if (offset + 4 > data.size())
      return "?";
   uint32_t word = 0;
   std::memcpy(&word, data.data() + offset, sizeof(word));
   switch (kind)
   {
   case Variable::Kind::Float:
   {
      float value = 0.f;
      std::memcpy(&value, &word, sizeof(value));
      return std::format("{:g}", value);
   }
   case Variable::Kind::Int: return std::to_string(int32_t(word));
   case Variable::Kind::Uint: return std::to_string(word);
   case Variable::Kind::Bool: return word ? "true" : "false";
   default: return std::format("0x{:08X}", word);
   }
}

// `count` words from `offset`, "a, b, c"
std::string FormatWords(std::span<const std::byte> data, uint32_t offset, uint32_t count, Variable::Kind kind)
{
   std::string text;
   for (uint32_t i = 0; i < count; i++)
      text += (i ? ", " : "") + FormatWord(data, offset + i * 4, kind);
   return text;
}

// One element: a scalar or vector in a register, a matrix in one register per row (or per column)
std::string FormatElement(std::span<const std::byte> data, uint32_t offset, const Variable& variable)
{
   const bool matrix = variable.rows > 1 || variable.column_major;
   if (!matrix)
      return FormatWords(data, offset, variable.columns, variable.kind);
   const uint32_t registers = variable.column_major ? variable.columns : variable.rows;
   const uint32_t components = variable.column_major ? variable.rows : variable.columns;
   std::string text;
   for (uint32_t i = 0; i < registers; i++)
      text += std::format("{}({})", i ? " " : "", FormatWords(data, offset + i * REGISTER, components, variable.kind));
   return text;
}

std::string FormatVariable(std::span<const std::byte> data, const Variable& variable)
{
   if (variable.kind == Variable::Kind::Other)
      return FormatWords(data, variable.offset, std::min(variable.size / 4, MAX_ELEMENTS), Variable::Kind::Other);
   if (variable.elements == 0)
      return FormatElement(data, variable.offset, variable);
   // Each array element starts on a register
   const uint32_t registers = variable.rows > 1 || variable.column_major ? (variable.column_major ? variable.columns : variable.rows) : 1;
   std::string text = "[";
   for (uint32_t i = 0; i < std::min(variable.elements, MAX_ELEMENTS); i++)
      text += (i ? "; " : "") + FormatElement(data, variable.offset + i * registers * REGISTER, variable);
   return text + (variable.elements > MAX_ELEMENTS ? "; ...]" : "]");
}

const Shaders::Compiler::ConstantBuffer* FindSlot(const std::vector<Shaders::Compiler::ConstantBuffer>& layouts, uint32_t slot)
{
   const auto it = std::ranges::find(layouts, slot, &Shaders::Compiler::ConstantBuffer::slot);
   return it != layouts.end() ? &*it : nullptr;
}

Description FromLayout(std::string source, const Shaders::Compiler::ConstantBuffer& layout, std::span<const std::byte> data)
{
   Description description = {std::move(source), layout.name, {}};
   for (const Variable& variable : layout.variables)
      description.fields.push_back({variable.name, variable.type, variable.offset, variable.size, FormatVariable(data, variable)});
   return description;
}
} // namespace

Description Describe(uint32_t hash, uint32_t slot, std::span<const std::byte> data, bool full_raw, std::string_view luma_name)
{
   if (!full_raw)
   {
      if (const Shaders::Database::Shader* shader = Shaders::Database::Find(hash))
      {
         if (const Shaders::Compiler::ConstantBuffer* layout = FindSlot(shader->reflection.constant_buffer_layouts, slot))
            return FromLayout("reflection", *layout, data);
      }
      // Games strip their reflection data; Luma's variants of the shader declare the cbuffer they read
      for (const Shaders::Variants::Variant& variant : Shaders::Variants::GetVariants(hash))
      {
         if (!variant.compiled)
            continue;
         const std::vector<Shaders::Compiler::ConstantBuffer> layouts = Shaders::Compiler::ReflectConstantBuffers(variant.compiled->bytecode);
         if (const Shaders::Compiler::ConstantBuffer* layout = FindSlot(layouts, slot))
            return FromLayout("replacement", *layout, data);
      }
      // Luma's own cbuffers (LumaSettings, LumaData, LumaComposition): reflect any compiled Core shader that declares it
      if (!luma_name.empty())
      {
         for (uint32_t i = 0; i < Resources::Database::GetCount(); i++)
         {
            const Resources::Database::Entry& entry = Resources::Database::Get(i);
            if (entry.kind != Resources::Database::Kind::Shader)
               continue;
            const Resources::Database::ShaderState shader_state = Resources::Database::GetShader(i);
            if (!shader_state.compiled)
               continue;
            const std::vector<Shaders::Compiler::ConstantBuffer> layouts = Shaders::Compiler::ReflectConstantBuffers(shader_state.compiled->bytecode);
            const auto it = std::ranges::find(layouts, luma_name, &Shaders::Compiler::ConstantBuffer::name);
            if (it != layouts.end())
               return FromLayout("reflection", *it, data);
         }
      }
   }
   Description description = {"raw", {}, {}};
   for (uint32_t offset = 0; offset < data.size(); offset += REGISTER)
      description.fields.push_back({std::format("c{}", offset / REGISTER), "float4", offset, REGISTER, FormatWords(data, offset, 4, Variable::Kind::Float)});
   return description;
}
} // namespace Cbuffers
} // namespace Trace
} // namespace Luma
#endif
