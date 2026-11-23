#pragma once

#include "System/MonitorDisplay.hpp"

// LumaSettings' display mode and white levels, from the output mode, the display, the Display settings and the game's HDR switch.
namespace Luma {
namespace Display {
namespace DisplayMode {
// Registers the Display white-level settings before Game::Init
void Register();
// Presenting thread, when the output mode or the display changes: SDR output is SDR at 80 nits; HDR uses the
// Display settings, the peak the display's unless the user set one
void ApplyOutputMode(bool hdr, const System::Display::Info& display);
// Presenting thread: swapchain is in Native mode (Luma does not manage HDR output or white levels)
void SetUnmanaged();
// Presenting thread: re-syncs LumaSettings white levels after UI draw type or settings change
void Refresh();
// Any thread: the game's image is HDR or SDR now (its own HDR option). Applied at the next present.
void SetGameHdr(bool hdr);
// Presenting thread, at every present: applies any change from SetGameHdr
void Update();
// Whether Luma is currently driving an HDR display mode
bool IsHdr();
} // namespace DisplayMode
} // namespace Display
} // namespace Luma
