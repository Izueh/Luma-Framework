#include "UI/Vulkan/VulkanOverlay.hpp"

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <imgui.h>
#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES
#include <backends/imgui_impl_vulkan.h>

#include "Hooks/HookScope.hpp"
#include "Log/Log.hpp"
#include "UI/Overlay.hpp"
#include "UI/Vulkan/VulkanOverlayShaders.hpp"

namespace Luma {
namespace UI {
namespace Vulkan {
namespace {
constexpr uint32_t MIN_SIZE = 64;
constexpr ULONGLONG HANDOFF_MS = 1000;

struct LoaderContext
{
   VkInstance instance = VK_NULL_HANDLE;
   VkDevice device = VK_NULL_HANDLE;
   PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
   PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
};

struct FrameResources
{
   VkImageView image_view = VK_NULL_HANDLE;
   VkFramebuffer framebuffer = VK_NULL_HANDLE;
   VkCommandPool command_pool = VK_NULL_HANDLE;
   VkCommandBuffer command_buffer = VK_NULL_HANDLE;
   VkFence fence = VK_NULL_HANDLE;
   VkSemaphore render_finished = VK_NULL_HANDLE;
};

struct RendererState
{
   VkDevice device = VK_NULL_HANDLE;
   VkSwapchainKHR swapchain = VK_NULL_HANDLE;
   HWND window = nullptr;
   uint32_t width = 0;
   uint32_t height = 0;
   VkFormat format = VK_FORMAT_UNDEFINED;
   uint32_t queue_family = 0;
   ULONGLONG last_present = 0;

   LoaderContext loader;
   PFN_vkDeviceWaitIdle device_wait_idle = nullptr;
   PFN_vkQueueSubmit queue_submit = nullptr;
   PFN_vkCreateDescriptorPool create_descriptor_pool = nullptr;
   PFN_vkDestroyDescriptorPool destroy_descriptor_pool = nullptr;
   PFN_vkCreateRenderPass create_render_pass = nullptr;
   PFN_vkDestroyRenderPass destroy_render_pass = nullptr;
   PFN_vkCreateImageView create_image_view = nullptr;
   PFN_vkDestroyImageView destroy_image_view = nullptr;
   PFN_vkCreateFramebuffer create_framebuffer = nullptr;
   PFN_vkDestroyFramebuffer destroy_framebuffer = nullptr;
   PFN_vkCreateCommandPool create_command_pool = nullptr;
   PFN_vkDestroyCommandPool destroy_command_pool = nullptr;
   PFN_vkResetCommandPool reset_command_pool = nullptr;
   PFN_vkAllocateCommandBuffers allocate_command_buffers = nullptr;
   PFN_vkBeginCommandBuffer begin_command_buffer = nullptr;
   PFN_vkEndCommandBuffer end_command_buffer = nullptr;
   PFN_vkCmdBeginRenderPass cmd_begin_render_pass = nullptr;
   PFN_vkCmdEndRenderPass cmd_end_render_pass = nullptr;
   PFN_vkCreateFence create_fence = nullptr;
   PFN_vkDestroyFence destroy_fence = nullptr;
   PFN_vkWaitForFences wait_for_fences = nullptr;
   PFN_vkResetFences reset_fences = nullptr;
   PFN_vkCreateSemaphore create_semaphore = nullptr;
   PFN_vkDestroySemaphore destroy_semaphore = nullptr;

   VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
   VkRenderPass render_pass = VK_NULL_HANDLE;
   std::vector<FrameResources> frames;
   bool imgui_initialized = false;
};

bool IsSrgbFormat(VkFormat format)
{
   switch (format)
   {
   case VK_FORMAT_R8G8B8A8_SRGB:
   case VK_FORMAT_B8G8R8A8_SRGB:
   case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return true;
   default: return false;
   }
}

std::span<const uint32_t> SelectFragmentShader(VkFormat format, VkColorSpaceKHR color_space, std::string_view& encoding_name)
{
   if (color_space == VK_COLOR_SPACE_HDR10_ST2084_EXT)
   {
      encoding_name = "HDR10 PQ";
      return FRAG_HDR10_PQ_SPV;
   }
   if (color_space == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT || format == VK_FORMAT_R16G16B16A16_SFLOAT)
   {
      encoding_name = "scRGB";
      return FRAG_SCRGB_SPV;
   }
   if (IsSrgbFormat(format))
   {
      encoding_name = "sRGB";
      return FRAG_SRGB_TO_LINEAR_SPV;
   }
   encoding_name = "SDR";
   return {};
}

// Under UI::Lock
std::optional<RendererState> state;

PFN_vkVoidFunction LoadVulkanFunction(const char* name, void* user_data)
{
   const auto* ctx = static_cast<const LoaderContext*>(user_data);
   if (!ctx || !name)
      return nullptr;
   if (ctx->device && ctx->get_device_proc_addr)
   {
      if (PFN_vkVoidFunction fn = ctx->get_device_proc_addr(ctx->device, name))
         return fn;
   }
   return ctx->get_instance_proc_addr ? ctx->get_instance_proc_addr(ctx->instance, name) : nullptr;
}

template <typename Fn>
Fn LoadProc(const LoaderContext& ctx, const char* name)
{
   return reinterpret_cast<Fn>(LoadVulkanFunction(name, const_cast<LoaderContext*>(&ctx)));
}

void DestroyRenderer()
{
   if (!state)
      return;
   RendererState& current = *state;
   if (current.wait_for_fences && current.device)
   {
      for (const FrameResources& frame : current.frames)
      {
         if (frame.fence)
            current.wait_for_fences(current.device, 1, &frame.fence, VK_TRUE, 2'000'000'000ull);
      }
   }
   if (current.imgui_initialized)
   {
      ImGui_ImplVulkan_Shutdown();
      current.imgui_initialized = false;
   }
   for (const FrameResources& frame : current.frames)
   {
      if (frame.render_finished && current.destroy_semaphore)
         current.destroy_semaphore(current.device, frame.render_finished, nullptr);
      if (frame.fence && current.destroy_fence)
         current.destroy_fence(current.device, frame.fence, nullptr);
      if (frame.command_pool && current.destroy_command_pool)
         current.destroy_command_pool(current.device, frame.command_pool, nullptr);
      if (frame.framebuffer && current.destroy_framebuffer)
         current.destroy_framebuffer(current.device, frame.framebuffer, nullptr);
      if (frame.image_view && current.destroy_image_view)
         current.destroy_image_view(current.device, frame.image_view, nullptr);
   }
   if (current.render_pass && current.destroy_render_pass)
      current.destroy_render_pass(current.device, current.render_pass, nullptr);
   if (current.descriptor_pool && current.destroy_descriptor_pool)
      current.destroy_descriptor_pool(current.device, current.descriptor_pool, nullptr);
   DetachWindow();
   Log::Info("UI: Vulkan overlay detached from swapchain {}", reinterpret_cast<const void*>(current.swapchain));
   state.reset();
}

bool InitializeRenderer(const Hooks::Vulkan::DeviceDispatch& dispatch, const Display::Swapchain::Vulkan::SwapchainInfo& swapchain, VkQueue queue)
{
   if (!swapchain.window || !IsWindow(swapchain.window) || swapchain.width < MIN_SIZE || swapchain.height < MIN_SIZE)
      return false;
   if (!dispatch.instance || !dispatch.physical_device || !dispatch.device || !dispatch.get_swapchain_images)
      return false;
   if (!AttachWindow(swapchain.window))
      return false;

   RendererState& next = state.emplace();
   next.device = dispatch.device;
   next.swapchain = swapchain.swapchain;
   next.window = swapchain.window;
   next.width = swapchain.width;
   next.height = swapchain.height;
   next.format = swapchain.format;
   const uint32_t present_family = Hooks::Vulkan::FindQueueFamily(queue);
   next.queue_family = present_family != UINT32_MAX ? present_family
                                                    : (dispatch.graphics_queue_family != UINT32_MAX ? dispatch.graphics_queue_family : 0);
   next.last_present = GetTickCount64();
   next.loader = {dispatch.instance, dispatch.device, dispatch.get_instance_proc_addr, dispatch.get_device_proc_addr};

   next.device_wait_idle = LoadProc<PFN_vkDeviceWaitIdle>(next.loader, "vkDeviceWaitIdle");
   next.queue_submit = LoadProc<PFN_vkQueueSubmit>(next.loader, "vkQueueSubmit");
   next.create_descriptor_pool = LoadProc<PFN_vkCreateDescriptorPool>(next.loader, "vkCreateDescriptorPool");
   next.destroy_descriptor_pool = LoadProc<PFN_vkDestroyDescriptorPool>(next.loader, "vkDestroyDescriptorPool");
   next.create_render_pass = LoadProc<PFN_vkCreateRenderPass>(next.loader, "vkCreateRenderPass");
   next.destroy_render_pass = LoadProc<PFN_vkDestroyRenderPass>(next.loader, "vkDestroyRenderPass");
   next.create_image_view = LoadProc<PFN_vkCreateImageView>(next.loader, "vkCreateImageView");
   next.destroy_image_view = LoadProc<PFN_vkDestroyImageView>(next.loader, "vkDestroyImageView");
   next.create_framebuffer = LoadProc<PFN_vkCreateFramebuffer>(next.loader, "vkCreateFramebuffer");
   next.destroy_framebuffer = LoadProc<PFN_vkDestroyFramebuffer>(next.loader, "vkDestroyFramebuffer");
   next.create_command_pool = LoadProc<PFN_vkCreateCommandPool>(next.loader, "vkCreateCommandPool");
   next.destroy_command_pool = LoadProc<PFN_vkDestroyCommandPool>(next.loader, "vkDestroyCommandPool");
   next.reset_command_pool = LoadProc<PFN_vkResetCommandPool>(next.loader, "vkResetCommandPool");
   next.allocate_command_buffers = LoadProc<PFN_vkAllocateCommandBuffers>(next.loader, "vkAllocateCommandBuffers");
   next.begin_command_buffer = LoadProc<PFN_vkBeginCommandBuffer>(next.loader, "vkBeginCommandBuffer");
   next.end_command_buffer = LoadProc<PFN_vkEndCommandBuffer>(next.loader, "vkEndCommandBuffer");
   next.cmd_begin_render_pass = LoadProc<PFN_vkCmdBeginRenderPass>(next.loader, "vkCmdBeginRenderPass");
   next.cmd_end_render_pass = LoadProc<PFN_vkCmdEndRenderPass>(next.loader, "vkCmdEndRenderPass");
   next.create_fence = LoadProc<PFN_vkCreateFence>(next.loader, "vkCreateFence");
   next.destroy_fence = LoadProc<PFN_vkDestroyFence>(next.loader, "vkDestroyFence");
   next.wait_for_fences = LoadProc<PFN_vkWaitForFences>(next.loader, "vkWaitForFences");
   next.reset_fences = LoadProc<PFN_vkResetFences>(next.loader, "vkResetFences");
   next.create_semaphore = LoadProc<PFN_vkCreateSemaphore>(next.loader, "vkCreateSemaphore");
   next.destroy_semaphore = LoadProc<PFN_vkDestroySemaphore>(next.loader, "vkDestroySemaphore");

   if (!next.queue_submit || !next.create_descriptor_pool || !next.create_render_pass || !next.create_image_view || !next.create_framebuffer ||
      !next.create_command_pool || !next.allocate_command_buffers || !next.begin_command_buffer || !next.end_command_buffer ||
      !next.cmd_begin_render_pass || !next.cmd_end_render_pass || !next.create_fence || !next.wait_for_fences || !next.reset_fences ||
      !next.create_semaphore)
   {
      DestroyRenderer();
      return false;
   }

   uint32_t image_count = 0;
   if (dispatch.get_swapchain_images(dispatch.device, swapchain.swapchain, &image_count, nullptr) != VK_SUCCESS || image_count == 0)
   {
      DestroyRenderer();
      return false;
   }
   std::vector<VkImage> images(image_count);
   if (dispatch.get_swapchain_images(dispatch.device, swapchain.swapchain, &image_count, images.data()) != VK_SUCCESS)
   {
      DestroyRenderer();
      return false;
   }

   const VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
   const VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      .maxSets = 16,
      .poolSizeCount = 1,
      .pPoolSizes = &pool_size,
   };
   if (next.create_descriptor_pool(dispatch.device, &pool_info, nullptr, &next.descriptor_pool) != VK_SUCCESS)
   {
      DestroyRenderer();
      return false;
   }

   const VkAttachmentDescription attachment = {
      .format = swapchain.format,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
   };
   const VkAttachmentReference color_attachment = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
   const VkSubpassDescription subpass = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment,
   };
   const VkSubpassDependency dependency = {
      .srcSubpass = VK_SUBPASS_EXTERNAL,
      .dstSubpass = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
   };
   const VkRenderPassCreateInfo render_pass_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &attachment,
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 1,
      .pDependencies = &dependency,
   };
   if (next.create_render_pass(dispatch.device, &render_pass_info, nullptr, &next.render_pass) != VK_SUCCESS)
   {
      DestroyRenderer();
      return false;
   }

