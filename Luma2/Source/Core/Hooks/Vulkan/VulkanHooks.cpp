#include "Hooks/HookManager.hpp"

#include <atomic>
#include <cstring>
#include <string_view>
#include <vector>

#include <Windows.h>
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include "Display/Vulkan/VulkanSwapchain.hpp"
#include "Handlers/Vulkan/VulkanHandlers.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/Vulkan/VulkanDispatch.hpp"
#include "Log/Log.hpp"
#include "Startup.hpp"
#include "Utils/Debug.hpp"

namespace Luma {
namespace Hooks {
namespace {
using Vulkan::DeviceDispatch;
using Vulkan::InstanceDispatch;

constexpr const wchar_t* VULKAN_MODULE = L"vulkan-1.dll";

std::atomic<bool> installed = false;
std::atomic<bool> layer_active = false;
Vulkan::Functions requested_functions;

PFN_vkGetInstanceProcAddr original_get_instance_proc_addr = nullptr;
PFN_vkGetDeviceProcAddr original_get_device_proc_addr = nullptr;
PFN_vkCreateInstance original_create_instance = nullptr;
PFN_vkDestroyInstance original_destroy_instance = nullptr;
PFN_vkCreateWin32SurfaceKHR original_create_win32_surface = nullptr;
PFN_vkDestroySurfaceKHR original_destroy_surface = nullptr;
PFN_vkCreateDevice original_create_device = nullptr;
PFN_vkDestroyDevice original_destroy_device = nullptr;

VkLayerInstanceCreateInfo* FindLayerInstanceLinkInfo(const VkInstanceCreateInfo* create_info)
{
   if (!create_info)
      return nullptr;
   auto* info = const_cast<VkLayerInstanceCreateInfo*>(reinterpret_cast<const VkLayerInstanceCreateInfo*>(create_info->pNext));
   while (info)
   {
      if (info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && info->function == VK_LAYER_LINK_INFO)
         return info;
      info = const_cast<VkLayerInstanceCreateInfo*>(reinterpret_cast<const VkLayerInstanceCreateInfo*>(info->pNext));
   }
   return nullptr;
}

VkLayerDeviceCreateInfo* FindLayerDeviceLinkInfo(const VkDeviceCreateInfo* create_info)
{
   if (!create_info)
      return nullptr;
   auto* info = const_cast<VkLayerDeviceCreateInfo*>(reinterpret_cast<const VkLayerDeviceCreateInfo*>(create_info->pNext));
   while (info)
   {
      if (info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && info->function == VK_LAYER_LINK_INFO)
         return info;
      info = const_cast<VkLayerDeviceCreateInfo*>(reinterpret_cast<const VkLayerDeviceCreateInfo*>(info->pNext));
   }
   return nullptr;
}

uint32_t FindGraphicsQueueFamily(const InstanceDispatch* instance_dispatch, VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info)
{
   if (!instance_dispatch || !instance_dispatch->get_physical_device_queue_family_properties || !physical_device || !create_info)
      return 0;
   uint32_t family_count = 0;
   instance_dispatch->get_physical_device_queue_family_properties(physical_device, &family_count, nullptr);
   if (family_count == 0)
      return 0;
   std::vector<VkQueueFamilyProperties> families(family_count);
   instance_dispatch->get_physical_device_queue_family_properties(physical_device, &family_count, families.data());
   for (uint32_t i = 0; i < create_info->queueCreateInfoCount; i++)
   {
      const uint32_t family = create_info->pQueueCreateInfos[i].queueFamilyIndex;
      if (family < family_count && (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
         return family;
   }
   return create_info->queueCreateInfoCount > 0 ? create_info->pQueueCreateInfos[0].queueFamilyIndex : 0;
}

PFN_vkVoidFunction VKAPI_CALL DetourGetInstanceProcAddr(VkInstance instance, const char* name);
PFN_vkVoidFunction VKAPI_CALL DetourGetDeviceProcAddr(VkDevice device, const char* name);

VkResult VKAPI_CALL DetourCreateInstance(const VkInstanceCreateInfo* create_info, const VkAllocationCallbacks* allocator, VkInstance* instance)
{
   VkLayerInstanceCreateInfo* link_info = FindLayerInstanceLinkInfo(create_info);
   if (link_info && link_info->u.pLayerInfo)
   {
      PFN_vkGetInstanceProcAddr next_gipa = link_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
      link_info->u.pLayerInfo = link_info->u.pLayerInfo->pNext;
      const auto next_create = reinterpret_cast<PFN_vkCreateInstance>(next_gipa ? next_gipa(VK_NULL_HANDLE, "vkCreateInstance") : nullptr);
      if (!next_create)
         return VK_ERROR_INITIALIZATION_FAILED;
      VkResult result = VK_ERROR_INITIALIZATION_FAILED;
      {
         Scope::Guard scope;
         result = next_create(create_info, allocator, instance);
      }
      if (result == VK_SUCCESS && instance && *instance)
      {
         Vulkan::RegisterInstance(*instance, next_gipa);
         Log::Info("Vulkan: instance {} created (layer)", static_cast<void*>(*instance));
      }
      return result;
   }

   if (!original_create_instance)
      return VK_ERROR_INITIALIZATION_FAILED;
   if (Scope::IsActive() || layer_active.load(std::memory_order_acquire))
      return original_create_instance(create_info, allocator, instance);

   VkResult result = VK_ERROR_INITIALIZATION_FAILED;
   {
      Scope::Guard scope;
      result = original_create_instance(create_info, allocator, instance);
   }
   if (result == VK_SUCCESS && instance && *instance)
   {
      Vulkan::RegisterInstance(*instance, original_get_instance_proc_addr);
      Log::Info("Vulkan: instance {} created (hook)", static_cast<void*>(*instance));
   }
   return result;
}

void VKAPI_CALL DetourDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator)
{
   const bool use_layer = layer_active.load(std::memory_order_acquire);
   const std::shared_ptr<InstanceDispatch> dispatch = Vulkan::FindInstance(instance);
   PFN_vkDestroyInstance next = use_layer ? (dispatch ? dispatch->destroy_instance : nullptr) : original_destroy_instance;
   if (!use_layer && Scope::IsActive())
   {
      if (next)
         next(instance, allocator);
      return;
   }
   Vulkan::UnregisterInstance(instance);
   if (next)
   {
      Scope::Guard scope;
      next(instance, allocator);
   }
}

VkResult VKAPI_CALL DetourCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* create_info,
   const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface)
{
   const bool use_layer = layer_active.load(std::memory_order_acquire);
   const std::shared_ptr<InstanceDispatch> dispatch = Vulkan::FindInstance(instance);
   PFN_vkCreateWin32SurfaceKHR next = use_layer ? (dispatch ? dispatch->create_win32_surface : nullptr) : original_create_win32_surface;
   if (!next)
      return VK_ERROR_EXTENSION_NOT_PRESENT;
   if (!use_layer && Scope::IsActive())
      return next(instance, create_info, allocator, surface);
   VkResult result = VK_ERROR_INITIALIZATION_FAILED;
   {
      Scope::Guard scope;
      result = next(instance, create_info, allocator, surface);
   }
   if (result == VK_SUCCESS && surface && *surface != VK_NULL_HANDLE && create_info)
      Vulkan::RegisterSurface(*surface, create_info->hwnd);
   return result;
}

void VKAPI_CALL DetourDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* allocator)
{
   const bool use_layer = layer_active.load(std::memory_order_acquire);
   const std::shared_ptr<InstanceDispatch> dispatch = Vulkan::FindInstance(instance);
   PFN_vkDestroySurfaceKHR next = use_layer ? (dispatch ? dispatch->destroy_surface : nullptr) : original_destroy_surface;
   if (!use_layer && Scope::IsActive())
   {
      if (next)
         next(instance, surface, allocator);
      return;
   }
   Vulkan::UnregisterSurface(surface);
   if (next)
   {
      Scope::Guard scope;
      next(instance, surface, allocator);
   }
}

VkResult VKAPI_CALL DetourCreateDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
   const VkAllocationCallbacks* allocator, VkDevice* device)
{
   const std::shared_ptr<InstanceDispatch> instance_dispatch = Vulkan::FindInstance(physical_device);
   VkLayerDeviceCreateInfo* link_info = FindLayerDeviceLinkInfo(create_info);
   if (link_info && link_info->u.pLayerInfo)
   {
      PFN_vkGetInstanceProcAddr next_gipa = link_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
      PFN_vkGetDeviceProcAddr next_gdpa = link_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
      link_info->u.pLayerInfo = link_info->u.pLayerInfo->pNext;
      const auto next_create = reinterpret_cast<PFN_vkCreateDevice>(next_gipa ? next_gipa(VK_NULL_HANDLE, "vkCreateDevice") : nullptr);
      if (!next_create)
         return VK_ERROR_INITIALIZATION_FAILED;
      VkResult result = VK_ERROR_INITIALIZATION_FAILED;
      {
         Scope::Guard scope;
         result = next_create(physical_device, create_info, allocator, device);
      }
      if (result == VK_SUCCESS && device && *device)
      {
         const uint32_t graphics_family = FindGraphicsQueueFamily(instance_dispatch.get(), physical_device, create_info);
         Vulkan::RegisterDevice(*device, physical_device, instance_dispatch ? instance_dispatch->instance : VK_NULL_HANDLE, next_gipa, next_gdpa,
            graphics_family);
         Scope::Guard scope;
         Handlers::Vulkan::PostCreateDevice(*device, physical_device, instance_dispatch.get());
      }
      return result;
   }

   if (!original_create_device)
      return VK_ERROR_INITIALIZATION_FAILED;
   if (Scope::IsActive() || layer_active.load(std::memory_order_acquire))
      return original_create_device(physical_device, create_info, allocator, device);

   VkResult result = VK_ERROR_INITIALIZATION_FAILED;
   {
      Scope::Guard scope;
      result = original_create_device(physical_device, create_info, allocator, device);
   }
   if (result == VK_SUCCESS && device && *device)
   {
      PFN_vkGetInstanceProcAddr next_gipa = instance_dispatch ? instance_dispatch->get_instance_proc_addr : original_get_instance_proc_addr;
      PFN_vkGetDeviceProcAddr next_gdpa = original_get_device_proc_addr;
      if (!next_gdpa && next_gipa && instance_dispatch)
         next_gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(next_gipa(instance_dispatch->instance, "vkGetDeviceProcAddr"));
      const uint32_t graphics_family = FindGraphicsQueueFamily(instance_dispatch.get(), physical_device, create_info);
      Vulkan::RegisterDevice(*device, physical_device, instance_dispatch ? instance_dispatch->instance : VK_NULL_HANDLE, next_gipa, next_gdpa,
         graphics_family);
      Scope::Guard scope;
      Handlers::Vulkan::PostCreateDevice(*device, physical_device, instance_dispatch.get());
   }
   return result;
}

void VKAPI_CALL DetourDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator)
{
   const bool use_layer = layer_active.load(std::memory_order_acquire);
   const std::shared_ptr<DeviceDispatch> dispatch = Vulkan::FindDevice(device);
   PFN_vkDestroyDevice next = use_layer ? (dispatch ? dispatch->destroy_device : nullptr) : original_destroy_device;
   if (!use_layer && Scope::IsActive())
   {
      if (next)
         next(device, allocator);
      return;
   }
   if (dispatch)
   {
      Scope::Guard scope;
      Handlers::Vulkan::OnDeviceDestroyed(device);
   }
   Vulkan::UnregisterDevice(device);
   if (next)
   {
      Scope::Guard scope;
      next(device, allocator);
   }
}

