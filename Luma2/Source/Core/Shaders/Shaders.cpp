#include "Shaders/Shaders.hpp"

#include <algorithm>
#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>

#include "Game/Registry.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Shaders/Cache.hpp"
#include "Shaders/Compiler.hpp"
#include "Shaders/Database.hpp"
#include "Shaders/Defines.hpp"
#include "Shaders/Files.hpp"
#include "Shaders/Patching.hpp"
#include "System/Jobs.hpp"
#include "Shaders/Variants.hpp"
#include "System/Paths.hpp"
#include "Utils/Hash.hpp"
#include "Utils/Log.hpp"

namespace Luma {
namespace Shaders {
namespace {
std::atomic<uint64_t> frame = 0;

// Presenting thread
bool started = false;
// Decided by Start, before the hooks
bool needed = false;
bool building = false;
bool reload_pending = false;
// Any thread sets it (a device's creation), the presenting thread builds for it
std::atomic<int32_t> api = -1;
// The API the shaders were last built for (presenting thread)
std::optional<Api> built_api;
// Replacement file hashes, added by each scan. Never held with another lock.
std::mutex replaced_mutex;
std::unordered_set<uint32_t> replaced;
// Written on the presenting thread. Never held with another lock.
std::mutex last_build_mutex;
BuildInfo last_build;

struct Build
{
   std::vector<Variants::Variant> replacements;
   std::vector<std::pair<uint32_t, Resources::Database::ShaderState>> luma_shaders;
   BuildInfo info;
};

struct Result
{
   std::shared_ptr<const CompiledShader> compiled;
   std::string messages;
   bool failed = false;
};

std::string_view GetTargetPrefix(ShaderStage stage)
{
   constexpr std::string_view PREFIXES[] = {"vs", "hs", "ds", "gs", "ps", "cs"};
   return PREFIXES[size_t(stage)];
}

// "ps_5_0": the pixel stage
ShaderStage GetStage(std::string_view target)
{
   constexpr std::string_view PREFIXES[] = {"vs", "hs", "ds", "gs", "ps", "cs"};
   for (size_t i = 0; i < std::size(PREFIXES); i++)
   {
      if (target.starts_with(PREFIXES[i]))
         return ShaderStage(i);
   }
   return ShaderStage::Pixel;
}

// 1.0 tells a custom shader its stage, so one file can hold several
std::string_view GetStageDefine(ShaderStage stage)
{
   switch (stage)
   {
   case ShaderStage::Vertex: return "VS";
   case ShaderStage::Geometry: return "GS";
   case ShaderStage::Pixel: return "PS";
   case ShaderStage::Compute: return "CS";
   default: return {};
   }
}

// What the bytecode requests from Resources::Database: Luma's cbuffers at the game's slots whatever its reflection says (1.0 binds
// them to every stage running a Luma shader), and the other entries its cbuffers and textures are named after, at their registers (a
// texture on D3D12 in Luma's table: LUMA_TEXTURE_SPACE)
std::shared_ptr<const CompiledShader> MakeCompiledShader(Bytecode bytecode)
{
   auto compiled = std::make_shared<CompiledShader>();
   compiled->requests = Resources::LumaCBuffers::GetRequests();
   for (const Compiler::Binding& binding : Compiler::ReflectConstantBufferBindings(bytecode))
   {
      const std::optional<uint32_t> entry = Resources::Database::Find(Utils::Hash::Name(binding.name));
      if (entry && Resources::Database::Get(*entry).kind == Resources::Database::Kind::ConstantBuffer &&
         !std::ranges::contains(compiled->requests, *entry, &Request::entry))
         compiled->requests.push_back({*entry, binding.slot});
   }
   for (const Compiler::Binding& binding : Compiler::ReflectTextureBindings(bytecode))
   {
      const std::optional<uint32_t> entry = Resources::Database::Find(Utils::Hash::Name(binding.name));
      if (entry && Resources::Database::Get(*entry).kind == Resources::Database::Kind::Texture && !std::ranges::contains(compiled->requests, *entry, &Request::entry))
         compiled->requests.push_back({*entry, binding.slot});
   }
   compiled->bytecode = std::move(bytecode);
   return compiled;
}

// Worker thread. From the cache when the preprocessed source was compiled before.
Result Compile(std::span<const std::filesystem::path> roots, std::string_view file, std::span<const Compiler::Define> defines, std::string_view target,
   std::string_view entry, BuildInfo& info)
{
   const Compiler::Preprocessed preprocessed = Compiler::Preprocess(roots, file, defines);
   if (preprocessed.failed)
   {
      info.failed++;
      return {nullptr, preprocessed.messages, true};
   }
   const uint64_t key = Cache::GetKey(preprocessed.text, target, entry);
   if (std::optional<Bytecode> cached = Cache::ReadShader(key))
   {
      info.cached++;
      return {MakeCompiledShader(std::move(*cached))};
   }
   Compiler::Compiled compiled = Compiler::Compile(roots, file, defines, target, entry);
   if (compiled.failed)
   {
      info.failed++;
      return {nullptr, std::move(compiled.messages), true};
   }
   info.compiled++;
   Cache::WriteShader(key, compiled.bytecode);
   return {MakeCompiledShader(std::move(compiled.bytecode)), std::move(compiled.messages)};
}

// 1.0's log of failures, and of warnings in Development and Test builds
void AddMessages(BuildInfo& info, std::string_view file, const Result& result)
{
#if !DEVELOPMENT && !TEST
   if (!result.failed)
      return;
#endif
   if (!result.messages.empty())
      info.errors += std::format("{}: {}\n", file, result.messages);
}

void AddReplaced(const std::vector<Files::File>& files)
{
   std::scoped_lock lock(replaced_mutex);
   for (const Files::File& file : files)
   {
      for (const Files::Hash& hash : file.hashes)
         replaced.insert(hash.value);
   }
}

// Worker thread
Build Run(std::vector<Compiler::Define> defines, Api build_api)
{
   const std::span<const std::filesystem::path> roots = System::Paths::GetShaderRoots();
   const std::vector<Files::File> files = Files::Scan(roots);
   AddReplaced(files);
   Build build;
   uint32_t total = 0;
   for (const Files::File& file : files)
      total += uint32_t(file.hashes.size() + file.declarations.size());
   uint32_t done = 0;
   System::Jobs::SetProgress(done, total);

   for (const Files::File& file : files)
   {
      if (file.kind != Files::Kind::Replacement)
         continue;
      for (const Files::Hash& hash : file.hashes)
      {
         // The hash being compiled, for a file of several
         std::vector<Compiler::Define> hash_defines = defines;
         hash_defines.push_back({"_" + hash.text, "1"});
         Result result = Compile(roots, file.path, hash_defines, GetTarget(file.target, build_api), "main", build.info);
         AddMessages(build.info, file.name, result);
         build.replacements.push_back({.hash = hash.value,
            .source = Variants::Source::File,
            .priority = Variants::FILE_PRIORITY,
            .file = file.path,
            .compiled = std::move(result.compiled),
            .messages = std::move(result.messages),
            .failed = result.failed});
         System::Jobs::SetProgress(++done, total);
      }
   }

   for (const Files::File& file : files)
   {
      for (const Files::Declaration& declared : file.declarations)
      {
         // Registered by Start: a program declared since needs a restart
         const std::optional<uint32_t> entry = Resources::Database::Find(Utils::Hash::Name(declared.name));
         if (!entry)
            continue;
         std::vector<Compiler::Define> shader_defines = defines;
         if (const std::string_view stage = GetStageDefine(GetStage(declared.target)); !stage.empty())
            shader_defines.push_back({std::string(stage), "1"});
         for (const auto& [name, value] : declared.defines)
            shader_defines.push_back({name, value});
         Result result = Compile(roots, file.path, shader_defines, GetTarget(declared.target, build_api), declared.entry, build.info);
         AddMessages(build.info, file.name, result);
         build.luma_shaders.push_back({*entry, {std::move(result.compiled), std::move(result.messages), result.failed}});
         System::Jobs::SetProgress(++done, total);
      }
   }
   return build;
}

// Presenting thread
System::Jobs::Outcome Publish(Build build)
{
   Variants::Publish(Variants::Source::File, std::move(build.replacements));
   Resources::Database::PublishShaders(std::move(build.luma_shaders));
   const BuildInfo& info = build.info;
   Utils::Log::Info("Shaders: {} compiled, {} from the cache, {} failed", info.compiled, info.cached, info.failed);
   if (!info.errors.empty())
      Utils::Log::Warning("Shaders: {}", info.errors);
   System::Jobs::Outcome outcome = {info.failed != 0, info.failed ? std::format("{} shader{} failed to compile", info.failed, info.failed == 1 ? "" : "s") : std::format("Shaders ready: {} compiled, {} from the cache", info.compiled, info.cached)};
   {
      std::scoped_lock lock(last_build_mutex);
      build.info.id = last_build.id + 1;
      last_build = std::move(build.info);
   }

   building = false;
   if (reload_pending)
   {
      reload_pending = false;
      Reload();
   }
   return outcome;
}

void OnSettingChanged(const Settings::Setting& setting, const Settings::Value& value)
{
   if (!started)
      return;
   const bool is_define = std::holds_alternative<Settings::Sources::Define>(setting.source);
   const bool compile_on = setting.key == Settings::Core::AUTO_COMPILE.key && std::get<bool>(value);
   if ((is_define && Settings::Get(Settings::Core::AUTO_COMPILE)) || (compile_on && Defines::NeedsCompile()))
      Reload();
}
} // namespace

void Register()
{
   Defines::Register();
   Database::Register();
   Settings::AddChangeListener(&OnSettingChanged);
}

void Start()
{
   std::vector<std::string> overridden;
   const std::vector<Files::File> files = Files::Scan(System::Paths::GetShaderRoots(), &overridden);
   AddReplaced(files);
   if (const std::optional<Api> declared = Game::Registry::GetInfo().api)
      SetApi(*declared);
#if DEVELOPMENT
   // The documented override (a game's file replacing a shared one), logged so it's never by accident
   for (const std::string& path : overridden)
      Utils::Log::Info("Shaders: the game's {} overrides Global's", path);
#endif
   bool any_program = false;
   for (const Files::File& file : files)
   {
      for (const Files::Declaration& declaration : file.declarations)
      {
         any_program |= Resources::Database::Register({.name_hash = Utils::Hash::Name(declaration.name),
                                                         .name = declaration.name,
                                                         .kind = Resources::Database::Kind::Shader,
                                                         .stage = GetStage(declaration.target),
                                                         .file = file.path})
                           .has_value();
      }
   }
#if DEVELOPMENT || TEST
   needed = true;
#else
   needed = Game::Registry::HasDrawCallbacks() || Patching::IsNeeded() ||
      std::ranges::any_of(files, [](const Files::File& file) { return file.kind == Files::Kind::Replacement; });
#endif
   started = true;
   if (needed || any_program)
      Reload();
}

bool IsNeeded()
{
   return needed;
}

Hooks::D3D11::Functions GetD3D11Functions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   if (!needed)
      return functions;
   for (const Hooks::D3D11::Function function :
      {// The database
         CreateVertexShader, CreateHullShader, CreateDomainShader, CreateGeometryShader, CreateGeometryShaderWithStreamOutput, CreatePixelShader,
         CreateComputeShader,
         // The variants, and what clears a context's state
         VSSetShader, HSSetShader, DSSetShader, GSSetShader, PSSetShader, CSSetShader, ClearState, FinishCommandList, ExecuteCommandList,
         // The resources variants request
         Draw, DrawInstanced, DrawIndexed, DrawIndexedInstanced, DrawAuto, DrawInstancedIndirect, DrawIndexedInstancedIndirect, Dispatch, DispatchIndirect})
      functions.set(size_t(function));
   return functions;
}

Hooks::D3D12::Functions GetD3D12Functions()
{
   using enum Hooks::D3D12::Function;
   Hooks::D3D12::Functions functions;
   if (!needed)
      return functions;
   for (const Hooks::D3D12::Function function :
      {// The database, the extended root signatures, the variant pipelines
         CreatePipelineState, CreateRootSignature, CreateStateObject,
         // The variants swapped in, and what resets a list
         Reset, ClearState, SetPipelineState,
         // Luma's cbuffers at Luma's root parameters, its textures in the heaps' tails
         SetRootSignature, Draw, Dispatch, ExecuteIndirect, ExecuteBundle, CreateDescriptorHeap, SetDescriptorHeaps})
      functions.set(size_t(function));
   return functions;
}

void SetApi(Api set)
{
   int32_t expected = -1;
   if (!api.compare_exchange_strong(expected, int32_t(set)) && expected != int32_t(set))
      Utils::Log::Warning("Shaders: a {} device after a {} one, the shaders stay built for {}", GetName(set), GetName(Api(expected)), GetName(Api(expected)));
}

std::optional<Api> GetApi()
{
   const int32_t value = api.load(std::memory_order_acquire);
   return value < 0 ? std::nullopt : std::optional(Api(value));
}

std::string GetTarget(std::string_view target, Api build_api)
{
   std::string result(target);
   if (build_api == Api::D3D12 && result.ends_with("_5_0"))
      result.back() = '1';
   return result;
}

bool HasReplacementFile(uint32_t hash)
{
   std::scoped_lock lock(replaced_mutex);
   return replaced.contains(hash);
}

void Reload()
{
   const std::optional<Api> build_api = GetApi();
   // Built at the first present after a device says which API (CountFrame)
   if (!build_api)
   {
      reload_pending = true;
      return;
   }
   if (building)
   {
      reload_pending = true;
      return;
   }
   building = true;
   built_api = build_api;
   std::vector<Compiler::Define> defines = Defines::Collect();
   Defines::SetCompiled(defines);
   // The registers of Luma's cbuffers depend on the API, and D3D12 has its own (LUMA_TEXTURE_SPACE)
   for (const Compiler::Define& register_define : Resources::LumaCBuffers::GetDefines(*build_api))
   {
      const auto it = std::ranges::find(defines, register_define.name, &Compiler::Define::name);
      if (it != defines.end())
         it->value = register_define.value;
      else
         defines.push_back(register_define);
   }
   // "#if _D3D12" in shaders of games that run on both APIs
   defines.push_back({"_D3D11", *build_api == Api::D3D11 ? "1" : "0"});
   defines.push_back({"_D3D12", *build_api == Api::D3D12 ? "1" : "0"});
   auto build = std::make_shared<Build>();
   System::Jobs::Queue(
      "Compiling shaders", [build, defines = std::move(defines), build_api = *build_api] { *build = Run(defines, build_api); },
      [build] { return Publish(std::move(*build)); });
}

void Unload()
{
   Variants::SetLoaded(false);
   Utils::Log::Info("Shaders: unloaded");
}

void Load()
{
   Variants::SetLoaded(true);
   Utils::Log::Info("Shaders: loaded");
   Reload();
}

void CountFrame()
{
   // A build that waited for the API
   if (started && !building && reload_pending && GetApi())
   {
      reload_pending = false;
      Reload();
   }
   frame.fetch_add(1, std::memory_order_release);
}

uint64_t GetFrame()
{
   return frame.load(std::memory_order_acquire);
}

BuildInfo GetLastBuild()
{
   std::scoped_lock lock(last_build_mutex);
   return last_build;
}
} // namespace Shaders
} // namespace Luma
