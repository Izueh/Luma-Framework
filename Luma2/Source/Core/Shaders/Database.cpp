#include "Shaders/Database.hpp"

#include <array>
#include <cstring>
#include <format>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <regex>
#include <sstream>
#include <utility>

#include <d3d11shader.h>
#include <wrl/client.h>

#include "Game/Registry.hpp"
#include "Resources/Bindings.hpp"
#include "Settings/Keys.hpp"
#include "Shaders/Cache.hpp"
#include "Shaders/Compiler.hpp"
#include "Settings/Registry.hpp"
#include "Shaders/Patching.hpp"
#include "Shaders/Signatures.hpp"
#include "System/Jobs.hpp"
#include "System/Paths.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Hash.hpp"
#include "Utils/Json.hpp"
#include "Utils/Log.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Shaders {
namespace Database {
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
// Never held with another lock
std::shared_mutex mutex;
std::unordered_map<uint32_t, std::unique_ptr<Shader>> shaders;

struct Names
{
   std::string_view name;
   std::span<const uint32_t> hashes;
};

// Written while Init runs, read without a lock after
std::vector<Names> names;

#if DEVELOPMENT || TEST
const std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::DUMP_SHADERS.key,
      .label = "Dump Shaders",
      .tooltip = "Writes every shader the game creates to Luma/Dump, once (\"0x<hash>.<target>.cso\")",
      .category = "Development",
      .visibility = Settings::Visibility::Development,
      .default_value = true,
   },
};

// Lock order: never held with another lock
std::mutex dump_mutex;
// Created since the last present
std::vector<const Shader*> created;
// Presenting thread
std::unordered_set<uint32_t> dumped;
bool dumped_scanned = false;
bool dumping = false;

// A hash isn't dumped again across boots: what's in the folder is done
void ScanDumped()
{
   std::error_code error;
   for (std::filesystem::directory_iterator it(System::Paths::GetDump(), error), end; !error && it != end; it.increment(error))
   {
      const std::wstring name = it->path().filename().wstring();
      uint32_t hash = 0;
      if (it->path().extension() == L".cso" && name.starts_with(L"0x") && swscanf_s(name.c_str() + 2, L"%8x", &hash) == 1)
         dumped.insert(hash);
   }
}
#endif
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

