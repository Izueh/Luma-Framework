#include "Trace/Mcp/McpTools.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Shaders/ShaderCompiler.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderVariants.hpp"
#include "Shaders/Shaders.hpp"
#include "System/JobQueue.hpp"
#include "System/Paths.hpp"
#include "Trace/D3D12/D3D12Stats.hpp"
#include "Utils/Json.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Trace {
namespace Mcp {
// In a named namespace: glaze can't reflect types with internal linkage
namespace Replies {
struct Stats
{
   bool ok = true;
   std::string path;
   std::string note;
};

struct Shader
{
   std::string hash;
   std::optional<std::string> name;
   std::string stage;
   std::optional<bool> live; // False for a shader without objects now (include_unloaded)
   uint32_t pipelines = 0; // The game's objects with this bytecode (a D3D11 pipeline is one shader)
   std::string replaced; // "file" or "none"
   std::string version;
   uint32_t bytes = 0;
   // From reflection, when it found them
   std::optional<std::vector<uint32_t>> cbs;
   std::optional<std::vector<uint32_t>> srvs;
   std::optional<std::vector<uint32_t>> uavs;
   std::optional<std::vector<uint32_t>> rtvs;
   std::optional<std::vector<uint32_t>> samplers;
};

struct ShaderList
{
   bool ok = true;
   std::vector<Shader> shaders;
   uint32_t live = 0;
   uint32_t loaded = 0;
   uint32_t matched = 0;
   uint32_t offset = 0;
   uint32_t returned = 0;
   std::vector<std::string> ignored_args;
};

struct ReplacementFile
{
   std::string file;
   bool is_hlsl = true;
   bool is_luma_native = false;
   bool compilation_failed = false;
   std::string compilation_errors;
   std::optional<std::string> disasm;
};

struct Replaced
{
   std::string hash;
   std::string stage;
   std::string replaced;
   std::string file;
};

struct ShaderStates
{
   bool ok = true;
   std::string compilation_errors;
   bool loaded = true;
   std::vector<Replaced> replaced;
   std::vector<std::string> ignored_args;
};

struct ShaderState
{
   bool ok = true;
   std::string hash;
   std::optional<std::string> name;
   std::optional<std::string> stage; // Unset if the game never created it
   uint32_t pipelines = 0;
   std::string replaced;
   // Its variants that compiled, highest priority first, and the trace's choice ("auto" when none: the highest-priority one runs)
   std::vector<std::string> variants;
   std::string forced = "auto";
   std::optional<ReplacementFile> custom_shader;
   std::vector<std::string> ignored_args;
};

struct DumpShader
{
   bool ok = true;
   std::string file;
   std::vector<std::string> ignored_args;
};

struct ReloadShaders
{
   bool ok = true;
   bool loaded = true;
   uint32_t replaced = 0; // Game shaders running a replacement now
   std::string compilation_errors;
   std::vector<std::string> ignored_args;
};
} // namespace Replies

namespace {
std::optional<std::string> GetName(const Shaders::Database::Shader& shader)
{
   return shader.name.empty() ? std::nullopt : std::optional(std::string(shader.name));
}

// Replaced while the variants are loaded
bool IsReplaced(uint32_t hash)
{
   return Shaders::Variants::IsLoaded() && Shaders::Variants::HasVariant(hash);
}

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

std::vector<const Shaders::Database::Shader*> GetSortedShaders()
{
   std::vector<const Shaders::Database::Shader*> shaders = Shaders::Database::GetShaders();
   std::ranges::sort(shaders, {}, &Shaders::Database::Shader::hash);
   return shaders;
}
} // namespace

namespace Tools {
std::string ShaderList(const Request& request)
{
   const auto stage = request.args.find("stage");
   const bool replaced_only = GetBool(request, "replaced_only");
   const bool include_unloaded = GetBool(request, "include_unloaded");
   // ~150 bytes per entry: the default stays under an agent's tool output cap
   const uint32_t offset = GetNumber(request, "offset", 0, 0, UINT32_MAX);
   const uint32_t limit = GetNumber(request, "limit", 200, 1, 100000);
   Replies::ShaderList reply;
   reply.ignored_args = GetIgnoredArgs(request, {"stage", "replaced_only", "include_unloaded", "offset", "limit"});
   for (const Shaders::Database::Shader* shader : GetSortedShaders())
   {
      reply.loaded++;
      const bool live = shader->objects != 0;
      reply.live += live;
      const bool replaced = live && IsReplaced(shader->hash);
      if ((!live && (!include_unloaded || replaced_only)) || (replaced_only && !replaced) ||
         (stage != request.args.end() && _stricmp(stage->second.c_str(), std::string(GetStageName(shader->stage)).c_str()) != 0))
         continue;
      if (reply.matched++ < offset || reply.shaders.size() >= limit)
         continue;
      Replies::Shader& entry = reply.shaders.emplace_back();
      entry.hash = FormatHash(shader->hash);
      entry.name = GetName(*shader);
      entry.stage = GetStageName(shader->stage);
      if (!live)
         entry.live = false;
      entry.pipelines = shader->objects;
      entry.replaced = replaced ? "file" : "none";
      entry.version = shader->target;
      entry.bytes = shader->size;
      if (shader->reflection.found)
      {
         entry.cbs = ToList(shader->reflection.constant_buffers);
         entry.srvs = ToList(shader->reflection.shader_resources);
         entry.uavs = ToList(shader->reflection.unordered_access);
         entry.rtvs = ToList(shader->reflection.render_targets);
         entry.samplers = ToList(shader->reflection.samplers);
      }
   }
   reply.offset = offset;
   reply.returned = uint32_t(reply.shaders.size());
   return Utils::Json::Write(reply);
}

std::string ShaderState(const Request& request)
{
   const std::vector<Shaders::Variants::Variant> replacements = Shaders::Variants::GetAll();
   const auto find_replacement = [&](uint32_t hash) { return std::ranges::find(replacements, hash, &Shaders::Variants::Variant::hash); };
   if (request.args.contains("hash"))
   {
      const std::optional<uint32_t> hash = GetHash(request);
      if (!hash)
         return Error("'hash' is a shader hash, like 0x12345678");
      Replies::ShaderState reply;
      reply.ignored_args = GetIgnoredArgs(request, {"hash", "disasm", "variant"});
      reply.hash = FormatHash(*hash);
      Shaders::Database::Shader* shader = Shaders::Database::Find(*hash);
      // The trace's variant picker: what the hash's binds take from now on, "auto" to clear it
      if (const auto variant = request.args.find("variant"); variant != request.args.end())
      {
         if (!shader)
            return Error(std::format("shader {} was never created by the game", FormatHash(*hash)));
         constexpr std::array CHOICES = {Shaders::Variants::Choice::Auto, Shaders::Variants::Choice::Original, Shaders::Variants::Choice::File,
            Shaders::Variants::Choice::Patch};
         const auto choice = std::ranges::find(CHOICES, variant->second, [](Shaders::Variants::Choice choice) { return Shaders::Variants::GetName(choice); });
         if (choice == CHOICES.end())
            return Error("'variant' is auto, original, file or patch");
         shader->forced_variant = *choice;
         Shaders::Variants::MarkChoiceChanged();
      }
      if (shader)
      {
         reply.name = GetName(*shader);
         reply.stage = std::string(GetStageName(shader->stage));
         reply.pipelines = shader->objects;
         reply.forced = Shaders::Variants::GetName(shader->forced_variant.load());
      }
      for (const Shaders::Variants::Variant& variant : Shaders::Variants::GetVariants(*hash))
      {
         if (variant.compiled)
            reply.variants.emplace_back(Shaders::Variants::GetName(variant.source));
      }
      reply.replaced = reply.pipelines && IsReplaced(*hash) ? "file" : "none";
      if (const auto replacement = find_replacement(*hash); replacement != replacements.end())
      {
         Replies::ReplacementFile& file = reply.custom_shader.emplace();
         file.file = replacement->file;
         file.compilation_failed = replacement->failed;
         file.compilation_errors = replacement->messages;
         if (GetBool(request, "disasm") && replacement->compiled)
            file.disasm = Shaders::Compiler::Disassemble(replacement->compiled->bytecode).value_or("disassembly failed");
      }
      return Utils::Json::Write(reply);
   }
   Replies::ShaderStates reply;
   reply.ignored_args = GetIgnoredArgs(request, {"hash", "disasm", "variant"});
   reply.compilation_errors = Shaders::GetLastBuild().errors;
   reply.loaded = Shaders::Variants::IsLoaded();
   for (const Shaders::Database::Shader* shader : GetSortedShaders())
   {
      if (!shader->objects || !IsReplaced(shader->hash))
         continue;
      const auto replacement = find_replacement(shader->hash);
      reply.replaced.push_back({FormatHash(shader->hash), std::string(GetStageName(shader->stage)), replacement != replacements.end() ? std::string(Shaders::Variants::GetName(replacement->source)) : std::string("file"), replacement != replacements.end() ? replacement->file : std::string()});
   }
   return Utils::Json::Write(reply);
}

std::string DumpShader(const Request& request)
{
   const std::optional<uint32_t> hash = GetHash(request);
   if (!hash)
      return Error("dump_shader takes a 'hash', like 0x12345678");
   const Shaders::Database::Shader* shader = Shaders::Database::Find(*hash);
   if (!shader)
      return Error(std::format("shader {} was never created by the game", FormatHash(*hash)));
   const std::filesystem::path file = Shaders::Database::Dump(*shader);
   if (file.empty())
      return Error(std::format("couldn't write {} to {}", FormatHash(*hash), Utils::Strings::ToUtf8(System::Paths::GetDump())));
   Replies::DumpShader reply;
   reply.file = Utils::Strings::ToUtf8(file);
   reply.ignored_args = GetIgnoredArgs(request, {"hash"});
   return Utils::Json::Write(reply);
}

// Waits on the pipe thread for the build the reload starts
// D3D12Stats.json, written now on a job (it's also written with each frame capture)
std::string DumpStats(const Request& request)
{
   if (!Trace::Stats::D3D12::HasData())
      return Error("no D3D12 device yet: the stats are D3D12's");
   return RunOnRenderThread(
      [] {
         const std::filesystem::path path = Trace::Stats::D3D12::Write();
         return Utils::Json::Write(Replies::Stats{.path = Utils::Strings::ToUtf8(path), .note = "written by a background job, ready within a frame or two"});
      },
      GetTimeout(request));
}

std::string ReloadShaders(const Request& request)
{
   const bool unload = GetBool(request, "unload");
   const uint32_t timeout = GetTimeout(request);
   const uint64_t previous_build = Shaders::GetLastBuild().id;
   const std::string requested = RunOnRenderThread(
      [unload] {
         unload ? Shaders::Unload() : Shaders::Load();
         return std::string();
      },
      timeout);
   if (!requested.empty())
      return requested;
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
   const auto is_busy = [] {
      const System::Jobs::Status status = System::Jobs::GetStatus();
      return status.running != 0 || status.queued != 0;
   };
   while (!unload && (Shaders::GetLastBuild().id == previous_build || is_busy()))
   {
      if (std::chrono::steady_clock::now() > deadline)
         return Error(std::format("the shaders didn't finish compiling in {} ms (see luma_status)", timeout));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
   }
   Replies::ReloadShaders reply;
   reply.loaded = Shaders::Variants::IsLoaded();
   for (const Shaders::Database::Shader* shader : Shaders::Database::GetShaders())
      reply.replaced += shader->objects && IsReplaced(shader->hash);
   reply.compilation_errors = Shaders::GetLastBuild().errors;
   reply.ignored_args = GetIgnoredArgs(request, {"unload"});
   return Utils::Json::Write(reply);
}
} // namespace Tools
} // namespace Mcp
} // namespace Trace
} // namespace Luma
#endif
