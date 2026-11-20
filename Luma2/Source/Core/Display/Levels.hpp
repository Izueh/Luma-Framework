#pragma once

#include "System/Display.hpp"

// What the image is made for (Guidelines.md section 12): LumaSettings' display mode and white levels, from the output mode, the display,
// the user's Display settings and the game's own HDR switch.
namespace Luma {
namespace Display {
namespace Levels {
// Its settings, before the game's Init
void Register();
// Presenting thread, when the output mode or the display changes: SDR output is SDR at 80 nits; HDR uses the
// Display settings, the peak the display's unless the user set one
void ApplyOutputMode(bool hdr, const System::Display::Info& display);
// Any thread: the game's image is HDR or SDR now (its own HDR option). Applied at the next present.
void SetGameHdr(bool hdr);
// Presenting thread, at every present: a change of the game's
void Update();
} // namespace Levels
} // namespace Display
} // namespace Luma