// From the reflection data, checking that the bindings were really found: games strip reflection data in ways that
// still pass, with no bindings left
bool ReflectSlots(std::span<const uint8_t> bytecode, ShaderStage stage, Reflection& reflection)
{
   // FXC's reflector is used under its lock (one thread at a time), released before it ends
   const Compiler::Lock lock;
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

// The fallback: the declarations at the top of the disassembly. Slow, so the result is cached.
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

void Register()
{
#if DEVELOPMENT || TEST
   Settings::Register(SETTINGS, Settings::Kind::Development);
#endif
}

void Name(std::string_view name, std::span<const uint32_t> hashes)
{
   names.push_back({name, hashes});
}

std::string_view GetName(uint32_t hash)
{
   for (const Names& entry : names)
   {
      if (std::ranges::contains(entry.hashes, hash))
         return entry.name;
   }
   return {};
}

Shader* Find(uint32_t hash)
{
   std::shared_lock lock(mutex);
   const auto it = shaders.find(hash);
   return it != shaders.end() ? it->second.get() : nullptr;
}

Shader& Add(std::unique_ptr<Shader> shader)
{
   Shader* kept = nullptr;
   {
      std::scoped_lock lock(mutex);
      auto [it, added] = shaders.try_emplace(shader->hash, std::move(shader));
      kept = it->second.get();
      if (!added)
         return *kept;
   }
#if DEVELOPMENT || TEST
   std::scoped_lock lock(dump_mutex);
   created.push_back(kept);
#endif
   return *kept;
}

#if DEVELOPMENT
namespace {
// A game shader declaring a register in Luma's own space (SM5.1, SM6) would read Luma's bindings: its root signature isn't
// extended (Shaders::RootSignatures::D3D12), and the mod picks another space
void CheckLumaSpace(const Shader& shader)
{
   const uint32_t space = Resources::Bindings::GetOptions().d3d12_space;
   if (space == 0 || !shader.bindings.UsesSpace(space))
      return;
   Utils::Log::Error("Shaders: 0x{:08X} ({}) declares registers in space {}, Luma's: pick another in Game::Options::bindings", shader.hash, shader.target, space);
   ASSERT_ONCE_MSG(false, std::format("Game shader 0x{:08X} uses register space {}, Luma's: pick another in Game::Options::bindings", shader.hash, space));
}
} // namespace
#endif

Shader& Track(ShaderStage stage, std::span<const uint8_t> bytecode)
{
   const uint32_t hash = Utils::Hash::CRC32(bytecode);
   if (Shader* shader = Find(hash))
      return *shader;
   auto added = std::make_unique<Shader>();
   added->hash = hash;
   added->stage = stage;
   added->target = Container::GetTarget(bytecode);
   added->size = uint32_t(bytecode.size());
   added->name = GetName(hash);
   added->draw_callback = Game::Registry::FindDrawCallback(hash) != nullptr || Game::Registry::FindD3D12DrawCallback(hash) != nullptr;
#if DEVELOPMENT || TEST
   added->bytecode.assign(bytecode.begin(), bytecode.end());
#else
   if (Patching::IsNeeded())
      added->bytecode.assign(bytecode.begin(), bytecode.end());
#endif
#if DEVELOPMENT
   added->reflection = Reflect(*added);
   added->signature = Container::GetSignature(bytecode);
   added->bindings = Container::GetBindings(bytecode);
   CheckLumaSpace(*added);
#endif
   const Shader* const tracked = added.get();
   Shader& kept = Add(std::move(added));
   // Once per hash, by the thread whose shader was kept: the game's sync patch runs in this creation call
   if (&kept == tracked)
      Patching::TrackShader(kept);
#if DEVELOPMENT
   // Replacements compiled before the game created their original are checked now
   for (const Variants::Variant& variant : Variants::GetVariants(hash))
   {
      if (variant.compiled)
         Signatures::Check(hash, kept.stage, kept.signature, variant.compiled->bytecode, variant.file);
   }
#endif
   return kept;
}

std::vector<const Shader*> GetShaders()
{
   std::shared_lock lock(mutex);
   std::vector<const Shader*> list;
   list.reserve(shaders.size());
   for (const auto& [hash, shader] : shaders)
      list.push_back(shader.get());
   return list;
}

#if DEVELOPMENT || TEST
void QueueDumps()
{
   if (!dumped_scanned)
   {
      dumped_scanned = true;
      ScanDumped();
   }
   // Switched off, what's created meanwhile waits to be dumped once it's on again
   if (dumping || !Settings::Get(Settings::Core::DUMP_SHADERS))
      return;
   std::vector<const Shader*> to_dump;
   {
      std::scoped_lock lock(dump_mutex);
      to_dump.swap(created);
   }
   std::erase_if(to_dump, [](const Shader* shader) { return !dumped.insert(shader->hash).second; });
   if (to_dump.empty())
      return;
   dumping = true;
   System::Jobs::Queue(
      "Dumping shaders",
      [to_dump] {
         for (const Shader* shader : to_dump)
            Dump(*shader);
      },
      [] {
         dumping = false;
         return System::Jobs::Outcome{};
      },
      System::Jobs::Priority::Low);
}

std::filesystem::path Dump(const Shader& shader)
{
   const std::filesystem::path directory = System::Paths::GetDump();
   std::error_code error;
   std::filesystem::create_directories(directory, error);
   const std::filesystem::path path = directory / std::format(L"0x{:08X}{}{}.cso", shader.hash, shader.target.empty() ? L"" : L".", std::wstring(shader.target.begin(), shader.target.end()));
   std::ofstream stream(path, std::ios::binary | std::ios::trunc);
   stream.write(reinterpret_cast<const char*>(shader.bytecode.data()), std::streamsize(shader.bytecode.size()));
   if (!stream)
   {
      Utils::Log::Warning("Shaders: couldn't dump {}", Utils::Strings::ToUtf8(path));
      return {};
   }
   return path;
}
#endif
} // namespace Database
} // namespace Shaders
} // namespace Luma
