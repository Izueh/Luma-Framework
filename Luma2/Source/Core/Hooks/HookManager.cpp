#include "Hooks/HookManager.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <Windows.h>
#include <TlHelp32.h>
#include <winternl.h>

#include <safetyhook.hpp>

#include "Log/Log.hpp"
#include "System/Modules.hpp"
#include "Utils/Strings.hpp"

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
   uint32_t install_order = 0;
   // The target right after Luma's patch, what Verify and UninstallAll compare it with
   std::vector<uint8_t> patched_bytes;
   const void* detoured_to = nullptr; // Where the target already jumped when Luma installed, if another hooker was there
   Chain chain = Chain::Intact;
   const void* chained_to = nullptr;
   bool waits_for_dll = false; // An export forwarded to a dll that isn't loaded yet
};

struct State
{
   std::mutex mutex;
   std::vector<Entry> entries;
   uint32_t install_count = 0;
};

// Leaked at process exit: unhooking from static destructors then isn't safe. UninstallAll deletes it (a real unload).
std::atomic<State*> state_pointer = nullptr;

State& GetState()
{
   State* state = state_pointer.load(std::memory_order_acquire);
   if (state)
      return *state;
   auto* created = new State();
   if (state_pointer.compare_exchange_strong(state, created))
      return *created;
   delete created;
   return *state;
}

// Neutralised and kept hooks with their trampolines: the chain they're in still runs through them, so they live until process exit
std::vector<safetyhook::InlineHook>& GetLeftInChains()
{
   static auto* hooks = new std::vector<safetyhook::InlineHook>();
   return *hooks;
}

std::atomic<uint32_t> neutralised_count = 0;
// Never held with another lock
std::mutex last_verification_mutex;
Verification last_verification;

std::string GetTargetName(const Hook& hook)
{
   if (const auto* target = std::get_if<ExportTarget>(&hook.target))
      return std::format("{}!{}", Utils::Strings::ToUtf8(target->module), target->name);
   if (const auto* target = std::get_if<AddressTarget>(&hook.target))
      return target->name;
   return std::get<VtableTarget>(hook.target).name;
}

bool IsTargetAvailable(const Hook& hook)
{
   if (const auto* target = std::get_if<ExportTarget>(&hook.target))
      return System::Modules::FindSystemModule(target->module) != nullptr;
   if (const auto* target = std::get_if<AddressTarget>(&hook.target))
      return target->address != nullptr;
   return Vtables::Get(std::get<VtableTarget>(hook.target).vtable) != nullptr;
}

bool IsVersionSupported(const Hook& hook)
{
   const auto* target = std::get_if<VtableTarget>(&hook.target);
   return !target || target->required_version == GUID_NULL || Vtables::Supports(target->vtable, target->required_version);
}

// Takes the loader lock for exports, so never with the state locked
void* FindTarget(const Hook& hook, bool& waits_for_dll)
{
   waits_for_dll = false;
   if (const auto* target = std::get_if<ExportTarget>(&hook.target))
      return System::Modules::FindExport(System::Modules::FindSystemModule(target->module), target->name, &waits_for_dll);
   if (const auto* target = std::get_if<AddressTarget>(&hook.target))
      return target->address;
   const auto& target = std::get<VtableTarget>(hook.target);
   return Vtables::GetMethod(target.vtable, target.slot);
}

bool IsReadable(const void* address, size_t size)
{
   MEMORY_BASIC_INFORMATION info = {};
   if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
      return false;
   return uintptr_t(address) + size <= uintptr_t(info.BaseAddress) + info.RegionSize;
}

template <typename T>
T Read(const uint8_t* at)
{
   T value;
   std::memcpy(&value, at, sizeof(T));
   return value;
}

