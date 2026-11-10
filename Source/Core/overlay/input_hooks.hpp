#pragma once

namespace Luma {
namespace Overlay {
namespace InputHooks {
// Declares the hooks that keep the game from seeing input while the overlay is open (window messages, the cursor, key
// states, raw input and DirectInput). DirectInput's are installed once the game loads "dinput8.dll".
void Install();

// Releases the game's cursor clip while the overlay is open, and restores it afterwards
void OnOpenChanged(bool open);
} // namespace InputHooks
} // namespace Overlay
} // namespace Luma
