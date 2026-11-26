#include "Shaders/Shaders.hpp"

#include <algorithm>
#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>

#include "Game/GameRegistry.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Shaders/ShaderCache.hpp"
#include "Shaders/ShaderCompiler.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderDefines.hpp"
#include "Shaders/ShaderFiles.hpp"
#include "Shaders/ShaderInvariants.hpp"
#include "Shaders/ShaderPatching.hpp"
#include "Shaders/ShaderStore.hpp"
#include "System/JobQueue.hpp"
#include "Shaders/ShaderVariants.hpp"
#include "System/Paths.hpp"
#include "Utils/Hash.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Shaders {
namespace {
std::atomic<uint64_t> frame = 0;

// Decided by Start, before the hooks
bool started = false;
bool needed = false;
bool builds = false;
// Replacement file hashes, added by each scan. Never held with another lock.
std::mutex replaced_mutex;
std::unordered_set<uint32_t> replaced;

// In the device's private data: the store dies with the device
struct StoreHolder
{
   std::shared_ptr<Store> store;
};

// Live devices' stores, for what applies to all of them (a define change) and for the dev tools. Never held with another lock.
std::mutex stores_mutex;
std::vector<std::weak_ptr<Store>> stores;
std::weak_ptr<Store> shown;

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

// A custom shader is told its stage, so one file can hold several
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

// What the bytecode requests from Resources::Database: Luma's cbuffers at the game's slots whatever its reflection says (they're
// bound to every stage running a Luma shader), and the other entries its cbuffers and textures are named after, at their registers (a
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
Result Compile(std::span<const std::filesystem::path> roots, std::string_view file, std::span<const Compiler::Define> defines, std::string_view declared,
   Api build_api, std::string_view entry, BuildInfo& info)
{
   const std::string target = GetTarget(declared, build_api);
   if (target.empty())
   {
      info.failed++;
      return {nullptr, std::format("target {} is DXIL, which isn't compiled for {} yet: the game's shader runs", declared, GetName(build_api)), true};
   }
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

// The log of failures, and of warnings in Development and Test builds
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

// Worker thread. Stops when the device is released.
Build Run(std::vector<Compiler::Define> defines, Api build_api, const std::weak_ptr<Store>& store)
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
         if (store.expired())
            return build;
         // The hash being compiled, for a file of several
         std::vector<Compiler::Define> hash_defines = defines;
         hash_defines.push_back({"_" + hash.text, "1"});
         Result result = Compile(roots, file.path, hash_defines, file.target, build_api, "main", build.info);
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
         if (store.expired())
            return build;
         std::vector<Compiler::Define> shader_defines = defines;
         if (const std::string_view stage = GetStageDefine(GetStage(declared.target)); !stage.empty())
            shader_defines.push_back({std::string(stage), "1"});
         for (const auto& [name, value] : declared.defines)
            shader_defines.push_back({name, value});
         Result result = Compile(roots, file.path, shader_defines, declared.target, build_api, declared.entry, build.info);
         AddMessages(build.info, file.name, result);
         build.luma_shaders.push_back({*entry, {std::move(result.compiled), std::move(result.messages), result.failed}});
         System::Jobs::SetProgress(++done, total);
      }
   }
   return build;
}

void StartBuild(const std::shared_ptr<Store>& store);

// Presenting thread
System::Jobs::Outcome Publish(const std::shared_ptr<Store>& alive, Build build, const std::vector<Compiler::Define>& compiled_defines)
{
   Store& store = *alive;
#if DEVELOPMENT || TEST
   for (Variants::Variant& replacement : build.replacements)
   {
      if (!replacement.compiled)
         continue;
      if (const std::string errors = Invariants::CheckVariant(store.api, replacement.hash, replacement.file, *replacement.compiled); !errors.empty())
      {
         replacement.compiled.reset();
         replacement.rejected = true;
         replacement.messages += errors;
         build.info.errors += std::format("{} (0x{:08X}): {}", replacement.file, replacement.hash, errors);
      }
   }
#endif
   Defines::SetCompiled(compiled_defines);
   Variants::Publish(store, Variants::Source::File, std::move(build.replacements));
   Resources::Database::PublishShaders(store, std::move(build.luma_shaders));
   const BuildInfo& info = build.info;
   Log::Info("Shaders: {} compiled, {} from the cache, {} failed for {}", info.compiled, info.cached, info.failed, GetName(store.api));
   if (!info.errors.empty())
      Log::Warning("Shaders: {}", info.errors);
   System::Jobs::Outcome outcome = {info.failed != 0, info.failed ? std::format("{} shader{} failed to compile", info.failed, info.failed == 1 ? "" : "s") : std::format("Shaders ready: {} compiled, {} from the cache", info.compiled, info.cached)};
   {
      std::scoped_lock lock(store.last_build_mutex);
      build.info.id = store.last_build.id + 1;
      store.last_build = std::move(build.info);
   }

   store.building = false;
   if (store.reload_pending)
   {
      store.reload_pending = false;
      StartBuild(alive);
   }
   return outcome;
}