const uint8_t* DecodeJump(const uint8_t* code)
{
   if (!IsReadable(code, 2))
      return nullptr;
   if (code[0] == 0xE9 && IsReadable(code, 5))
      return code + 5 + Read<int32_t>(code + 1);
   if (code[0] == 0xEB)
      return code + 2 + Read<int8_t>(code + 1);
   if (code[0] == 0xFF && code[1] == 0x25 && IsReadable(code, 6))
   {
#ifdef _WIN64
      const uint8_t* slot = code + 6 + Read<int32_t>(code + 2);
#else
      const auto* slot = reinterpret_cast<const uint8_t*>(uintptr_t(Read<uint32_t>(code + 2)));
#endif
      return IsReadable(slot, sizeof(void*)) ? Read<const uint8_t*>(slot) : nullptr;
   }
#ifdef _WIN64
   // mov rax, imm64; jmp rax
   if (code[0] == 0x48 && code[1] == 0xB8 && IsReadable(code, 12) && code[10] == 0xFF && code[11] == 0xE0)
      return Read<const uint8_t*>(code + 2);
#else
   // push imm32; ret
   if (code[0] == 0x68 && IsReadable(code, 6) && code[5] == 0xC3)
      return reinterpret_cast<const uint8_t*>(uintptr_t(Read<uint32_t>(code + 1)));
#endif
   return nullptr;
}

// A function's address may itself be a jump (an incremental linking thunk in Debug builds)
const void* Resolve(const void* code)
{
   const void* destination = FollowJump(code);
   return destination ? destination : code;
}

// A jump into a system dll is an import stub (user32 into win32u), not a hooker
const void* FindHooker(const void* target)
{
   const void* destination = FollowJump(target);
   return destination && !System::Modules::IsInSystemModule(destination) ? destination : nullptr;
}

Chain ReadChain(const Entry& entry, const void*& chained_to)
{
   chained_to = nullptr;
   const uint8_t* target = entry.inline_hook.target();
   const size_t size = entry.patched_bytes.size();
   if (!target || size == 0 || !IsReadable(target, size) || std::memcmp(target, entry.patched_bytes.data(), size) == 0)
      return Chain::Intact;
   const std::vector<uint8_t>& original = entry.inline_hook.original_bytes();
   if (original.size() == size && std::memcmp(target, original.data(), size) == 0)
      return Chain::Restored;
   chained_to = FollowJump(target);
   return Chain::Chained;
}

struct Candidate
{
   size_t index;
   Hook hook;
   void* target = nullptr;
   const void* detoured_to = nullptr;
   bool target_available = false;
   bool version_supported = true;
   bool waits_for_dll = false;
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
      entry.install_order = ++state.install_count;
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
   const uint8_t* patched = entry.inline_hook.target();
   entry.patched_bytes.assign(patched, patched + entry.inline_hook.original_bytes().size());
   entry.status = Status::Installed;
   entry.install_order = ++state.install_count;
   return {"", false};
}

struct CodeRange
{
   uintptr_t begin = 0;
   uintptr_t end = 0;
};

bool IsInside(std::span<const CodeRange> ranges, uintptr_t address)
{
   for (const CodeRange& range : ranges)
   {
      if (address >= range.begin && address < range.end)
         return true;
   }
   return false;
}

std::vector<CodeRange> GetLumaCode()
{
   HMODULE module = nullptr;
   if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&GetLumaCode), &module))
      return {};
   const auto base = reinterpret_cast<uintptr_t>(module);
   const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
   const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
   std::vector<CodeRange> ranges;
   for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++)
   {
      if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE)
         ranges.push_back({base + section->VirtualAddress, base + section->VirtualAddress + section->Misc.VirtualSize});
   }
   return ranges;
}

// Documented in "winternl.h" without the TEB
struct ThreadBasicInformation
{
   NTSTATUS exit_status;
   void* teb;
   CLIENT_ID client_id;
   KAFFINITY affinity_mask;
   LONG priority;
   LONG base_priority;
};

