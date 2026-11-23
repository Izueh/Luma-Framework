#include "Shaders/ShaderSignatures.hpp"

#include <format>
#include <mutex>
#include <unordered_set>

#include "Utils/Debug.hpp"
#include "Utils/Hash.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Shaders {
namespace Signatures {
namespace {
using Container::Element;

// D3D_NAME values the device generates instead of the previous stage: a replacement may read them when its original didn't
constexpr uint32_t VERTEX_ID = 6;
constexpr uint32_t PRIMITIVE_ID = 7;
constexpr uint32_t INSTANCE_ID = 8;
constexpr uint32_t IS_FRONT_FACE = 9;
constexpr uint32_t SAMPLE_INDEX = 10;

bool IsGenerated(ShaderStage stage, const Element& element)
{
   switch (element.system_value)
   {
   case VERTEX_ID:
   case INSTANCE_ID: return stage == ShaderStage::Vertex;
   case PRIMITIVE_ID: return stage != ShaderStage::Vertex;
   case IS_FRONT_FACE:
   case SAMPLE_INDEX: return stage == ShaderStage::Pixel;
   default: return false;
   }
}

std::string FormatMask(uint8_t mask)
{
   std::string text = ".";
   for (uint32_t i = 0; i < 4; i++)
   {
      if (mask & (1u << i))
         text += "xyzw"[i];
   }
   return text;
}

std::string_view FormatType(uint32_t type)
{
   switch (type)
   {
   case 1: return "uint";
   case 2: return "int";
   case 3: return "float";
   default: return "unknown";
   }
}

std::string FormatRegister(uint32_t reg)
{
   return reg == UINT32_MAX ? std::string("none") : std::to_string(reg);
}

const Element* Find(std::span<const Element> elements, const Element& element)
{
   for (const Element& candidate : elements)
   {
      if (candidate.semantic == element.semantic && candidate.index == element.index && candidate.stream == element.stream)
         return &candidate;
   }
   return nullptr;
}

// Same register, system value and type; the components in `needed` must be in `given`
void CompareElement(std::vector<std::string>& differences, std::string_view kind, const Element& replaced, const Element& original, bool check_register, uint8_t needed,
   uint8_t given, std::string_view mask_difference)
{
   const std::string name = Container::GetName(replaced);
   if (check_register && replaced.reg != original.reg)
      differences.push_back(std::format("{} {} is in register {}, the original's in {}", kind, name, FormatRegister(replaced.reg), FormatRegister(original.reg)));
   if (replaced.system_value != original.system_value)
      differences.push_back(std::format("{} {} has system value {}, the original's {}", kind, name, replaced.system_value, original.system_value));
   if (replaced.component_type != original.component_type)
      differences.push_back(std::format("{} {} is {}, the original's {}", kind, name, FormatType(replaced.component_type), FormatType(original.component_type)));
   if ((needed & ~given) != 0)
   {
      const std::string needed_mask = FormatMask(needed);
      const std::string given_mask = FormatMask(given);
      differences.push_back(std::vformat(mask_difference, std::make_format_args(kind, name, needed_mask, given_mask)));
   }
}

// What the previous stage (or the input layout) gives the original must cover what the replacement reads
void CompareInputs(std::vector<std::string>& differences, std::string_view kind, ShaderStage stage, std::span<const Element> original, std::span<const Element> replacement)
{
   // The input layout matches vertex shader inputs by semantic, the other stages link by register
   const bool by_register = stage != ShaderStage::Vertex;
   for (const Element& replaced : replacement)
   {
      const Element* found = Find(original, replaced);
      if (!found)
      {
         if (!IsGenerated(stage, replaced))
            differences.push_back(std::format("{} {} isn't one of the original's", kind, Container::GetName(replaced)));
         continue;
      }
      // What it reads, not what it declares: a replacement often declares float4 where the original declared .xy (D3D12 accepts it)
      CompareElement(differences, kind, replaced, *found, by_register, replaced.used, found->mask, "{} {} reads {}, the original declares {}");
   }
}

// The next stage reads what the original writes, where it writes it
void CompareOutputs(std::vector<std::string>& differences, std::string_view kind, std::span<const Element> original, std::span<const Element> replacement)
{
   for (const Element& written : original)
   {
      const Element* found = Find(replacement, written);
      if (!found)
      {
         differences.push_back(std::format("{} {} of the original is missing", kind, Container::GetName(written)));
         continue;
      }
      CompareElement(differences, kind, *found, written, true, written.mask, found->mask, "{} {} of the original writes {}, the replacement {}");
   }
}

// Render targets and depth: one the original didn't write would change what's bound there
void CompareTargets(std::vector<std::string>& differences, std::span<const Element> original, std::span<const Element> replacement)
{
   for (const Element& written : replacement)
   {
      const Element* found = Find(original, written);
      if (!found)
      {
         differences.push_back(std::format("output {} isn't written by the original", Container::GetName(written)));
         continue;
      }
      // Render targets: only the register, system value and type (the blend and write masks decide the rest)
      CompareElement(differences, "output", written, *found, true, 0, 0, {});
   }
}

#if DEVELOPMENT
std::mutex checked_mutex;
// Hash and replacement bytecode CRC: each build of a replacement is reported once
std::unordered_set<uint64_t> checked;
#endif
} // namespace

std::vector<std::string> Compare(ShaderStage stage, const Container::Signature& original, const Container::Signature& replacement)
{
   std::vector<std::string> differences;
   if (stage == ShaderStage::Compute)
      return differences;
   CompareInputs(differences, "input", stage, original.inputs, replacement.inputs);
   if (stage == ShaderStage::Pixel)
      CompareTargets(differences, original.outputs, replacement.outputs);
   else
      CompareOutputs(differences, "output", original.outputs, replacement.outputs);
   if (stage == ShaderStage::Hull)
      CompareOutputs(differences, "patch constant", original.patch_constants, replacement.patch_constants);
   else if (stage == ShaderStage::Domain)
      CompareInputs(differences, "patch constant", stage, original.patch_constants, replacement.patch_constants);
   return differences;
}

#if DEVELOPMENT
void Check(uint32_t hash, ShaderStage stage, const Container::Signature& original, std::span<const uint8_t> replacement, std::string_view file)
{
   {
      std::scoped_lock lock(checked_mutex);
      if (!checked.insert((uint64_t(hash) << 32) | Utils::Hash::CRC32(replacement)).second)
         return;
   }
   const std::vector<std::string> differences = Compare(stage, original, Container::GetSignature(replacement));
   if (differences.empty())
      return;
   std::string text;
   for (const std::string& difference : differences)
      text += std::format("\n  {}", difference);
   Log::Error("Shaders: {} (0x{:08X}) doesn't link like the original, D3D12 refuses its pipelines and D3D11 reads the wrong data:{}", file, hash, text);
   ASSERT_ONCE_MSG(false, std::format("{} (0x{:08X}) doesn't link like the original, see the log", file, hash));
}
#endif
} // namespace Signatures
} // namespace Shaders
} // namespace Luma
