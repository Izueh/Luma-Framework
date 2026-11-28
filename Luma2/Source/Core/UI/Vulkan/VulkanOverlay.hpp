#pragma once

#include <cstdint>

#include <Windows.h>
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include "Display/Vulkan/VulkanSwapchain.hpp"
#include "Hooks/Vulkan/VulkanDispatch.hpp"

namespace Luma {
namespace UI {
namespace Vulkan {
// Attaches ImGui Vulkan renderer to the first eligible Vulkan swapchain and draws the overlay before vkQueuePresentKHR.
// Returns the semaphore that the present should wait on (either the newly signaled overlay semaphore, or VK_NULL_HANDLE if nothing was drawn).
VkSemaphore DrawOverlay(const Hooks::Vulkan::DeviceDispatch& dispatch, const Display::Swapchain::Vulkan::SwapchainInfo& swapchain, VkQueue queue,
   uint32_t image_index, uint32_t wait_semaphore_count, const VkSemaphore* wait_semaphores);

void DetachFromSwapchain(VkSwapchainKHR swapchain);
void DetachFromDevice(VkDevice device);
VkSwapchainKHR GetSwapchain();
} // namespace Vulkan
} // namespace UI
} // namespace Luma
