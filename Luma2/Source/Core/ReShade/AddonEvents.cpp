#include "ReShade/AddonEvents.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <utility>
#include <vector>

#include <Psapi.h>

#include "Hooks/HookManager.hpp"
#include "Hooks/HookScope.hpp"
#include "ReShade/ReShadeABI.hpp"
#if DEVELOPMENT
#include <d3d11.h>
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderStages.hpp"
#include "Trace/FrameCapture.hpp"
#endif
#include "System/Modules.hpp"
#include "Log/Log.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace ReShade {
namespace AddonEvents {
namespace {
constexpr size_t SLOT_COUNT = 256;
constexpr size_t MAX_ADDON_MODULES = 64;

// ReShade 6.x event handlers take up to 9 pointer/integer/handle args (no floats by value) and return void or bool,
// so 16 uint64_t params forward all register and stack args verbatim.
#define LUMA_HANDLER_ARGS_DECL                                                                                                                     \
   uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10, \
      uint64_t a11, uint64_t a12, uint64_t a13, uint64_t a14, uint64_t a15
#define LUMA_HANDLER_ARGS a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15

using Handler = uint64_t(__cdecl*)(LUMA_HANDLER_ARGS_DECL);
using RegisterFunction = void(__cdecl*)(uint32_t event, void* callback);
using RegisterForAddonFunction = void(__cdecl*)(void* module, uint32_t event, void* callback);
using UnregisterAddonFunction = void(__cdecl*)(void* module);

std::array<std::atomic<void*>, SLOT_COUNT> handlers = {};
#if DEVELOPMENT
std::array<std::atomic<uint32_t>, SLOT_COUNT> slot_events = {};
std::array<std::string, SLOT_COUNT> slot_owner_names = {};

bool IsDrawOrDispatchEvent(uint32_t event)
{
   switch (static_cast<ABI::Event>(event))
   {
   case ABI::Event::Draw:
   case ABI::Event::DrawIndexed:
   case ABI::Event::Dispatch:
   case ABI::Event::DrawOrDispatchIndirect:
   case ABI::Event::DispatchMesh:
   case ABI::Event::DispatchRays: return true;
   default: return false;
   }
}

std::string_view GetDrawEventName(uint32_t event)
{
   switch (static_cast<ABI::Event>(event))
   {
   case ABI::Event::Draw: return "Draw";
   case ABI::Event::DrawIndexed: return "DrawIndexed";
   case ABI::Event::Dispatch: return "Dispatch";
   case ABI::Event::DrawOrDispatchIndirect: return "DrawOrDispatchIndirect";
   case ABI::Event::DispatchMesh: return "DispatchMesh";
   case ABI::Event::DispatchRays: return "DispatchRays";
   default: return "Call";
   }
}

void CaptureBoundShadersForIntercept(uint32_t event, uint64_t cmd_list_arg, std::vector<Trace::Capture::Shader>& out_shaders, bool& out_deferred)
{
   auto* cmd_list = reinterpret_cast<ABI::DeviceObject*>(static_cast<uintptr_t>(cmd_list_arg));
   if (!cmd_list)
      return;
   ABI::Device* device = cmd_list->GetDevice();
   if (!device || device->GetApi() != ABI::DeviceApi::D3D11)
      return;
   auto* context = reinterpret_cast<ID3D11DeviceContext*>(static_cast<uintptr_t>(cmd_list->GetNative()));
   if (!context)
      return;
   out_deferred = context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED;
   const Shaders::Variants::D3D11::BoundShaders* bound = Shaders::Variants::D3D11::GetBoundShaders(context);
   if (!bound)
      return;
   const Shaders::StageSet stages = static_cast<ABI::Event>(event) == ABI::Event::Dispatch ? Shaders::COMPUTE_STAGES
      : static_cast<ABI::Event>(event) == ABI::Event::DrawOrDispatchIndirect               ? (Shaders::GRAPHICS_STAGES | Shaders::COMPUTE_STAGES)
                                                                                           : Shaders::GRAPHICS_STAGES;
   for (size_t i = 0; i < bound->shaders.size(); i++)
   {
      const ShaderStage stage = ShaderStage(i);
      const Shaders::Database::Shader* shader = bound->shaders[i];
      if (!stages.Has(stage) || !shader)
         continue;
      Trace::Capture::Shader& recorded = out_shaders.emplace_back(Trace::Capture::Shader{
         .stage = stage,
         .hash = shader->hash,
         .name = std::string(shader->name),
      });
      for (const Shaders::Variants::Variant& variant : Shaders::Variants::GetVariants(shader->hash))
      {
         if (variant.compiled)
            recorded.variants.push_back(variant.source);
      }
      recorded.forced = shader->forced_variant.load(std::memory_order_relaxed);
   }
}

void RecordInterceptedCall(size_t slot, uint32_t event_id, std::vector<Trace::Capture::Shader> shaders, bool deferred)
{
   Trace::Capture::Event entry;
   entry.type = Trace::Capture::EventType::ThirdPartyWork;
   entry.name = std::format("{} ({} intercepted)", slot_owner_names[slot].empty() ? std::string_view("ReShade add-on") : std::string_view(slot_owner_names[slot]),
      GetDrawEventName(event_id));
   entry.outcome = Trace::Capture::Outcome::Replaced;
   entry.thread = GetCurrentThreadId();
   entry.deferred = deferred;
   entry.shaders = std::move(shaders);
   Trace::Capture::Add(std::move(entry));
}
#endif

template <size_t Index>
uint64_t __cdecl Wrapper(LUMA_HANDLER_ARGS_DECL)
{
   const auto handler = reinterpret_cast<Handler>(handlers[Index].load(std::memory_order_acquire));
   if (!handler)
      return 0;
#if DEVELOPMENT
   const uint32_t event_id = slot_events[Index].load(std::memory_order_relaxed);
   const bool trace_draw = Trace::Capture::IsCapturing() && IsDrawOrDispatchEvent(event_id);
   std::vector<Trace::Capture::Shader> bound_shaders;
   bool deferred = false;
   if (trace_draw)
      CaptureBoundShadersForIntercept(event_id, a0, bound_shaders, deferred);
   uint64_t result = 0;
   {
      Hooks::Scope::Guard scope;
      result = handler(LUMA_HANDLER_ARGS);
   }
   // ReShade draw/dispatch callbacks return bool (low byte in AL): true means the add-on consumed the game's call
   if (trace_draw && (result & 0xFFu) != 0)
      RecordInterceptedCall(Index, event_id, std::move(bound_shaders), deferred);
   return result;
#else
   Hooks::Scope::Guard scope;
   return handler(LUMA_HANDLER_ARGS);
#endif
}

template <size_t... Index>
constexpr std::array<void*, sizeof...(Index)> MakeWrappers(std::index_sequence<Index...>)
{
   return {reinterpret_cast<void*>(&Wrapper<Index>)...};
}

const std::array<void*, SLOT_COUNT> WRAPPERS = MakeWrappers(std::make_index_sequence<SLOT_COUNT>());

struct Registration
{
   uint32_t event = 0;
   void* callback = nullptr; // The add-on's own, null when the slot is free
   HMODULE owner = nullptr;
};

// Lock-free reader table (seqlock) for native D3D calls whose return address lands directly inside an add-on module
struct ModuleRange
{
   std::atomic<uintptr_t> begin = 0;
   std::atomic<uintptr_t> end = 0;
};

std::mutex addon_modules_mutex;
std::array<ModuleRange, MAX_ADDON_MODULES> addon_ranges = {};
std::atomic<uint32_t> addon_range_count = 0;
std::atomic<uint32_t> addon_ranges_seq = 0;

std::mutex mutex;
std::array<Registration, SLOT_COUNT> registrations = {};
std::vector<HMODULE> logged_owners;
std::atomic<uint32_t> wrapped_count = 0;
std::atomic<bool> exhausted_logged = false;

RegisterFunction original_register = nullptr;
RegisterForAddonFunction original_register_for_addon = nullptr;
RegisterFunction original_unregister = nullptr;
RegisterForAddonFunction original_unregister_for_addon = nullptr;
UnregisterAddonFunction original_unregister_addon = nullptr;

std::atomic<HMODULE> reshade_module = nullptr;
std::atomic<bool> installed = false;
std::atomic<bool> missing_exports = false;
std::atomic<bool> registered_before = false;

HMODULE ModuleOf(const void* address)
{
   HMODULE module = nullptr;
   if (!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module))
      return nullptr;
   return module;
}

HMODULE SelfModule()
{
   // Found by an address of Luma's code: System::Modules::GetSelf() is null when Core is linked into an executable (Tests)
   static const HMODULE self = ModuleOf(reinterpret_cast<const void*>(&ModuleOf));
   return self;
}

bool IsAddonFileName(std::wstring_view name)
{
   constexpr std::array<std::wstring_view, 3> EXTENSIONS = {L".addon64", L".addon32", L".addon"};
   for (std::wstring_view ext : EXTENSIONS)
   {
      if (name.size() > ext.size() && Utils::Strings::EqualsIgnoreCase(name.substr(name.size() - ext.size()), ext))
         return true;
   }
   return false;
}

void AddAddonRange(const void* base, size_t size)
{
   if (!base || size == 0)
      return;
   const HMODULE module = static_cast<HMODULE>(const_cast<void*>(base));
   // Never add Luma or ReShade itself: game calls forwarded by ReShade's proxy return into ReShade's module
   if (module == SelfModule() || module == reshade_module.load(std::memory_order_acquire))
      return;
   const auto begin = reinterpret_cast<uintptr_t>(base);
   const uintptr_t end = begin + size;
   std::scoped_lock lock(addon_modules_mutex);
   const uint32_t count = addon_range_count.load(std::memory_order_relaxed);
   for (uint32_t i = 0; i < count; i++)
   {
      if (addon_ranges[i].begin.load(std::memory_order_relaxed) == begin)
         return;
   }
   if (count >= MAX_ADDON_MODULES)
      return;
   const uint32_t seq = addon_ranges_seq.load(std::memory_order_relaxed);
   addon_ranges_seq.store(seq + 1, std::memory_order_release);
   addon_ranges[count].begin.store(begin, std::memory_order_relaxed);
   addon_ranges[count].end.store(end, std::memory_order_relaxed);
   addon_range_count.store(count + 1, std::memory_order_relaxed);
   addon_ranges_seq.store(seq + 2, std::memory_order_release);
}

void RemoveAddonRange(const void* base)
{
   if (!base || addon_range_count.load(std::memory_order_relaxed) == 0)
      return;
   const auto begin = reinterpret_cast<uintptr_t>(base);
   std::scoped_lock lock(addon_modules_mutex);
   const uint32_t count = addon_range_count.load(std::memory_order_relaxed);
   for (uint32_t i = 0; i < count; i++)
   {
      if (addon_ranges[i].begin.load(std::memory_order_relaxed) != begin)
         continue;
      const uint32_t seq = addon_ranges_seq.load(std::memory_order_relaxed);
      addon_ranges_seq.store(seq + 1, std::memory_order_release);
      const uint32_t last = count - 1;
      if (i != last)
      {
         addon_ranges[i].begin.store(addon_ranges[last].begin.load(std::memory_order_relaxed), std::memory_order_relaxed);
         addon_ranges[i].end.store(addon_ranges[last].end.load(std::memory_order_relaxed), std::memory_order_relaxed);
      }
      addon_ranges[last].begin.store(0, std::memory_order_relaxed);
      addon_ranges[last].end.store(0, std::memory_order_relaxed);
      addon_range_count.store(last, std::memory_order_relaxed);
      addon_ranges_seq.store(seq + 2, std::memory_order_release);
      return;
   }
}

void RecordAddonModule(HMODULE module)
{
   if (!module || module == SelfModule() || module == reshade_module.load(std::memory_order_acquire))
      return;
   MODULEINFO info = {};
   if (GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) && info.SizeOfImage)
      AddAddonRange(info.lpBaseOfDll, info.SizeOfImage);
}

