#pragma once

#include <cstdint>
#include <memory>

#include <Windows.h>
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

namespace Luma {
namespace Hooks {
namespace Vulkan {
// Vulkan loader dispatch key: the first pointer-sized word of every dispatchable handle
inline const void* GetDispatchKey(const void* handle)
{
   return handle ? *reinterpret_cast<void* const*>(handle) : nullptr;
}

struct InstanceDispatch
{
   VkInstance instance = VK_NULL_HANDLE;
   PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
   PFN_vkDestroyInstance destroy_instance = nullptr;
   PFN_vkCreateDevice create_device = nullptr;
   PFN_vkCreateWin32SurfaceKHR create_win32_surface = nullptr;
   PFN_vkDestroySurfaceKHR destroy_surface = nullptr;
   PFN_vkGetPhysicalDeviceProperties get_physical_device_properties = nullptr;
   PFN_vkGetPhysicalDeviceQueueFamilyProperties get_physical_device_queue_family_properties = nullptr;
   PFN_vkGetPhysicalDeviceMemoryProperties get_physical_device_memory_properties = nullptr;
};

struct DeviceDispatch
{
   VkDevice device = VK_NULL_HANDLE;
   VkPhysicalDevice physical_device = VK_NULL_HANDLE;
   VkInstance instance = VK_NULL_HANDLE;
   PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
   PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
   PFN_vkDestroyDevice destroy_device = nullptr;
   PFN_vkGetDeviceQueue get_device_queue = nullptr;
   PFN_vkGetDeviceQueue2 get_device_queue2 = nullptr;
   PFN_vkCreateSwapchainKHR create_swapchain = nullptr;
   PFN_vkDestroySwapchainKHR destroy_swapchain = nullptr;
   PFN_vkGetSwapchainImagesKHR get_swapchain_images = nullptr;
   PFN_vkQueuePresentKHR queue_present = nullptr;
   PFN_vkQueueSubmit queue_submit = nullptr;
   PFN_vkQueueWaitIdle queue_wait_idle = nullptr;
   PFN_vkDeviceWaitIdle device_wait_idle = nullptr;
   uint32_t graphics_queue_family = UINT32_MAX;
   VkQueue graphics_queue = VK_NULL_HANDLE;
};

void RegisterInstance(VkInstance instance, PFN_vkGetInstanceProcAddr next_gipa);
void UnregisterInstance(VkInstance instance);
std::shared_ptr<InstanceDispatch> FindInstance(const void* dispatchable_handle);

void RegisterDevice(VkDevice device, VkPhysicalDevice physical_device, VkInstance instance, PFN_vkGetInstanceProcAddr next_gipa,
   PFN_vkGetDeviceProcAddr next_gdpa, uint32_t graphics_queue_family);
void UnregisterDevice(VkDevice device);
std::shared_ptr<DeviceDispatch> FindDevice(const void* dispatchable_handle);
void RecordDeviceQueue(VkDevice device, uint32_t queue_family_index, VkQueue queue);
uint32_t FindQueueFamily(VkQueue queue);

void RegisterSurface(VkSurfaceKHR surface, HWND hwnd);
void UnregisterSurface(VkSurfaceKHR surface);
HWND FindSurfaceWindow(VkSurfaceKHR surface);

uint32_t GetDeviceCount();
} // namespace Vulkan
} // namespace Hooks
} // namespace Luma
