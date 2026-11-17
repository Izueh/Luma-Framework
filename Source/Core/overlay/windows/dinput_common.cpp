#include "overlay/windows/dinput_common.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include <safetyhook.hpp>

#include "utils/log.hpp"

namespace Luma {
namespace Overlay {
namespace DirectInput {
namespace {
std::shared_mutex mutex;
std::unordered_map<void*, safetyhook::InlineHook> hooks;
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
   std::unique_lock lock(mutex);
   if (hooks.contains(target))
      return;
   if (auto hook = safetyhook::InlineHook::create(target, detour))
   {
      hooks.emplace(target, std::move(*hook));
      Utils::Log::Info("Hooks: {} at {}", name, target);
   }
   else
   {
      Utils::Log::Error("Hooks: {} failed", name);
   }
}

void* GetOriginalMethod(const void* object, size_t index)
{
   std::shared_lock lock(mutex);
   const auto it = hooks.find(GetMethod(object, index));
   return it != hooks.end() ? it->second.original<void*>() : nullptr;
}
} // namespace DirectInput
} // namespace Overlay
} // namespace Luma
