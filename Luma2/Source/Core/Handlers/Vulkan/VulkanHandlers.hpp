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
namespace Handlers {
namespace Vulkan {
void PostCreateDevice(VkDevice device, VkPhysicalDevice physical_device, const Hooks::Vulkan::InstanceDispatch* instance_dispatch);
void OnDeviceDestroyed(VkDevice device);

void PostCreateSwapchain(const Hooks::Vulkan::DeviceDispatch& dispatch, const Display::Swapchain::Vulkan::SwapchainInfo& info, bool resize);
void OnDestroySwapchain(VkSwapchainKHR swapchain);

// Runs per-frame Core work, Game::Vulkan::Events::on_present, and minimal Vulkan overlay.
// Returns a replacement wait semaphore if the overlay submitted work to `queue`, else VK_NULL_HANDLE.
VkSemaphore PrePresent(const Hooks::Vulkan::DeviceDispatch& dispatch, VkQueue queue, VkSwapchainKHR swapchain, uint32_t image_index,
   uint32_t wait_semaphore_count, const VkSemaphore* wait_semaphores);
} // namespace Vulkan
} // namespace Handlers
} // namespace Luma