   next.frames.resize(image_count);
   for (uint32_t i = 0; i < image_count; i++)
   {
      FrameResources& frame = next.frames[i];
      const VkImageViewCreateInfo view_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = images[i],
         .viewType = VK_IMAGE_VIEW_TYPE_2D,
         .format = swapchain.format,
         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      };
      const VkCommandPoolCreateInfo cmd_pool_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
         .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
         .queueFamilyIndex = next.queue_family,
      };
      const VkFenceCreateInfo fence_info = {
         .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
         .flags = VK_FENCE_CREATE_SIGNALED_BIT,
      };
      const VkSemaphoreCreateInfo sem_info = {
         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      };
      if (next.create_image_view(dispatch.device, &view_info, nullptr, &frame.image_view) != VK_SUCCESS ||
         next.create_command_pool(dispatch.device, &cmd_pool_info, nullptr, &frame.command_pool) != VK_SUCCESS ||
         next.create_fence(dispatch.device, &fence_info, nullptr, &frame.fence) != VK_SUCCESS ||
         next.create_semaphore(dispatch.device, &sem_info, nullptr, &frame.render_finished) != VK_SUCCESS)
      {
         DestroyRenderer();
         return false;
      }
      const VkFramebufferCreateInfo fb_info = {
         .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
         .renderPass = next.render_pass,
         .attachmentCount = 1,
         .pAttachments = &frame.image_view,
         .width = swapchain.width,
         .height = swapchain.height,
         .layers = 1,
      };
      const VkCommandBufferAllocateInfo cmd_alloc = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
         .commandPool = frame.command_pool,
         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
         .commandBufferCount = 1,
      };
      if (next.create_framebuffer(dispatch.device, &fb_info, nullptr, &frame.framebuffer) != VK_SUCCESS ||
         next.allocate_command_buffers(dispatch.device, &cmd_alloc, &frame.command_buffer) != VK_SUCCESS)
      {
         DestroyRenderer();
         return false;
      }
   }

   if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_0, &LoadVulkanFunction, &next.loader))
   {
      DestroyRenderer();
      return false;
   }
   ImGui_ImplVulkan_InitInfo init_info = {};
   init_info.ApiVersion = VK_API_VERSION_1_0;
   init_info.Instance = dispatch.instance;
   init_info.PhysicalDevice = dispatch.physical_device;
   init_info.Device = dispatch.device;
   init_info.QueueFamily = next.queue_family;
   init_info.Queue = queue;
   init_info.DescriptorPool = next.descriptor_pool;
   init_info.MinImageCount = image_count >= 2 ? 2 : 1;
   init_info.ImageCount = image_count;
   init_info.PipelineInfoMain.RenderPass = next.render_pass;
   init_info.PipelineInfoMain.Subpass = 0;
   init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
   std::string_view encoding_name = "SDR";
   if (const std::span<const uint32_t> spv = SelectFragmentShader(swapchain.format, swapchain.color_space, encoding_name); !spv.empty())
   {
      init_info.CustomShaderFragCreateInfo = {
         .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
         .codeSize = spv.size_bytes(),
         .pCode = spv.data(),
      };
   }
   if (!ImGui_ImplVulkan_Init(&init_info))
   {
      DestroyRenderer();
      return false;
   }
   next.imgui_initialized = true;
   Log::Info("UI: Vulkan overlay attached to swapchain {} ({}x{}, {} images, queue family {}, encoding {})",
      reinterpret_cast<const void*>(swapchain.swapchain), swapchain.width, swapchain.height, image_count, next.queue_family, encoding_name);
   return true;
}
} // namespace

