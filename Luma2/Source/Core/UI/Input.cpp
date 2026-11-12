#include "UI/Input.hpp"

#include <array>
#include <atomic>
#include <format>
#include <mutex>
#include <unordered_map>
#include <variant>

#include "Luma.hpp"
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
      .key = TOGGLE_KEY_SETTING,
      .label = "Overlay key",
      .tooltip = "Opens and closes this overlay",
      .category = "Overlay",
      .default_value = Settings::Key{VK_INSERT},
   },
   Settings::Setting{
      .key = ENABLED_KEY_SETTING,
      .label = "On/off key",
      .tooltip = "Switches Luma on and off, to compare with the game's own image",
      .category = "Luma",
      .default_value = Settings::Key{VK_F9},
   },
};

// Never held while calling into the overlay or a window procedure
std::mutex mutex;
HWND attached_window = nullptr;
// Kept for a window Luma can't unsubclass, so its procedure keeps passing messages through
std::unordered_map<HWND, WNDPROC> original_procedures;

std::atomic<uint32_t> toggle_key = VK_INSERT;
std::atomic<bool> capturing = false;
std::atomic<bool> capturing_toggle_key = false;
std::atomic<uint32_t> captured_key = 0;

bool IsInputMessage(UINT message)
{
   return (message >= WM_KEYFIRST && message <= WM_KEYLAST) || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_INPUT;
}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
   WNDPROC original = nullptr;
   {
      std::scoped_lock lock(mutex);
      if (const auto it = original_procedures.find(hwnd); it != original_procedures.end())
         original = it->second;
   }
   // WM_INPUT still needs DefWindowProc to free its data
   if (HandleMessage(hwnd, message, wparam, lparam))
      return message == WM_INPUT ? DefWindowProcW(hwnd, message, wparam, lparam) : 0;
   return original ? CallWindowProcW(original, hwnd, message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

void OnSettingChanged(const Settings::Setting& setting, const Settings::Value& value)
{
   if (setting.key != TOGGLE_KEY_SETTING)
      return;
   if (const Settings::Key* key = std::get_if<Settings::Key>(&value); key && key->code > 0 && key->code < 255)
      toggle_key = key->code;
}
} // namespace

bool HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
   HWND window;
   {
      std::scoped_lock lock(mutex);
      window = attached_window;
   }
   if (!window || !hwnd || (hwnd != window && !IsChild(window, hwnd)))
      return false;

   // Bit 30: the key was already down (auto repeat)
   const bool key_pressed = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(lparam & (LPARAM(1) << 30));
   if (key_pressed && capturing.exchange(false))
   {
      if (wparam != VK_ESCAPE)
         captured_key = uint32_t(wparam);
      return true;
   }
   if (key_pressed && wparam == toggle_key)
   {
      SetOpen(!IsOpen());
      return true;
   }
   // The window's thread may not be the presenting one: applied at the next present
   if (key_pressed && wparam == Settings::Get<Settings::Key>(ENABLED_KEY_SETTING).code)
   {
      Settings::Queue(ENABLED_SETTING, !IsEnabled());
      return true;
   }
   if (!IsOpen())
      return false;
   OnWindowMessage(window, message, wparam, lparam);
   return IsInputMessage(message);
}

void Initialize()
{
   Settings::Register(SETTINGS);
   Settings::AddChangeListener(&OnSettingChanged);
   toggle_key = Settings::Get<Settings::Key>(TOGGLE_KEY_SETTING).code;
}

void Attach(HWND window)
{
   {
      std::scoped_lock lock(mutex);
      if (GetWindowLongPtrW(window, GWLP_WNDPROC) != reinterpret_cast<LONG_PTR>(&WindowProcedure))
         original_procedures[window] = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProcedure)));
      attached_window = window;
   }
   Utils::Log::Info("UI: {} toggles the overlay", GetKeyName(toggle_key));
}

void Detach(HWND window)
{
   std::scoped_lock lock(mutex);
   attached_window = nullptr;
   if (GetWindowLongPtrW(window, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(&WindowProcedure))
   {
      SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_procedures[window]));
      original_procedures.erase(window);
   }
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
      Settings::Set(TOGGLE_KEY_SETTING, Settings::Key{key});
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