void VKAPI_CALL DetourGetDeviceQueue(VkDevice device, uint32_t queue_family_index, uint32_t queue_index, VkQueue* queue)
{
   const std::shared_ptr<DeviceDispatch> dispatch = Vulkan::FindDevice(device);
   if (!dispatch || !dispatch->get_device_queue)
      return;
   {
      Scope::Guard scope;
      dispatch->get_device_queue(device, queue_family_index, queue_index, queue);
   }
   if (queue && *queue)
      Vulkan::RecordDeviceQueue(device, queue_family_index, *queue);
}

void VKAPI_CALL DetourGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* queue_info, VkQueue* queue)
{
   const std::shared_ptr<DeviceDispatch> dispatch = Vulkan::FindDevice(device);
   if (!dispatch || !dispatch->get_device_queue2)
      return;
   {
      Scope::Guard scope;
      dispatch->get_device_queue2(device, queue_info, queue);
   }
   if (queue && *queue && queue_info)
      Vulkan::RecordDeviceQueue(device, queue_info->queueFamilyIndex, *queue);
}

VkResult VKAPI_CALL DetourCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* create_info,
   const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain)
{
   const std::shared_ptr<DeviceDispatch> dispatch = Vulkan::FindDevice(device);
   if (!dispatch || !dispatch->create_swapchain)
      return VK_ERROR_EXTENSION_NOT_PRESENT;

   const HWND window = create_info ? Vulkan::FindSurfaceWindow(create_info->surface) : nullptr;
   const bool resize = create_info && create_info->oldSwapchain != VK_NULL_HANDLE;
   VkResult result = VK_ERROR_INITIALIZATION_FAILED;
   const void* captured_dxgi = nullptr;
   {
      Display::Swapchain::Vulkan::CreationScope creation_scope(device, window);
      // Do not enter Scope::Guard here so DXGIHooks can observe driver-internal Vulkan-on-DXGI swapchain creation
      result = dispatch->create_swapchain(device, create_info, allocator, swapchain);
      captured_dxgi = creation_scope.captured_dxgi_swapchain;
   }
   if (result == VK_SUCCESS && swapchain && *swapchain != VK_NULL_HANDLE && create_info)
   {
      uint32_t image_count = create_info->minImageCount;
      if (dispatch->get_swapchain_images)
      {
         uint32_t actual = 0;
         Scope::Guard scope;
         if (dispatch->get_swapchain_images(device, *swapchain, &actual, nullptr) == VK_SUCCESS && actual > 0)
            image_count = actual;
      }
      const Display::Swapchain::Vulkan::SwapchainInfo info = {
         .swapchain = *swapchain,
         .device = device,
         .surface = create_info->surface,
         .window = window,
         .width = create_info->imageExtent.width,
         .height = create_info->imageExtent.height,
         .format = create_info->imageFormat,
         .color_space = create_info->imageColorSpace,
         .image_count = image_count,
         .path = captured_dxgi ? Display::Swapchain::Vulkan::PresentationPath::VulkanOnDxgi
                               : Display::Swapchain::Vulkan::PresentationPath::NativeVulkan,
         .dxgi_swapchain = captured_dxgi,
      };
      Scope::Guard scope;
      Handlers::Vulkan::PostCreateSwapchain(*dispatch, info, resize);
   }
   return result;
}

