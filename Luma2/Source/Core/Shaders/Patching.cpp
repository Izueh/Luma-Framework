#include "Shaders/Patching.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <expected>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if LUMA_USE_DXP
#include <dxp/RecipeReport.hpp>
#include <dxp/Version.hpp>
#include <dxp/sm5/Recipe.hpp>
#endif

#include "Resources/ResourceDatabase.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Shaders/Cache.hpp"
#include "Shaders/Container.hpp"
#include "Shaders/Database.hpp"
#include "Shaders/Shaders.hpp"
#include "Shaders/Variants.hpp"
#include "System/Jobs.hpp"
#include "System/Paths.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Hash.hpp"
#include "Utils/Json.hpp"
#include "Utils/Log.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Shaders {
namespace Patching {
// The patch index in the cache, by patch key. Not in an anonymous namespace: glaze can't reflect types with internal linkage.
struct IndexBinding
{
   std::string handle;
   std::string kind;
   uint32_t slot = 0;
   uint32_t space = 0;
};

struct IndexEntry
{
   bool modified = false;
   std::vector<std::string> recipes;
   std::vector<IndexBinding> bindings;
};

namespace {
// Written in Init, read without a lock after
Options options;
// Decided by Register
bool needed = false;
std::atomic<bool> loaded = true;

// Written on the presenting thread, read by the dev tools on any
std::atomic<bool> running = false;
// Presenting thread
bool patch_all = false;

// Lock order: never held with another lock
std::mutex pending_mutex;
std::vector<const Database::Shader*> pending;

// What each shader's patch came to. Lock order: never held with another lock.
std::mutex patches_mutex;
std::map<uint32_t, PatchInfo> patches;
// Patched in their creation call (Options::patch_sync): a job doesn't touch them (1.0: the game's bytecode patch wins)
std::unordered_set<uint32_t> sync_patched;
std::atomic<uint64_t> generation = 0;

std::string GetDumpName(const Database::Shader& shader)
{
   return std::format("0x{:08X}.patched{}{}.cso", shader.hash, shader.target.empty() ? "" : ".", shader.target);
}

#if DEVELOPMENT || TEST
// 1.0's ALLOW_SHADER_PATCHES_DUMPING: the patched shader next to the game's dumps
void Dump(const Database::Shader& shader, std::span<const uint8_t> bytecode)
{
   if (!Settings::Get(Settings::Core::DUMP_SHADERS))
      return;
   const std::filesystem::path directory = System::Paths::GetDump();
   std::error_code error;
   std::filesystem::create_directories(directory, error);
   std::ofstream stream(directory / GetDumpName(shader), std::ios::binary | std::ios::trunc);
   stream.write(reinterpret_cast<const char*>(bytecode.data()), std::streamsize(bytecode.size()));
}
#endif

// The game's bytecode patch on `shader`: its container with the new instructions, empty if the game left it as it is
std::vector<uint8_t> RunCallback(BytecodePatcher patcher, const Database::Shader& shader)
{
   const std::span<const uint8_t> code = Container::GetProgramCode(shader.bytecode);
   if (code.empty())
      return {};
   const std::optional<std::vector<uint8_t>> patched = patcher(shader.stage, shader.hash, code, shader.bytecode);
   if (!patched)
      return {};
   std::vector<uint8_t> container = Container::ReplaceProgramCode(shader.bytecode, *patched);
   if (container.empty())
      Utils::Log::Error("Shaders: the game's patch of 0x{:08X} isn't a whole number of tokens, it's ignored", shader.hash);
   return container;
}

Variants::Variant MakeVariant(uint32_t hash, std::string file, std::shared_ptr<const CompiledShader> compiled)
{
   return {.hash = hash, .source = Variants::Source::Patch, .priority = Variants::PATCH_PRIORITY, .file = std::move(file), .compiled = std::move(compiled)};
}

#if LUMA_USE_DXP
// One recipe file, read at boot. Its strings are what its settings point at: a deque, they never move.
struct Recipe
{
   struct Env
   {
      std::string name;
      std::string label;
      std::string tooltip;
      std::string key;
      Settings::Visibility visibility = Settings::Visibility::Development;
      dxp::PrimitiveValue default_value = false;
   };

