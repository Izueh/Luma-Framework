#include "Shaders/ShaderInvariants.hpp"

#if DEVELOPMENT || TEST
#include <algorithm>
#include <atomic>
#include <format>
#include <optional>
#include <vector>

#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceBindings.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Utils/Debug.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Shaders {
namespace Invariants {
namespace {
// An unbounded range is never Luma's, and comparing one register at a time stops here
constexpr uint32_t MAX_COMPARED = 256;

std::atomic<uint32_t> rejected = 0;

char GetLetter(Container::BindingType type)
{
   switch (type)
   {
   case Container::BindingType::ConstantBuffer: return 'b';
   case Container::BindingType::ShaderResource: return 't';
   case Container::BindingType::UnorderedAccess: return 'u';
   default: return 's';
   }
}

std::string Describe(const Container::Binding& binding)
{
   if (binding.count == 1)
      return std::format("{}{} space{}", GetLetter(binding.type), binding.base, binding.space);
   if (binding.count == UINT32_MAX)
      return std::format("{}{} and up, unbounded, space{}", GetLetter(binding.type), binding.base, binding.space);
   return std::format("{}{} to {}{} space{}", GetLetter(binding.type), binding.base, GetLetter(binding.type), binding.base + binding.count - 1, binding.space);
}

bool IsLumaCBuffer(uint32_t slot, const D3D12Rules& rules)
{
   return slot == rules.settings_slot || slot == rules.data_slot;
}

// Every register of the range is one Luma binds in its space
bool IsLumas(const Container::Binding& binding, const D3D12Rules& rules)
{
   if (binding.count == UINT32_MAX || binding.count == 0)
      return false;
   const uint64_t end = uint64_t(binding.base) + binding.count;
   switch (binding.type)
   {
   case Container::BindingType::ConstantBuffer:
      for (uint32_t slot = binding.base; slot < end; slot++)
      {
         if (!IsLumaCBuffer(slot, rules))
            return false;
      }
      return true;
   case Container::BindingType::ShaderResource:
      return binding.base >= Resources::Bindings::D3D12_FIRST_TEXTURE_REGISTER &&
         end <= Resources::Bindings::D3D12_FIRST_TEXTURE_REGISTER + Resources::Bindings::D3D12_TEXTURE_REGISTERS;
   case Container::BindingType::Sampler: return rules.static_samplers && end <= Resources::Bindings::D3D12_STATIC_SAMPLERS;
   default: return false;
   }
}
} // namespace

Findings CheckD3D12(std::span<const uint8_t> bytecode, const D3D12Rules& rules)
{
   Findings findings;
   const std::optional<Container::Program> program = Container::GetProgram(bytecode);
   if (!program)
   {
      findings.errors = "it has no shader program\n";
      return findings;
   }
   const bool sm51 = program->dxil || program->major > 5 || (program->major == 5 && program->minor >= 1);
   if (!sm51 && rules.reads_luma)
      findings.errors += std::format("its target {} is below SM5.1, which Luma's bindings in space{} need (a recipe without `upgrade_to_sm51`, a file built for D3D11)\n",
         Container::GetTarget(bytecode), rules.space);
   const Container::Bindings bindings = Container::GetBindings(bytecode);
   if (!bindings.found)
      return findings;
   if (rules.space != 0)
   {
      for (const uint32_t slot : rules.texture_slots)
      {
         if (!Container::Declares(bindings, Container::BindingType::ShaderResource, rules.space, slot))
            findings.errors += std::format("it reads one of Luma's textures at t{} but doesn't declare it in space{} (LUMA_TEXTURE_SPACE, a recipe's `space: LUMA_SPACE`)\n",
               slot, rules.space);
      }
      for (const Container::Binding& binding : bindings.bindings)
      {
         if (binding.space == rules.space && !IsLumas(binding, rules))
            findings.errors += std::format("it declares {}, which isn't one of Luma's registers in its space\n", Describe(binding));
      }
   }
   if (!rules.original || !rules.original->found)
      return findings;
   for (const Container::Binding& binding : bindings.bindings)
   {
      if (binding.space != 0)
         continue;
      const uint32_t count = std::min(binding.count, MAX_COMPARED);
      for (uint32_t i = 0; i < count; i++)
      {
         const uint32_t slot = binding.base + i;
         // With a space of 0 Luma's cbuffers are at the game's slots
         if (rules.space == 0 && binding.type == Container::BindingType::ConstantBuffer && IsLumaCBuffer(slot, rules))
            continue;
         if (!Container::Declares(*rules.original, binding.type, 0, slot))
         {
            findings.warnings += std::format("it declares {}, which the game's shader doesn't: it reads what the game's root signature binds there, if anything\n",
               Describe(binding));
            break;
         }
      }
   }
   return findings;
}

std::string CheckVariant(Api api, uint32_t hash, std::string_view source, const CompiledShader& compiled)
{
   if (api != Api::D3D12)
      return {};
   std::vector<uint32_t> textures;
   for (const Request& request : compiled.requests)
   {
      if (Resources::Database::Get(request.entry).kind == Resources::Database::Kind::Texture)
         textures.push_back(request.slot);
   }
   const Resources::LumaCBuffers::Options& cbuffers = Resources::LumaCBuffers::GetOptions();
   const Resources::Bindings::Options& bindings = Resources::Bindings::GetOptions();
   D3D12Rules rules = {.space = bindings.d3d12_space,
      .settings_slot = cbuffers.settings_slot,
      .data_slot = cbuffers.data_slot,
      .reads_luma = !compiled.requests.empty(),
      .static_samplers = bindings.static_samplers,
      .texture_slots = textures};
#if DEVELOPMENT
   if (const Database::Shader* original = Database::Find(hash))
      rules.original = &original->bindings;
#endif
   const Findings findings = CheckD3D12(compiled.bytecode, rules);
   if (!findings.warnings.empty())
      Log::Warning("Shaders: 0x{:08X} ({}) {}", hash, source, findings.warnings);
   if (findings.errors.empty())
      return {};
   rejected++;
   Log::Error("Shaders: 0x{:08X} ({}) can't run in the game's D3D12 pipelines, the game's shader runs:\n{}", hash, source, findings.errors);
   ASSERT_ONCE_MSG(false, std::format("0x{:08X} ({}) can't run in the game's D3D12 pipelines, see the log", hash, source));
   return findings.errors;
}

uint32_t GetRejectedCount()
{
   return rejected.load();
}
} // namespace Invariants
} // namespace Shaders
} // namespace Luma
#endif