void ScanLoadedAddonModules()
{
   std::vector<HMODULE> loaded(256);
   DWORD needed = 0;
   while (EnumProcessModules(GetCurrentProcess(), loaded.data(), DWORD(loaded.size() * sizeof(HMODULE)), &needed) && needed > loaded.size() * sizeof(HMODULE))
      loaded.resize(needed / sizeof(HMODULE));
   loaded.resize(needed / sizeof(HMODULE));
   for (HMODULE module : loaded)
   {
      wchar_t name[MAX_PATH];
      const DWORD length = GetModuleBaseNameW(GetCurrentProcess(), module, name, MAX_PATH);
      if (length && IsAddonFileName(std::wstring_view(name, length)))
      {
         MODULEINFO info = {};
         if (GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) && info.SizeOfImage)
            AddAddonRange(info.lpBaseOfDll, info.SizeOfImage);
      }
   }
}

// With the lock held
size_t FindWrapperLocked(const void* address)
{
   for (size_t slot = 0; slot < SLOT_COUNT; slot++)
   {
      if (WRAPPERS[slot] == address)
         return slot;
   }
   return SLOT_COUNT;
}

size_t FindRegistrationLocked(uint32_t event, const void* callback)
{
   for (size_t slot = 0; slot < SLOT_COUNT; slot++)
   {
      if (registrations[slot].callback == callback && registrations[slot].event == event)
         return slot;
   }
   return SLOT_COUNT;
}

