#include <doctest/doctest.h>

#include <cstdint>

#include "Common/Api.hpp"
#include "Common/Device.hpp"
#include "Display/Swapchain.hpp"
#include "Display/Vulkan/VulkanSwapchain.hpp"
#include "FrameState/FrameState.hpp"
#include "Game/Game.hpp"
#include "Game/GameRegistry.hpp"
#include "Hooks/HookManager.hpp"
#include "Hooks/Vulkan/VulkanDispatch.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Shaders/Shaders.hpp"

namespace {
struct FakeVkDispatchable
{
   void* loader_dispatch_table = nullptr;
};

struct TestVulkanDeviceData
{
   int value = 42;
};

PFN_vkVoidFunction VKAPI_CALL DummyGetInstanceProcAddr(VkInstance, const char*)
{
   return nullptr;
}

PFN_vkVoidFunction VKAPI_CALL DummyGetDeviceProcAddr(VkDevice, const char*)
{
   return nullptr;
}
} // namespace

TEST_CASE("Vulkan: Api enum and Device variant work without COM")
{
   using namespace Luma;

   CHECK(GetName(Api::Vulkan) == "Vulkan");
   const ApiSet apis = {Api::Vulkan};
   CHECK(apis.Has(Api::Vulkan));
   CHECK_FALSE(apis.Has(Api::D3D11));
   CHECK_FALSE(apis.Has(Api::D3D12));

   FakeVkDispatchable fake_device;
   auto* vk_device = reinterpret_cast<VkDevice>(&fake_device);
   const Device device = vk_device;
   CHECK(GetRawDeviceKey(device) == vk_device);

   FrameState::CreateDeviceState(device);
   FrameState::SetOutputResolution(device, {2560, 1440});
   FrameState::SetRenderResolution(device, {1920, 1080});
   CHECK(FrameState::Get(device).output == Math::uint2{2560, 1440});
   CHECK(FrameState::Get(device).render == Math::uint2{1920, 1080});
   FrameState::AdvanceFrame(device);
   CHECK(FrameState::GetTiming(device).frame_index == 1);
   FrameState::DestroyDeviceState(device);
   CHECK(FrameState::Find(device) == nullptr);

   Resources::Scaling::Begin(device);
   CHECK(Resources::Scaling::IsActive(device));
   Resources::Scaling::End(device);
   CHECK_FALSE(Resources::Scaling::IsActive(device));
   Resources::Scaling::DestroyDeviceState(device);

   Resources::Samplers::SetLodBiasOffset(device, -0.5f);
   CHECK(Resources::Samplers::GetLodBiasOffset(device) == doctest::Approx(-0.5f));
   Resources::Samplers::DestroyDeviceState(device);
   CHECK(Resources::Samplers::GetLodBiasOffset(device) == doctest::Approx(0.f));

   Shaders::CreateStore(device, Api::Vulkan);
   CHECK(Shaders::GetStore(device) != nullptr);
   Shaders::DestroyStore(device);
   CHECK(Shaders::GetStore(device) == nullptr);

   Game::Registry::Reset();
   Game::SetOptions({.device_data = {[]() -> Game::GameDeviceData* { return reinterpret_cast<Game::GameDeviceData*>(new TestVulkanDeviceData()); },
                        [](Game::GameDeviceData* data) { delete reinterpret_cast<TestVulkanDeviceData*>(data); }},
      .supported_apis = {Api::Vulkan}});
   CHECK_FALSE(Display::Swapchain::IsSupported());
   CHECK_FALSE(Resources::Mirrors::IsSupported());
   CHECK_FALSE(Shaders::IsSupported());
   Game::Registry::CreateDeviceData(device);
   auto* data = reinterpret_cast<TestVulkanDeviceData*>(Game::GetGameDeviceData(device));
   REQUIRE(data != nullptr);
   CHECK(data->value == 42);
   Game::Registry::DestroyDeviceData(device);
   CHECK(Game::GetGameDeviceData(device) == nullptr);
   Game::Registry::Reset();
   CHECK(Display::Swapchain::IsSupported());
   CHECK(Resources::Mirrors::IsSupported());
   CHECK(Shaders::IsSupported());
}

