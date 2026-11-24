#include "Trace/Mcp/McpTools.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Settings/SettingsRegistry.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderPatching.hpp"
#include "Utils/Json.hpp"

namespace Luma {
namespace Trace {
namespace Mcp {
// In a named namespace: glaze can't reflect types with internal linkage
namespace Replies {
struct PatchEnv
{
   std::string name;
   std::string label;
   std::string key; // Its setting (luma_set_setting takes it too)
   std::string default_value;
   std::string value;
};

struct PatchRecipe
{
   std::string identity;
   std::string name;
   std::optional<std::string> description;
   std::optional<std::string> stage; // Its stage folder, none for any stage
   std::string key;
   bool enabled = false;
   bool registered = false; // It describes itself (`meta:`): shown to users. Otherwise Development builds only.
   uint32_t matched = 0;
   std::optional<std::string> error;
   std::vector<PatchEnv> env;
};

struct PatchBinding
{
   std::string handle;
   std::string kind;
   uint32_t slot = 0;
   uint32_t space = 0;
   bool resolved = false;
};

struct PatchShader
{
   std::string hash;
   std::optional<std::string> name;
   std::string stage;
   bool patched = false; // Its variant is published
   std::string method; // "recipe" or "callback" (the game's bytecode patch)
   bool cached = false;
   std::vector<std::string> recipes;
   std::vector<PatchBinding> bindings;
   std::optional<std::string> error;
   std::optional<std::string> report;
};

struct PatchState
{
   bool ok = true;
   bool needed = false;
   bool loaded = true;
   bool running = false;
   uint32_t patched = 0;
   uint32_t unmatched = 0;
   uint32_t failed = 0;
   uint32_t pending = 0;
   uint32_t broken_recipes = 0;
   std::vector<PatchRecipe> recipes;
   std::vector<PatchShader> shaders;
   std::vector<std::string> ignored_args;
};

struct ReloadPatches
{
   bool ok = true;
   bool loaded = true;
   uint32_t patched = 0;
   uint32_t failed = 0;
   std::vector<std::string> ignored_args;
};
} // namespace Replies

namespace {
Replies::PatchShader ToReply(const Shaders::Patching::PatchInfo& info, bool detail)
{
   Replies::PatchShader shader;
   shader.hash = FormatHash(info.hash);
   if (const std::string_view name = Shaders::Database::GetName(info.hash); !name.empty())
      shader.name = std::string(name);
   shader.stage = GetStageName(info.stage);
   shader.patched = info.patched;
   shader.method = info.callback ? "callback" : "recipe";
   shader.cached = info.cached;
   shader.recipes = info.recipes;
   for (const Shaders::Patching::BindingInfo& binding : info.bindings)
      shader.bindings.push_back({binding.handle, binding.kind, binding.slot, binding.space, binding.resolved});
   if (!info.error.empty())
      shader.error = info.error;
   if (detail && !info.report.empty())
      shader.report = info.report;
   return shader;
}

// Waits on the pipe thread for the patch job a change starts (the next present queues it)
std::optional<std::string> WaitForPatches(uint64_t previous_generation, uint32_t timeout)
{
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
   while (true)
   {
      const Shaders::Patching::Status status = Shaders::Patching::GetStatus();
      if (status.generation != previous_generation && !status.running)
         return std::nullopt;
      if (std::chrono::steady_clock::now() > deadline)
         return Error(std::format("the shaders weren't patched in {} ms (see luma_status)", timeout));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
   }
}
} // namespace

namespace Tools {
// The recipes, and the shaders a patch changed or failed on (`include_unmatched` adds those a recipe ran on and left as they were)
std::string PatchState(const Request& request)
{
   Replies::PatchState reply;
   reply.ignored_args = GetIgnoredArgs(request, {"hash", "include_unmatched"});
   const Shaders::Patching::Status status = Shaders::Patching::GetStatus();
   reply.needed = status.needed;
   reply.loaded = status.loaded;
   reply.running = status.running;
   reply.patched = status.patched;
   reply.unmatched = status.unmatched;
   reply.failed = status.failed;
   reply.pending = status.pending;
   reply.broken_recipes = status.broken_recipes;
   for (Shaders::Patching::RecipeInfo& recipe : Shaders::Patching::GetRecipes())
   {
      Replies::PatchRecipe& info = reply.recipes.emplace_back();
      info.identity = std::move(recipe.identity);
      info.name = std::move(recipe.name);
      if (!recipe.description.empty())
         info.description = std::move(recipe.description);
      if (!recipe.stage.empty())
         info.stage = std::move(recipe.stage);
      info.key = std::move(recipe.key);
      info.enabled = recipe.enabled;
      info.registered = recipe.registered;
      info.matched = recipe.matched;
      if (!recipe.error.empty())
         info.error = std::move(recipe.error);
      for (Shaders::Patching::EnvValue& env : recipe.env)
         info.env.push_back({std::move(env.name), std::move(env.label), std::move(env.key), std::move(env.default_value), std::move(env.value)});
   }
   if (request.args.contains("hash"))
   {
      const std::optional<uint32_t> hash = GetHash(request);
      if (!hash)
         return Error("'hash' is a shader hash, like 0x12345678");
      const std::optional<Shaders::Patching::PatchInfo> info = Shaders::Patching::FindPatch(*hash);
      if (!info)
         return Error(std::format("no patch was tried on {} (no recipe for its stage, or the game didn't create it)", FormatHash(*hash)));
      reply.shaders.push_back(ToReply(*info, true));
      return Utils::Json::Write(reply);
   }
   const bool include_unmatched = GetBool(request, "include_unmatched");
   for (const Shaders::Patching::PatchInfo& info : Shaders::Patching::GetPatches())
   {
      if (info.patched || !info.error.empty() || include_unmatched)
         reply.shaders.push_back(ToReply(info, false));
   }
   return Utils::Json::Write(reply);
}

// The set_recipe tool: a recipe's switch, or one of its env values set or reset, through their settings (saved like the overlay's)
std::string SetRecipe(const Request& request)
{
   const auto identity = request.args.find("identity");
   if (identity == request.args.end())
      return Error("set_recipe takes the 'identity' of a recipe (see luma_patch_state)");
   const uint64_t previous_generation = Shaders::Patching::GetStatus().generation;
   const uint32_t timeout = GetTimeout(request);
   constexpr std::string_view UNCHANGED = "unchanged";
   const std::string answer = RunOnRenderThread(
      [request, identity = identity->second, UNCHANGED]() -> std::string {
         const std::vector<Shaders::Patching::RecipeInfo> recipes = Shaders::Patching::GetRecipes();
         const auto recipe = std::ranges::find(recipes, identity, &Shaders::Patching::RecipeInfo::identity);
         if (recipe == recipes.end())
            return Error(std::format("no recipe '{}' (see luma_patch_state)", identity));
         bool changed = false;
         if (const auto enabled = request.args.find("enabled"); enabled != request.args.end())
         {
            const bool value = enabled->second == "1" || enabled->second == "true";
            changed |= Settings::Get<bool>(recipe->key) != value;
            Settings::Set(recipe->key, value);
         }
         if (const auto env = request.args.find("env"); env != request.args.end())
         {
            const auto variable = std::ranges::find(recipe->env, env->second, &Shaders::Patching::EnvValue::name);
            if (variable == recipe->env.end())
               return Error(std::format("recipe '{}' has no env '{}'", identity, env->second));
            const Settings::Setting* setting = Settings::Find(variable->key);
            if (!setting)
               return Error(std::format("recipe '{}' has no env '{}'", identity, env->second));
            const std::optional<Settings::Value> before = Settings::GetValue(variable->key);
            if (GetBool(request, "reset"))
               Settings::Reset(variable->key);
            else if (const auto text = request.args.find("value"); text != request.args.end())
            {
               const std::optional<Settings::Value> value = Settings::Parse(*setting, text->second);
               if (!value)
                  return Error(std::format("'{}' isn't a {} value", text->second, Settings::GetTypeName(setting->default_value)));
               Settings::Set(variable->key, *value);
            }
            else
               return Error("'env' takes a 'value', or 'reset'");
            changed |= Settings::GetValue(variable->key) != before;
         }
         return changed ? std::string() : std::string(UNCHANGED);
      },
      timeout);
   if (!answer.empty() && answer != UNCHANGED)
      return answer;
   if (answer.empty())
   {
      if (std::optional<std::string> error = WaitForPatches(previous_generation, timeout))
         return *error;
   }
   return PatchState(Request{.tool = "patch_state"});
}

std::string ReloadPatches(const Request& request)
{
   const bool unload = GetBool(request, "unload");
   const uint32_t timeout = GetTimeout(request);
   const uint64_t previous_generation = Shaders::Patching::GetStatus().generation;
   const std::string requested = RunOnRenderThread(
      [unload] {
         if (unload)
            Shaders::Patching::SetLoaded(false);
         else
         {
            Shaders::Patching::SetLoaded(true);
            Shaders::Patching::Reload();
         }
         return std::string();
      },
      timeout);
   if (!requested.empty())
      return requested;
   if (!unload)
   {
      if (std::optional<std::string> error = WaitForPatches(previous_generation, timeout))
         return *error;
   }
   const Shaders::Patching::Status status = Shaders::Patching::GetStatus();
   Replies::ReloadPatches reply;
   reply.loaded = status.loaded;
   reply.patched = status.patched;
   reply.failed = status.failed;
   reply.ignored_args = GetIgnoredArgs(request, {"unload"});
   return Utils::Json::Write(reply);
}
} // namespace Tools
} // namespace Mcp
} // namespace Trace
} // namespace Luma
#endif
