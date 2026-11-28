#include "Hooks/Vulkan/VulkanDispatch.hpp"

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace Luma {
namespace Hooks {
namespace Vulkan {
namespace {
std::shared_mutex instance_mutex;
std::unordered_map<const void*, std::shared_ptr<InstanceDispatch>> instances;

std::shared_mutex device_mutex;
std::unordered_map<const void*, std::shared_ptr<DeviceDispatch>> devices;
std::atomic<uint32_t> live_device_count = 0;

std::shared_mutex surface_mutex;
std::unordered_map<uint64_t, HWND> surfaces;

std::shared_mutex queue_mutex;
std::unordered_map<VkQueue, uint32_t> queue_families;

template <typename Fn>
Fn LoadInstanceProc(VkInstance instance, PFN_vkGetInstanceProcAddr gipa, const char* name)
{
   return gipa ? reinterpret_cast<Fn>(gipa(instance, name)) : nullptr;
}

template <typename Fn>
Fn LoadDeviceProc(VkDevice device, PFN_vkGetDeviceProcAddr gdpa, const char* name)
{
   return gdpa ? reinterpret_cast<Fn>(gdpa(device, name)) : nullptr;
}
} // namespace

void RegisterInstance(VkInstance instance, PFN_vkGetInstanceProcAddr next_gipa)
{
   if (!instance || !next_gipa)
      return;
   auto dispatch = std::make_shared<InstanceDispatch>();
   dispatch->instance = instance;
   dispatch->get_instance_proc_addr = next_gipa;
   dispatch->destroy_instance = LoadInstanceProc<PFN_vkDestroyInstance>(instance, next_gipa, "vkDestroyInstance");
   dispatch->create_device = LoadInstanceProc<PFN_vkCreateDevice>(instance, next_gipa, "vkCreateDevice");
   dispatch->create_win32_surface = LoadInstanceProc<PFN_vkCreateWin32SurfaceKHR>(instance, next_gipa, "vkCreateWin32SurfaceKHR");
   dispatch->destroy_surface = LoadInstanceProc<PFN_vkDestroySurfaceKHR>(instance, next_gipa, "vkDestroySurfaceKHR");
   dispatch->get_physical_device_properties =
      LoadInstanceProc<PFN_vkGetPhysicalDeviceProperties>(instance, next_gipa, "vkGetPhysicalDeviceProperties");
   dispatch->get_physical_device_queue_family_properties =
      LoadInstanceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(instance, next_gipa, "vkGetPhysicalDeviceQueueFamilyProperties");
   dispatch->get_physical_device_memory_properties =
      LoadInstanceProc<PFN_vkGetPhysicalDeviceMemoryProperties>(instance, next_gipa, "vkGetPhysicalDeviceMemoryProperties");
   std::unique_lock lock(instance_mutex);
   instances[GetDispatchKey(instance)] = std::move(dispatch);
}

void UnregisterInstance(VkInstance instance)
{
   if (!instance)
      return;
   std::unique_lock lock(instance_mutex);
   instances.erase(GetDispatchKey(instance));
}

std::shared_ptr<InstanceDispatch> FindInstance(const void* dispatchable_handle)
{
   const void* key = GetDispatchKey(dispatchable_handle);
   if (!key)
      return nullptr;
   std::shared_lock lock(instance_mutex);
   const auto it = instances.find(key);
   return it != instances.end() ? it->second : nullptr;
}

void RegisterDevice(VkDevice device, VkPhysicalDevice physical_device, VkInstance instance, PFN_vkGetInstanceProcAddr next_gipa,
   PFN_vkGetDeviceProcAddr next_gdpa, uint32_t graphics_queue_family)
{
   if (!device || !next_gdpa)
      return;
   auto dispatch = std::make_shared<DeviceDispatch>();
   dispatch->device = device;
   dispatch->physical_device = physical_device;
   dispatch->instance = instance;
   dispatch->get_instance_proc_addr = next_gipa;
   dispatch->get_device_proc_addr = next_gdpa;
   dispatch->destroy_device = LoadDeviceProc<PFN_vkDestroyDevice>(device, next_gdpa, "vkDestroyDevice");
   dispatch->get_device_queue = LoadDeviceProc<PFN_vkGetDeviceQueue>(device, next_gdpa, "vkGetDeviceQueue");
   dispatch->get_device_queue2 = LoadDeviceProc<PFN_vkGetDeviceQueue2>(device, next_gdpa, "vkGetDeviceQueue2");
   dispatch->create_swapchain = LoadDeviceProc<PFN_vkCreateSwapchainKHR>(device, next_gdpa, "vkCreateSwapchainKHR");
   dispatch->destroy_swapchain = LoadDeviceProc<PFN_vkDestroySwapchainKHR>(device, next_gdpa, "vkDestroySwapchainKHR");
   dispatch->get_swapchain_images = LoadDeviceProc<PFN_vkGetSwapchainImagesKHR>(device, next_gdpa, "vkGetSwapchainImagesKHR");
   dispatch->queue_present = LoadDeviceProc<PFN_vkQueuePresentKHR>(device, next_gdpa, "vkQueuePresentKHR");
   dispatch->queue_submit = LoadDeviceProc<PFN_vkQueueSubmit>(device, next_gdpa, "vkQueueSubmit");
   dispatch->queue_wait_idle = LoadDeviceProc<PFN_vkQueueWaitIdle>(device, next_gdpa, "vkQueueWaitIdle");
   dispatch->device_wait_idle = LoadDeviceProc<PFN_vkDeviceWaitIdle>(device, next_gdpa, "vkDeviceWaitIdle");
   dispatch->graphics_queue_family = graphics_queue_family;

   std::unique_lock lock(device_mutex);
   const bool inserted = devices.insert_or_assign(GetDispatchKey(device), std::move(dispatch)).second;
   if (inserted)
      live_device_count.fetch_add(1, std::memory_order_relaxed);
}

void UnregisterDevice(VkDevice device)
{
   if (!device)
      return;
   std::unique_lock lock(device_mutex);
   if (devices.erase(GetDispatchKey(device)) > 0)
      live_device_count.fetch_sub(1, std::memory_order_relaxed);
}

std::shared_ptr<DeviceDispatch> FindDevice(const void* dispatchable_handle)
{
   const void* key = GetDispatchKey(dispatchable_handle);
   if (!key)
      return nullptr;
   std::shared_lock lock(device_mutex);
   const auto it = devices.find(key);
   return it != devices.end() ? it->second : nullptr;
}

void RecordDeviceQueue(VkDevice device, uint32_t queue_family_index, VkQueue queue)
{
   if (!device || !queue)
      return;
   {
      std::unique_lock lock(queue_mutex);
      queue_families[queue] = queue_family_index;
   }
   std::unique_lock lock(device_mutex);
   const auto it = devices.find(GetDispatchKey(device));
   if (it == devices.end())
      return;
   if (queue_family_index == it->second->graphics_queue_family && it->second->graphics_queue == VK_NULL_HANDLE)
      it->second->graphics_queue = queue;
}

uint32_t FindQueueFamily(VkQueue queue)
{
   if (!queue)
      return UINT32_MAX;
   std::shared_lock lock(queue_mutex);
   const auto it = queue_families.find(queue);
   return it != queue_families.end() ? it->second : UINT32_MAX;
}

void RegisterSurface(VkSurfaceKHR surface, HWND hwnd)
{
   if (surface == VK_NULL_HANDLE || !hwnd)
      return;
   std::unique_lock lock(surface_mutex);
   surfaces[reinterpret_cast<uint64_t>(surface)] = hwnd;
}

void UnregisterSurface(VkSurfaceKHR surface)
{
   if (surface == VK_NULL_HANDLE)
      return;
   std::unique_lock lock(surface_mutex);
   surfaces.erase(reinterpret_cast<uint64_t>(surface));
}

HWND FindSurfaceWindow(VkSurfaceKHR surface)
{
   if (surface == VK_NULL_HANDLE)
      return nullptr;
   std::shared_lock lock(surface_mutex);
   const auto it = surfaces.find(reinterpret_cast<uint64_t>(surface));
   return it != surfaces.end() ? it->second : nullptr;
}

uint32_t GetDeviceCount()
{
   return live_device_count.load(std::memory_order_relaxed);
}
} // namespace Vulkan
} // namespace Hooks
} // namespace Luma
