#include "hooks/hooks.hpp"

#include <mutex>
#include <string>
#include <vector>

#include <safetyhook.hpp>

#include "utils/log.hpp"
#include "utils/modules.hpp"
#include "utils/strings.hpp"

namespace Luma {
namespace Hooks {
namespace {
enum class Status : uint8_t
{
   Pending,
   Installed,
   Failed,
};

struct Entry
{
   Hook hook;
   Status status = Status::Pending;
   safetyhook::InlineHook inline_hook; // Empty when shared with another entry's hook
};

struct State
{
   std::mutex mutex;
   std::vector<Entry> entries;
};

// Leaked: unhooking from static destructors at process exit isn't safe
State& GetState()
{
   static State* state = new State();
   return *state;
}

std::string GetTargetName(const Hook& hook)
{
   if (const auto* target = std::get_if<ExportTarget>(&hook.target))
      return std::format("{}!{}", Utils::Strings::ToUtf8(target->module), target->name);
   return std::get<VtableTarget>(hook.target).name;
}

bool IsTargetAvailable(const Hook& hook)
{
   if (const auto* target = std::get_if<ExportTarget>(&hook.target))
      return Utils::Modules::FindSystemModule(target->module) != nullptr;
   return Vtables::Get(std::get<VtableTarget>(hook.target).vtable) != nullptr;
}

bool IsVersionSupported(const Hook& hook)
{
   const auto* target = std::get_if<VtableTarget>(&hook.target);
   return !target || target->required_version == GUID_NULL || Vtables::Supports(target->vtable, target->required_version);
}

// Takes the loader lock for exports, so never with the state locked
void* FindTarget(const Hook& hook)
{
   if (const auto* target = std::get_if<ExportTarget>(&hook.target))
      return Utils::Modules::FindExport(Utils::Modules::FindSystemModule(target->module), target->name);
   const auto& target = std::get<VtableTarget>(hook.target);
   return Vtables::GetMethod(target.vtable, target.slot);
}

struct Candidate
{
   size_t index;
   Hook hook;
   void* target = nullptr;
   bool target_available = false;
   bool version_supported = true;
};

struct Result
{
   std::string message;
   bool is_error;
};

// Called with the state locked
Result Install(Entry& entry, void* target)
{
   const Hook& hook = entry.hook;
   if (!target)
   {
      entry.status = Status::Failed;
      return {"not available", true};
   }

   State& state = GetState();
   for (Entry& other : state.entries)
   {
      if (&other == &entry || other.status != Status::Installed || other.inline_hook.target() != target)
         continue;
      if (other.hook.detour != hook.detour && !(other.hook.interchangeable && hook.interchangeable))
      {
         entry.status = Status::Failed;
         return {std::format("already hooked by {} with a different detour", GetTargetName(other.hook)), true};
      }
      *hook.original = *other.hook.original;
      entry.status = Status::Installed;
      return {std::format("shared with {}", GetTargetName(other.hook)), false};
   }

   // Created disabled so the original is set before any call can reach the detour
   auto created = safetyhook::InlineHook::create(target, hook.detour, safetyhook::InlineHook::StartDisabled);
   if (!created)
   {
      entry.status = Status::Failed;
      return {std::format("SafetyHook error {} at {}", uint32_t(created.error().type), static_cast<const void*>(created.error().ip)), true};
   }
   *hook.original = created->original<void*>();
   if (auto enabled = created->enable(); !enabled)
   {
      entry.status = Status::Failed;
      return {std::format("SafetyHook enable error {}", uint32_t(enabled.error().type)), true};
   }
   entry.inline_hook = std::move(*created);
   entry.status = Status::Installed;
   return {"", false};
}
} // namespace

void Declare(std::span<const Hook> hooks)
{
   {
      State& state = GetState();
      std::scoped_lock lock(state.mutex);
      for (const Hook& hook : hooks)
         state.entries.push_back({.hook = hook});
   }
   InstallPending();
}

void InstallPending()
{
   State& state = GetState();

   // 1. Snapshot the pending hooks
   std::vector<Candidate> candidates;
   {
      std::scoped_lock lock(state.mutex);
      for (size_t i = 0; i < state.entries.size(); i++)
      {
         const Entry& entry = state.entries[i];
         if (entry.status == Status::Pending)
            candidates.push_back({.index = i, .hook = entry.hook});
      }
   }
   if (candidates.empty())
      return;

   // 2. Find their targets without the lock (this may take the loader lock)
   for (Candidate& candidate : candidates)
   {
      candidate.target_available = IsTargetAvailable(candidate.hook);
      candidate.version_supported = candidate.target_available && IsVersionSupported(candidate.hook);
      if (candidate.version_supported)
         candidate.target = FindTarget(candidate.hook);
   }

   // 3. Install
   std::vector<std::pair<const Candidate*, Result>> results;
   {
      std::scoped_lock lock(state.mutex);
      for (const Candidate& candidate : candidates)
      {
         Entry& entry = state.entries[candidate.index];
         if (!candidate.target_available || entry.status != Status::Pending) // Not loaded yet, or installed by another thread meanwhile
            continue;
         if (!candidate.version_supported)
         {
            entry.status = Status::Failed;
            results.emplace_back(&candidate, Result{"skipped, the object doesn't implement the interface version with this method", false});
            continue;
         }
         results.emplace_back(&candidate, Install(entry, candidate.target));
      }
   }

   // 4. Log without the lock (finding the module of an address takes the loader lock)
   for (const auto& [candidate, result] : results)
   {
      const std::string name = GetTargetName(candidate->hook);
      if (result.is_error)
         Utils::Log::Error("Hooks: {} failed: {}", name, result.message);
      else if (!candidate->target)
         Utils::Log::Info("Hooks: {} {}", name, result.message);
      else
         Utils::Log::Info("Hooks: {} at {} in {}{}{}", name, candidate->target, Utils::Modules::GetName(candidate->target),
            result.message.empty() ? "" : ", ", result.message);
   }
}

void OnModuleLoaded(std::wstring_view name)
{
   bool is_hooked_module = false;
   {
      State& state = GetState();
      std::scoped_lock lock(state.mutex);
      for (const Entry& entry : state.entries)
      {
         const auto* target = std::get_if<ExportTarget>(&entry.hook.target);
         if (entry.status == Status::Pending && target &&
            Utils::Strings::EqualsIgnoreCase(target->module, name))
         {
            is_hooked_module = true;
            break;
         }
      }
   }
   if (is_hooked_module)
      InstallPending();
}

void UninstallAll()
{
   State& state = GetState();
   std::scoped_lock lock(state.mutex);
   for (Entry& entry : state.entries)
   {
      entry.inline_hook.reset();
      entry.status = Status::Pending;
   }
}

Counts GetCounts()
{
   Counts counts;
   State& state = GetState();
   std::scoped_lock lock(state.mutex);
   counts.declared = uint32_t(state.entries.size());
   for (const Entry& entry : state.entries)
   {
      counts.installed += entry.status == Status::Installed;
      counts.failed += entry.status == Status::Failed;
   }
   return counts;
}

void LogSummary()
{
   const Counts counts = GetCounts();
   Utils::Log::Info("Hooks: installed {}/{}, {} failed", counts.installed, counts.declared, counts.failed);
}
} // namespace Hooks
} // namespace Luma