// The presenting thread, or a creation before the store is shared
void StartBuild(const std::shared_ptr<Store>& shared)
{
   Store& store = *shared;
   if (store.building)
   {
      store.reload_pending = true;
      return;
   }
   store.building = true;
   auto build = std::make_shared<Build>();
   std::vector<Compiler::Define> defines = Defines::Collect();
   std::vector<Compiler::Define> compiled_defines = defines;
   // The registers of Luma's cbuffers depend on the API, and D3D12 has its own (LUMA_TEXTURE_SPACE)
   for (const Compiler::Define& register_define : Resources::LumaCBuffers::GetDefines(store.api))
   {
      const auto it = std::ranges::find(defines, register_define.name, &Compiler::Define::name);
      if (it != defines.end())
         it->value = register_define.value;
      else
         defines.push_back(register_define);
   }
   // "#if _D3D12" in shaders of games that run on both APIs
   defines.push_back({"_D3D11", store.api == Api::D3D11 ? "1" : "0"});
   defines.push_back({"_D3D12", store.api == Api::D3D12 ? "1" : "0"});
   std::weak_ptr<Store> weak = shared;
   System::Jobs::Queue(
      "Compiling shaders", [build, defines = std::move(defines), api = store.api, weak] { *build = Run(defines, api, weak); },
      [build, compiled_defines = std::move(compiled_defines), weak] {
         const std::shared_ptr<Store> alive = weak.lock();
         return alive ? Publish(alive, std::move(*build), compiled_defines) : System::Jobs::Outcome{};
      });
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
#if DEVELOPMENT
   // The documented override (a game's file replacing a shared one), logged so it's never by accident
   for (const std::string& path : overridden)
      Log::Info("Shaders: the game's {} overrides Global's", path);
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
   builds = needed || any_program;
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

void CreateStore(IUnknown* device, Api api)
{
   auto store = std::make_shared<Store>(api);
   if (!Hooks::Lifetime::SetStateOf<StoreHolder>(device, store))
      return;
   if (started && builds)
      StartBuild(store);
   std::scoped_lock lock(stores_mutex);
   std::erase_if(stores, [](const std::weak_ptr<Store>& weak) { return weak.expired(); });
   stores.push_back(store);
}

void DestroyStore(IUnknown* device)
{
   Hooks::Lifetime::RemoveStateOf<StoreHolder>(device);
   std::scoped_lock lock(stores_mutex);
   std::erase_if(stores, [](const std::weak_ptr<Store>& weak) { return weak.expired(); });
}

std::shared_ptr<Store> GetStore(IUnknown* device)
{
   const StoreHolder* holder = device ? Hooks::Lifetime::GetStateOf<StoreHolder>(device) : nullptr;
   return holder ? holder->store : nullptr;
}

std::vector<std::shared_ptr<Store>> GetStores()
{
   std::vector<std::shared_ptr<Store>> live;
   std::scoped_lock lock(stores_mutex);
   for (const std::weak_ptr<Store>& weak : stores)
   {
      if (std::shared_ptr<Store> store = weak.lock())
         live.push_back(std::move(store));
   }
   return live;
}

std::shared_ptr<Store> GetShownStore()
{
   {
      std::scoped_lock lock(stores_mutex);
      if (std::shared_ptr<Store> store = shown.lock())
         return store;
   }
   std::vector<std::shared_ptr<Store>> live = GetStores();
   return live.empty() ? nullptr : live.front();
}

std::string GetTarget(std::string_view target, Api build_api)
{
   if (build_api != Api::D3D12)
      return std::string(target);
   // "ps_5_0": the stage, then the model
   const size_t separator = target.find('_');
   const std::string_view model = separator != std::string_view::npos ? target.substr(separator + 1) : std::string_view();
   if (model.starts_with('6'))
      return {};
   // SM5.1 names Luma's space; DXBC below it runs in the same pipelines as the game's SM5.0 stages
   if (model == "4_0" || model == "4_1" || model == "5_0")
      return std::format("{}_5_1", target.substr(0, separator));
   return std::string(target);
}

bool HasReplacementFile(uint32_t hash)
{
   std::scoped_lock lock(replaced_mutex);
   return replaced.contains(hash);
}

void Reload()
{
   if (!builds)
      return;
   for (const std::shared_ptr<Store>& store : GetStores())
      StartBuild(store);
}

void Unload()
{
   Variants::SetLoaded(false);
   Log::Info("Shaders: unloaded");
}

void Load()
{
   Variants::SetLoaded(true);
   Log::Info("Shaders: loaded");
   Reload();
}

void CountFrame(IUnknown* device)
{
   if (std::shared_ptr<Store> store = GetStore(device))
   {
      std::scoped_lock lock(stores_mutex);
      shown = store;
   }
   frame.fetch_add(1, std::memory_order_release);
}

uint64_t GetFrame()
{
   return frame.load(std::memory_order_acquire);
}

BuildInfo GetLastBuild(const Store& store)
{
   std::scoped_lock lock(store.last_build_mutex);
   return store.last_build;
}

BuildInfo GetLastBuild()
{
   const std::shared_ptr<Store> store = GetShownStore();
   return store ? GetLastBuild(*store) : BuildInfo();
}
} // namespace Shaders
} // namespace Luma