using NtQueryInformationThreadFunction = NTSTATUS(NTAPI*)(HANDLE thread, ULONG information_class, void* information, ULONG size, ULONG* returned);

// Suspends every other thread. Nothing allocates, logs or locks meanwhile: a suspended thread may hold the heap's or the log's lock
struct FrozenThreads
{
   std::vector<HANDLE> threads;
   NtQueryInformationThreadFunction query_information = nullptr;

   FrozenThreads()
   {
      query_information = reinterpret_cast<NtQueryInformationThreadFunction>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
      HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      if (snapshot == INVALID_HANDLE_VALUE)
         return;
      const DWORD process = GetCurrentProcessId();
      const DWORD self = GetCurrentThreadId();
      THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
      for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
      {
         if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self)
            continue;
         if (HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID))
            threads.push_back(thread);
      }
      CloseHandle(snapshot);
      for (HANDLE& thread : threads)
      {
         if (SuspendThread(thread) == DWORD(-1))
         {
            CloseHandle(thread);
            thread = nullptr;
         }
      }
   }

   ~FrozenThreads()
   {
      for (HANDLE thread : threads)
      {
         if (!thread)
            continue;
         ResumeThread(thread);
         CloseHandle(thread);
      }
   }

   FrozenThreads(const FrozenThreads&) = delete;
   FrozenThreads& operator=(const FrozenThreads&) = delete;

   // Any stack value pointing in `returning` counts: a stale one only makes this wait or keep something longer
   bool AnyInside(std::span<const CodeRange> running, std::span<const CodeRange> returning) const
   {
      for (HANDLE thread : threads)
      {
         if (!thread)
            continue;
         CONTEXT context = {};
         context.ContextFlags = CONTEXT_CONTROL;
         if (!GetThreadContext(thread, &context))
            return true;
#ifdef _WIN64
         const uintptr_t ip = context.Rip;
         const uintptr_t sp = context.Rsp;
#else
         const uintptr_t ip = context.Eip;
         const uintptr_t sp = context.Esp;
#endif
         if (IsInside(running, ip) || IsInside(returning, ip))
            return true;
         ThreadBasicInformation information = {};
         if (returning.empty() || !query_information || query_information(thread, 0, &information, sizeof(information), nullptr) < 0 || !information.teb)
            continue;
         const auto* tib = static_cast<const NT_TIB*>(information.teb);
         const auto base = uintptr_t(tib->StackBase);
         // On another stack (a fiber's) only the instruction pointer is known
         if (sp < uintptr_t(tib->StackLimit) || sp >= base)
            continue;
         for (uintptr_t slot = sp & ~uintptr_t(sizeof(void*) - 1); slot + sizeof(void*) <= base; slot += sizeof(void*))
         {
            if (IsInside(returning, *reinterpret_cast<const uintptr_t*>(slot)))
               return true;
         }
      }
      return false;
   }
};

// With the other threads frozen: retargets the jump to the detour at the original's first instructions, so a hooker above that
// copied Luma's patch bypasses Luma. False for a far jump (FF 25): it holds the detour's address, which that hooker may have copied.
bool Neutralise(const safetyhook::InlineHook& hook, std::span<const uint8_t> patched)
{
   if (patched.size() < 5 || patched[0] != 0xE9)
      return false;
   uint8_t* relay = hook.target() + 5 + Read<int32_t>(patched.data() + 1);
   const safetyhook::Allocation& trampoline = hook.trampoline();
#ifdef _WIN64
   constexpr uint8_t FAR_JUMP[] = {0xFF, 0x25, 0, 0, 0, 0};
   constexpr size_t RELAY_SIZE = sizeof(FAR_JUMP) + sizeof(uintptr_t);
#else
   constexpr size_t RELAY_SIZE = 5;
#endif
   if (relay < trampoline.data() || relay + RELAY_SIZE > trampoline.data() + trampoline.size())
      return false;
#ifdef _WIN64
   if (std::memcmp(relay, FAR_JUMP, sizeof(FAR_JUMP)) != 0)
      return false;
   const uintptr_t start = trampoline.address();
   std::memcpy(relay + sizeof(FAR_JUMP), &start, sizeof(start));
#else
   if (relay[0] != 0xE9)
      return false;
   const int32_t to_start = int32_t(trampoline.address() - (uintptr_t(relay) + 5));
   std::memcpy(relay + 1, &to_start, sizeof(to_start));
#endif
   FlushInstructionCache(GetCurrentProcess(), relay, RELAY_SIZE);
   return true;
}