struct Wrapped
{
   void* wrapper = nullptr;
   HMODULE owner = nullptr;
};

// The wrapper to register in place of `callback`, null to register it as it is
Wrapped TryWrap(void* module, uint32_t event, void* callback)
{
   if (!callback)
      return {};
   const HMODULE owner = module ? static_cast<HMODULE>(module) : ModuleOf(callback);
   // Luma's own handlers (the bridge's) do Luma's work: they run as they are
   const HMODULE self = SelfModule();
   if (!owner || owner == self || ModuleOf(callback) == self)
      return {};
   RecordAddonModule(owner);
   bool log_owner = false;
   std::unique_lock lock(mutex);
   if (FindWrapperLocked(callback) != SLOT_COUNT)
      return {};
   // The same handler registered twice for an event shares its wrapper, as ReShade removes all copies at once
   size_t slot = FindRegistrationLocked(event, callback);
   if (slot == SLOT_COUNT)
   {
      for (size_t index = 0; index < SLOT_COUNT; index++)
      {
         if (!registrations[index].callback)
         {
            slot = index;
            break;
         }
      }
      if (slot == SLOT_COUNT)
      {
         lock.unlock();
         if (!exhausted_logged.exchange(true))
            Log::Warning("ReShade: more than {} add-on event handlers registered, the rest run unwrapped (their calls are seen by Luma's hooks)", SLOT_COUNT);
         return {};
      }
      registrations[slot] = {event, callback, owner};
#if DEVELOPMENT
      slot_owner_names[slot] = Utils::Strings::ToUtf8(System::Modules::GetPath(owner).filename());
      slot_events[slot].store(event, std::memory_order_relaxed);
#endif
      handlers[slot].store(callback, std::memory_order_release);
      wrapped_count.fetch_add(1, std::memory_order_relaxed);
      if (std::find(logged_owners.begin(), logged_owners.end(), owner) == logged_owners.end())
      {
         logged_owners.push_back(owner);
         log_owner = true;
      }
   }
   lock.unlock();
   if (log_owner)
      Log::Info("ReShade: the event handlers of add-on {} run in a Luma scope: what it creates, binds or draws isn't seen by Luma's hooks",
         Utils::Strings::ToUtf8(System::Modules::GetPath(owner).filename()));
   return {WRAPPERS[slot], owner};
}

