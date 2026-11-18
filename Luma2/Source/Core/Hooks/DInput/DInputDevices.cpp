#include "Hooks/DInput/DInputDevices.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "Hooks/Hooks.hpp"

namespace Luma {
namespace Hooks {
namespace Input {
namespace DInput {
namespace {
// Never held while calling into DirectInput, the hooks' originals or Hooks::Declare
std::shared_mutex mutex;
// The original of each hooked method, where Hooks::Declare writes it: a map's values don't move
std::unordered_map<void*, void*> originals;
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
   void** original;
   {
      std::unique_lock lock(mutex);
      if (originals.contains(target))
         return;
      original = &originals[target];
   }
   // Through the hook manager, so it's verified and unhooked like the others
   const Hook hook = {AddressTarget{target, name}, detour, original};
   Declare({&hook, 1});
}

void* GetOriginalMethod(const void* object, size_t index)
{
   std::shared_lock lock(mutex);
   const auto it = originals.find(GetMethod(object, index));
   return it != originals.end() ? it->second : nullptr;
}
} // namespace DInput
} // namespace Input
} // namespace Hooks
} // namespace Luma