void VKAPI_CALL DetourDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator)
{
   const std::shared_ptr<DeviceDispatch> dispatch = Vulkan::FindDevice(device);
   if (swapchain != VK_NULL_HANDLE)
   {
      Scope::Guard scope;
      Handlers::Vulkan::OnDestroySwapchain(swapchain);
   }
   if (dispatch && dispatch->destroy_swapchain)
   {
      Scope::Guard scope;
      dispatch->destroy_swapchain(device, swapchain, allocator);
   }
}

VkResult VKAPI_CALL DetourQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* present_info)
{
   const std::shared_ptr<DeviceDispatch> dispatch = Vulkan::FindDevice(queue);
   if (!dispatch || !dispatch->queue_present)
      return VK_ERROR_DEVICE_LOST;
   if (!present_info || present_info->swapchainCount == 0 || Scope::IsActive())
      return dispatch->queue_present(queue, present_info);

   VkPresentInfoKHR modified = *present_info;
   VkSemaphore overlay_semaphore = VK_NULL_HANDLE;
   {
      Scope::Guard scope;
      overlay_semaphore = Handlers::Vulkan::PrePresent(*dispatch, queue, present_info->pSwapchains[0], present_info->pImageIndices[0],
         present_info->waitSemaphoreCount, present_info->pWaitSemaphores);
   }
   if (overlay_semaphore != VK_NULL_HANDLE)
   {
      modified.waitSemaphoreCount = 1;
      modified.pWaitSemaphores = &overlay_semaphore;
   }
   Scope::Guard scope;
   return dispatch->queue_present(queue, &modified);
}

