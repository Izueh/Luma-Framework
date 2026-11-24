#pragma once

#if DEVELOPMENT
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <format>
#include <guiddef.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Common/ShaderStage.hpp"
#include "Hooks/HookFunctions.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderStages.hpp"
#include "Shaders/ShaderVariants.hpp"
#include "Utils/Strings.hpp"

// One frame's calls, recorded while a capture runs: the data behind the Trace tab. Development builds only; the
// draw path asks IsCapturing() (a relaxed load) and does nothing else for trace outside a capture. Each API fills events with
// what its context has bound, as plain values.
namespace Luma {
namespace Trace {
namespace Capture {
enum class EventType : uint8_t
{
   Draw,
   Dispatch,
   LumaPass, // Luma's own pass (Draw's helpers)
   Present,
   Copy, // A copy or a resolve of a whole resource or a region
   Clear, // A view cleared
   CpuWrite, // Map for writing, UpdateSubresource
   CpuRead, // Map for reading
   ThirdPartyWork, // ReShade's and its add-ons' calls at the last present, which aren't recorded one by one
};

enum class CopyKind : uint8_t
{
   Resource,
   Region,
   Resolve,
   BufferRegion,
};

// A copy's arguments, as the game passed them
struct Copy
{
   CopyKind kind = CopyKind::Resource;
   uint32_t source_subresource = 0;
   uint32_t destination_subresource = 0;
   std::array<uint64_t, 3> destination_offset = {};
   // left, top, front, right, bottom, back (bytes for buffers)
   std::optional<std::array<uint64_t, 6>> source_box;
   uint32_t resolve_format = 0;
};

// Details of a CPU read/write operation (Map vs UpdateSubresource)
struct CpuAccess
{
   std::string api_function; // "Map" or "UpdateSubresource"
   std::string map_type; // "Read", "Write", "ReadWrite", "WriteDiscard", "WriteNoOverwrite" (empty for UpdateSubresource)
   uint32_t subresource = 0;
   std::optional<std::array<uint32_t, 6>> destination_box; // UpdateSubresource box (left, top, front, right, bottom, back)
   uint32_t row_pitch = 0;
   uint32_t depth_pitch = 0;
};

// What happened to the game's call
enum class Outcome : uint8_t
{
   Ran,
   Skipped, // A draw callback skipped it
   Replaced, // A draw callback drew in its place
   SkippedByTrace, // The trace's "skip draws" action
};

struct Resource
{
   std::string id; // The object's address ("0x1F2E3D4C5B60")
   std::string type; // "Texture2D", "Buffer"...
   uint32_t format = 0; // DXGI_FORMAT
   // Width, height, depth or array size, mips
   std::array<uint32_t, 4> size = {};
   std::string debug_name;
   // Luma's mirror of it swapped in for the game's (Resources::Mirrors): its format and size
   std::optional<std::string> mirror;
};

struct View
{
   uint32_t slot = 0;
   Resource resource;
   uint32_t format = 0; // The view's
   std::array<uint32_t, 3> size = {}; // At the view's mip
   uint32_t mip = 0;
   // A render target's blend ("SRC_ALPHA ADD INV_SRC_ALPHA / alpha ONE ADD ZERO"), empty when off, and write mask
   std::string blend;
   uint8_t write_mask = 0;
   uint32_t space = 0; // D3D12's register space, after the slot
};

struct Sampler
{
   uint32_t slot = 0;
   std::string filter;
   uint32_t filter_id = 0;
   std::array<uint32_t, 3> address = {}; // U, V, W
   std::string address_names;
   float mip_lod_bias = 0.f;
   uint32_t space = 0;
};

struct ConstantBuffer
{
   ShaderStage stage = ShaderStage::Pixel;
   uint32_t slot = 0;
   std::string id;
   uint32_t size = 0;
   // The D3D11.1 range, 0 when the whole buffer is bound
   uint32_t first_constant = 0;
   uint32_t constant_count = 0;
   // Read back at the end of the capture, for buffers the shader reads on the immediate context (D3D11); copied at the call from
   // an upload heap buffer's memory, or the root constants (D3D12)
   std::vector<std::byte> contents;
   // The contents are the bound range only (D3D12, whose buffers are often large rings), not the whole buffer
   bool contents_ranged = false;
   uint32_t space = 0;
   // Non-empty when this slot is bound to a Luma constant buffer (e.g. "LumaSettings", "LumaData")
   std::string luma_name;
};

struct Shader
{
   ShaderStage stage = ShaderStage::Pixel;
   uint32_t hash = 0; // 0 for one Luma didn't see created, or Luma's own (`luma_shader`)
   std::string name;
   std::string luma_shader; // A Luma pass's custom shader name
   // The hash's variants that compiled, highest priority first, and the one that ran (none for the game's shader)
   std::vector<Shaders::Variants::Source> variants;
   std::optional<Shaders::Variants::Source> variant;
   Shaders::Variants::Choice forced = Shaders::Variants::Choice::Auto; // The trace's choice for the hash then
};

struct DepthStencil
{
   bool state_bound = false;
   bool depth_enable = true;
   bool depth_write = true;
   std::string depth_func;
   bool stencil_enable = false;
   uint32_t stencil_ref = 0;
   uint32_t read_mask = 0;
   uint32_t write_mask = 0;
   std::string front; // "func fail depth_fail pass"
   std::string back;
};

struct Rasterizer
{
   bool state_bound = false;
   std::string cull;
   std::string fill;
   bool front_ccw = false;
   int32_t depth_bias = 0;
   float slope_scaled_depth_bias = 0.f;
   bool depth_clip = true;
   bool scissor_enable = false;
   bool multisample = false;
};

struct Event
{
   EventType type = EventType::Draw;
   uint32_t thread = 0;
   bool deferred = false; // Recorded on a deferred context, in recording order
   // D3D12: the queue that executed the command list the call was recorded on (events are in submission order), and the list
   std::string queue;
   std::string list;
   std::string name; // A Luma pass's name, or the native function
   std::vector<Shader> shaders;
   Outcome outcome = Outcome::Ran;