   std::string identity;
   std::filesystem::path path;
   std::optional<ShaderStage> stage;
   std::string stage_name;
   std::string name;
   std::string description;
   std::string key;
   bool registered = false;
   Settings::Visibility visibility = Settings::Visibility::Development;
   bool default_enabled = true;
   std::vector<Env> env;
   // Replaced by a reload. Lock order: `recipes_mutex` guards these three.
   std::shared_ptr<const dxp::sm5::Recipe> parsed;
   uint64_t content = 0; // FNV-1a of the file: a changed recipe misses the cache
   std::string error;
};

std::mutex recipes_mutex;
std::deque<Recipe> recipes;
std::deque<Settings::Setting> recipe_settings;

// The index of what recipe chains did to shaders, by patch key. Lock order: never held with another lock.
std::mutex index_mutex;
std::unordered_map<std::string, IndexEntry> index;
bool index_loaded = false;
bool index_dirty = false;

// The enabled recipes as a job sees them: copied on the presenting thread when it's queued
struct Link
{
   std::string identity;
   std::optional<ShaderStage> stage;
   std::shared_ptr<const dxp::sm5::Recipe> recipe;
   dxp::PatchOptions options;
   std::string key; // What the recipe and its environment are, for the cache key
};

using Chain = std::vector<Link>;

constexpr std::pair<std::string_view, ShaderStage> STAGE_FOLDERS[] = {{"VS", ShaderStage::Vertex}, {"HS", ShaderStage::Hull}, {"DS", ShaderStage::Domain},
   {"GS", ShaderStage::Geometry}, {"PS", ShaderStage::Pixel}, {"CS", ShaderStage::Compute}};

std::optional<std::string> ReadFile(const std::filesystem::path& path)
{
   std::ifstream stream(path, std::ios::binary);
   if (!stream)
      return std::nullopt;
   return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

Settings::Visibility ParseVisibility(const dxp::MetaValue* value, Settings::Visibility fallback)
{
   const std::string* text = value ? value->AsString() : nullptr;
   if (!text)
      return fallback;
   if (*text == "release")
      return Settings::Visibility::Release;
   if (*text == "test")
      return Settings::Visibility::Test;
   if (*text == "development")
      return Settings::Visibility::Development;
   Utils::Log::Warning("Shaders: unknown recipe visibility \"{}\" (release, test, development)", *text);
   return fallback;
}

std::string ToText(const dxp::PrimitiveValue& value)
{
   return std::visit(
      [](const auto& typed) -> std::string {
         if constexpr (std::is_same_v<std::decay_t<decltype(typed)>, bool>)
            return typed ? "true" : "false";
         else
            return std::format("{}", typed);
      },
      value);
}

Settings::Value ToSetting(const dxp::PrimitiveValue& value)
{
   return std::visit(
      [](const auto& typed) -> Settings::Value {
         using T = std::decay_t<decltype(typed)>;
         if constexpr (std::is_same_v<T, bool>)
            return typed;
         else if constexpr (std::is_same_v<T, double>)
            return float(typed);
         else
            return int32_t(typed);
      },
      value);
}

// The setting's value with the type of the recipe's default (DXP compares immediates by type)
dxp::PrimitiveValue FromSetting(const Settings::Value& value, const dxp::PrimitiveValue& type_of)
{
   double number = 0.0;
   if (const bool* typed = std::get_if<bool>(&value))
      number = *typed ? 1.0 : 0.0;
   else if (const int32_t* typed_int = std::get_if<int32_t>(&value))
      number = double(*typed_int);
   else if (const float* typed_float = std::get_if<float>(&value))
      number = double(*typed_float);
   return std::visit([number](const auto& typed) -> dxp::PrimitiveValue { return std::decay_t<decltype(typed)>(number); }, type_of);
}

// Parses the file into `recipe`. Presenting thread or Register.
void Parse(Recipe& recipe)
{
   const std::optional<std::string> text = ReadFile(recipe.path);
   std::scoped_lock lock(recipes_mutex);
   if (!text)
   {
      recipe.error = "The file is gone";
      recipe.parsed.reset();
      return;
   }
   auto parsed = dxp::sm5::Recipe::ParseFromText(*text, recipe.identity);
   if (!parsed)
   {
      // Its shaders go back to the game's (1.0 skipped a recipe that didn't parse) until it's fixed and reloaded
      recipe.error = parsed.error();
      recipe.parsed.reset();
      Utils::Log::Error("Shaders: recipe {} doesn't parse: {}", recipe.identity, recipe.error);
      return;
   }
   recipe.error.clear();
   recipe.content = Utils::Hash::Name(*text);
   recipe.parsed = std::make_shared<const dxp::sm5::Recipe>(std::move(*parsed));
}

// The recipe files under the Recipes folder: its root for any stage, the stage folders for one. In each, in name order.
void Scan(std::deque<Recipe>& found)
{
   const std::filesystem::path root = System::Paths::GetRecipes();
   const auto scan_folder = [&](const std::filesystem::path& folder, std::optional<ShaderStage> stage, std::string_view stage_name) {
      std::error_code error;
      std::vector<std::filesystem::path> files;
      for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
      {
         const std::wstring name = it->path().filename().wstring();
         if (it->is_regular_file(error) && name.ends_with(L".recipe.yml"))
            files.push_back(it->path());
      }
      std::ranges::sort(files, {}, [](const std::filesystem::path& path) { return path.filename().wstring(); });
      for (const std::filesystem::path& path : files)
      {
         Recipe& recipe = found.emplace_back();
         recipe.identity = Utils::Strings::ToUtf8(std::filesystem::path(std::filesystem::relative(path, root, error).generic_wstring()));
         recipe.path = path;
         recipe.stage = stage;
         recipe.stage_name = stage_name;
      }
   };
   scan_folder(root, std::nullopt, {});
   for (const auto& [name, stage] : STAGE_FOLDERS)
      scan_folder(root / name, stage, name);
   std::error_code error;
   for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error))
   {
      const std::string name = Utils::Strings::ToUtf8(it->path().filename());
      if (it->is_directory(error) && !std::ranges::contains(STAGE_FOLDERS, name, &std::pair<std::string_view, ShaderStage>::first))
         Utils::Log::Warning("Shaders: recipe folder {} isn't a stage (VS, HS, DS, GS, PS, CS), it's ignored", name);
   }
}

// The recipe's own description (its `meta:`), which DXP reports without reading: name, description, default_enabled, visibility,
// env: {VAR: {label, tooltip, visibility}}
void ReadMeta(Recipe& recipe)
{
   const std::optional<dxp::MetaValue>& meta = recipe.parsed ? recipe.parsed->GetMeta() : std::optional<dxp::MetaValue>();
   recipe.name = recipe.identity;
   if (!meta)
      return;
   if (const dxp::MetaValue* name = meta->Find("name"); name && name->AsString())
   {
      recipe.name = *name->AsString();
      recipe.registered = true;
      recipe.visibility = Settings::Visibility::Release;
   }
   if (const dxp::MetaValue* description = meta->Find("description"); description && description->AsString())
      recipe.description = *description->AsString();
   if (const dxp::MetaValue* enabled = meta->Find("default_enabled"); enabled && enabled->AsBool())
      recipe.default_enabled = *enabled->AsBool();
   recipe.visibility = ParseVisibility(meta->Find("visibility"), recipe.visibility);
}

void RegisterSettings(Recipe& recipe)
{
   recipe.key = "Recipes." + recipe.identity;
   recipe_settings.push_back({.key = recipe.key,
      .label = recipe.name,
      .tooltip = recipe.description,
      .category = "Recipes",
      .visibility = recipe.visibility,
      .default_value = recipe.default_enabled,
      .source = Settings::Sources::Recipe{recipe.identity, {}}});
   Settings::Register(std::span(&recipe_settings.back(), 1), Settings::Kind::Recipe);
   if (!recipe.parsed)
      return;
   const std::optional<dxp::MetaValue>& meta = recipe.parsed->GetMeta();
   const dxp::MetaValue* env_meta = meta ? meta->Find("env") : nullptr;
   std::vector<std::pair<std::string, dxp::PrimitiveValue>> env(recipe.parsed->GetEnv().begin(), recipe.parsed->GetEnv().end());
   std::ranges::sort(env, {}, &std::pair<std::string, dxp::PrimitiveValue>::first);
   for (auto& [name, value] : env)
   {
      Recipe::Env& variable = recipe.env.emplace_back();
      variable.name = name;
      variable.label = name;
      variable.default_value = value;
      variable.key = std::format("Recipes.{}.{}", recipe.identity, name);
      // An environment value the recipe doesn't describe is a developer's knob
      if (const dxp::MetaValue* described = env_meta ? env_meta->Find(name) : nullptr)
      {
         if (const dxp::MetaValue* label = described->Find("label"); label && label->AsString())
            variable.label = *label->AsString();
         if (const dxp::MetaValue* tooltip = described->Find("tooltip"); tooltip && tooltip->AsString())
            variable.tooltip = *tooltip->AsString();
         variable.visibility = ParseVisibility(described->Find("visibility"), recipe.visibility);
      }
   }
   for (const Recipe::Env& variable : recipe.env)
   {
      recipe_settings.push_back({.key = variable.key,
         .label = variable.label,
         .tooltip = variable.tooltip,
         .category = "Recipes",
         .visibility = variable.visibility,
         .default_value = ToSetting(variable.default_value),
         .source = Settings::Sources::Recipe{recipe.identity, variable.name}});
      Settings::Register(std::span(&recipe_settings.back(), 1), Settings::Kind::Recipe);
   }
}

// Presenting thread: the enabled recipes and their environment as set now
Chain MakeChain()
{
   Chain chain;
   std::scoped_lock lock(recipes_mutex);
   for (const Recipe& recipe : recipes)
   {
      if (!recipe.parsed || !Settings::Get<bool>(recipe.key))
         continue;
      Link& link = chain.emplace_back();
      link.identity = recipe.identity;
      link.stage = recipe.stage;
      link.recipe = recipe.parsed;
      link.key = std::format("{}#{:016X}", recipe.identity, recipe.content);
      for (const Recipe::Env& variable : recipe.env)
      {
         const dxp::PrimitiveValue value = FromSetting(Settings::GetValue(variable.key).value_or(ToSetting(variable.default_value)), variable.default_value);
         link.options.SetEnv(variable.name, value);
         link.key += std::format(";{}={}", variable.name, ToText(value));
      }
   }
   return chain;
}

std::string_view GetKindName(dxp::BindingClass kind)
{
   switch (kind)
   {
   case dxp::BindingClass::Texture: return "texture";
   case dxp::BindingClass::TextureUav: return "texture uav";
   case dxp::BindingClass::RawResource: return "raw buffer";
   case dxp::BindingClass::StructuredResource: return "structured buffer";
   case dxp::BindingClass::CBuffer: return "cbuffer";
   case dxp::BindingClass::Sampler: return "sampler";
   case dxp::BindingClass::Uav: return "uav";
   default: return "other";
   }
}

void LoadIndex()
{
   std::scoped_lock lock(index_mutex);
   if (index_loaded)
      return;
   index_loaded = true;
   if (const std::optional<std::string> text = Cache::ReadPatchIndex(); text && !Utils::Json::Read(*text, index))
   {
      Utils::Log::Warning("Shaders: the patch index in the cache doesn't read, patching again");
      index.clear();
   }
}

void SaveIndex()
{
   std::string text;
   {
      std::scoped_lock lock(index_mutex);
      if (!index_dirty)
         return;
      index_dirty = false;
      text = Utils::Json::Write(index);
   }
   Cache::WritePatchIndex(text);
}

// What the recipe chain makes of one shader: from the index and the cache, or by running it (and writing both)
struct ChainResult
{
   std::vector<uint8_t> bytecode; // Empty when no recipe changed it
   IndexEntry entry;
   bool ran = false; // A recipe applies to the shader's stage
   bool cached = false;
   std::string error;
   std::string report;
};

ChainResult RunChain(const Database::Shader& shader, const Chain& chain)
{
   ChainResult result;
   std::string chain_key = std::format("dxp {}", DXP_VERSION_STRING);
   std::vector<const Link*> links;
   for (const Link& link : chain)
   {
      if (link.stage && *link.stage != shader.stage)
         continue;
      links.push_back(&link);
      chain_key += "|" + link.key;
   }
   if (links.empty())
      return result;
   result.ran = true;
   const uint64_t key = Cache::GetPatchKey(shader.hash, chain_key);
   const std::string key_text = std::format("{:016X}", key);
   {
      std::scoped_lock lock(index_mutex);
      if (const auto it = index.find(key_text); it != index.end())
      {
         result.entry = it->second;
         result.cached = true;
      }
   }
   if (result.cached)
   {
      if (!result.entry.modified)
         return result;
      if (std::optional<Bytecode> bytecode = Cache::ReadShader(key))
      {
         result.bytecode = std::move(*bytecode);
         return result;
      }
      // The index outlived its bytecode (a cleared folder): patched again
      result.cached = false;
      result.entry = {};
   }

   // Each enabled recipe patches the previous one's output (1.0)
   std::vector<uint8_t> code;
   std::map<std::string, dxp::ResourceBinding> bindings;
   for (const Link* link : links)
   {
      dxp::PatchOptions link_options = link->options;
      link_options.logger = [identity = link->identity, hash = shader.hash](dxp::LogLevel level, const std::string& message) {
         if (level == dxp::LogLevel::Error)
            Utils::Log::Error("Shaders: recipe {} on 0x{:08X}: {}", identity, hash, message);
         else if (level == dxp::LogLevel::Warning)
            Utils::Log::Warning("Shaders: recipe {} on 0x{:08X}: {}", identity, hash, message);
      };
      const std::span<const uint8_t> input = code.empty() ? std::span<const uint8_t>(shader.bytecode) : std::span<const uint8_t>(code);
      std::expected<dxp::RecipeReport, std::string> report;
      try
      {
         report = link->recipe->Execute(input, link_options);
      }
      catch (const std::exception& exception)
      {
         report = std::unexpected(std::string(exception.what()));
      }
      if (!report)
      {
         // Not cached: a failure may be the recipe's, fixed by editing it, which changes its key anyway
         result.error += std::format("{}: {}\n", link->identity, report.error());
         continue;
      }
#if DEVELOPMENT
      for (const dxp::StepReport& step : report->steps)
      {
         if (const dxp::ApplyRuleResults* rule = std::get_if<dxp::ApplyRuleResults>(&step.results))
            result.report += std::format("{} {}: {} matched, {} applied\n", link->identity, step.name, rule->match_count, rule->applied_count);
         else
            result.report += std::format("{} {}: {}\n", link->identity, step.name, step.success ? "done" : "skipped");
      }
#endif
      if (!report->modified || report->output_bytes.empty())
         continue;
      code = std::move(report->output_bytes);
      result.entry.recipes.push_back(link->identity);
      for (auto& [handle, binding] : report->new_bindings)
         bindings.insert_or_assign(handle, binding);
   }
   if (!result.error.empty())
      return result;
   result.entry.modified = !code.empty();
   for (const auto& [handle, binding] : bindings)
      result.entry.bindings.push_back({handle, std::string(GetKindName(binding.binding_class)), binding.register_index, binding.space});
   if (result.entry.modified)
   {
      Cache::WriteShader(key, code);
      result.bytecode = std::move(code);
   }
   std::scoped_lock lock(index_mutex);
   index.insert_or_assign(key_text, result.entry);
   index_dirty = true;
   return result;
}
#endif

// A job's answer for one shader
struct Outcome
{
   PatchInfo info;
   bool considered = false; // A callback or a recipe applied: it has an entry in the dev tools
   std::shared_ptr<const CompiledShader> compiled; // Null when it isn't patched (or can't be used)
};

struct Job
{
   std::vector<const Database::Shader*> shaders;
   bool all = false;
#if LUMA_USE_DXP
   Chain chain;
#endif
   bool d3d12 = false;
   std::vector<Outcome> outcomes;
};

// The requests of a patch's bindings, by Resources::Database name. False, with `info.error`, when one can't be had.
bool ResolveBindings(PatchInfo& info, std::vector<Request>& requests, bool d3d12)
{
   bool usable = true;
   for (BindingInfo& binding : info.bindings)
   {
      const std::optional<uint32_t> entry = Resources::Database::Find(Utils::Hash::Name(binding.handle));
      const Resources::Database::Kind expected = binding.kind == "cbuffer" ? Resources::Database::Kind::ConstantBuffer : Resources::Database::Kind::Texture;
      binding.resolved = entry && (binding.kind == "cbuffer" || binding.kind == "texture") && Resources::Database::Get(*entry).kind == expected;
      if (!binding.resolved)
      {
         info.error += std::format("No {} named \"{}\" in Luma's resources (register {})\n", binding.kind, binding.handle, binding.slot);
         usable = false;
         continue;
      }
      requests.push_back({*entry, binding.slot});
   }
   // DXP puts what it adds in space 0 of SM5.0, which the game's root signature doesn't describe and Luma's space can't be named in
   if (usable && d3d12 && !requests.empty())
   {
      LUMA_UNIMPLEMENTED("Shaders::Patching bindings on D3D12 (DXP's upgrade to SM5.1)");
      info.error += "Adds bindings, which D3D12 can't take until DXP upgrades patches to SM5.1\n";
      usable = false;
   }
   return usable;
}

// Worker thread
Outcome Patch(const Database::Shader& shader, const Job& job)
{
   Outcome outcome;
   PatchInfo& info = outcome.info;
   info.hash = shader.hash;
   info.stage = shader.stage;
   if (shader.bytecode.empty())
      return outcome;
   // The game's patches win over recipes (1.0). The sync one runs here too when everything is patched again (a load).
   std::vector<uint8_t> bytecode;
   for (const BytecodePatcher patcher : {options.patch_sync, options.patch_async})
   {
      if (patcher && bytecode.empty())
         bytecode = RunCallback(patcher, shader);
   }
   info.callback = !bytecode.empty();
   outcome.considered = info.callback;
#if LUMA_USE_DXP
   if (bytecode.empty() && !job.chain.empty())
   {
      if (const std::optional<Container::Program> program = Container::GetProgram(shader.bytecode); program && program->dxil)
      {
         LUMA_UNIMPLEMENTED("Shaders::Patching of DXIL shaders (DXP's SM6 recipes)");
         return outcome;
      }
      ChainResult result = RunChain(shader, job.chain);
      outcome.considered = result.ran;
      info.cached = result.cached;
      info.recipes = std::move(result.entry.recipes);
      info.error = std::move(result.error);
      info.report = std::move(result.report);
      for (IndexBinding& binding : result.entry.bindings)
         info.bindings.push_back({std::move(binding.handle), std::move(binding.kind), binding.slot, binding.space});
      bytecode = std::move(result.bytecode);
   }
#endif
   if (bytecode.empty())
      return outcome;
   std::vector<Request> requests;
   if (!info.error.empty() || !ResolveBindings(info, requests, job.d3d12))
      return outcome;
#if DEVELOPMENT || TEST
   if (!info.cached)
      Dump(shader, bytecode);
#endif
   auto compiled = std::make_shared<CompiledShader>();
   compiled->bytecode = std::move(bytecode);
   compiled->requests = std::move(requests);
   outcome.compiled = std::move(compiled);
   info.patched = true;
   return outcome;
}

// Presenting thread
System::Jobs::Outcome Publish(Job& job)
{
   std::vector<Variants::Variant> changed;
   std::vector<uint32_t> removed;
   uint32_t patched = 0;
   uint32_t cached = 0;
   uint32_t failed = 0;
   {
      std::scoped_lock lock(patches_mutex);
      for (Outcome& outcome : job.outcomes)
      {
         const uint32_t hash = outcome.info.hash;
         if (sync_patched.contains(hash))
            continue;
         const bool considered = outcome.considered;
         if (outcome.compiled && loaded)
         {
            std::string file = outcome.info.callback ? "the game's bytecode patch" : "";
            for (const std::string& recipe : outcome.info.recipes)
               file += (file.empty() ? "" : ", ") + recipe;
            changed.push_back(MakeVariant(hash, std::move(file), std::move(outcome.compiled)));
            patched++;
            cached += outcome.info.cached;
         }
         else
         {
            removed.push_back(hash);
            failed += !outcome.info.error.empty();
         }
         if (considered)
            patches.insert_or_assign(hash, std::move(outcome.info));
         else
            patches.erase(hash);
      }
   }
   Variants::Update(Variants::Source::Patch, std::move(changed), removed);
   generation.fetch_add(1, std::memory_order_release);
   running = false;
   if (failed)
      return {true, std::format("{} shader patch{} failed, see the Patches tab", failed, failed == 1 ? "" : "es")};
   // A job for shaders the game just created has nothing to say: only a job patching everything again reports
   if (!job.all)
      return {};
   return {false, std::format("Patches ready: {} shader{} patched, {} from the cache", patched, patched == 1 ? "" : "s", cached)};
}

void OnSettingChanged(const Settings::Setting& setting, const Settings::Value&)
{
   if (std::holds_alternative<Settings::Sources::Recipe>(setting.source))
      patch_all = true;
}
} // namespace

