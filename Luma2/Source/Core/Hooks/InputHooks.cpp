#include "Hooks/InputHooks.hpp"

#include <atomic>

#include "Hooks/DInput/DInput7InputHooks.hpp"
#include "Hooks/DInput/DInput8InputHooks.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/User32/User32InputHooks.hpp"

namespace Luma {
namespace Hooks {
namespace Input {
namespace {
std::atomic<bool> open = false;
std::atomic<MessageHandler> message_handler = nullptr;
} // namespace

void Install()
{
   User32::Install();
   // Each of these waits for its dll to load if the game hasn't yet
   DInput7::Install();
   DInput8::Install();
}

void OnOpenChanged(bool value)
{
   open = value;
   User32::OnOpenChanged(value);
}

bool IsOverlayOpen()
{
   return open;
}

bool IsBlocking()
{
   return open && !Scope::IsActive();
}

void SetMessageHandler(MessageHandler handler)
{
   message_handler = handler;
}

bool HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
   const MessageHandler handler = message_handler;
   return handler && handler(hwnd, message, wparam, lparam);
}

bool IsRawInputWindow(HWND window)
{
   return User32::IsRawInputWindow(window);
}

uint32_t GetRawInputFlags(HWND window)
{
   return User32::GetRawInputFlags(window);
}
} // namespace Input
} // namespace Hooks
} // namespace Luma
