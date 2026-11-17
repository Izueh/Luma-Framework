/*
 * The handling of raw input and of key releases is adapted from ReShade (https://github.com/crosire/reshade):
 *   source/input.cpp, Copyright (C) 2014 Patrick Mours ("input::handle_window_message")
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "UI/Input.hpp"

#include <array>
#include <atomic>
#include <format>
#include <iterator>
#include <mutex>
#include <variant>

#include "Hooks/InputHooks.hpp"
#include "Hooks/Scope.hpp"
#include "Luma.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "UI/Overlay.hpp"
#include "Utils/Log.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace UI {
namespace Input {

namespace {
constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::OVERLAY_KEY.key,
      .label = "Overlay key",
      .tooltip = "Opens and closes this overlay",
      .category = "Overlay",
      .default_value = Settings::Key{VK_INSERT},
   },
   Settings::Setting{
      .key = Settings::Core::ENABLED_KEY.key,
      .label = "On/off key",
      .tooltip = "Switches Luma on and off, to compare with the game's own image",
      .category = "Luma",
      .default_value = Settings::Key{VK_F9},
   },
};

// Never held while calling into the overlay
std::mutex mutex;
HWND attached_window = nullptr;

std::atomic<uint32_t> toggle_key = VK_INSERT;
std::atomic<bool> capturing = false;
std::atomic<bool> capturing_toggle_key = false;
std::atomic<uint32_t> captured_key = 0;

bool IsInputMessage(UINT message)
{
   return (message >= WM_KEYFIRST && message <= WM_KEYLAST) || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_INPUT;
}

void OnSettingChanged(const Settings::Setting& setting, const Settings::Value& value)
{
   if (setting.key != Settings::Core::OVERLAY_KEY.key)
      return;
   if (const Settings::Key* key = std::get_if<Settings::Key>(&value); key && key->code > 0 && key->code < 255)
      toggle_key = key->code;
}

// The keys the overlay took the press of: the game must not get their release (and repeats) either
std::atomic<bool> blocked_keys[256] = {};
// Raw input has no repeat flag
std::atomic<bool> raw_keys_down[256] = {};

// The overlay's keys, and ImGui's input. True if the game must not see the message.
bool HandleWindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
   const bool key_down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
   const bool key_up = message == WM_KEYUP || message == WM_SYSKEYUP;
   std::atomic<bool>* blocked = (key_down || key_up) && wparam < std::size(blocked_keys) ? &blocked_keys[wparam] : nullptr;

   // Bit 30: the key was already down (auto repeat)
   const bool key_pressed = key_down && !(lparam & (LPARAM(1) << 30));
   if (key_pressed && capturing.exchange(false))
   {
      if (wparam != VK_ESCAPE)
         captured_key = uint32_t(wparam);
      if (blocked)
         *blocked = true;
      return true;
   }
   if (key_pressed && wparam == toggle_key)
   {
      SetOpen(!IsOpen());
      if (blocked)
         *blocked = true;
      return true;
   }
   // The window's thread may not be the presenting one: applied at the next present
   if (key_pressed && wparam == Settings::Get(Settings::Core::ENABLED_KEY).code)
   {
      Settings::Queue(Settings::Core::ENABLED, !IsEnabled());
      if (blocked)
         *blocked = true;
      return true;
   }
   if (!IsOpen())
   {
      if (blocked && key_up && blocked->exchange(false))
         return true; // The game never saw it go down
      if (blocked && key_down)
      {
         // The repeats of a key that went down while the overlay was open. A new press is the game's, whatever released the last one.
         if (!key_pressed)
            return *blocked;
         *blocked = false;
      }
      return false;
   }

   bool block = IsInputMessage(message);
   if (blocked && key_down)
      *blocked = true;
   // Do not block key up messages if the key down one was not blocked previously (so the key does not get stuck for the game)
   if (blocked && key_up && !blocked->exchange(false))
      block = false;
   OnWindowMessage(window, message, wparam, lparam);
   return block;
}

// "WM_INPUT" sent to `target`. The mouse's clicks and wheel, and the keyboard's keys, only reach the overlay from here when the game
// registered the device without legacy messages ("RIDEV_NOLEGACY", e.g. Need for Speed 2016): otherwise the legacy messages give them,
// and each click would count twice.
bool HandleRawInput(HWND window, HWND target, WPARAM wparam, LPARAM lparam)
{
   const bool open = IsOpen();
   // Input sink messages come while the window isn't in the foreground
   if (GET_RAWINPUT_CODE_WPARAM(wparam) != RIM_INPUT)
      return open;
   const uint32_t flags = Hooks::Input::GetRawInputFlags(target);
   if (!open && !(flags & Hooks::Input::RAW_INPUT_NO_LEGACY_KEYBOARD))
      return false;

   RAWINPUT raw = {};
   UINT size = sizeof(raw);
   if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size, sizeof(raw.header)) == UINT(-1))
      return open;

   switch (raw.header.dwType)
   {
   case RIM_TYPEMOUSE:
      if (open && (flags & Hooks::Input::RAW_INPUT_NO_LEGACY_MOUSE))
         OnRawMouse(raw.data.mouse.usButtonFlags, raw.data.mouse.usButtonData);
      return open;
   case RIM_TYPEKEYBOARD:
   {
      const RAWKEYBOARD& keyboard = raw.data.keyboard;
      if (keyboard.VKey == 0 || keyboard.VKey >= std::size(blocked_keys))
         return false; // Ignore messages without a valid key code
      const bool is_break = (keyboard.Flags & RI_KEY_BREAK) != 0;
      if (!(flags & Hooks::Input::RAW_INPUT_NO_LEGACY_KEYBOARD))
      {
         // The legacy messages give the key, but a release must not reach the game if the press was blocked
         return open && !(is_break && !blocked_keys[keyboard.VKey]);
      }

      // As the legacy message the window doesn't get
      const bool repeat = !is_break && raw_keys_down[keyboard.VKey].exchange(true);
      if (is_break)
         raw_keys_down[keyboard.VKey] = false;
      LPARAM key_lparam = 1 | (LPARAM(keyboard.MakeCode) << 16) | ((keyboard.Flags & RI_KEY_E0) ? LPARAM(1) << 24 : 0);
      if (is_break)
         key_lparam |= (LPARAM(1) << 30) | (LPARAM(1) << 31);
      else if (repeat)
         key_lparam |= LPARAM(1) << 30;
      const bool block = HandleWindowMessage(window, is_break ? WM_KEYUP : WM_KEYDOWN, keyboard.VKey, key_lparam);

      // No "WM_CHAR" messages are sent if legacy keyboard messages are disabled, so the text is made here
      // The 0x2 flag (Windows 10 version 1607) keeps the keyboard state (dead keys) from being changed
      if (!is_break && IsOpen())
      {
         Hooks::Scope::Guard scope; // The key state reads must not be blocked
         BYTE key_state[256] = {};
         WCHAR text[3] = {};
         const int count = GetKeyboardState(key_state) ? ToUnicode(keyboard.VKey, keyboard.MakeCode, key_state, text, 2, 0x2) : 0;
         for (int i = 0; i < count; i++)
            OnWindowMessage(window, WM_CHAR, text[i], 0);
      }
      return block;
   }
   }
   return open;
}
} // namespace

bool HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
   HWND window;
   {
      std::scoped_lock lock(mutex);
      window = attached_window;
   }
   if (!window || !hwnd)
      return false;
   // Some applications handle input in a child window of the render window, or the render window is a child of the one getting
   // the messages. Raw input is sent to the window it was registered for.
   if (hwnd != window && !IsChild(window, hwnd) && !IsChild(hwnd, window) && !(message == WM_INPUT && Hooks::Input::IsRawInputWindow(hwnd)))
      return false;

   if (message == WM_INPUT)
      return HandleRawInput(window, hwnd, wparam, lparam);
   return HandleWindowMessage(window, message, wparam, lparam);
}

void Initialize()
{
   Settings::Register(SETTINGS, Settings::Kind::Luma);
   Settings::AddChangeListener(&OnSettingChanged);
   Hooks::Input::SetMessageHandler(&HandleMessage);
   toggle_key = Settings::Get(Settings::Core::OVERLAY_KEY).code;
}

void Attach(HWND window)
{
   {
      std::scoped_lock lock(mutex);
      attached_window = window;
   }
   Utils::Log::Info("UI: {} toggles the overlay", GetKeyName(toggle_key));
}

void Detach(HWND window)
{
   std::scoped_lock lock(mutex);
   if (attached_window == window)
      attached_window = nullptr;
}

void CaptureKey()
{
   capturing_toggle_key = false;
   capturing = true;
}

void CaptureToggleKey()
{
   capturing_toggle_key = true;
   capturing = true;
}

bool IsCapturingKey()
{
   return capturing;
}

uint32_t TakeCapturedKey()
{
   return capturing ? 0 : captured_key.exchange(0);
}

void ApplyCapturedKey()
{
   if (!capturing_toggle_key || capturing)
      return;
   capturing_toggle_key = false;
   if (const uint32_t key = captured_key.exchange(0))
      Settings::Set(Settings::Core::OVERLAY_KEY, Settings::Key{key});
}

std::string GetKeyName(uint32_t virtual_key)
{
   // Extended keys share scan codes with the numeric keypad unless flagged
   LONG scan_code = LONG(MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC)) << 16;
   switch (virtual_key)
   {
   case VK_INSERT:
   case VK_DELETE:
   case VK_HOME:
   case VK_END:
   case VK_PRIOR:
   case VK_NEXT:
   case VK_LEFT:
   case VK_RIGHT:
   case VK_UP:
   case VK_DOWN:
   case VK_DIVIDE:
   case VK_NUMLOCK:
      scan_code |= 1 << 24;
      break;
   }
   wchar_t name[64] = {};
   if (GetKeyNameTextW(scan_code, name, int(std::size(name))) > 0)
      return Utils::Strings::ToUtf8(name);
   return std::format("Key 0x{:02X}", virtual_key);
}
} // namespace Input
} // namespace UI
} // namespace Luma
