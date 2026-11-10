/*
 * Input blocking while Luma's overlay is open.
 *
 * Adapted from ReShade (https://github.com/crosire/reshade):
 *   source/input_windows.cpp, Copyright (C) 2014 Patrick Mours
 *   source/windows/dinput8.cpp, Copyright (C) 2021 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2014 Patrick Mours. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *   * Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 *     disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided with the distribution.
 *   * Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
 *     products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Changes from ReShade: there is one overlay window instead of per window input objects, so blocking simply follows
 * whether the overlay is open; Luma's own calls (inside a Luma scope, e.g. ImGui's backend) are never blocked; hooks
 * go through Luma's hook installer; DirectInput is hooked through its export rather than a proxy dll; "GetMessage" calls
 * the original instead of being reimplemented on "PeekMessage".
 */

#include "overlay/input_hooks.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>

#include <safetyhook.hpp>

#include "hooks/hooks.hpp"
#include "hooks/scope.hpp"
#include "overlay/input.hpp"
#include "overlay/overlay.hpp"
#include "utils/log.hpp"

namespace Luma {
namespace Overlay {
namespace InputHooks {
namespace {
// From dinput.h, defined here so neither "dinput8.lib" nor "dxguid.lib" is linked
constexpr GUID IID_DIRECT_INPUT8_A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
constexpr GUID IID_DIRECT_INPUT8_W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
constexpr GUID GUID_SYS_MOUSE = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID GUID_SYS_KEYBOARD = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

// What the game last asked for, so it can be given back when the overlay closes
std::mutex cursor_mutex;
RECT last_clip_cursor = {};
POINT last_cursor_position = {};

decltype(&::PeekMessageA) original_peek_message_a = nullptr;
decltype(&::PeekMessageW) original_peek_message_w = nullptr;
decltype(&::GetMessageA) original_get_message_a = nullptr;
decltype(&::GetMessageW) original_get_message_w = nullptr;
decltype(&::ClipCursor) original_clip_cursor = nullptr;
decltype(&::SetCursorPos) original_set_cursor_pos = nullptr;
decltype(&::GetCursorPos) original_get_cursor_pos = nullptr;
decltype(&::GetKeyState) original_get_key_state = nullptr;
decltype(&::GetAsyncKeyState) original_get_async_key_state = nullptr;
decltype(&::GetKeyboardState) original_get_keyboard_state = nullptr;
decltype(&::GetRawInputBuffer) original_get_raw_input_buffer = nullptr;
using DirectInput8CreateFunction = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
DirectInput8CreateFunction original_direct_input8_create = nullptr;

// The overlay is open and the call is the game's
bool IsBlocking()
{
   return IsOpen() && !Hooks::Scope::IsActive();
}

// Feeds the overlay a message the game took off its queue, and hides it from the game if the overlay used it
void FilterMessage(MSG* message, bool removed)
{
   if (!message || !message->hwnd || !removed || Hooks::Scope::IsActive())
      return;
   if (Input::HandleMessage(message->hwnd, message->message, message->wParam, message->lParam))
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

BOOL WINAPI ClipCursor(const RECT* rect)
{
   if (Hooks::Scope::IsActive())
      return original_clip_cursor(rect);
   {
      std::scoped_lock lock(cursor_mutex);
      last_clip_cursor = rect ? *rect : RECT{};
   }
   // Some games clip the cursor to a tiny area, so disable that while the overlay has the mouse
   if (IsOpen())
      rect = nullptr;
   return original_clip_cursor(rect);
}

BOOL WINAPI SetCursorPos(int x, int y)
{
   if (Hooks::Scope::IsActive())
      return original_set_cursor_pos(x, y);
   {
      std::scoped_lock lock(cursor_mutex);
      last_cursor_position = {x, y};
   }
   // Games that keep the cursor centered (e.g. for camera control) would pin the overlay's cursor
   if (IsOpen())
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
   if (result && point && !Hooks::Scope::IsActive())
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

// DirectInput: methods are hooked by their address (the code), found through the object's vtable on each call, as
// several objects (and the "A" and "W" interfaces) can share them
std::shared_mutex com_mutex;
std::unordered_map<void*, safetyhook::InlineHook> com_hooks;
std::unordered_map<const void*, BYTE> device_types; // Only the mouse and keyboard ones

void* GetMethod(const void* object, size_t index)
{
   return (*reinterpret_cast<void* const* const*>(object))[index];
}

template <typename Function>
Function GetOriginal(const void* object, size_t index)
{
   std::shared_lock lock(com_mutex);
   const auto it = com_hooks.find(GetMethod(object, index));
   return it != com_hooks.end() ? it->second.original<Function>() : nullptr;
}

void HookMethod(const void* object, size_t index, void* detour, const char* name)
{
   void* const target = GetMethod(object, index);
   std::unique_lock lock(com_mutex);
   if (com_hooks.contains(target))
      return;
   if (auto hook = safetyhook::InlineHook::create(target, detour))
   {
      com_hooks.emplace(target, std::move(*hook));
      Utils::Log::Info("Hooks: {} at {}", name, target);
   }
   else
   {
      Utils::Log::Error("Hooks: {} failed", name);
   }
}

BYTE GetDeviceType(const void* device)
{
   std::shared_lock lock(com_mutex);
   const auto it = device_types.find(device);
   return it != device_types.end() ? it->second : 0;
}

constexpr size_t GET_DEVICE_STATE_INDEX = 9;
constexpr size_t GET_DEVICE_DATA_INDEX = 10;
constexpr size_t CREATE_DEVICE_INDEX = 3;

template <typename Device>
HRESULT STDMETHODCALLTYPE GetDeviceState(Device* device, DWORD data_size, LPVOID data)
{
   using Function = HRESULT(STDMETHODCALLTYPE*)(Device*, DWORD, LPVOID);
   const auto original = GetOriginal<Function>(device, GET_DEVICE_STATE_INDEX);
   if (!original)
      return DIERR_GENERIC;
   const HRESULT result = original(device, data_size, data);
   if (SUCCEEDED(result) && data && IsBlocking())
   {
      switch (GetDeviceType(device))
      {
      case DI8DEVTYPE_MOUSE:
         if (data_size == sizeof(DIMOUSESTATE) || data_size == sizeof(DIMOUSESTATE2))
         {
            std::memset(static_cast<LPBYTE>(data) + offsetof(DIMOUSESTATE, rgbButtons), 0, data_size - offsetof(DIMOUSESTATE, rgbButtons));
            // Luma change: ReShade only clears the buttons, so the camera doesn't reset when the overlay opens (RoadCraft).
            // That's for absolute axes: relative ones (most games, e.g. FFXV) are deltas, zeroing them stops the camera.
            DIPROPDWORD axis_mode = {{sizeof(DIPROPDWORD), sizeof(DIPROPHEADER), 0, DIPH_DEVICE}};
            if (SUCCEEDED(device->GetProperty(DIPROP_AXISMODE, &axis_mode.diph)) && axis_mode.dwData == DIPROPAXISMODE_REL)
            {
               auto* mouse_state = static_cast<DIMOUSESTATE*>(data);
               mouse_state->lX = 0;
               mouse_state->lY = 0;
               mouse_state->lZ = 0;
            }
         }
         break;
      case DI8DEVTYPE_KEYBOARD:
         std::memset(data, 0, data_size);
         break;
      }
   }
   return result;
}

// This may be called a lot, so it stays cheap
template <typename Device>
HRESULT STDMETHODCALLTYPE GetDeviceData(Device* device, DWORD object_data_size, LPDIDEVICEOBJECTDATA object_data, LPDWORD in_out, DWORD flags)
{
   using Function = HRESULT(STDMETHODCALLTYPE*)(Device*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
   const auto original = GetOriginal<Function>(device, GET_DEVICE_DATA_INDEX);
   if (!original)
      return DIERR_GENERIC;
   HRESULT result = original(device, object_data_size, object_data, in_out, flags);
   if (SUCCEEDED(result) && (flags & DIGDD_PEEK) == 0 && object_data && in_out && *in_out != 0 && IsBlocking())
   {
      const BYTE type = GetDeviceType(device);
      if (type == DI8DEVTYPE_MOUSE || type == DI8DEVTYPE_KEYBOARD)
      {
         *in_out = 0;
         result = DI_OK; // Overwrites a potential "DI_BUFFEROVERFLOW"
      }
   }
   return result;
}

template <typename DirectInput, typename Device>
HRESULT STDMETHODCALLTYPE CreateDevice(DirectInput* direct_input, REFGUID guid, Device** device, LPUNKNOWN outer)
{
   using Function = HRESULT(STDMETHODCALLTYPE*)(DirectInput*, REFGUID, Device**, LPUNKNOWN);
   const auto original = GetOriginal<Function>(direct_input, CREATE_DEVICE_INDEX);
   if (!original)
      return DIERR_GENERIC;
   const HRESULT result = original(direct_input, guid, device, outer);
   if (FAILED(result) || !device || !*device)
      return result;
   const BYTE type = guid == GUID_SYS_MOUSE ? DI8DEVTYPE_MOUSE : guid == GUID_SYS_KEYBOARD ? DI8DEVTYPE_KEYBOARD : 0;
   // Only the mouse and keyboard: other devices aren't all proxied by the Steam overlay, hooks could end up on both
   if (type == 0)
      return result;
   {
      std::unique_lock lock(com_mutex);
      device_types[*device] = type;
   }
   constexpr bool is_wide = std::is_same_v<Device, IDirectInputDevice8W>;
   HookMethod(*device, GET_DEVICE_STATE_INDEX, reinterpret_cast<void*>(&GetDeviceState<Device>), is_wide ? "IDirectInputDevice8W::GetDeviceState" : "IDirectInputDevice8A::GetDeviceState");
   HookMethod(*device, GET_DEVICE_DATA_INDEX, reinterpret_cast<void*>(&GetDeviceData<Device>), is_wide ? "IDirectInputDevice8W::GetDeviceData" : "IDirectInputDevice8A::GetDeviceData");
   return result;
}

HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer)
{
   const HRESULT result = original_direct_input8_create(instance, version, iid, out, outer);
   if (FAILED(result) || !out || !*out || Hooks::Scope::IsActive())
      return result;
   if (iid == IID_DIRECT_INPUT8_W)
      HookMethod(*out, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInput8W, IDirectInputDevice8W>), "IDirectInput8W::CreateDevice");
   else if (iid == IID_DIRECT_INPUT8_A)
      HookMethod(*out, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInput8A, IDirectInputDevice8A>), "IDirectInput8A::CreateDevice");
   return result;
}

template <auto& Original, typename Detour>
Hooks::Hook MakeHook(const wchar_t* module, const char* name, Detour detour)
{
   return Hooks::Hook{Hooks::ExportTarget{module, name}, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&Original)};
}
} // namespace

void Install()
{
   const Hooks::Hook hooks[] = {
      MakeHook<original_peek_message_a>(L"user32.dll", "PeekMessageA", &PeekMessageA),
      MakeHook<original_peek_message_w>(L"user32.dll", "PeekMessageW", &PeekMessageW),
      MakeHook<original_get_message_a>(L"user32.dll", "GetMessageA", &GetMessageA),
      MakeHook<original_get_message_w>(L"user32.dll", "GetMessageW", &GetMessageW),
      MakeHook<original_clip_cursor>(L"user32.dll", "ClipCursor", &ClipCursor),
      MakeHook<original_set_cursor_pos>(L"user32.dll", "SetCursorPos", &SetCursorPos),
      MakeHook<original_get_cursor_pos>(L"user32.dll", "GetCursorPos", &GetCursorPos),
      MakeHook<original_get_key_state>(L"user32.dll", "GetKeyState", &GetKeyState),
      MakeHook<original_get_async_key_state>(L"user32.dll", "GetAsyncKeyState", &GetAsyncKeyState),
      MakeHook<original_get_keyboard_state>(L"user32.dll", "GetKeyboardState", &GetKeyboardState),
      MakeHook<original_get_raw_input_buffer>(L"user32.dll", "GetRawInputBuffer", &GetRawInputBuffer),
      MakeHook<original_direct_input8_create>(L"dinput8.dll", "DirectInput8Create", &DirectInput8Create),
   };
   Hooks::Declare(hooks);
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
} // namespace InputHooks
} // namespace Overlay
} // namespace Luma