std::string_view ToString(Chain chain)
{
   switch (chain)
   {
   case Chain::Intact: return "first in its chain";
   case Chain::Chained: return "chained under";
   case Chain::Restored: return "unhooked by someone else (the original bytes are back)";
   }
   return "?";
}
} // namespace

const void* FollowJump(const void* code)
{
   const uint8_t* destination = nullptr;
   const auto* at = static_cast<const uint8_t*>(code);
   // A few relays at most: a hooker's near one, then its detour
   for (int hop = 0; hop < 8 && at; hop++)
   {
      at = DecodeJump(at);
      if (at)
         destination = at;
   }
   return destination;
}

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

   // 2. Find their targets without the lock (this may take the loader lock), and whether another hooker is already there
   for (Candidate& candidate : candidates)
   {
      candidate.target_available = IsTargetAvailable(candidate.hook);
      candidate.version_supported = candidate.target_available && IsVersionSupported(candidate.hook);
      if (candidate.version_supported)
         candidate.target = FindTarget(candidate.hook, candidate.waits_for_dll);
      if (candidate.target)
         candidate.detoured_to = FindHooker(candidate.target);
   }

   // 3. Install
   std::vector<std::pair<const Candidate*, Result>> results;
   {
      std::scoped_lock lock(state.mutex);
      for (const Candidate& candidate : candidates)
      {
         Entry& entry = state.entries[candidate.index];
         entry.waits_for_dll = candidate.waits_for_dll;
         // Not loaded yet, or installed by another thread meanwhile
         if (!candidate.target_available || candidate.waits_for_dll || entry.status != Status::Pending)
            continue;
         if (!candidate.version_supported)
         {
            entry.status = Status::Failed;
            results.emplace_back(&candidate, Result{"skipped, the object doesn't implement the interface version with this method", false});
            continue;
         }
         entry.detoured_to = candidate.detoured_to;
         results.emplace_back(&candidate, Install(entry, candidate.target));
      }
   }

   // 4. Log without the lock (finding the module of an address takes the loader lock)
   for (const auto& [candidate, result] : results)
   {
      const std::string name = GetTargetName(candidate->hook);
      if (result.is_error)
      {
         Log::Error("Hooks: {} failed: {}", name, result.message);
         continue;
      }
      if (!candidate->target)
      {
         Log::Info("Hooks: {} {}", name, result.message);
         continue;
      }
      const std::string detoured = candidate->detoured_to ? std::format(", on top of {}'s hook", System::Modules::GetName(candidate->detoured_to)) : "";
      Log::Info("Hooks: {} at {} in {}{}{}{}", name, candidate->target, System::Modules::GetName(candidate->target), detoured,
         result.message.empty() ? "" : ", ", result.message);
      // Another thread may have hooked it in between
      if (result.message.empty() && Resolve(candidate->target) != Resolve(candidate->hook.detour))
         Log::Warning("Hooks: {} leads to {} right after its install, not to Luma's detour", name, System::Modules::GetName(Resolve(candidate->target)));
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
         if (entry.status == Status::Pending && target && (entry.waits_for_dll || Utils::Strings::EqualsIgnoreCase(target->module, name)))
         {
            is_hooked_module = true;
            break;
         }
      }
   }
   if (is_hooked_module)
      InstallPending();
}