PFN_vkVoidFunction LookupDeviceDetour(std::string_view name)
{
   if (name == "vkGetDeviceProcAddr")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourGetDeviceProcAddr);
   if (name == "vkDestroyDevice")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourDestroyDevice);
   if (name == "vkGetDeviceQueue")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourGetDeviceQueue);
   if (name == "vkGetDeviceQueue2")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourGetDeviceQueue2);
   if (name == "vkCreateSwapchainKHR")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourCreateSwapchainKHR);
   if (name == "vkDestroySwapchainKHR")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourDestroySwapchainKHR);
   if (name == "vkQueuePresentKHR")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourQueuePresentKHR);
   return nullptr;
}

PFN_vkVoidFunction LookupInstanceDetour(std::string_view name)
{
   if (name == "vkGetInstanceProcAddr")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourGetInstanceProcAddr);
   if (name == "vkCreateInstance")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourCreateInstance);
   if (name == "vkDestroyInstance")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourDestroyInstance);
   if (name == "vkCreateWin32SurfaceKHR")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourCreateWin32SurfaceKHR);
   if (name == "vkDestroySurfaceKHR")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourDestroySurfaceKHR);
   if (name == "vkCreateDevice")
      return reinterpret_cast<PFN_vkVoidFunction>(&DetourCreateDevice);
   return LookupDeviceDetour(name);
}

