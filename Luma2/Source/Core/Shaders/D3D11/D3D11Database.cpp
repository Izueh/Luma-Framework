#include "Shaders/D3D11/D3D11Database.hpp"

#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <d3d11shader.h>
#include <wrl/client.h>

#include "Game/Registry.hpp"
#include "Hooks/Lifetime.hpp"
#include "Shaders/Cache.hpp"
#include "Shaders/Compiler.hpp"
#include "Utils/Hash.hpp"
#include "Utils/Json.hpp"

namespace Luma {
namespace Shaders {
namespace Database {
namespace D3D11 {
#if DEVELOPMENT
// A shader's reflection in the cache, slots as lists. Not in an anonymous namespace: glaze can't reflect types with internal
// linkage.
struct CachedReflection
{
   std::vector<uint32_t> constant_buffers;
   std::vector<uint32_t> samplers;
   std::vector<uint32_t> shader_resources;
   std::vector<uint32_t> unordered_access;
   std::vector<uint32_t> render_targets;
   bool found = false;
};
#endif

namespace {
// In the private data of each of the game's shader objects
struct Object
{
   Shader* shader;

   explicit Object(Shader* shader_) : shader(shader_) { shader->objects++; }

   ~Object() { shader->objects--; }

   Object(const Object&) = delete;
   Object& operator=(const Object&) = delete;
};

#if DEVELOPMENT
template <size_t N>
std::vector<uint32_t> ToList(const std::bitset<N>& slots)
{
   std::vector<uint32_t> list;
   for (uint32_t i = 0; i < N; i++)
   {
      if (slots[i])
         list.push_back(i);
   }
   return list;
}

template <size_t N>
std::bitset<N> FromList(const std::vector<uint32_t>& list)
{
   std::bitset<N> slots;
   for (const uint32_t slot : list)
   {
      if (slot < N)
         slots.set(slot);
   }
   return slots;
}

// 1.0's ReflectShaderSlots, and its check that the bindings were really found: games strip reflection data in ways that
// still pass, with no bindings left
bool ReflectSlots(std::span<const uint8_t> bytecode, ShaderStage stage, Reflection& reflection)
{
   Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflector;
   D3D11_SHADER_DESC desc = {};
   if (!Compiler::Reflect(bytecode, __uuidof(ID3D11ShaderReflection), &reflector) || FAILED(reflector->GetDesc(&desc)))
      return false;
   bool found_render_targets = false;
   bool found_other_bindings = false;
   for (UINT i = 0; i < desc.OutputParameters; i++)
   {
      D3D11_SIGNATURE_PARAMETER_DESC output = {};
      if (SUCCEEDED(reflector->GetOutputParameterDesc(i, &output)) && std::string_view(output.SemanticName) == "SV_Target" &&
         output.SemanticIndex < reflection.render_targets.size())
      {
         reflection.render_targets.set(output.SemanticIndex);
         found_render_targets = true;
      }
   }
   for (UINT i = 0; i < desc.BoundResources; i++)
   {
      D3D11_SHADER_INPUT_BIND_DESC binding = {};
      if (FAILED(reflector->GetResourceBindingDesc(i, &binding)))
         continue;
      for (UINT slot = binding.BindPoint; slot < binding.BindPoint + binding.BindCount; slot++)
      {
         if (binding.Type == D3D_SIT_TEXTURE && slot < reflection.shader_resources.size())
         {
            reflection.shader_resources.set(slot);
            found_other_bindings = true;
         }
         else if (binding.Type == D3D_SIT_UAV_RWTYPED && slot < reflection.unordered_access.size())
         {
            reflection.unordered_access.set(slot);
            found_other_bindings = true;
         }
         else if (binding.Type == D3D_SIT_CBUFFER && slot < reflection.constant_buffers.size())
         {
            reflection.constant_buffers.set(slot);
         }
         // Always there, even when the rest was stripped: not a sign of anything
         else if (binding.Type == D3D_SIT_SAMPLER && slot < reflection.samplers.size())
         {
            reflection.samplers.set(slot);
         }
      }
   }
   // In Dishonored 2 pixel shaders' render targets aren't found, while their other bindings are
   const bool has_code = desc.FloatInstructionCount != 0 || desc.IntInstructionCount != 0 || desc.UintInstructionCount != 0;
   return (found_other_bindings || has_code) && (found_render_targets || stage != ShaderStage::Pixel);
}

// 1.0's fallback: the declarations at the top of the disassembly. Slow, so the result is cached.
bool DisassembleSlots(std::span<const uint8_t> bytecode, ShaderStage stage, std::string_view target, Reflection& reflection)
{
   const std::optional<std::string> disassembly = Compiler::Disassemble(bytecode);
   if (!disassembly)
      return false;
   const std::regex constant_buffer_pattern(R"(dcl_constantbuffer.*[cC][bB]([0-9]{1,2}))");
   const std::regex sampler_pattern(R"(dcl_sampler.*[sS]([0-9]{1,2}))");
   const std::regex shader_resource_pattern(R"(.*dcl_resource_texture.*[tT]([0-9]{1,3})$)");
   const std::regex unordered_access_pattern(R"(.*dcl_uav_.*[uU]([0-9]{1,2})$)");
   const std::regex render_target_pattern(R"(dcl_output.*[oO]([0-9]{1,1}))");
   const std::regex shader_model_pattern(R"(\b(ps|vs|gs|hs|ds|cs)_(\d+)_(\d+)\s*$)", std::regex_constants::icase);
   const int major_version = target.size() > 3 && target[3] >= '0' && target[3] <= '9' ? target[3] - '0' : -1;

   std::istringstream lines(*disassembly);
   std::string line;
   std::smatch match;
   // Some shaders carry an older version's disassembly as a comment above theirs (Mafia II): its "0 " instruction lines are skipped
   bool stop_at_next_0 = true;
   bool matching_line = false;
   while (std::getline(lines, line))
   {
      const bool previous_matching_line = matching_line;
      if (!matching_line && std::regex_search(line, match, shader_model_pattern) && std::stoi(match[2].str()) != major_version)
         stop_at_next_0 = false;
      // The first instruction ("0 ...") ends the declarations
      const size_t first = line.find_first_not_of(" \t");
      if (first != std::string::npos && first + 1 < line.size() && line[first] == '0' && line[first + 1] == ' ')
      {
         if (stop_at_next_0)
            break;
         stop_at_next_0 = true;
      }
      const auto add = [&](const std::regex& pattern, bool full_match, auto& slots) {
         if (!(full_match ? std::regex_match(line, match, pattern) : std::regex_search(line, match, pattern)))
            return;
         const int slot = std::stoi(match[1].str());
         if (slot >= 0 && size_t(slot) < slots.size())
         {
            slots.set(size_t(slot));
            matching_line = true;
         }
      };
      add(constant_buffer_pattern, false, reflection.constant_buffers);
      add(sampler_pattern, false, reflection.samplers);
      add(shader_resource_pattern, true, reflection.shader_resources);
      if (stage == ShaderStage::Pixel || stage == ShaderStage::Compute)
         add(unordered_access_pattern, true, reflection.unordered_access);
      if (stage == ShaderStage::Pixel)
         add(render_target_pattern, false, reflection.render_targets);
      if (previous_matching_line && !matching_line)
         break;
   }
   return true;
}

// Reflection first: it answers in microseconds. The cache only keeps what the slow disassembly fallback found, so creating
// shaders (thousands while a game loads) does no file I/O for the ones with reflection data.
Reflection Reflect(const Shader& shader)
{
   Reflection reflection;
   if (ReflectSlots(shader.bytecode, shader.stage, reflection))
   {
      reflection.found = true;
      reflection.constant_buffer_layouts = Compiler::ReflectConstantBuffers(shader.bytecode);
      return reflection;
   }
   if (const std::optional<std::string> text = Cache::ReadReflection(shader.hash))
   {
      CachedReflection cached;
      if (Utils::Json::Read(*text, cached))
      {
         return {FromList<14>(cached.constant_buffers), FromList<16>(cached.samplers), FromList<128>(cached.shader_resources),
            FromList<64>(cached.unordered_access), FromList<8>(cached.render_targets), cached.found};
      }
   }
   reflection = {};
   reflection.found = DisassembleSlots(shader.bytecode, shader.stage, shader.target, reflection);
   // Every slot then, so nothing is hidden
   if (!reflection.found)
   {
      reflection.constant_buffers.set();
      reflection.samplers.set();
      reflection.shader_resources.set();
      reflection.unordered_access.set();
      reflection.render_targets.set();
   }
   Cache::WriteReflection(shader.hash, Utils::Json::Write(CachedReflection{ToList(reflection.constant_buffers), ToList(reflection.samplers), ToList(reflection.shader_resources), ToList(reflection.unordered_access), ToList(reflection.render_targets), reflection.found}));
   return reflection;
}
#endif
} // namespace

uint32_t TrackShader(ShaderStage stage, std::span<const uint8_t> bytecode, ID3D11DeviceChild* object)
{
   const uint32_t hash = Utils::Hash::CRC32(bytecode);
   Shader* shader = Database::Find(hash);
   if (!shader)
   {
      auto created = std::make_unique<Shader>();
      created->hash = hash;
      created->stage = stage;
      created->target = GetTarget(bytecode);
      created->size = uint32_t(bytecode.size());
      created->name = GetName(hash);
      created->draw_callback = Game::Registry::FindDrawCallback(hash) != nullptr;
#if DEVELOPMENT || TEST
      created->bytecode.assign(bytecode.begin(), bytecode.end());
#endif
#if DEVELOPMENT
      created->reflection = Reflect(*created);
#endif
      shader = &Add(std::move(created));
   }
   Hooks::Lifetime::SetState<Object>(object, shader);
   return hash;
}

const Shader* Find(ID3D11DeviceChild* object)
{
   const Object* state = Hooks::Lifetime::GetState<Object>(object);
   return state ? state->shader : nullptr;
}
} // namespace D3D11
} // namespace Database
} // namespace Shaders
} // namespace Luma
