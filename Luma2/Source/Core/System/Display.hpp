#pragma once

#include <Windows.h>

struct IDXGIOutput;

// The monitor a window is on: its HDR state and luminance. No graphics API objects but DXGI's output, the one
// place the peak is known.
namespace Luma {
namespace System {
namespace Display {
// Windows' SDR reference white
inline constexpr float SRGB_WHITE_NITS = 80.f;
// BT.2408's reference white
inline constexpr float DEFAULT_PAPER_WHITE_NITS = 203.f;
// When the display doesn't say
inline constexpr float DEFAULT_PEAK_NITS = 1000.f;

struct Info
{
   bool hdr_supported = false;
   bool hdr_enabled = false; // Implies supported
   float peak_nits = DEFAULT_PEAK_NITS; // The display's, as Windows' calibration has it
   float min_nits = 0.f;
   float max_full_frame_nits = DEFAULT_PEAK_NITS;
   float sdr_white_nits = SRGB_WHITE_NITS; // Windows' "SDR content brightness" in HDR
   HMONITOR monitor = nullptr;
};

// The display `window` is on (its biggest part), the primary for none. `output` (the swapchain's containing one, optional) gives the
// luminance and is the fallback for the HDR state on Windows before 11 24H2.
Info Query(HWND window, IDXGIOutput* output = nullptr);
// Any display's configuration changed since the last call (Windows' HDR switch, a mode, a monitor): DXGI's own sign (a factory that
// isn't current), cheap enough for every present, where the queries above aren't. The first call is true.
bool HasChanged();
// Turns Windows HDR on or off for the display the window is on. True if it now is as asked.
bool SetHdrEnabled(HWND window, bool enabled);
} // namespace Display
} // namespace System
} // namespace Luma
