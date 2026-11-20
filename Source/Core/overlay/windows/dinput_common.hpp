#pragma once

#include <cstddef>
#include <cstdint>

#include <Windows.h>

// What the DirectInput 1/2/7 hooks ("dinput.cpp") and the DirectInput 8 ones ("dinput8.cpp") share. They're separate files
// because each needs its own "DIRECTINPUT_VERSION", so this doesn't include "dinput.h".
namespace Luma {
namespace Overlay {
namespace DirectInput {
// From dinput.h, defined here so neither "dinput8.lib" nor "dxguid.lib" is linked
constexpr GUID GUID_SYS_MOUSE = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID GUID_SYS_KEYBOARD = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

// ReShade's "g_dinput_device_type". Only the mouse and keyboard are blocked
enum class DeviceKind : uint8_t
{
   Other,
   Mouse,
   Keyboard,
};

// Records what a device the game just created is (`Other` forgets it, as an address can be reused)
void SetDeviceKind(const void* device, DeviceKind kind);
DeviceKind GetDeviceKind(const void* device);

// Each detour is hooked once, at the first object's method code, and always calls that hook's original: a detour shared by several
// hooks can't tell which one it runs for. A later object whose slot holds other code (another tool patched the shared vtable) isn't
// hooked again, that code usually calls the hooked one. Code another detour already hooked (interfaces sharing code) is left to it.
void HookMethod(const void* object, size_t index, void* detour, const char* name);
// The original of the method `detour` is hooked at, null if it isn't hooked
void* GetOriginalMethod(const void* detour);

template <typename Function>
Function GetOriginal(Function detour)
{
   return reinterpret_cast<Function>(GetOriginalMethod(reinterpret_cast<const void*>(detour)));
}
} // namespace DirectInput
} // namespace Overlay
} // namespace Luma
