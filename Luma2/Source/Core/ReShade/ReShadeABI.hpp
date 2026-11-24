#pragma once

#include <cstdint>

// ReShade v6.8.0's "reshade.hpp", "reshade_events.hpp" and "reshade_api_device.hpp", the parts the bridge uses. Event numbers are part of
// ReShade's ABI, callbacks are plain function pointers.
namespace Luma {
namespace ReShade {
namespace ABI {
// RESHADE_API_VERSION. ReShade accepts a version up to its own with the same "/ 10000" major.
constexpr uint32_t API_VERSION = 20;

// reshade::addon_event (uint32_t)
enum class Event : uint32_t
{
   InitEffectRuntime = 9, // void (api::effect_runtime* runtime)
   DestroyEffectRuntime = 10, // void (api::effect_runtime* runtime)
   Draw = 52, // bool (api::command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
   DrawIndexed = 53, // bool (api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
   Dispatch = 54, // bool (api::command_list* cmd_list, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
   DrawOrDispatchIndirect = 55, // bool (api::command_list* cmd_list, api::indirect_command type, api::resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride)
   Present = 74, // void (api::command_queue* queue, api::swapchain* swapchain, const api::rect* source_rect, const api::rect* dest_rect, uint32_t dirty_rect_count, const api::rect* dirty_rects)
   ReShadeBeginEffects = 76, // void (api::effect_runtime* runtime, api::command_list* cmd_list, api::resource_view rtv, api::resource_view rtv_srgb)
   ReShadeFinishEffects = 77, // same as ReShadeBeginEffects
   ReShadeOpenOverlay = 86, // bool (api::effect_runtime* runtime, bool open, api::input_source source): true prevents the change
   DispatchMesh = 89, // bool (api::command_list* cmd_list, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
   DispatchRays = 90, // bool (api::command_list* cmd_list, ...)
   FinishPresent = 100, // void (api::command_queue* queue, api::swapchain* swapchain)
};

// api::device_api
enum class DeviceApi : int32_t
{
   D3D11 = 0xb000,
   D3D12 = 0xc000,
};

// api::resource_view (RESHADE_DEFINE_HANDLE: a struct holding a uint64_t)
struct Handle
{
   uint64_t handle;
};

// api::api_object: the first virtuals, in order
struct __declspec(novtable) ApiObject
{
   virtual uint64_t GetNative() const = 0;
   virtual void GetPrivateData(const uint8_t guid[16], uint64_t* data) const = 0;
   virtual void SetPrivateData(const uint8_t guid[16], const uint64_t data) = 0;
};

// api::device: its first own virtual
struct __declspec(novtable) Device : ApiObject
{
   virtual DeviceApi GetApi() const = 0;
};

// api::device_object (api::swapchain derives from it): its only own virtual
struct __declspec(novtable) DeviceObject : ApiObject
{
   virtual Device* GetDevice() = 0;
};

// extern "C" exports of ReShade's module
using RegisterAddonFunction = bool(__cdecl*)(void* module, uint32_t api_version);
using RegisterEventFunction = void(__cdecl*)(Event event, void* callback);
} // namespace ABI
} // namespace ReShade
} // namespace Luma