void SetOptions(const Options& set)
{
   options = set;
}

const Options& GetOptions()
{
   return options;
}

void Register()
{
#if LUMA_USE_DXP
   Scan(recipes);
   for (Recipe& recipe : recipes)
   {
      Parse(recipe);
      ReadMeta(recipe);
      RegisterSettings(recipe);
   }
   if (!recipes.empty())
      Utils::Log::Info("Shaders: {} recipe{} in {}", recipes.size(), recipes.size() == 1 ? "" : "s", Utils::Strings::ToUtf8(System::Paths::GetRecipes()));
   needed = !recipes.empty();
#else
   std::error_code error;
   if (std::filesystem::exists(System::Paths::GetRecipes(), error) && !std::filesystem::is_empty(System::Paths::GetRecipes(), error))
      Utils::Log::Warning("Shaders: this build has no DXP (UseLumaDXP), the recipes are ignored");
#endif
   needed |= options.patch_sync != nullptr || options.patch_async != nullptr;
   if (needed)
      Settings::AddChangeListener(&OnSettingChanged);
}

bool IsNeeded()
{
   return needed;
}

void TrackShader(const Database::Shader& shader)
{
   if (!needed || shader.bytecode.empty())
      return;
   if (options.patch_sync && loaded)
   {
      std::vector<uint8_t> bytecode = RunCallback(options.patch_sync, shader);
      if (!bytecode.empty())
      {
#if DEVELOPMENT || TEST
         Dump(shader, bytecode);
#endif
         auto compiled = std::make_shared<CompiledShader>();
         compiled->bytecode = std::move(bytecode);
         {
            std::scoped_lock lock(patches_mutex);
            sync_patched.insert(shader.hash);
            patches.insert_or_assign(shader.hash, PatchInfo{.hash = shader.hash, .stage = shader.stage, .patched = true, .callback = true});
         }
         // Now, in the creation call: the variant is there when the object attaches to its hash's variants
         Variants::Update(Variants::Source::Patch, {MakeVariant(shader.hash, "the game's bytecode patch", std::move(compiled))}, {});
         return;
      }
   }
   std::scoped_lock lock(pending_mutex);
   pending.push_back(&shader);
}

