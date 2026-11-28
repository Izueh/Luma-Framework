#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <Windows.h>
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

namespace Luma {
namespace Display {
namespace Swapchain {
namespace Vulkan {
enum class PresentationPath : uint8_t
{
   NativeVulkan,
   VulkanOnDxgi,
};

constexpr std::string_view GetName(PresentationPath path)
{
   switch (path)
   {
   case PresentationPath::NativeVulkan: return "NativeVulkan";
   case PresentationPath::VulkanOnDxgi: return "VulkanOnDxgi";
   }
   return "unknown";
}

struct SwapchainInfo
{
   VkSwapchainKHR swapchain = VK_NULL_HANDLE;
   VkDevice device = VK_NULL_HANDLE;
   VkSurfaceKHR surface = VK_NULL_HANDLE;
   HWND window = nullptr;
   uint32_t width = 0;
   uint32_t height = 0;
   VkFormat format = VK_FORMAT_UNDEFINED;
   VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
   uint32_t image_count = 0;
   PresentationPath path = PresentationPath::NativeVulkan;
   const void* dxgi_swapchain = nullptr;
};

// Thread-local scope set around vkCreateSwapchainKHR so DXGIHandlers can detect driver-internal Vulkan-on-DXGI swapchains
struct CreationScope
{
   VkDevice device = VK_NULL_HANDLE;
   HWND window = nullptr;
   const void* captured_dxgi_swapchain = nullptr;

   CreationScope(VkDevice device_, HWND window_);
   ~CreationScope();
   CreationScope(const CreationScope&) = delete;
   CreationScope& operator=(const CreationScope&) = delete;
};

// Called from DXGIHandlers; returns true while vkCreateSwapchainKHR is running on this thread
bool IsCreatingSwapchain();
// Called from DXGIHandlers::PostCreateSwapChain; returns true if the DXGI swapchain was created inside vkCreateSwapchainKHR
bool NotifyDxgiSwapchainCreated(const void* dxgi_swapchain);
bool IsVulkanDxgiSwapchain(const void* dxgi_swapchain);
void NotifyDxgiSwapchainDestroyed(const void* dxgi_swapchain);

// Swapchain registry
void TrackCreated(const SwapchainInfo& info);
void TrackDestroyed(VkSwapchainKHR swapchain);
void ForgetDevice(VkDevice device);
std::optional<SwapchainInfo> Find(VkSwapchainKHR swapchain);
} // namespace Vulkan
} // namespace Swapchain
} // namespace Display
} // namespace Luma