TEST_CASE("Vulkan: dispatch registry and dual swapchain path correlation")
{
   using namespace Luma;

   int instance_table = 0;
   int device_table = 0;
   FakeVkDispatchable fake_instance = {&instance_table};
   FakeVkDispatchable fake_physical = {&instance_table};
   FakeVkDispatchable fake_device = {&device_table};
   FakeVkDispatchable fake_queue = {&device_table};

   auto* vk_instance = reinterpret_cast<VkInstance>(&fake_instance);
   auto* vk_physical = reinterpret_cast<VkPhysicalDevice>(&fake_physical);
   auto* vk_device = reinterpret_cast<VkDevice>(&fake_device);
   auto* vk_queue = reinterpret_cast<VkQueue>(&fake_queue);

   Hooks::Vulkan::RegisterInstance(vk_instance, &DummyGetInstanceProcAddr);
   CHECK(Hooks::Vulkan::FindInstance(vk_instance) != nullptr);
   CHECK(Hooks::Vulkan::FindInstance(vk_physical) != nullptr);

   const uint32_t count_before = Hooks::GetVulkanDeviceCount();
   Hooks::Vulkan::RegisterDevice(vk_device, vk_physical, vk_instance, &DummyGetInstanceProcAddr, &DummyGetDeviceProcAddr, 0);
   CHECK(Hooks::GetVulkanDeviceCount() == count_before + 1);
   CHECK(Hooks::Vulkan::FindDevice(vk_device) != nullptr);
   CHECK(Hooks::Vulkan::FindDevice(vk_queue) != nullptr);

   const auto fake_surface = reinterpret_cast<VkSurfaceKHR>(uintptr_t(0x1001));
   const auto fake_hwnd = reinterpret_cast<HWND>(uintptr_t(0x2002));
   Hooks::Vulkan::RegisterSurface(fake_surface, fake_hwnd);
   CHECK(Hooks::Vulkan::FindSurfaceWindow(fake_surface) == fake_hwnd);

   // Simulate Vulkan-on-DXGI swapchain creation inside vkCreateSwapchainKHR
   const void* fake_dxgi_swapchain = reinterpret_cast<const void*>(uintptr_t(0x3003));
   {
      Display::Swapchain::Vulkan::CreationScope scope(vk_device, fake_hwnd);
      CHECK(Display::Swapchain::Vulkan::IsCreatingSwapchain());
      CHECK(Display::Swapchain::Vulkan::NotifyDxgiSwapchainCreated(fake_dxgi_swapchain));
      CHECK(scope.captured_dxgi_swapchain == fake_dxgi_swapchain);
   }
   CHECK_FALSE(Display::Swapchain::Vulkan::IsCreatingSwapchain());
   CHECK(Display::Swapchain::Vulkan::IsVulkanDxgiSwapchain(fake_dxgi_swapchain));

   const auto fake_swapchain = reinterpret_cast<VkSwapchainKHR>(uintptr_t(0x4004));
   Display::Swapchain::Vulkan::TrackCreated({
      .swapchain = fake_swapchain,
      .device = vk_device,
      .surface = fake_surface,
      .window = fake_hwnd,
      .width = 1920,
      .height = 1080,
      .format = VK_FORMAT_B8G8R8A8_UNORM,
      .image_count = 3,
      .path = Display::Swapchain::Vulkan::PresentationPath::VulkanOnDxgi,
      .dxgi_swapchain = fake_dxgi_swapchain,
   });
   const auto found = Display::Swapchain::Vulkan::Find(fake_swapchain);
   REQUIRE(found.has_value());
   CHECK(found->path == Display::Swapchain::Vulkan::PresentationPath::VulkanOnDxgi);
   CHECK(found->dxgi_swapchain == fake_dxgi_swapchain);

   Display::Swapchain::Vulkan::TrackDestroyed(fake_swapchain);
   CHECK_FALSE(Display::Swapchain::Vulkan::Find(fake_swapchain).has_value());
   CHECK_FALSE(Display::Swapchain::Vulkan::IsVulkanDxgiSwapchain(fake_dxgi_swapchain));

   Hooks::Vulkan::UnregisterSurface(fake_surface);
   Hooks::Vulkan::UnregisterDevice(vk_device);
   Hooks::Vulkan::UnregisterInstance(vk_instance);
   CHECK(Hooks::GetVulkanDeviceCount() == count_before);
}