Verification Verify()
{
   struct Change
   {
      std::string name;
      Chain chain;
      const void* chained_to;
   };

   Verification verification;
   std::vector<Change> changes;
   {
      State& state = GetState();
      std::scoped_lock lock(state.mutex);
      for (Entry& entry : state.entries)
      {
         if (entry.status != Status::Installed || !entry.inline_hook)
            continue;
         const void* chained_to = nullptr;
         const Chain chain = ReadChain(entry, chained_to);
         verification.intact += chain == Chain::Intact;
         verification.chained += chain == Chain::Chained;
         verification.restored += chain == Chain::Restored;
         verification.detoured_before += entry.detoured_to != nullptr;
         if (chain != entry.chain || chained_to != entry.chained_to)
            changes.push_back({GetTargetName(entry.hook), chain, chained_to});
         entry.chain = chain;
         entry.chained_to = chained_to;
      }
   }
   {
      std::scoped_lock lock(last_verification_mutex);
      last_verification = verification;
   }
   for (const Change& change : changes)
   {
      if (change.chain == Chain::Chained)
         Log::Info("Hooks: {} is {} {}", change.name, ToString(change.chain), change.chained_to ? System::Modules::GetName(change.chained_to) : "an unknown patch");
      else
         Log::Info("Hooks: {} is {}", change.name, ToString(change.chain));
   }
   return verification;
}

Verification GetLastVerification()
{
   std::scoped_lock lock(last_verification_mutex);
   return last_verification;
}

void VerifyOnPresent()
{
   static std::atomic<uint64_t> next = 0;
   const uint64_t now = GetTickCount64();
   uint64_t due = next.load(std::memory_order_relaxed);
   if (due == UINT64_MAX || now < due)
      return;
#if DEVELOPMENT
   constexpr uint64_t INTERVAL_MS = 60 * 1000;
   if (!next.compare_exchange_strong(due, now + INTERVAL_MS))
      return;
#else
   if (!next.compare_exchange_strong(due, UINT64_MAX))
      return;
#endif
   const Verification verification = Verify();
   if (due == 0)
      Log::Info("Hooks: at the first present {} first in their chain, {} chained under another hooker, {} unhooked by someone else, {} installed on top of one",
         verification.intact, verification.chained, verification.restored, verification.detoured_before);
}

std::optional<ChainState> GetChainState(const void* detour)
{
   const void* detoured_to = nullptr;
   const void* chained_to = nullptr;
   Chain chain;
   {
      State& state = GetState();
      std::scoped_lock lock(state.mutex);
      const auto it = std::ranges::find_if(state.entries, [detour](const Entry& entry) { return entry.status == Status::Installed && entry.hook.detour == detour && entry.inline_hook; });
      if (it == state.entries.end())
         return std::nullopt;
      chain = ReadChain(*it, chained_to);
      detoured_to = it->detoured_to;
   }
   ChainState state = {.chain = chain};
   if (detoured_to)
      state.detoured_by = System::Modules::GetName(detoured_to);
   if (chained_to)
      state.chained_by = System::Modules::GetName(chained_to);
   return state;
}

