#pragma once

#include <cstddef>
#include <cstdint>

#include <Windows.h>

namespace Luma {
namespace Hooks {
namespace Input {
namespace DInput {
// From dinput.h, defined here so neither "dinput8.lib" nor "dxguid.lib" is linked
constexpr GUID GUID_SYS_MOUSE = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID GUID_SYS_KEYBOARD = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

// Only the mouse and keyboard are blocked
enum class DeviceKind : uint8_t
{
   Other,
   Mouse,
   Keyboard,
};

// `Other` forgets the device: an address can be reused
void SetDeviceKind(const void* device, DeviceKind kind);
DeviceKind GetDeviceKind(const void* device);

// Methods are hooked by their address (the code), found through the object's vtable on each call, as several objects (and the
// "A" and "W" interfaces) can share them. A method that's already hooked is left alone.
void HookMethod(const void* object, size_t index, void* detour, const char* name);
// The function that calls the original of the object's method, null if it isn't hooked
void* GetOriginalMethod(const void* object, size_t index);

template <typename Function>
Function GetOriginal(const void* object, size_t index)
{
   return reinterpret_cast<Function>(GetOriginalMethod(object, index));
}
} // namespace DInput
} // namespace Input
} // namespace Hooks
} // namespace Luma