PFN_vkVoidFunction VKAPI_CALL DetourGetDeviceProcAddr(VkDevice device, const char* name)
{
   const std::shared_ptr<DeviceDispatch> dispatch = Vulkan::FindDevice(device);
   PFN_vkGetDeviceProcAddr next = dispatch && dispatch->get_device_proc_addr ? dispatch->get_device_proc_addr : original_get_device_proc_addr;
   if (!name)
      return nullptr;
   if (PFN_vkVoidFunction detour = LookupDeviceDetour(name))
   {
      // Only return extension detours if the underlying driver actually supports the extension
      if (!next || next(device, name) != nullptr)
         return detour;
      return nullptr;
   }
   return next ? next(device, name) : nullptr;
}

PFN_vkVoidFunction VKAPI_CALL DetourGetInstanceProcAddr(VkInstance instance, const char* name)
{
   const std::shared_ptr<InstanceDispatch> dispatch = Vulkan::FindInstance(instance);
   PFN_vkGetInstanceProcAddr next = dispatch && dispatch->get_instance_proc_addr ? dispatch->get_instance_proc_addr : original_get_instance_proc_addr;
   if (!name)
      return nullptr;
   if (PFN_vkVoidFunction detour = LookupInstanceDetour(name))
   {
      if (instance == VK_NULL_HANDLE || !next || next(instance, name) != nullptr)
         return detour;
      return nullptr;
   }
   return next ? next(instance, name) : nullptr;
}
} // namespace

