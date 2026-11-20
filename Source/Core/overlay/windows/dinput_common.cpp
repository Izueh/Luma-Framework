#include "overlay/windows/dinput_common.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "hooks/hooks.hpp"
#include "utils/log.hpp"
#include "utils/modules.hpp"

namespace Luma {
namespace Overlay {
namespace DirectInput {
namespace {
// Never held while calling into DirectInput, the hooks' originals, Hooks::Declare or the log
std::shared_mutex mutex;

struct DirectInputMethodHook
{
   const char* name;
   void* first_target; // The first one the detour was given: the only one it's hooked at
   void* original = nullptr; // Where Hooks::Declare writes it: a map's values don't move
   bool logged_skipped_target = false;
};
std::unordered_map<const void*, DirectInputMethodHook> method_hooks_by_detour;
std::unordered_map<const void*, const void*> target_detours; // The detour hooked at each target
std::unordered_map<const void*, DeviceKind> device_kinds; // Only the mouse and keyboard ones

void* GetMethod(const void* object, size_t index)
{
   return (*reinterpret_cast<void* const* const*>(object))[index];
}
} // namespace

void SetDeviceKind(const void* device, DeviceKind kind)
{
   std::unique_lock lock(mutex);
   if (kind == DeviceKind::Other)
      device_kinds.erase(device);
   else
      device_kinds[device] = kind;
}

DeviceKind GetDeviceKind(const void* device)
{
   std::shared_lock lock(mutex);
   const auto it = device_kinds.find(device);
   return it != device_kinds.end() ? it->second : DeviceKind::Other;
}

void HookMethod(const void* object, size_t index, void* detour, const char* name)
{
   void* const target = GetMethod(object, index);
   void** original = nullptr;
   const char* shared_with = nullptr;
   void* hooked_target = nullptr;
   {
      std::unique_lock lock(mutex);
      if (const auto it = method_hooks_by_detour.find(detour); it != method_hooks_by_detour.end())
      {
         // Another tool patched the shared vtable (Steam's overlay, ReShade): a second hook would give this detour two originals,
         // and picking one by the object's vtable recurses when that patch calls the hooked code
         if (it->second.first_target == target || it->second.logged_skipped_target)
            return;
         it->second.logged_skipped_target = true;
         hooked_target = it->second.first_target;
      }
      else
      {
         const auto [owner, is_new] = target_detours.try_emplace(target, detour);
         DirectInputMethodHook& method = method_hooks_by_detour.try_emplace(detour, DirectInputMethodHook{name, target}).first->second;
         if (is_new)
            original = &method.original;
         else
            shared_with = method_hooks_by_detour.at(owner->second).name;
      }
   }
   if (hooked_target)
   {
      Utils::Log::Info("DirectInput: {} isn't hooked again at {} in {}, its hook is at {} in {}: only the calls that reach that one are blocked",
         name, static_cast<const void*>(target), Utils::Modules::GetName(target), static_cast<const void*>(hooked_target), Utils::Modules::GetName(hooked_target));
      return;
   }
   // The same code behind two interfaces (A and W, or two versions): its first detour handles both
   if (shared_with)
   {
      Utils::Log::Info("DirectInput: {} at {} is the code {} is hooked at, that hook handles it", name, static_cast<const void*>(target), shared_with);
      return;
   }
   // Through the hook manager, so it's verified and unhooked like the others
   const Hooks::Hook hook = {Hooks::AddressTarget{target, name}, detour, original};
   Hooks::Declare({&hook, 1});
}

void* GetOriginalMethod(const void* detour)
{
   std::shared_lock lock(mutex);
   const auto it = method_hooks_by_detour.find(detour);
   return it != method_hooks_by_detour.end() ? it->second.original : nullptr;
}
} // namespace DirectInput
} // namespace Overlay
} // namespace Luma
