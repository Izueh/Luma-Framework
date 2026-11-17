#pragma once

namespace Luma {
namespace Overlay {
namespace InputHooks {
// Declares the hooks that keep the game from seeing input while the overlay is open: user32's window messages, cursor,
// key states and raw input, and DirectInput (1 to 8). The DirectInput ones install once the game loads their dll.
void Install();

// Releases the game's cursor clip while the overlay is open, and restores it afterwards
void OnOpenChanged(bool open);

// The overlay is open and the call is the game's: Luma's own calls (inside a scope, e.g. ImGui's backend) are never blocked
bool IsBlocking();
} // namespace InputHooks
} // namespace Overlay
} // namespace Luma