void InstallVulkan(const Vulkan::Functions& functions)
{
   requested_functions = functions;
   if (functions.any())
      LUMA_UNIMPLEMENTED("Hooks::Vulkan optional feature hooks");
   if (installed.exchange(true))
      return;
   const Hook hooks[] = {
      ExportHook(VULKAN_MODULE, "vkGetInstanceProcAddr", &DetourGetInstanceProcAddr, original_get_instance_proc_addr),
      ExportHook(VULKAN_MODULE, "vkGetDeviceProcAddr", &DetourGetDeviceProcAddr, original_get_device_proc_addr),
      ExportHook(VULKAN_MODULE, "vkCreateInstance", &DetourCreateInstance, original_create_instance),
      ExportHook(VULKAN_MODULE, "vkDestroyInstance", &DetourDestroyInstance, original_destroy_instance),
      ExportHook(VULKAN_MODULE, "vkCreateWin32SurfaceKHR", &DetourCreateWin32SurfaceKHR, original_create_win32_surface),
      ExportHook(VULKAN_MODULE, "vkDestroySurfaceKHR", &DetourDestroySurfaceKHR, original_destroy_surface),
      ExportHook(VULKAN_MODULE, "vkCreateDevice", &DetourCreateDevice, original_create_device),
      ExportHook(VULKAN_MODULE, "vkDestroyDevice", &DetourDestroyDevice, original_destroy_device),
   };
   Declare(hooks);
}

uint32_t GetVulkanDeviceCount()
{
   return Vulkan::GetDeviceCount();
}

PFN_vkVoidFunction VulkanLayerGetInstanceProcAddr(VkInstance instance, const char* name)
{
   Startup::Initialize();
   return DetourGetInstanceProcAddr(instance, name);
}

PFN_vkVoidFunction VulkanLayerGetDeviceProcAddr(VkDevice device, const char* name)
{
   Startup::Initialize();
   return DetourGetDeviceProcAddr(device, name);
}

VkResult VulkanLayerNegotiate(VkNegotiateLayerInterface* version_struct, PFN_vkGetInstanceProcAddr gipa, PFN_vkGetDeviceProcAddr gdpa)
{
   if (!version_struct || version_struct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
      return VK_ERROR_INITIALIZATION_FAILED;
   Startup::Initialize();
   layer_active.store(true, std::memory_order_release);
   if (version_struct->loaderLayerInterfaceVersion > 2)
      version_struct->loaderLayerInterfaceVersion = 2;
   version_struct->pfnGetInstanceProcAddr = gipa;
   version_struct->pfnGetDeviceProcAddr = gdpa;
   version_struct->pfnGetPhysicalDeviceProcAddr = nullptr;
   Log::Info("Vulkan: layer negotiated (interface v{})", version_struct->loaderLayerInterfaceVersion);
   return VK_SUCCESS;
}
} // namespace Hooks
} // namespace Luma

extern "C"
{
   __declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL LumaVulkan_GetInstanceProcAddr(VkInstance instance, const char* name)
   {
      return Luma::Hooks::VulkanLayerGetInstanceProcAddr(instance, name);
   }

   __declspec(dllexport) PFN_vkVoidFunction VKAPI_CALL LumaVulkan_GetDeviceProcAddr(VkDevice device, const char* name)
   {
      return Luma::Hooks::VulkanLayerGetDeviceProcAddr(device, name);
   }

   __declspec(dllexport) VkResult VKAPI_CALL LumaVulkan_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version_struct)
   {
      return Luma::Hooks::VulkanLayerNegotiate(version_struct, &LumaVulkan_GetInstanceProcAddr, &LumaVulkan_GetDeviceProcAddr);
   }
}

#pragma comment(linker, "/export:vkNegotiateLoaderLayerInterfaceVersion=LumaVulkan_NegotiateLoaderLayerInterfaceVersion")