   // Draw arguments, dispatch group counts
   uint32_t vertex_or_index_count = 0;
   uint32_t instance_count = 0;
   uint32_t start_vertex_or_index = 0;
   int32_t base_vertex = 0;
   uint32_t start_instance = 0;
   bool indexed = false;
   bool indirect = false;
   std::array<uint32_t, 3> dispatch = {};
   uint32_t topology = 0;

   // The views of the draw's pixel stage (its vertex stage without one), or the dispatch's compute stage
   std::vector<View> render_targets;
   std::optional<View> depth_stencil_view;
   std::vector<View> shader_resources;
   std::vector<View> unordered_access;
   std::vector<Sampler> samplers;
   // Of every stage with a shader
   std::vector<ConstantBuffer> constant_buffers;

   bool alpha_to_coverage = false;
   std::array<float, 4> blend_factor = {1.f, 1.f, 1.f, 1.f};
   DepthStencil depth_stencil;
   Rasterizer rasterizer;
   std::vector<std::array<float, 6>> viewports; // x, y, width, height, min depth, max depth
   std::vector<std::array<int32_t, 4>> scissor_rects; // left, top, right, bottom

   // Copies, clears and CPU accesses name whole resources, not views: a copy's source and destination, a
   // cleared view's resource, the resource read or written
   std::optional<Resource> source;
   std::optional<Resource> target;
   std::optional<Copy> copy;
   std::optional<CpuAccess> cpu_access;
   // A clear's color, or depth and stencil
   std::array<float, 4> clear_values = {};
};

struct Frame
{
   uint64_t id = 0; // Captures finished so far, this one included
   std::vector<Event> events;
};

// The shader a call is listed by: the pixel one, else the vertex one (a draw without a pixel shader, as a depth only pass), else the
// compute one, else the first. Null for a present.
const Shader* GetPrimaryShader(const Event& event);
// The instance of the `stage` shader `hash` at `index` in the frame: how many calls of it came before (Track's and readbacks' instance)
uint32_t GetInstance(const Frame& frame, size_t index, ShaderStage stage, uint32_t hash);

// What the trace list shows, in the overlay and through MCP's trace_list. A call is one event with all its stages: the shader
// conditions match any of them.
struct Filter
{
   // Also lists the draws whose primary shader is the vertex one, without a pixel shader; MCP's include_vs
   bool include_vertex_only = false;
   bool luma_passes = true;
   std::optional<ShaderStage> stage; // Has a shader of this stage
   std::optional<uint32_t> hash; // Has this shader
   // Part of a shader's hash ("0x1A2B", "1a2b3c4d") or debug name, or of a Luma pass's name or its shader's, any case
   std::string text;
   bool replaced_only = false; // Its primary shader has variants
   bool cancelled_only = false; // A draw callback or the trace skipped or replaced it
   // Copies, clears, CPU writes and CPU reads; buffer accesses (cbuffer uploads, frequent) only with `buffer_writes`
   bool copies = true;
   bool clears = true;
   bool cpu_writes = true;
   bool cpu_reads = true;
   bool buffer_writes = false;
};

enum class ResourceUsage : uint8_t
{
   None,
   Read,
   Write,
   ReadWrite,
};

bool Matches(const Event& event, const Filter& filter);
// A copy, a clear or a CPU read/write: whole resources, no shader
bool IsResourceEvent(const Event& event);
// How `event` accesses the resource identified by `resource_id` (read, write, or both)
ResourceUsage GetResourceUsage(const Event& event, std::string_view resource_id);
// Resource id highlighted across trace events (set from Trace or Resources panel)
void SetTrackedResource(std::string resource_id);
std::string GetTrackedResource();

// The native functions to hook: the draws and dispatches it records (Development builds always install them)
Hooks::D3D11::Functions GetD3D11Functions();
// The D3D12 ones: draws, and the list state and descriptors a call's state is read from
Hooks::D3D12::Functions GetD3D12Functions();
// Any thread, on the draw path
bool IsCapturing();
// Any thread: captures the frame starting at the next present
void Request();
// Presenting thread: ends the frame being captured, starts a requested one
// Ends any active capture (appending Present and publishing `last`)
void FinishFrame();
// Starts capturing if a capture was requested (called after Present/overlay work so the next frame starts cleanly)
void StartRequestedFrame();
void AdvanceFrame();

struct Added
{
   uint64_t capture = 0;
   size_t index = 0; // In the frame
};

// Any thread, while capturing
Added Add(Event event);
// Before OnPresent ends the frame: a constant buffer's contents read back. Ignored for an event of another capture.
void SetContents(const Added& event, size_t constant_buffer, std::vector<std::byte> contents);

std::shared_ptr<const Frame> GetLast();
void ClearLast();

template <typename Object>
std::string GetObjectId(const Object* object)
{
   return std::format("0x{:X}", reinterpret_cast<uintptr_t>(object));
}

bool IsAnyStageSkipped(std::span<const Shaders::Database::Shader* const> shaders, Shaders::StageSet stages);

template <typename D3DObject>
std::string GetD3DDebugName(D3DObject* object)
{
   constexpr GUID NAME_W = {0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x13, 0x2c, 0x87, 0x3d, 0x93, 0x1c, 0x9a}};
   constexpr GUID NAME_A = {0x429b8c22, 0x9188, 0x4b0c, {0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00}};
   std::array<wchar_t, 256> wide = {};
   uint32_t size = uint32_t(sizeof(wide) - sizeof(wchar_t));
   if (object->GetPrivateData(NAME_W, &size, wide.data()) >= 0)
      return Utils::Strings::ToUtf8(std::wstring_view(wide.data(), wcsnlen(wide.data(), size / sizeof(wchar_t))));
   std::array<char, 256> narrow = {};
   size = uint32_t(narrow.size() - 1);
   if (object->GetPrivateData(NAME_A, &size, narrow.data()) >= 0)
      return std::string(narrow.data(), strnlen(narrow.data(), size));
   return {};
}
} // namespace Capture
} // namespace Trace
} // namespace Luma
#endif
