#pragma once

#include <optional>
#include <string_view>
#include <utility>

#include "Common/Api.hpp"
#include "Frame/FrameState.hpp"
#include "Game/GameRegistry.hpp"
#include "Handlers/DXGI/DXGIHandlers.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "Shaders/Shaders.hpp"
#include "Shaders/ShaderStages.hpp"
#if DEVELOPMENT
#include "Trace/FrameCapture.hpp"
#include "Trace/ResourceReadback.hpp"
#include "Trace/CbufferTracker.hpp"
#endif
#include "UI/Overlay.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Handlers {
template <Api api, typename Device, typename StartValidationFn, typename CreateCoreFn>
void HandlePostCreateDevice(Device* device, std::string_view details, StartValidationFn&& start_validation, CreateCoreFn&& create_core_objects)
{
   start_validation();
   if (details.empty())
      Log::Info("{}: device {} created ({} live)", GetName(api), static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   else
      Log::Info("{}: device {} created ({}, {} live)", GetName(api), static_cast<void*>(device), details, Hooks::DeviceProxy::GetCount());
   Game::Registry::CreateDeviceData(device);
   Frame::CreateDeviceState(device);
   Shaders::SetApi(api);
   create_core_objects();
   Game::Registry::Call(Game::Registry::GetEvents<api>().on_init_device, device);
}

template <Api api, typename Device, typename ReleaseCoreFn, typename StopValidationFn>
void HandleOnDeviceDestroyed(Device* device, ReleaseCoreFn&& release_core_objects, StopValidationFn&& stop_validation)
{
   Log::Info("{}: device {} released by the game ({} live)", GetName(api), static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   Game::Registry::Call(Game::Registry::GetEvents<api>().on_destroy_device, device);
   UI::DetachFromDevice(device);
   ForgetSwapchains(device);
   release_core_objects();
   stop_validation();
}

#if DEVELOPMENT
inline Trace::Capture::Outcome GetOutcome(Game::DrawResult result, bool skipped)
{
   if (skipped)
      return Trace::Capture::Outcome::SkippedByTrace;
   switch (result)
   {
   case Game::DrawResult::Skip: return Trace::Capture::Outcome::Skipped;
   case Game::DrawResult::Replaced: return Trace::Capture::Outcome::Replaced;
   default: return Trace::Capture::Outcome::Ran;
   }
}

template <bool IsDispatch, typename BeginCaptureFn, typename TrackCallFn, typename ReadAfterCallFn, typename EndCaptureFn, typename RunDrawFn>
bool HandleDrawOrDispatch(BeginCaptureFn&& begin_capture, TrackCallFn&& track_call, ReadAfterCallFn&& read_after_call, EndCaptureFn&& end_capture,
   RunDrawFn&& run_draw)
{
   constexpr Shaders::StageSet stages = IsDispatch ? Shaders::COMPUTE_STAGES : Shaders::GRAPHICS_STAGES;
   auto recording = Trace::Capture::IsCapturing() ? std::optional(begin_capture()) : std::nullopt;
   if (Trace::Track::IsActive())
      track_call(stages);
   Game::DrawResult result = Game::DrawResult::None;
   bool skipped = false;
   bool handled = run_draw(stages, result, skipped);
   if (Trace::Readback::IsArmed())
      handled = read_after_call(stages, handled);
   if (recording)
      end_capture(std::move(*recording), GetOutcome(result, skipped));
   return handled;
}
#endif
} // namespace Handlers
} // namespace Luma