void OnPresent()
{
   if (!needed || running || !loaded)
      return;
   auto job = std::make_shared<Job>();
   job->all = patch_all;
   if (patch_all)
   {
      patch_all = false;
      std::scoped_lock lock(pending_mutex);
      pending.clear();
      for (const Database::Shader* shader : Database::GetShaders())
         job->shaders.push_back(shader);
   }
   else
   {
      std::scoped_lock lock(pending_mutex);
      job->shaders.swap(pending);
   }
   if (job->shaders.empty())
      return;
#if LUMA_USE_DXP
   job->chain = MakeChain();
#endif
   job->d3d12 = GetApi() == Api::D3D12;
   running = true;
   System::Jobs::Queue(
      "Patching shaders",
      [job] {
#if LUMA_USE_DXP
         LoadIndex();
#endif
         const uint32_t total = uint32_t(job->shaders.size());
         System::Jobs::SetProgress(0, total);
         job->outcomes.reserve(total);
         for (const Database::Shader* shader : job->shaders)
         {
            job->outcomes.push_back(Patch(*shader, *job));
            System::Jobs::SetProgress(uint32_t(job->outcomes.size()), total);
         }
#if LUMA_USE_DXP
         SaveIndex();
#endif
      },
      [job] { return Publish(*job); });
}