VkSemaphore DrawOverlay(const Hooks::Vulkan::DeviceDispatch& dispatch, const Display::Swapchain::Vulkan::SwapchainInfo& swapchain, VkQueue queue,
   uint32_t image_index, uint32_t wait_semaphore_count, const VkSemaphore* wait_semaphores)
{
   Hooks::Scope::Guard scope;
   const auto lock = Lock();
   const ULONGLONG now = GetTickCount64();

   if (state && state->swapchain == swapchain.swapchain)
   {
      state->last_present = now;
   }
   else if (!state || now - state->last_present >= HANDOFF_MS)
   {
      DestroyRenderer();
      if (!InitializeRenderer(dispatch, swapchain, queue))
         return VK_NULL_HANDLE;
   }
   else
   {
      return VK_NULL_HANDLE;
   }

   if (!BeginPresent() || image_index >= state->frames.size())
      return VK_NULL_HANDLE;

   FrameResources& frame = state->frames[image_index];
   if (state->wait_for_fences(state->device, 1, &frame.fence, VK_TRUE, 2'000'000'000ull) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   if (state->reset_command_pool)
      state->reset_command_pool(state->device, frame.command_pool, 0);

   ImGui_ImplVulkan_NewFrame();
   BuildFrame({state->width, state->height});

   const VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   if (state->begin_command_buffer(frame.command_buffer, &begin_info) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   const VkRenderPassBeginInfo pass_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = state->render_pass,
      .framebuffer = frame.framebuffer,
      .renderArea = {{0, 0}, {state->width, state->height}},
   };
   state->cmd_begin_render_pass(frame.command_buffer, &pass_info, VK_SUBPASS_CONTENTS_INLINE);
   ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.command_buffer);
   state->cmd_end_render_pass(frame.command_buffer);
   if (state->end_command_buffer(frame.command_buffer) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   std::vector<VkPipelineStageFlags> wait_stages(wait_semaphore_count, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
   const VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = wait_semaphore_count,
      .pWaitSemaphores = wait_semaphores,
      .pWaitDstStageMask = wait_stages.empty() ? nullptr : wait_stages.data(),
      .commandBufferCount = 1,
      .pCommandBuffers = &frame.command_buffer,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &frame.render_finished,
   };
   state->reset_fences(state->device, 1, &frame.fence);
   if (state->queue_submit(queue, 1, &submit_info, frame.fence) != VK_SUCCESS)
   {
      Log::Warning("UI: Vulkan overlay vkQueueSubmit failed");
      return VK_NULL_HANDLE;
   }

   EndFrame();
   return frame.render_finished;
}

void DetachFromSwapchain(VkSwapchainKHR swapchain)
{
   Hooks::Scope::Guard scope;
   const auto lock = Lock();
   if (state && state->swapchain == swapchain)
      DestroyRenderer();
}

void DetachFromDevice(VkDevice device)
{
   Hooks::Scope::Guard scope;
   const auto lock = Lock();
   if (state && state->device == device)
      DestroyRenderer();
}

VkSwapchainKHR GetSwapchain()
{
   const auto lock = Lock();
   return state ? state->swapchain : VK_NULL_HANDLE;
}
} // namespace Vulkan
} // namespace UI
} // namespace Luma
