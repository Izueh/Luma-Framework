#include "Handlers/Vulkan/VulkanHandlers.hpp"

#include <format>
#include <string>

#include "FrameState/FrameState.hpp"
#include "Game/GameRegistry.hpp"
#include "Handlers/Handlers.hpp"
#include "Hooks/HookManager.hpp"
#include "Log/Log.hpp"
#if DEVELOPMENT
#include "Mcp/McpServer.hpp"
#endif
#include "Resources/LumaCBuffers.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Shaders/Shaders.hpp"
#include "System/JobQueue.hpp"
#include "Trace/RuntimeStatus.hpp"
#include "UI/Vulkan/VulkanOverlay.hpp"
#include "Utils/Debug.hpp"

namespace Luma {
namespace Handlers {
namespace Vulkan {
void PostCreateDevice(VkDevice device, VkPhysicalDevice physical_device, const Hooks::Vulkan::InstanceDispatch* instance_dispatch)
{
   std::string details;
   if (instance_dispatch && instance_dispatch->get_physical_device_properties && physical_device)
   {
      VkPhysicalDeviceProperties props = {};
      instance_dispatch->get_physical_device_properties(physical_device, &props);
      details = std::format("{}, Vulkan {}.{}.{}", props.deviceName, VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
         VK_VERSION_PATCH(props.apiVersion));
   }
   HandlePostCreateDevice<Api::Vulkan>(
      device, details,
      [] { LUMA_UNIMPLEMENTED("Validation::Vulkan"); },
      [] {});
}

void OnDeviceDestroyed(VkDevice device)
{
   HandleOnDeviceDestroyed<Api::Vulkan>(
      device,
      [device] {
         UI::Vulkan::DetachFromDevice(device);
         Display::Swapchain::Vulkan::ForgetDevice(device);
      },
      [] {});
}

void PostCreateSwapchain(const Hooks::Vulkan::DeviceDispatch& dispatch, const Display::Swapchain::Vulkan::SwapchainInfo& info, bool resize)
{
   (void)dispatch;
   Display::Swapchain::Vulkan::TrackCreated(info);
   const std::string_view path_name = Display::Swapchain::Vulkan::GetName(info.path);
   Log::Info("Vulkan: {} swapchain {} {} ({}x{}, format {}, color space {}, {} images, window {}, dxgi {})", path_name,
      reinterpret_cast<const void*>(info.swapchain), resize ? "recreated" : "created", info.width, info.height, uint32_t(info.format),
      uint32_t(info.color_space), info.image_count, static_cast<void*>(info.window), info.dxgi_swapchain);

   const std::string api_label = info.path == Display::Swapchain::Vulkan::PresentationPath::VulkanOnDxgi ? "Vulkan (DXGI)" : "Vulkan";
   Trace::SetSwapchain({reinterpret_cast<uint64_t>(info.swapchain), api_label, info.width, info.height, uint32_t(info.format), info.image_count});

   FrameState::SetOutputResolution(info.device, {info.width, info.height});
   if (info.window)
   {
      MONITORINFO monitor = {.cbSize = sizeof(monitor)};
      GetMonitorInfoW(MonitorFromWindow(info.window, MONITOR_DEFAULTTONEAREST), &monitor);
      FrameState::SetSwapchainResolution(info.device, {info.width, info.height},
         {uint32_t(monitor.rcMonitor.right - monitor.rcMonitor.left), uint32_t(monitor.rcMonitor.bottom - monitor.rcMonitor.top)});
   }

   if (info.path == Display::Swapchain::Vulkan::PresentationPath::VulkanOnDxgi)
      LUMA_UNIMPLEMENTED("Display::Vulkan::DxgiInterop");
   else
      LUMA_UNIMPLEMENTED("Display::Vulkan::Swapchain");

   Game::Registry::Call(Game::Registry::GetVulkanEvents().on_init_swapchain, info.swapchain, info.device, resize);
}

void OnDestroySwapchain(VkSwapchainKHR swapchain)
{
   UI::Vulkan::DetachFromSwapchain(swapchain);
   Trace::RemoveSwapchain(reinterpret_cast<const void*>(swapchain));
   Display::Swapchain::Vulkan::TrackDestroyed(swapchain);
   Log::Info("Vulkan: swapchain {} destroyed", reinterpret_cast<const void*>(swapchain));
}

VkSemaphore PrePresent(const Hooks::Vulkan::DeviceDispatch& dispatch, VkQueue queue, VkSwapchainKHR swapchain, uint32_t image_index,
   uint32_t wait_semaphore_count, const VkSemaphore* wait_semaphores)
{
   const std::optional<Display::Swapchain::Vulkan::SwapchainInfo> info = Display::Swapchain::Vulkan::Find(swapchain);
   if (!info)
      return VK_NULL_HANDLE;

   Trace::RecordPresent();
   Settings::ApplyQueued();
#if DEVELOPMENT
   Mcp::RunPendingJobs();
#endif
   System::Jobs::RunFinished();
   Shaders::CountFrame(info->device);
   Game::Registry::Call(Game::Registry::GetVulkanEvents().on_present, swapchain, queue);
   FrameState::AdvanceFrame(info->device);

   VkSemaphore overlay_semaphore = UI::Vulkan::DrawOverlay(dispatch, *info, queue, image_index, wait_semaphore_count, wait_semaphores);

   Resources::LumaCBuffers::MarkSettingsDirty();
   return overlay_semaphore;
}
} // namespace Vulkan
} // namespace Handlers
} // namespace Luma