void Reload()
{
#if LUMA_USE_DXP
   std::vector<std::filesystem::path> known;
   for (Recipe& recipe : recipes)
   {
      Parse(recipe);
      known.push_back(recipe.path);
   }
   // A recipe added since boot has no settings yet: found by a scan, reported, not loaded
   std::deque<Recipe> scanned;
   Scan(scanned);
   for (const Recipe& recipe : scanned)
   {
      if (!std::ranges::contains(known, recipe.path))
         Utils::Log::Warning("Shaders: recipe {} is new, restart the game to load it", recipe.identity);
   }
#endif
   patch_all = true;
   Utils::Log::Info("Shaders: reloading the patches");
}

void SetLoaded(bool value)
{
   if (loaded.exchange(value) == value)
      return;
   if (value)
   {
      patch_all = true;
      Utils::Log::Info("Shaders: patches loaded");
      return;
   }
   std::vector<uint32_t> removed;
   {
      std::scoped_lock lock(patches_mutex);
      for (auto& [hash, info] : patches)
      {
         removed.push_back(hash);
         info.patched = false;
      }
      sync_patched.clear();
   }
   Variants::Update(Variants::Source::Patch, {}, removed);
   generation.fetch_add(1, std::memory_order_release);
   Utils::Log::Info("Shaders: patches unloaded");
}