// The wrapper registered for `callback` (or `callback` itself if it is one), now free. Null if it was never wrapped.
Wrapped Release(uint32_t event, void* callback)
{
   std::unique_lock lock(mutex);
   size_t slot = FindWrapperLocked(callback);
   if (slot == SLOT_COUNT)
      slot = FindRegistrationLocked(event, callback);
   if (slot == SLOT_COUNT || !registrations[slot].callback)
      return {};
   const Wrapped found = {WRAPPERS[slot], registrations[slot].owner};
   handlers[slot].store(nullptr, std::memory_order_release);
#if DEVELOPMENT
   slot_events[slot].store(0, std::memory_order_relaxed);
#endif
   registrations[slot] = {};
   wrapped_count.fetch_sub(1, std::memory_order_relaxed);
   return found;
}

void ReleaseAllForOwner(HMODULE owner)
{
   if (!owner)
      return;
   std::scoped_lock lock(mutex);
   uint32_t released = 0;
   for (size_t slot = 0; slot < SLOT_COUNT; slot++)
   {
      if (registrations[slot].callback && registrations[slot].owner == owner)
      {
         handlers[slot].store(nullptr, std::memory_order_release);
#if DEVELOPMENT
         slot_events[slot].store(0, std::memory_order_relaxed);
#endif
         registrations[slot] = {};
         released++;
      }
   }
   if (released)
      wrapped_count.fetch_sub(released, std::memory_order_relaxed);
   std::erase(logged_owners, owner);
}

