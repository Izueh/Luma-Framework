#include "Hooks/DInput/DInputDevices.hpp"

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "Hooks/HookManager.hpp"
#include "System/Modules.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Hooks {
namespace Input {
namespace DInput {
namespace {
// Never held while calling into DirectInput, the hooks' originals, Hooks::Declare or the log
std::shared_mutex mutex;

struct MethodHook
{
   std::string name; // The hook manager keeps a pointer to it: a map's values don't move
   void* first_target; // The only one the detour is hooked at
   void* original = nullptr; // Where Hooks::Declare writes it
   bool logged_skipped_target = false;
};

std::unordered_map<const void*, MethodHook> method_hooks_by_detour;
std::unordered_map<const void*, DeviceKind> device_kinds;

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
   MethodHook* new_hook = nullptr;
   void* hooked_target = nullptr;
   {
      std::unique_lock lock(mutex);
      const auto [it, is_new] = method_hooks_by_detour.try_emplace(detour, MethodHook{name, target});
      if (is_new)
      {
         new_hook = &it->second;
      }
      else
      {
         // A second hook would give the detour two originals, and picking one by the object's vtable slot recurses once
         // another tool patched that slot (Steam's overlay, ReShade) and its code calls the hooked one
         if (it->second.first_target == target || it->second.logged_skipped_target)
            return;
         it->second.logged_skipped_target = true;
         hooked_target = it->second.first_target;
      }
   }
   if (hooked_target)
   {
      Log::Info("Hooks: {} isn't hooked again at {} in {}, its hook is at {} in {}: only the calls that reach that one are blocked", name,
         static_cast<const void*>(target), System::Modules::GetName(target), static_cast<const void*>(hooked_target), System::Modules::GetName(hooked_target));
      return;
   }
   // Through the hook manager, so it's verified and unhooked like the others. Interchangeable: the "A" and "W" interfaces (or two
   // versions) can share the method's code, whose first detour then handles both.
   const Hook hook = {AddressTarget{target, new_hook->name.c_str()}, detour, &new_hook->original, true};
   Declare({&hook, 1});
}

void* GetOriginalMethod(const void* detour)
{
   std::shared_lock lock(mutex);
   const auto it = method_hooks_by_detour.find(detour);
   return it != method_hooks_by_detour.end() ? it->second.original : nullptr;
}
} // namespace DInput
} // namespace Input
} // namespace Hooks
} // namespace Luma
