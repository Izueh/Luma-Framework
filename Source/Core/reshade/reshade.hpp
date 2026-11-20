#pragma once

#include <cstdint>
#include <string>

// ReShade as an optional guest: Luma loads "ReShade64.dll"/"ReShade32.dll" from its own folder so ReShade runs injected, on top of Luma.
// No ReShade header or file is used, it's only found by its file name and "ReShadeVersion" export.
namespace Luma {
namespace ReShade {
enum class Arrangement : uint8_t
{
   Absent, // Neither loaded nor next to Luma
   Disabled, // Next to Luma, the setting turned the load off
   InjectedByLuma, // Loaded by Luma, after Luma's loader notification: its hooks go in after Luma's
   AlreadyLoaded, // Another module exports "ReShadeVersion" (ReShade as dxgi.dll, or injected by something else): not loaded twice
   LoadFailed, // Next to Luma, LoadLibrary refused it (its DllMain failed, another architecture...)
};

// From Loader::Attach, after Luma's loader notification is registered and the loaded dlls were seen. Not from the notification itself.
void Load();
// The presenting thread, after Hooks::VerifyOnPresent. Once: finds a ReShade loaded after Luma, logs whether its hooks sit above Luma's
void OnPresent();

// The bridge: Load (or the first present, for a ReShade found then) registers Luma as a ReShade add-on, so Luma's present work runs at
// ReShade's "present" event, before its effects. Off with "[ReShade] Bridge = false".
// Before Luma's overlay draws: `after_third_party_work` when PrePresent ran at ReShade's present event (logged once each way)
void OnPresentOverlay(bool after_third_party_work);
// ReShade's overlay is open: Luma's stays closed, one overlay at a time
bool IsOverlayOpen();

Arrangement GetArrangement();
// One line for the Info tab
std::string Describe();
std::string DescribeBridge();
} // namespace ReShade
} // namespace Luma