bool IsLoaded()
{
   return loaded.load(std::memory_order_acquire);
}

Status GetStatus()
{
   Status status = {.needed = needed, .loaded = loaded, .running = running, .generation = generation.load(std::memory_order_acquire)};
#if LUMA_USE_DXP
   {
      std::scoped_lock lock(recipes_mutex);
      status.recipes = uint32_t(recipes.size());
      status.broken_recipes = uint32_t(std::ranges::count_if(recipes, [](const Recipe& recipe) { return !recipe.error.empty(); }));
   }
#endif
   {
      std::scoped_lock lock(pending_mutex);
      status.pending = uint32_t(pending.size());
   }
   std::scoped_lock lock(patches_mutex);
   status.shaders = uint32_t(patches.size());
   for (const auto& [hash, info] : patches)
   {
      if (info.patched)
         status.patched++;
      else if (!info.error.empty())
         status.failed++;
      else
         status.unmatched++;
   }
   return status;
}

std::vector<RecipeInfo> GetRecipes()
{
   std::vector<RecipeInfo> list;
#if LUMA_USE_DXP
   {
      std::scoped_lock lock(recipes_mutex);
      for (const Recipe& recipe : recipes)
      {
         RecipeInfo& info = list.emplace_back();
         info.identity = recipe.identity;
         info.name = recipe.name;
         info.description = recipe.description;
         info.stage = recipe.stage_name;
         info.key = recipe.key;
         info.enabled = Settings::Get<bool>(recipe.key);
         info.registered = recipe.registered;
         info.error = recipe.error;
         for (const Recipe::Env& variable : recipe.env)
         {
            const std::optional<Settings::Value> value = Settings::GetValue(variable.key);
            info.env.push_back({variable.name, variable.label, variable.key, ToText(variable.default_value),
               value ? ToText(FromSetting(*value, variable.default_value)) : ToText(variable.default_value)});
         }
      }
   }
   std::scoped_lock lock(patches_mutex);
   for (const auto& [hash, patch] : patches)
   {
      for (const std::string& identity : patch.recipes)
      {
         if (const auto it = std::ranges::find(list, identity, &RecipeInfo::identity); it != list.end() && patch.patched)
            it->matched++;
      }
   }
#endif
   return list;
}

std::vector<PatchInfo> GetPatches()
{
   std::scoped_lock lock(patches_mutex);
   std::vector<PatchInfo> list;
   list.reserve(patches.size());
   for (const auto& [hash, info] : patches)
      list.push_back(info);
   return list;
}

std::optional<PatchInfo> GetPatch(uint32_t hash)
{
   std::scoped_lock lock(patches_mutex);
   const auto it = patches.find(hash);
   return it != patches.end() ? std::optional(it->second) : std::nullopt;
}
} // namespace Patching
} // namespace Shaders
} // namespace Luma