void __cdecl RegisterEventForAddon(void* module, uint32_t event, void* callback)
{
   if (const Wrapped wrapped = TryWrap(module, event, callback); wrapped.wrapper)
      // For the add-on's module, not Luma's: ReShade tells whose handler it is by its module, and a wrapper lies in Luma's
      return original_register_for_addon(wrapped.owner, event, wrapped.wrapper);
   original_register_for_addon(module, event, callback);
}

void __cdecl RegisterEvent(uint32_t event, void* callback)
{
   // Not through the original of this one: it'd look the add-on up from the callback's address
   if (const Wrapped wrapped = TryWrap(nullptr, event, callback); wrapped.wrapper)
      return original_register_for_addon(wrapped.owner, event, wrapped.wrapper);
   original_register(event, callback);
}

void __cdecl UnregisterEventForAddon(void* module, uint32_t event, void* callback)
{
   if (const Wrapped wrapped = Release(event, callback); wrapped.wrapper)
      return original_unregister_for_addon(module ? module : wrapped.owner, event, wrapped.wrapper);
   original_unregister_for_addon(module, event, callback);
}

void __cdecl UnregisterEvent(uint32_t event, void* callback)
{
   if (const Wrapped wrapped = Release(event, callback); wrapped.wrapper)
      return original_unregister_for_addon(wrapped.owner, event, wrapped.wrapper);
   original_unregister(event, callback);
}

void __cdecl UnregisterAddon(void* module)
{
   // Let ReShade remove our wrapper pointers from its event lists before we recycle the slots
   original_unregister_addon(module);
   ReleaseAllForOwner(static_cast<HMODULE>(module));
}
} // namespace

