#pragma once

#include <string>

#include <Windows.h>

struct IDXGIOutput;

namespace Luma {
namespace System {
namespace Display {
// Windows' SDR reference white
inline constexpr float SRGB_WHITE_NITS = 80.f;
// BT.2408's reference white
inline constexpr float DEFAULT_PAPER_WHITE_NITS = 203.f;
// When the display doesn't say
inline constexpr float DEFAULT_PEAK_NITS = 1000.f;
// Minimum and maximum HDR peak luminance slider range
inline constexpr float MIN_PEAK_NITS = 400.f;
inline constexpr float MAX_PEAK_NITS = 10000.f;
// Maximum SDR reference white level accepted by Windows 11
inline constexpr float MAX_OS_SDR_WHITE_NITS = 480.f;

struct Info
{
   bool hdr_supported = false;
   bool hdr_enabled = false; // Implies supported
   float peak_nits = DEFAULT_PEAK_NITS; // The display's, as Windows' calibration or HDR10+ Gaming has it
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
// Sets Windows' "SDR content brightness" reference white level (80 to 480 nits) for the display the window is on.
bool SetSdrWhiteLevel(HWND window, float nits);
// Any thread
std::string GetAdapterName(LUID luid);
} // namespace Display
} // namespace System
} // namespace Luma