UninstallReport UninstallAll(bool can_wait)
{
   UninstallReport report;
   std::vector<std::string> left_in_chains;
   {
      State& state = GetState();
      std::scoped_lock lock(state.mutex);

      std::vector<Entry*> installed;
      for (Entry& entry : state.entries)
      {
         if (entry.status == Status::Installed)
            installed.push_back(&entry);
      }
      std::ranges::sort(installed, [](const Entry* a, const Entry* b) { return a->install_order > b->install_order; });

      // 1. In the reverse order of install: Luma's bytes are restored only where they're still what Luma wrote
      std::vector<Entry*> to_neutralise;
      std::vector<Entry*> to_free;
      for (Entry* entry : installed)
      {
         if (!entry->inline_hook) // Shared with another entry, which owns the hook
         {
            report.removed++;
            continue;
         }
         // Its dll was unloaded: nothing to restore, and freeing the hook would write there
         if (!IsReadable(entry->inline_hook.target(), entry->patched_bytes.size()))
         {
            GetLeftInChains().push_back(std::move(entry->inline_hook));
            report.removed++;
            report.leaked_trampolines++;
            continue;
         }
         const void* chained_to = nullptr;
         const Chain chain = ReadChain(*entry, chained_to);
         // Restored: the original bytes are back, nothing jumps into Luma's patch any more
         if (chain == Chain::Restored || (chain == Chain::Intact && entry->inline_hook.disable()))
         {
            to_free.push_back(entry);
            continue;
         }
         to_neutralise.push_back(entry);
      }

      // 2. The others stay where they are in the chain, without Luma's detour where that's possible
      if (!to_neutralise.empty())
      {
         std::vector<bool> neutralised(to_neutralise.size());
         {
            FrozenThreads frozen;
            for (size_t i = 0; i < to_neutralise.size(); i++)
               neutralised[i] = Neutralise(to_neutralise[i]->inline_hook, to_neutralise[i]->patched_bytes);
         }
         for (size_t i = 0; i < to_neutralise.size(); i++)
         {
            Entry* entry = to_neutralise[i];
            report.neutralised += neutralised[i];
            report.kept += !neutralised[i];
            left_in_chains.push_back(std::format("{} ({})", GetTargetName(entry->hook), neutralised[i] ? "neutralised" : "kept"));
            GetLeftInChains().push_back(std::move(entry->inline_hook));
         }
         neutralised_count += report.neutralised + report.kept;
      }

      // 3. A trampoline is freed once no thread is in it or in Luma's code, nor returns into it (that also decides if the dll can go)
      std::vector<CodeRange> trampolines;
      for (const Entry* entry : to_free)
      {
         const safetyhook::Allocation& trampoline = entry->inline_hook.trampoline();
         trampolines.push_back({trampoline.address(), trampoline.address() + trampoline.size()});
      }
      const std::vector<CodeRange> luma_code = GetLumaCode();
      // Each try freezes every thread: few, spaced out
      const uint32_t tries = can_wait ? 400 : 20;
      const DWORD pause = can_wait ? 5 : 1;
      bool trampolines_in_use = true;
      for (uint32_t i = 0; i < tries; i++)
      {
         {
            FrozenThreads frozen;
            trampolines_in_use = frozen.AnyInside(trampolines, {});
            report.code_in_use = frozen.AnyInside({}, luma_code);
         }
         if (!trampolines_in_use && !report.code_in_use)
            break;
         Sleep(pause);
      }
      for (Entry* entry : to_free)
      {
         if (trampolines_in_use || report.code_in_use)
         {
            // Outside Luma's dll: it stays valid without it
            GetLeftInChains().push_back(std::move(entry->inline_hook));
            report.leaked_trampolines++;
         }
         else
         {
            entry->inline_hook.reset();
         }
         report.removed++;
      }
      state.entries.clear();
   }
   // Declare starts again from an empty state
   delete state_pointer.exchange(nullptr);

   for (const std::string& name : left_in_chains)
      Log::Info("Hooks: {} stays in another hooker's chain", name);
   Log::Info("Hooks: uninstalled, {} removed, {} neutralised, {} kept{}{}", report.removed, report.neutralised, report.kept,
      report.leaked_trampolines ? std::format(", {} trampolines left with a thread inside", report.leaked_trampolines) : "",
      report.code_in_use ? ", a thread is still in Luma's code" : "");
   return report;
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
   counts.neutralised = neutralised_count;
   return counts;
}

void LogSummary()
{
   const Counts counts = GetCounts();
   Log::Info("Hooks: installed {}/{}, {} failed", counts.installed, counts.declared, counts.failed);
}
} // namespace Hooks
} // namespace Luma
