#include "overlay/input.hpp"

#include <atomic>
#include <format>
#include <iterator>
#include <mutex>

#include "overlay/overlay.hpp"
#include "utils/config.hpp"
#include "utils/log.hpp"
#include "utils/strings.hpp"

namespace Luma {
namespace Overlay {
namespace Input {
namespace {
constexpr const char* CONFIG_SECTION = "Luma";
constexpr const char* TOGGLE_KEY_CONFIG = "OverlayKey";

// Never held while calling into the overlay
std::mutex mutex;
HWND attached_window = nullptr;

std::atomic<uint32_t> toggle_key = VK_INSERT;
std::atomic<bool> capturing = false;
std::atomic<uint32_t> captured_key = 0;

bool IsInputMessage(UINT message)
{
   return (message >= WM_KEYFIRST && message <= WM_KEYLAST) || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_INPUT;
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
   if (!IsOpen())
      return false;
   OnWindowMessage(window, message, wparam, lparam);
   return IsInputMessage(message);
}

void Attach(HWND window)
{
   uint32_t key = VK_INSERT;
   if (Config::Get(CONFIG_SECTION, TOGGLE_KEY_CONFIG, key) && key > 0 && key < 255)
      toggle_key = key;
   {
      std::scoped_lock lock(mutex);
      attached_window = window;
   }
   Utils::Log::Info("Overlay: {} toggles the overlay", GetKeyName(toggle_key));
}

void Detach(HWND window)
{
   std::scoped_lock lock(mutex);
   if (attached_window == window)
      attached_window = nullptr;
}

uint32_t GetToggleKey()
{
   return toggle_key;
}

void CaptureToggleKey()
{
   capturing = true;
}

bool IsCapturingToggleKey()
{
   return capturing;
}

void ApplyCapturedKey()
{
   if (const uint32_t key = captured_key.exchange(0))
   {
      toggle_key = key;
      Config::SetValue(CONFIG_SECTION, TOGGLE_KEY_CONFIG, int64_t(key));
      Config::Save();
   }
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
} // namespace Overlay
} // namespace Luma
