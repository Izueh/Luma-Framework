/*
 * Adapted from ReShade (https://github.com/crosire/reshade):
 *   source/input_windows.cpp, Copyright (C) 2014 Patrick Mours
 *   source/input.cpp, Copyright (C) 2014 Patrick Mours ("register_window_with_raw_input")
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Hooks/User32/User32InputHooks.hpp"

#include <Windows.h>
#include <concurrent_unordered_map.h>

#include <cassert>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "Hooks/HookManager.hpp"
#include "Hooks/InputHooks.hpp"
#include "Hooks/HookScope.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Hooks {
namespace Input {
namespace User32 {
namespace {
// What the game last asked for, so it can be given back when the overlay closes
std::mutex cursor_mutex;
RECT last_clip_cursor = {};
POINT last_cursor_position = {};

// The windows the game registered for raw input, with what it did for each. The null window holds the registrations without a target.
std::shared_mutex raw_input_mutex;
std::unordered_map<HWND, uint32_t> raw_input_windows;

decltype(&::PeekMessageA) original_peek_message_a = nullptr;
decltype(&::PeekMessageW) original_peek_message_w = nullptr;
decltype(&::GetMessageA) original_get_message_a = nullptr;
decltype(&::GetMessageW) original_get_message_w = nullptr;
decltype(&::PostMessageA) original_post_message_a = nullptr;
decltype(&::PostMessageW) original_post_message_w = nullptr;
decltype(&::RegisterRawInputDevices) original_register_raw_input_devices = nullptr;
decltype(&::ClipCursor) original_clip_cursor = nullptr;
decltype(&::SetCursorPos) original_set_cursor_pos = nullptr;
decltype(&::GetCursorPos) original_get_cursor_pos = nullptr;
decltype(&::GetKeyState) original_get_key_state = nullptr;
decltype(&::GetAsyncKeyState) original_get_async_key_state = nullptr;
decltype(&::GetKeyboardState) original_get_keyboard_state = nullptr;
decltype(&::GetRawInputBuffer) original_get_raw_input_buffer = nullptr;
decltype(&::SetWindowsHookExA) original_set_windows_hook_ex_a = nullptr;
decltype(&::SetWindowsHookExW) original_set_windows_hook_ex_w = nullptr;
decltype(&::UnhookWindowsHookEx) original_unhook_windows_hook_ex = nullptr;

// Feeds the overlay a message the game took off its queue, and hides it from the game if the overlay used it
void FilterMessage(MSG* message, bool removed)
{
   if (!message || !message->hwnd || !removed || Scope::IsActive())
      return;
   if (HandleMessage(message->hwnd, message->message, message->wParam, message->lParam))
   {
      // We still want "WM_CHAR" messages, so translate the message
      TranslateMessage(message);
      // Change the message so it's ignored by the recipient window
      message->message = WM_NULL;
   }
}

BOOL WINAPI PeekMessageA(LPMSG message, HWND hwnd, UINT filter_min, UINT filter_max, UINT remove)
{
   if (!original_peek_message_a(message, hwnd, filter_min, filter_max, remove))
      return FALSE;
   FilterMessage(message, (remove & PM_REMOVE) != 0);
   return TRUE;
}

BOOL WINAPI PeekMessageW(LPMSG message, HWND hwnd, UINT filter_min, UINT filter_max, UINT remove)
{
   if (!original_peek_message_w(message, hwnd, filter_min, filter_max, remove))
      return FALSE;
   FilterMessage(message, (remove & PM_REMOVE) != 0);
   return TRUE;
}

BOOL WINAPI GetMessageA(LPMSG message, HWND hwnd, UINT filter_min, UINT filter_max)
{
   const BOOL result = original_get_message_a(message, hwnd, filter_min, filter_max);
   // Negative on error
   if (result > 0)
      FilterMessage(message, true);
   return result;
}

BOOL WINAPI GetMessageW(LPMSG message, HWND hwnd, UINT filter_min, UINT filter_max)
{
   const BOOL result = original_get_message_w(message, hwnd, filter_min, filter_max);
   if (result > 0)
      FilterMessage(message, true);
   return result;
}

BOOL WINAPI PostMessageA(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
   // Do not allow mouse movement simulation while we block input
   if (message == WM_MOUSEMOVE && IsBlocking())
      return TRUE;
   return original_post_message_a(hwnd, message, wparam, lparam);
}

BOOL WINAPI PostMessageW(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
   if (message == WM_MOUSEMOVE && IsBlocking())
      return TRUE;
   return original_post_message_w(hwnd, message, wparam, lparam);
}

// Sets the bits of `mask` to the matching ones of `flags`
void RegisterWindowWithRawInput(HWND window, uint32_t mask, uint32_t flags)
{
   const std::unique_lock lock(raw_input_mutex);
   uint32_t& window_flags = raw_input_windows.emplace(window, 0u).first->second;
   window_flags = (window_flags & ~mask) | flags;
}

BOOL WINAPI RegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT device_count, UINT size)
{
   for (UINT i = 0; devices && i < device_count; ++i)
   {
      const RAWINPUTDEVICE& device = devices[i];

      if (device.usUsagePage != 1)
         continue;

      // Keyboard and mouse: the window doesn't get their legacy messages ("WM_KEYDOWN", "WM_LBUTTONDOWN" and friends)
      const uint32_t mask = device.usUsage == 0x06 ? RAW_INPUT_NO_LEGACY_KEYBOARD : device.usUsage == 0x02 ? RAW_INPUT_NO_LEGACY_MOUSE
                                                                                                           : 0;
      if ((device.dwFlags & RIDEV_REMOVE) != 0)
      {
         // Removals have no target window: the usage is no longer registered for any
         const std::unique_lock lock(raw_input_mutex);
         for (auto& window : raw_input_windows)
            window.second &= ~mask;
      }
      else if (device.hwndTarget != nullptr || mask != 0)
      {
         RegisterWindowWithRawInput(device.hwndTarget, mask, (device.dwFlags & RIDEV_NOLEGACY) != 0 ? mask : 0);
      }
   }

   if (!original_register_raw_input_devices(devices, device_count, size))
   {
      Log::Warning("Hooks: RegisterRawInputDevices failed with error code {}", GetLastError());
      return FALSE;
   }
   return TRUE;
}

BOOL WINAPI ClipCursor(const RECT* rect)
{
   if (Scope::IsActive())
      return original_clip_cursor(rect);
   {
      std::scoped_lock lock(cursor_mutex);
      last_clip_cursor = rect ? *rect : RECT{};
   }
   // Some games clip the cursor to a tiny area, so disable that while the overlay has the mouse
   if (IsBlocking())
      rect = nullptr;
   return original_clip_cursor(rect);
}

BOOL WINAPI SetCursorPos(int x, int y)
{
   if (Scope::IsActive())
      return original_set_cursor_pos(x, y);
   {
      std::scoped_lock lock(cursor_mutex);
      last_cursor_position = {x, y};
   }
   // Games that keep the cursor centered (e.g. for camera control) would pin the overlay's cursor
   if (IsBlocking())
      return TRUE;
   return original_set_cursor_pos(x, y);
}

BOOL WINAPI GetCursorPos(LPPOINT point)
{
   if (IsBlocking() && point)
   {
      std::scoped_lock lock(cursor_mutex);
      *point = last_cursor_position;
      return TRUE;
   }
   const BOOL result = original_get_cursor_pos(point);
   if (result && point && !Scope::IsActive())
   {
      // In case it's not set through "SetCursorPos"
      std::scoped_lock lock(cursor_mutex);
      last_cursor_position = *point;
   }
   return result;
}

// Valid keyboard keys are between 8 and 255, some games query the mouse buttons through these too
bool IsBlockedKey(int virtual_key)
{
   return (virtual_key & 0xF8) != 0 || (virtual_key >= VK_LBUTTON && virtual_key <= VK_XBUTTON2);
}

SHORT WINAPI GetKeyState(int virtual_key)
{
   if (IsBlocking() && IsBlockedKey(virtual_key))
      return 0;
   return original_get_key_state(virtual_key);
}

SHORT WINAPI GetAsyncKeyState(int virtual_key)
{
   if (IsBlocking() && IsBlockedKey(virtual_key))
      return 0;
   return original_get_async_key_state(virtual_key);
}

BOOL WINAPI GetKeyboardState(PBYTE key_state)
{
   const BOOL result = original_get_keyboard_state(key_state);
   if (result && key_state && IsBlocking())
      std::memset(key_state, 0, 256); // The first 7 are the mouse buttons
   return result;
}

// This variant of raw input doesn't use "WM_INPUT" messages, so it has to be hooked to block its input
UINT WINAPI GetRawInputBuffer(PRAWINPUT data, PUINT size, UINT header_size)
{
   const UINT result = original_get_raw_input_buffer(data, size, header_size);
   // This is a high throughput API (e.g. 8 kHz mouse polling), so exit early
   if (result == UINT(-1) || !data || !size || *size == 0 || !IsBlocking())
      return result;

   using QWORD = UINT64; // "NEXTRAWINPUTBLOCK" needs it on 64 bit

   for (UINT i = 0; i < result; ++i, data = NEXTRAWINPUTBLOCK(data))
   {
      switch (data->header.dwType)
      {
      case RIM_TYPEMOUSE:
         data->header.hDevice = nullptr;
         data->header.wParam = RIM_INPUTSINK;
         // Zero the mouse data, so it's ignored even if "RIM_INPUTSINK" isn't respected
         std::memset(&data->data.mouse, 0, data->header.dwSize - sizeof(RAWINPUTHEADER));
         break;
      case RIM_TYPEKEYBOARD:
         // An invalid device makes SDL return early, before it asks HID APIs for a report we don't want it to see
         data->header.hDevice = nullptr;
         // Most engines honor the input sink state
         data->header.wParam = RIM_INPUTSINK;
         if ((data->data.keyboard.Flags & RI_KEY_BREAK) == 0)
         {
            // Fake a key up
            data->data.keyboard.Flags |= RI_KEY_BREAK;
            data->data.keyboard.VKey = 0;
         }
         break;
      }
   }
   return result;
}

// The hook function and the one the game asked for, by hook type and thread (0 for all)
concurrency::concurrent_unordered_map<UINT64, std::pair<HHOOK, HOOKPROC>> windows_hooks;

template <DWORD hook_id>
LRESULT CALLBACK HandleWindowsHook(int code, WPARAM wparam, LPARAM lparam)
{
   if ((code == HC_ACTION || code == HC_NOREMOVE) && !IsBlocking())
   {
      const UINT64 global_lookup_key = (static_cast<UINT64>(hook_id) << 32);
      const UINT64 thread_lookup_key = GetCurrentThreadId() | global_lookup_key;

      // Look up original hook function for the current thread and hook type
      if (const auto it = windows_hooks.find(thread_lookup_key); it != windows_hooks.end() && it->second.second)
      {
         const HOOKPROC original_hook_proc = it->second.second;
         // A non-zero result value prevents the system from passing the message to the target window procedure
         // In that case our "GetMessage" and "PeekMessage" hooks will not receive them, so ignore that result and continue with the hook chain instead below
         if (original_hook_proc(code, wparam, lparam) == 0)
            return 0;
      }

      // Alternatively look up global hook function for this hook type (registered for thread ID zero)
      if (const auto it = windows_hooks.find(global_lookup_key); it != windows_hooks.end() && it->second.second)
      {
         const HOOKPROC original_hook_proc = it->second.second;
         if (original_hook_proc(code, wparam, lparam) == 0)
            return 0;
      }
   }

   // Bypass the original hook function and continue with the hook chain
   return CallNextHookEx(nullptr, code, wparam, lparam);
}

// The same for "SetWindowsHookExA" and "SetWindowsHookExW"
template <auto& Original>
HHOOK SetWindowsHook(int hook_type, HOOKPROC proc, HINSTANCE module, DWORD thread_id)
{
   HOOKPROC original_hook_proc = proc;
   const UINT64 thread_lookup_key = thread_id | (static_cast<UINT64>(hook_type) << 32);

   if (const auto it = windows_hooks.find(thread_lookup_key); it == windows_hooks.end() || it->second.first == nullptr)
   {
      switch (hook_type)
      {
      case WH_MOUSE:
         proc = &HandleWindowsHook<WH_MOUSE>;
         break;
      case WH_MOUSE_LL:
         proc = &HandleWindowsHook<WH_MOUSE_LL>;
         break;
      case WH_KEYBOARD:
         proc = &HandleWindowsHook<WH_KEYBOARD>;
         break;
      case WH_KEYBOARD_LL:
         proc = &HandleWindowsHook<WH_KEYBOARD_LL>;
         break;
      }
   }
   else
   {
      Log::Verbose("Hooks: Windows hook function of type {} was already registered for thread {}", hook_type, thread_id);
   }

   const HHOOK result = Original(hook_type, proc, module, thread_id);

   if (result != nullptr && proc != original_hook_proc)
   {
      Log::Verbose("Hooks: Windows hook function of type {} registered for thread {}", hook_type, thread_id);
      windows_hooks[thread_lookup_key] = std::make_pair(result, original_hook_proc);
   }
   return result;
}

HHOOK WINAPI SetWindowsHookExA(int hook_type, HOOKPROC proc, HINSTANCE module, DWORD thread_id)
{
   return SetWindowsHook<original_set_windows_hook_ex_a>(hook_type, proc, module, thread_id);
}

HHOOK WINAPI SetWindowsHookExW(int hook_type, HOOKPROC proc, HINSTANCE module, DWORD thread_id)
{
   return SetWindowsHook<original_set_windows_hook_ex_w>(hook_type, proc, module, thread_id);
}

BOOL WINAPI UnhookWindowsHookEx(HHOOK hook)
{
   for (const std::pair<const UINT64, std::pair<HHOOK, HOOKPROC>>& entry : windows_hooks)
   {
      if (entry.second.first == hook)
      {
         Log::Verbose("Hooks: Windows hook function of type {} unregistered for thread {}", static_cast<int>(entry.first >> 32), static_cast<DWORD>(entry.first & 0xFFFFFFFF));
         windows_hooks[entry.first] = std::make_pair(nullptr, nullptr);
         break;
      }
   }
   return original_unhook_windows_hook_ex(hook);
}
} // namespace

void Install()
{
   const Hook hooks[] = {
      ExportHook(L"user32.dll", "PeekMessageA", &PeekMessageA, original_peek_message_a),
      ExportHook(L"user32.dll", "PeekMessageW", &PeekMessageW, original_peek_message_w),
      ExportHook(L"user32.dll", "GetMessageA", &GetMessageA, original_get_message_a),
      ExportHook(L"user32.dll", "GetMessageW", &GetMessageW, original_get_message_w),
      ExportHook(L"user32.dll", "PostMessageA", &PostMessageA, original_post_message_a),
      ExportHook(L"user32.dll", "PostMessageW", &PostMessageW, original_post_message_w),
      ExportHook(L"user32.dll", "RegisterRawInputDevices", &RegisterRawInputDevices, original_register_raw_input_devices),
      ExportHook(L"user32.dll", "ClipCursor", &ClipCursor, original_clip_cursor),
      ExportHook(L"user32.dll", "SetCursorPos", &SetCursorPos, original_set_cursor_pos),
      ExportHook(L"user32.dll", "GetCursorPos", &GetCursorPos, original_get_cursor_pos),
      ExportHook(L"user32.dll", "GetKeyState", &GetKeyState, original_get_key_state),
      ExportHook(L"user32.dll", "GetAsyncKeyState", &GetAsyncKeyState, original_get_async_key_state),
      ExportHook(L"user32.dll", "GetKeyboardState", &GetKeyboardState, original_get_keyboard_state),
      ExportHook(L"user32.dll", "GetRawInputBuffer", &GetRawInputBuffer, original_get_raw_input_buffer),
      ExportHook(L"user32.dll", "SetWindowsHookExA", &SetWindowsHookExA, original_set_windows_hook_ex_a),
      ExportHook(L"user32.dll", "SetWindowsHookExW", &SetWindowsHookExW, original_set_windows_hook_ex_w),
      ExportHook(L"user32.dll", "UnhookWindowsHookEx", &UnhookWindowsHookEx, original_unhook_windows_hook_ex),
   };
   Declare(hooks);
}

void OnOpenChanged(bool open)
{
   if (!original_clip_cursor)
      return;
   if (open)
   {
      // The game may have clipped the cursor to a tiny area, the overlay needs the whole window
      original_clip_cursor(nullptr);
      return;
   }
   RECT clip;
   {
      std::scoped_lock lock(cursor_mutex);
      clip = last_clip_cursor;
   }
   // Restore the game's clip
   if ((clip.right - clip.left) != 0 && (clip.bottom - clip.top) != 0)
      original_clip_cursor(&clip);
}

bool IsRawInputWindow(HWND window)
{
   if (!window)
      return false;
   const std::shared_lock lock(raw_input_mutex);
   return raw_input_windows.contains(window);
}

uint32_t GetRawInputFlags(HWND window)
{
   const std::shared_lock lock(raw_input_mutex);
   uint32_t flags = 0;
   for (const HWND key : {window, HWND(nullptr)})
      if (const auto it = raw_input_windows.find(key); it != raw_input_windows.end())
         flags |= it->second;
   return flags;
}
} // namespace User32
} // namespace Input
} // namespace Hooks
} // namespace Luma
