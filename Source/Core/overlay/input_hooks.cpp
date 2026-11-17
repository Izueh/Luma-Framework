#include "overlay/input_hooks.hpp"

#include "hooks/scope.hpp"
#include "overlay/input_windows.hpp"
#include "overlay/overlay.hpp"
#include "overlay/windows/dinput.hpp"
#include "overlay/windows/dinput8.hpp"

namespace Luma {
namespace Overlay {
namespace InputHooks {
void Install()
{
   InputWindows::Install();
   // Each of these waits for its dll to load if the game hasn't yet
   DInput::Install();
   DInput8::Install();
}

void OnOpenChanged(bool open)
{
   InputWindows::OnOpenChanged(open);
}

bool IsBlocking()
{
   return IsOpen() && !Hooks::Scope::IsActive();
}
} // namespace InputHooks
} // namespace Overlay
} // namespace Luma
