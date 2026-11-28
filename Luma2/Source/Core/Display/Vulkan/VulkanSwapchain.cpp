#include "Display/Vulkan/VulkanSwapchain.hpp"

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>
#include <vector>

namespace Luma {
namespace Display {
namespace Swapchain {
namespace Vulkan {
namespace {
thread_local CreationScope* active_creation_scope = nullptr;

std::shared_mutex swapchains_mutex;
std::vector<SwapchainInfo> swapchains;
std::unordered_set<const void*> vulkan_dxgi_swapchains;
} // namespace

CreationScope::CreationScope(VkDevice device_, HWND window_) : device(device_), window(window_)
{
   active_creation_scope = this;
}

CreationScope::~CreationScope()
{
   if (active_creation_scope == this)
      active_creation_scope = nullptr;
}

bool IsCreatingSwapchain()
{
   return active_creation_scope != nullptr;
}

bool NotifyDxgiSwapchainCreated(const void* dxgi_swapchain)
{
   if (!active_creation_scope || !dxgi_swapchain)
      return false;
   active_creation_scope->captured_dxgi_swapchain = dxgi_swapchain;
   std::unique_lock lock(swapchains_mutex);
   vulkan_dxgi_swapchains.insert(dxgi_swapchain);
   return true;
}

bool IsVulkanDxgiSwapchain(const void* dxgi_swapchain)
{
   if (!dxgi_swapchain)
      return false;
   std::shared_lock lock(swapchains_mutex);
   return vulkan_dxgi_swapchains.contains(dxgi_swapchain);
}

void NotifyDxgiSwapchainDestroyed(const void* dxgi_swapchain)
{
   if (!dxgi_swapchain)
      return;
   std::unique_lock lock(swapchains_mutex);
   vulkan_dxgi_swapchains.erase(dxgi_swapchain);
}

void TrackCreated(const SwapchainInfo& info)
{
   if (info.swapchain == VK_NULL_HANDLE)
      return;
   std::unique_lock lock(swapchains_mutex);
   const auto it = std::ranges::find(swapchains, info.swapchain, &SwapchainInfo::swapchain);
   if (it != swapchains.end())
      *it = info;
   else
      swapchains.push_back(info);
   if (info.dxgi_swapchain)
      vulkan_dxgi_swapchains.insert(info.dxgi_swapchain);
}

void TrackDestroyed(VkSwapchainKHR swapchain)
{
   if (swapchain == VK_NULL_HANDLE)
      return;
   std::unique_lock lock(swapchains_mutex);
   const auto it = std::ranges::find(swapchains, swapchain, &SwapchainInfo::swapchain);
   if (it == swapchains.end())
      return;
   if (it->dxgi_swapchain)
      vulkan_dxgi_swapchains.erase(it->dxgi_swapchain);
   swapchains.erase(it);
}

void ForgetDevice(VkDevice device)
{
   if (device == VK_NULL_HANDLE)
      return;
   std::unique_lock lock(swapchains_mutex);
   for (const SwapchainInfo& entry : swapchains)
   {
      if (entry.device == device && entry.dxgi_swapchain)
         vulkan_dxgi_swapchains.erase(entry.dxgi_swapchain);
   }
   std::erase_if(swapchains, [device](const SwapchainInfo& entry) { return entry.device == device; });
}

std::optional<SwapchainInfo> Find(VkSwapchainKHR swapchain)
{
   if (swapchain == VK_NULL_HANDLE)
      return std::nullopt;
   std::shared_lock lock(swapchains_mutex);
   const auto it = std::ranges::find(swapchains, swapchain, &SwapchainInfo::swapchain);
   return it != swapchains.end() ? std::optional(*it) : std::nullopt;
}
} // namespace Vulkan
} // namespace Swapchain
} // namespace Display
} // namespace Luma