void Install(HMODULE reshade, bool registered_before_luma)
{
   if (installed.exchange(true))
      return;
   reshade_module.store(reshade, std::memory_order_release);
   ScanLoadedAddonModules();
   void* const register_event = reinterpret_cast<void*>(GetProcAddress(reshade, "ReShadeRegisterEvent"));
   void* const register_for_addon = reinterpret_cast<void*>(GetProcAddress(reshade, "ReShadeRegisterEventForAddon"));
   void* const unregister_event = reinterpret_cast<void*>(GetProcAddress(reshade, "ReShadeUnregisterEvent"));
   void* const unregister_for_addon = reinterpret_cast<void*>(GetProcAddress(reshade, "ReShadeUnregisterEventForAddon"));
   if (!register_event || !register_for_addon || !unregister_event || !unregister_for_addon)
   {
      missing_exports.store(true, std::memory_order_relaxed);
      Log::Warning("ReShade: add-on event handlers aren't wrapped, this ReShade doesn't export the registration functions by add-on (ReShadeRegisterEventForAddon...): "
                   "Luma's hooks see what the add-ons create and draw");
      return;
   }
   registered_before.store(registered_before_luma, std::memory_order_relaxed);
   std::vector<Hooks::Hook> hooks = {
      {Hooks::AddressTarget{register_event, "ReShadeRegisterEvent"}, reinterpret_cast<void*>(&RegisterEvent), reinterpret_cast<void**>(&original_register)},
      {Hooks::AddressTarget{register_for_addon, "ReShadeRegisterEventForAddon"}, reinterpret_cast<void*>(&RegisterEventForAddon),
         reinterpret_cast<void**>(&original_register_for_addon)},
      {Hooks::AddressTarget{unregister_event, "ReShadeUnregisterEvent"}, reinterpret_cast<void*>(&UnregisterEvent), reinterpret_cast<void**>(&original_unregister)},
      {Hooks::AddressTarget{unregister_for_addon, "ReShadeUnregisterEventForAddon"}, reinterpret_cast<void*>(&UnregisterEventForAddon),
         reinterpret_cast<void**>(&original_unregister_for_addon)},
   };
   if (void* const unregister_addon = reinterpret_cast<void*>(GetProcAddress(reshade, "ReShadeUnregisterAddon")))
      hooks.push_back({Hooks::AddressTarget{unregister_addon, "ReShadeUnregisterAddon"}, reinterpret_cast<void*>(&UnregisterAddon),
         reinterpret_cast<void**>(&original_unregister_addon)});
   Hooks::Declare(hooks);
   Hooks::Scope::SetCallerFilter(&IsAddonAddress);
   if (registered_before_luma)
      Log::Warning("ReShade: it wasn't loaded by Luma, the add-ons that registered their handlers already aren't wrapped (their calls are seen by Luma's hooks)");
}

void OnModuleLoaded(std::wstring_view name, const void* base, size_t size)
{
   if (!installed.load(std::memory_order_acquire))
      return;
   if (IsAddonFileName(name))
      AddAddonRange(base, size);
}

void OnModuleUnloaded(const void* base)
{
   RemoveAddonRange(base);
}

bool IsAddonAddress(const void* address)
{
   if (!address || addon_range_count.load(std::memory_order_relaxed) == 0)
      return false;
   const auto at = reinterpret_cast<uintptr_t>(address);
   for (;;)
   {
      const uint32_t seq = addon_ranges_seq.load(std::memory_order_acquire);
      if (seq & 1u)
         continue;
      bool hit = false;
      const uint32_t count = addon_range_count.load(std::memory_order_relaxed);
      for (uint32_t i = 0; i < count; i++)
      {
         const uintptr_t begin = addon_ranges[i].begin.load(std::memory_order_relaxed);
         const uintptr_t end = addon_ranges[i].end.load(std::memory_order_relaxed);
         if (at >= begin && at < end)
         {
            hit = true;
            break;
         }
      }
      std::atomic_thread_fence(std::memory_order_acquire);
      if (addon_ranges_seq.load(std::memory_order_relaxed) == seq)
         return hit;
   }
}

std::string Describe()
{
   if (missing_exports.load(std::memory_order_relaxed))
      return "add-on handlers not wrapped (missing exports)";
   if (!installed.load(std::memory_order_relaxed))
      return "add-on handlers not wrapped yet";
   return std::format("{} add-on handlers kept out of Luma's hooks{}", wrapped_count.load(std::memory_order_relaxed),
      registered_before.load(std::memory_order_relaxed) ? " (ReShade wasn't loaded by Luma: earlier ones aren't)" : "");
}
} // namespace AddonEvents
} // namespace ReShade
} // namespace Luma
