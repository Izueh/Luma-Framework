#pragma once

#include <cstdint>
#include <string>

namespace Luma {
namespace ReShade {
enum class LoadState : uint8_t
{
   NotLoaded, // Not next to Luma, turned off, or declined (the reason is in DescribeLoadState)
   LoadedByLuma, // After Luma's loader notification, so its hooks go in after Luma's
   LoadedByAnotherModule, // Another module exports "ReShadeVersion" (ReShade as dxgi.dll, or injected by something else): not loaded twice
};

// Its settings, before the saved values load
void Register();
// From Loader::Attach, after Luma's loader notification is registered and the loaded dlls were seen, never from the notification
void Load();
// The presenting thread, after Hooks::VerifyOnPresent. Once: finds a ReShade loaded after Luma, logs whether its hooks sit above Luma's.
void CheckHookOrderAtFirstPresent();
// At a swapchain's creation, before ReShade wraps it: a ReShade loaded after Luma, found while none is known
void FindReShadeLoadedAfterLuma();
// Before Luma's overlay draws: whether the present work ran at ReShade's present event (logged once each way)
void RecordWherePresentWorkRan(bool at_event);
// ReShade's overlay is open: Luma's stays closed, one overlay at a time
bool IsOverlayOpen();

LoadState GetLoadState();
// For the Info tab and luma_status: whether ReShade is loaded and by whom, or why it isn't
std::string DescribeLoadState();
std::string DescribeBridge();
// Whether ReShade's own reads of the Mirror swapchain get the real one, and the output switches through its wrapper
std::string DescribeMirrorSwapchainView();
} // namespace ReShade
} // namespace Luma
