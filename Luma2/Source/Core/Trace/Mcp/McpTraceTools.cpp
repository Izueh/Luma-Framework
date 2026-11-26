#include "Trace/Mcp/McpTools.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "Resources/ResourceChains.hpp"
#include "Shaders/ShaderVariants.hpp"
#include "Trace/Cbuffers.hpp"
#include "Trace/FrameCapture.hpp"
#include "Utils/Format.hpp"
#include "Utils/Json.hpp"

namespace Luma {
namespace Trace {
namespace Mcp {
// In a named namespace: glaze can't reflect types with internal linkage
namespace Replies {
struct HashCount
{
   std::string hash;
   std::string stage;
   uint32_t total = 0;
   std::optional<std::vector<uint32_t>> per_frame;
   std::optional<uint32_t> frames_seen;
   std::optional<uint32_t> min;
   std::optional<uint32_t> max;
};

struct TraceCapture
{
   bool ok = true;
   size_t count = 0; // Entries of the last capture
   uint32_t frames_captured = 0;
   std::optional<bool> trigger_found;
   std::optional<std::vector<size_t>> trigger_indices;
   std::optional<size_t> distinct_shaders;
   std::optional<std::vector<HashCount>> hash_counts;
   std::vector<std::string> ignored_args;
};

// The luma_trace_list fields of an entry
struct TraceEntry
{
   size_t index = 0;
   std::string type; // shader, custom, present, copy, clear, cpu_write
   std::optional<bool> deferred;
   std::optional<std::string> queue; // D3D12: the queue that executed it, and its command list
   std::optional<std::string> list;
   std::optional<std::string> name;
   std::optional<std::string> stage; // The primary shader's (PS, else VS, else CS)
   std::optional<std::string> stages; // Every stage of the call with a shader, "VS PS"
   std::optional<std::string> hash;
   std::optional<std::string> replaced; // none, file
   std::optional<std::string> variant; // original, file
   std::optional<bool> cancelled_or_replaced;
   std::optional<std::string> outcome; // skipped, replaced, skipped_by_trace
   std::optional<std::string> rt0;
   std::optional<std::string> depth;
   std::optional<std::string> copy_kind; // A copy other than a whole resource's: region, resolve, buffer_region
   std::optional<bool> upgraded;
   std::optional<bool> scaled;
   std::optional<bool> chain_seed;
   std::optional<bool> pending_mirror;
   std::optional<bool> cross_thread_scaling_overlap;
};

struct TraceList
{
   bool ok = true;
   size_t total = 0;
   std::vector<TraceEntry> entries;
   size_t matched = 0;
   size_t offset = 0;
   size_t returned = 0;
   std::vector<std::string> ignored_args;
};

struct TraceShader
{
   std::string stage;
   std::optional<std::string> hash;
   std::optional<std::string> name;
   std::optional<std::string> luma_shader;
   std::optional<std::string> replaced;
   std::optional<std::string> variant;
   std::optional<std::vector<std::string>> variants; // Those that compiled, highest priority first
   std::optional<std::string> forced; // The trace's choice, when not auto
};

struct TraceDraw
{
   uint32_t vertex_count = 0;
   uint32_t instance_count = 0;
   uint32_t first_vertex = 0;
   uint32_t first_instance = 0;
   uint32_t index_count = 0;
   uint32_t first_index = 0;
   int32_t vertex_offset = 0;
   bool indexed = false;
   std::array<uint32_t, 3> dispatch = {};
   bool indirect = false;
   uint32_t topology = 0;
};

struct TraceView
{
   uint32_t slot = 0;
   std::string format;
   std::string view_format;
   std::array<uint32_t, 4> size = {};
   std::array<uint32_t, 3> view_size = {};
   uint32_t mip = 0;
   std::string type;
   std::string resource;
   std::optional<std::string> debug_name;
   std::optional<uint32_t> write_mask;
   std::optional<std::string> blend;
   std::optional<uint32_t> space; // D3D12, a register space other than 0
   std::optional<std::string> mirror; // Luma's mirror swapped in: its format and size
   std::optional<std::string> mirror_format;
   std::optional<std::array<uint32_t, 4>> mirror_size;
   std::optional<std::array<float, 2>> mirror_scale;
   std::optional<std::string> mirror_origin;
   std::optional<std::string> mirror_origin_shader;
   std::optional<bool> mirror_pending;
   std::optional<std::string> role;
};

struct TraceDepthStencil
{
   bool state_bound = false;
   bool depth_enable = true;
   bool depth_write = true;
   std::string depth_func;
   bool stencil_enable = false;
   std::optional<uint32_t> stencil_ref;
   std::optional<uint32_t> read_mask;
   std::optional<uint32_t> write_mask;
   std::optional<std::string> front; // func fail depth_fail pass
   std::optional<std::string> back;
};

struct TraceRasterizer
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

struct TraceField
{
   std::string name;
   std::string type;
   uint32_t offset = 0;
   std::string value;
};

struct TraceConstantBuffer
{
   uint32_t slot = 0;
   std::string stage;
   std::string resource;
   uint32_t size = 0;
   std::optional<uint32_t> first_constant;
   std::optional<uint32_t> num_constants;
   // From the captured contents: where the layout came from (reflection, replacement, raw), and the fields
   std::optional<std::string> layout;
   std::optional<std::vector<TraceField>> fields;
   std::optional<bool> fields_truncated;
   std::optional<uint32_t> space;
   std::optional<std::string> heap_type;
   std::optional<uint32_t> view_size;
};

struct TraceSampler
{
   uint32_t slot = 0;
   std::string filter;
   uint32_t filter_id = 0;
   std::string address;
   std::array<uint32_t, 3> address_ids = {};
   float mip_lod_bias = 0.f;
   std::optional<uint32_t> space;
};

// A copy's, clear's or CPU write's whole resource
struct TraceResource
{
   std::string format;
   std::array<uint32_t, 4> size = {};
   std::string type;
   std::string resource;
   std::optional<std::string> debug_name;
   std::optional<std::string> mirror;
   std::optional<std::string> mirror_format;
   std::optional<std::array<uint32_t, 4>> mirror_size;
   std::optional<std::array<float, 2>> mirror_scale;
   std::optional<std::string> mirror_origin;
   std::optional<std::string> mirror_origin_shader;
   std::optional<bool> mirror_pending;
   std::optional<std::string> role;
};

struct TraceCopy
{
   std::string kind;
   uint32_t source_subresource = 0;
   uint32_t dest_subresource = 0;
   std::array<uint64_t, 3> dest_offset = {};
   std::optional<std::array<uint64_t, 6>> source_box;
   std::optional<std::string> resolve_format;
};

struct TraceCpuAccess
{
   std::string api_function;
   std::optional<std::string> map_type;
   uint32_t subresource = 0;
   std::optional<std::array<uint32_t, 6>> dest_box;
   uint32_t row_pitch = 0;
   uint32_t depth_pitch = 0;
};

struct TraceSrInfo
{
   std::string backend;
   std::string provider_version;
   uint32_t render_width = 0;
   uint32_t render_height = 0;
   uint32_t output_width = 0;
   uint32_t output_height = 0;
   bool dynamic_resolution = false;
   bool hdr = false;
   bool inverted_depth = false;
   bool auto_exposure = false;
   bool mvs_jittered = false;
   bool reset = false;
   bool succeeded = true;
   bool copy_back = false;
   float jitter_x = 0.f;
   float jitter_y = 0.f;
   int32_t jitter_phases = 0;
   float vert_fov = 0.f;
   float near_plane = 0.f;
   float far_plane = 0.f;
   float pre_exposure = 0.f;
   float mvs_x_scale = 1.f;
   float mvs_y_scale = 1.f;
   float sharpness = -1.f;
   float velocity_factor = 1.f;
};

// The entry's luma_trace_list fields, then its state
struct TraceGet
{
   bool ok = true;
   size_t index = 0;
   std::string type;
   std::optional<bool> deferred;
   std::optional<std::string> queue;
   std::optional<std::string> list;
   std::optional<std::string> name;
   std::optional<std::string> stage;
   std::optional<std::string> stages;
   std::optional<std::string> hash;
   std::optional<std::string> replaced;
   std::optional<std::string> variant;
   std::optional<bool> cancelled_or_replaced;
   std::optional<std::string> outcome;
   std::optional<std::string> rt0;
   std::optional<std::string> depth;
   std::optional<std::string> copy_kind;
   std::optional<bool> upgraded;
   std::optional<bool> scaled;
   std::optional<bool> chain_seed;
   std::optional<bool> pending_mirror;
   std::optional<bool> cross_thread_scaling_overlap;
   uint32_t thread = 0;
   std::optional<TraceResource> source;
   std::optional<TraceResource> target;
   std::optional<TraceCopy> copy;
   std::optional<bool> copy_scaled;
   std::optional<TraceCpuAccess> cpu_access;
   std::optional<std::array<float, 4>> clear_values;
   std::vector<TraceShader> shaders;
   TraceDraw draw;
   std::optional<TraceDepthStencil> depth_stencil;
   std::optional<TraceRasterizer> rasterizer;
   std::optional<size_t> viewport_count;
   std::optional<std::vector<std::array<float, 6>>> viewports;
   std::optional<bool> viewports_scaled;
   std::optional<std::vector<std::array<float, 6>>> original_viewports;
   std::optional<size_t> scissor_count;
   std::optional<std::vector<std::array<int32_t, 4>>> scissor_rects;
   std::optional<bool> scissors_scaled;
   std::optional<std::vector<std::array<int32_t, 4>>> original_scissor_rects;
   std::vector<TraceView> rtvs;
   bool alpha_to_coverage = false;
   std::array<float, 4> blend_factor = {};
   std::vector<TraceView> srvs;
   std::vector<TraceView> uavs;
   std::optional<TraceView> dsv;
   std::vector<TraceConstantBuffer> cbs;
   std::vector<TraceSampler> samplers;
   std::optional<TraceSrInfo> sr;
   std::vector<std::string> ignored_args;
};
} // namespace Replies

namespace {
using Capture::Event;
using Capture::EventType;

// "PS", " vs "...
std::optional<ShaderStage> ParseStage(std::string_view name)
{
   while (!name.empty() && std::isspace(uint8_t(name.front())))
      name.remove_prefix(1);
   while (!name.empty() && std::isspace(uint8_t(name.back())))
      name.remove_suffix(1);
   for (uint32_t i = 0; i <= uint32_t(ShaderStage::Compute); i++)
   {
      const std::string_view stage = GetStageName(ShaderStage(i));
      if (name.size() == stage.size() && _strnicmp(name.data(), stage.data(), stage.size()) == 0)
         return ShaderStage(i);
   }
   return std::nullopt;
}

// The "replaced" field: the source of the hash's first variant, or "none"
std::string GetReplacedName(const Capture::Shader& shader)
{
   return shader.variants.empty() ? "none" : std::string(Shaders::Variants::GetName(shader.variants.front()));
}

// The "variant" field: the source of the variant that ran, or "original" for a shader with variants that ran the game's
std::optional<std::string> GetVariantName(const Capture::Shader& shader)
{
   if (shader.variant)
      return std::string(Shaders::Variants::GetName(*shader.variant));
   return shader.variants.empty() ? std::nullopt : std::optional<std::string>("original");
}

std::string_view GetTypeName(EventType type)
{
   switch (type)
   {
   case EventType::LumaPass: return "custom";
   case EventType::Present: return "present";
   case EventType::Copy: return "copy";
   case EventType::Clear: return "clear";
   case EventType::CpuWrite: return "cpu_write";
   case EventType::CpuRead: return "cpu_read";
   case EventType::ThirdPartyWork: return "third_party";
   default: return "shader";
   }
}

// Fills a luma_trace_list entry from an event
template <typename Reply>
void WriteSummary(Reply& reply, const Event& event, size_t index)
{
   reply.index = index;
   reply.type = GetTypeName(event.type);
   if (event.deferred)
      reply.deferred = true;
   if (!event.queue.empty())
      reply.queue = event.queue;
   if (!event.list.empty())
      reply.list = event.list;
   if (event.type == EventType::LumaPass || event.type == EventType::Present || event.type == EventType::ThirdPartyWork || Capture::IsResourceEvent(event))
      reply.name = event.name;
   // A copy's, clear's or CPU access's target
   if (event.target)
   {
      if (event.target->type == "Buffer")
         reply.rt0 = std::format("Buffer {}B", event.target->size[0]);
      else
         reply.rt0 = std::format("{} {}x{}", Utils::Format::GetName(event.target->format), event.target->size[0], event.target->size[1]);
   }
   if (event.cpu_access && !event.cpu_access->map_type.empty())
      reply.name = std::format("{} ({})", event.name, event.cpu_access->map_type);
   if (event.copy && event.copy->kind != Capture::CopyKind::Resource)
   {
      constexpr std::string_view KINDS[] = {"resource", "region", "resolve", "buffer_region"};
      reply.copy_kind = std::string(KINDS[size_t(event.copy->kind)]);
   }
   // The call is listed once, by its primary shader, with its stages
   if (!event.shaders.empty())
   {
      std::string stages;
      for (const Capture::Shader& shader : event.shaders)
         stages += std::format("{}{}", stages.empty() ? "" : " ", GetStageName(shader.stage));
      reply.stages = std::move(stages);
   }
   if (const Capture::Shader* shader = Capture::GetPrimaryShader(event))
   {
      reply.stage = std::string(GetStageName(shader->stage));
      if (event.type != EventType::LumaPass)
      {
         reply.hash = FormatHash(shader->hash);
         reply.replaced = GetReplacedName(*shader);
         if (!shader->name.empty())
            reply.name = shader->name;
         reply.variant = GetVariantName(*shader);
      }
   }
   if (event.outcome != Capture::Outcome::Ran)
   {
      constexpr std::string_view OUTCOMES[] = {"", "skipped", "replaced", "skipped_by_trace"};
      reply.cancelled_or_replaced = true;
      reply.outcome = std::string(OUTCOMES[size_t(event.outcome)]);
   }
   if (!event.render_targets.empty() && event.render_targets.front().slot == 0)
   {
      const Capture::View& target = event.render_targets.front();
      reply.rt0 = std::format("{} {}x{}", Utils::Format::GetName(target.format), target.size[0], target.size[1]);
   }
   if (event.depth_stencil_view && event.depth_stencil.depth_enable)
      reply.depth = std::format("{}{}", event.depth_stencil.depth_func, event.depth_stencil.depth_write ? " write" : "");
   if (Capture::HasUpgradedResource(event))
      reply.upgraded = true;
   if (Capture::IsScaledEvent(event))
      reply.scaled = true;
   if (event.chain_seed)
      reply.chain_seed = true;
   const auto has_pending = [](const std::vector<Capture::View>& views) {
      return std::ranges::any_of(views, [](const Capture::View& view) { return view.resource.mirror_info && view.resource.mirror_info->pending; });
   };
   if (has_pending(event.render_targets) || has_pending(event.shader_resources) || has_pending(event.unordered_access) ||
      (event.depth_stencil_view && event.depth_stencil_view->resource.mirror_info && event.depth_stencil_view->resource.mirror_info->pending) ||
      (event.source && event.source->mirror_info && event.source->mirror_info->pending) ||
      (event.target && event.target->mirror_info && event.target->mirror_info->pending))
      reply.pending_mirror = true;
   if (event.cross_thread_scaling_overlap)
      reply.cross_thread_scaling_overlap = true;
}

bool HasResource(const Event& event, std::string_view id)
{
   const auto in = [&](const std::vector<Capture::View>& views) { return std::ranges::any_of(views, [&](const Capture::View& view) { return view.resource.id == id; }); };
   return in(event.render_targets) || in(event.shader_resources) || in(event.unordered_access) || (event.depth_stencil_view && event.depth_stencil_view->resource.id == id) ||
      std::ranges::any_of(event.constant_buffers, [&](const Capture::ConstantBuffer& buffer) { return buffer.id == id; }) ||
      (event.source && event.source->id == id) || (event.target && event.target->id == id);
}

// Waits on the pipe thread for the capture of the next frame
std::shared_ptr<const Capture::Frame> CaptureFrame(std::chrono::steady_clock::time_point deadline)
{
   const std::shared_ptr<const Capture::Frame> previous = Capture::GetLast();
   Capture::Request();
   while (Capture::GetLast() == previous)
   {
      if (std::chrono::steady_clock::now() > deadline)
         return nullptr;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
   }
   return Capture::GetLast();
}

template <typename Reply>
void PopulateMirrorInfo(Reply& reply, const Capture::Resource& resource)
{
   reply.mirror = resource.mirror;
   if (!resource.mirror_info)
      return;
   const Capture::MirrorInfo& info = *resource.mirror_info;
   if (info.pending)
   {
      reply.mirror_pending = true;
      return;
   }
   reply.mirror_format = std::string(Utils::Format::GetName(info.format));
   reply.mirror_size = info.size;
   if (info.is_scaled)
      reply.mirror_scale = info.scale;
   reply.mirror_origin = std::string(Resources::Chains::GetOriginName(info.origin.kind));
   if (info.origin.shader_hash != 0)
      reply.mirror_origin_shader = FormatHash(info.origin.shader_hash);
}

Replies::TraceView ToReply(const Capture::View& view)
{
   const Capture::Resource& resource = view.resource;
   Replies::TraceView reply = {view.slot, std::string(Utils::Format::GetName(resource.format)), std::string(Utils::Format::GetName(view.format)), resource.size,
      view.size, view.mip, resource.type, resource.id};
   if (!resource.debug_name.empty())
      reply.debug_name = resource.debug_name;
   if (view.space != 0)
      reply.space = view.space;
   if (!view.role.empty())
      reply.role = view.role;
   PopulateMirrorInfo(reply, resource);
   return reply;
}
} // namespace

namespace Tools {
std::string TraceCapture(const Request& request)
{
   std::optional<uint32_t> trigger;
   if (request.args.contains("trigger") && !(trigger = GetHash(request, "trigger")))
      return Error("'trigger' is a shader hash, like 0x12345678");
   const uint32_t max_frames = GetNumber(request, "max_frames", 120, 1, 3600);
   const uint32_t frames = trigger ? 1 : GetNumber(request, "frames", 1, 1, 600);
   const uint32_t timeout = GetTimeout(request);
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
   Replies::TraceCapture reply;
   reply.ignored_args = GetIgnoredArgs(request, {"trigger", "max_frames", "frames", "top", "summary"});
   // Draws per shader hash, per captured frame
   std::map<uint32_t, std::pair<ShaderStage, std::vector<uint32_t>>> counts;
   while (true)
   {
      const std::shared_ptr<const Capture::Frame> frame = CaptureFrame(deadline);
      if (!frame)
         return Error(std::format("timed out after {} ms with {} frames captured: the game isn't presenting (minimized, paused or loading)", timeout, reply.frames_captured));
      reply.frames_captured++;
      reply.count = frame->events.size();
      if (trigger)
      {
         std::vector<size_t> indices;
         for (size_t i = 0; i < frame->events.size() && indices.size() < 32; i++)
         {
            if (std::ranges::any_of(frame->events[i].shaders, [&](const Capture::Shader& shader) { return shader.hash == *trigger; }))
               indices.push_back(i);
         }
         if (indices.empty() && reply.frames_captured < max_frames)
            continue;
         reply.trigger_found = !indices.empty();
         reply.trigger_indices = std::move(indices);
         return Utils::Json::Write(reply);
      }
      if (frames == 1)
         return Utils::Json::Write(reply);
      for (const Event& event : frame->events)
      {
         for (const Capture::Shader& shader : event.shaders)
         {
            if (event.type == EventType::LumaPass || !shader.hash)
               continue;
            auto& [stage, per_frame] = counts.try_emplace(shader.hash, shader.stage, std::vector<uint32_t>()).first->second;
            per_frame.resize(reply.frames_captured, 0);
            per_frame.back()++;
         }
      }
      if (reply.frames_captured >= frames)
         break;
   }
   std::vector<std::pair<uint32_t, uint32_t>> totals; // Hash, draws
   for (auto& [hash, count] : counts)
   {
      count.second.resize(reply.frames_captured, 0);
      totals.emplace_back(hash, std::accumulate(count.second.begin(), count.second.end(), 0u));
   }
   std::ranges::sort(totals, std::greater(), &std::pair<uint32_t, uint32_t>::second);
   const uint32_t top = GetNumber(request, "top", 64, 1, 5000);
   const bool summary = GetBool(request, "summary");
   reply.distinct_shaders = totals.size();
   reply.hash_counts.emplace();
   for (size_t i = 0; i < std::min<size_t>(top, totals.size()); i++)
   {
      const auto& [stage, per_frame] = counts[totals[i].first];
      Replies::HashCount& entry = reply.hash_counts->emplace_back(FormatHash(totals[i].first), std::string(GetStageName(stage)), totals[i].second);
      if (summary)
      {
         entry.frames_seen = uint32_t(std::ranges::count_if(per_frame, [](uint32_t count) { return count != 0; }));
         entry.min = std::ranges::min(per_frame);
         entry.max = std::ranges::max(per_frame);
      }
      else
         entry.per_frame = per_frame;
   }
   return Utils::Json::Write(reply);
}

std::string TraceList(const Request& request)
{
   const std::shared_ptr<const Capture::Frame> frame = Capture::GetLast();
   if (!frame)
      return Error("nothing captured yet, run luma_trace_capture first");
   const auto arg = [&](std::string_view key) {
      const auto it = request.args.find(key);
      return it != request.args.end() ? std::optional(it->second) : std::nullopt;
   };
   const std::optional<std::string> type = arg("type");
   const std::optional<std::string> resource = arg("resource");
   const std::optional<std::string> variant = arg("variant");
   // The overlay's filter: the shader conditions match any stage of a call
   Capture::Filter filter = {
      .include_vertex_only = GetBool(request, "include_vs"),
      .text = arg("filter").value_or(std::string()),
      .replaced_only = GetBool(request, "replaced_only"),
      .cancelled_only = GetBool(request, "cancelled_only"),
      .upgraded_only = GetBool(request, "upgraded_only"),
      .scaled_only = GetBool(request, "scaled_only"),
      .buffer_writes = GetBool(request, "include_buffer_writes"),
      .third_party = GetBool(request, "include_third_party") || (type && *type == "third_party"),
   };
   if (const std::optional<std::string> stage = arg("stage"); stage && !(filter.stage = ParseStage(*stage)))
      return Error("'stage' is VS, HS, DS, GS, PS or CS");
   if (request.args.contains("hash") && !(filter.hash = GetHash(request)))
      return Error("'hash' is a shader hash, like 0x12345678");
   const uint32_t offset = GetNumber(request, "offset", 0, 0, UINT32_MAX);
   const uint32_t limit = GetNumber(request, "limit", 300, 1, 5000);
   Replies::TraceList reply;
   // 2.0 records no binding entries yet: include_bindings has nothing to include
   reply.ignored_args = GetIgnoredArgs(request,
      {"type", "stage", "resource", "variant", "hash", "filter", "include_vs", "include_bindings", "include_buffer_writes", "include_third_party", "replaced_only", "cancelled_only",
         "upgraded_only", "scaled_only", "offset", "limit"});
   reply.total = frame->events.size();
   for (size_t i = 0; i < frame->events.size(); i++)
   {
      const Event& event = frame->events[i];
      if (!Capture::Matches(event, filter) || (type && *type != GetTypeName(event.type)))
         continue;
      const Capture::Shader* main = Capture::GetPrimaryShader(event);
      if (variant && (!main || GetVariantName(*main) != *variant))
         continue;
      if (resource && !HasResource(event, *resource))
         continue;
      if (reply.matched++ < offset || reply.entries.size() >= limit)
         continue;
      WriteSummary(reply.entries.emplace_back(), event, i);
   }
   reply.offset = offset;
   reply.returned = reply.entries.size();
   return Utils::Json::Write(reply);
}

std::string TraceGet(const Request& request)
{
   const std::shared_ptr<const Capture::Frame> frame = Capture::GetLast();
   const uint32_t index = GetNumber(request, "index", UINT32_MAX, 0, UINT32_MAX);
   if (!frame || index >= frame->events.size())
      return Error(std::format("trace index {} is out of range (captured {})", index, frame ? frame->events.size() : 0));
   const Event& event = frame->events[index];
   Replies::TraceGet reply;
   reply.ignored_args = GetIgnoredArgs(request, {"index"});
   WriteSummary(reply, event, index);
   reply.thread = event.thread;
   if (Capture::IsResourceEvent(event))
   {
      const auto to_reply = [](const Capture::Resource& resource) {
         Replies::TraceResource out{std::string(Utils::Format::GetName(resource.format)), resource.size, resource.type, resource.id,
            resource.debug_name.empty() ? std::nullopt : std::optional(resource.debug_name)};
         PopulateMirrorInfo(out, resource);
         return out;
      };
      if (event.source)
         reply.source = to_reply(*event.source);
      if (event.target)
         reply.target = to_reply(*event.target);
      if (const std::optional<Capture::Copy>& copy = event.copy)
      {
         constexpr std::string_view KINDS[] = {"resource", "region", "resolve", "buffer_region"};
         reply.copy = Replies::TraceCopy{std::string(KINDS[size_t(copy->kind)]), copy->source_subresource, copy->destination_subresource, copy->destination_offset,
            copy->source_box, copy->kind == Capture::CopyKind::Resolve ? std::optional(std::string(Utils::Format::GetName(copy->resolve_format))) : std::nullopt};
      }
      if (event.copy_scaled)
         reply.copy_scaled = true;
      if (const std::optional<Capture::CpuAccess>& access = event.cpu_access)
      {
         reply.cpu_access = Replies::TraceCpuAccess{
            access->api_function,
            access->map_type.empty() ? std::nullopt : std::optional(access->map_type),
            access->subresource,
            access->destination_box,
            access->row_pitch,
            access->depth_pitch,
         };
      }
      if (event.type == EventType::Clear)
         reply.clear_values = event.clear_values;
      return Utils::Json::Write(reply);
   }
   for (const Capture::Shader& shader : event.shaders)
   {
      Replies::TraceShader& entry = reply.shaders.emplace_back();
      entry.stage = std::string(GetStageName(shader.stage));
      if (!shader.luma_shader.empty())
      {
         entry.luma_shader = shader.luma_shader;
         continue;
      }
      entry.hash = FormatHash(shader.hash);
      if (!shader.name.empty())
         entry.name = shader.name;
      entry.replaced = GetReplacedName(shader);
      entry.variant = GetVariantName(shader);
      if (!shader.variants.empty())
      {
         entry.variants.emplace();
         for (const Shaders::Variants::Source source : shader.variants)
            entry.variants->emplace_back(Shaders::Variants::GetName(source));
      }
      if (shader.forced != Shaders::Variants::Choice::Auto)
         entry.forced = std::string(Shaders::Variants::GetName(shader.forced));
   }
   reply.draw = {.vertex_count = event.indexed ? 0 : event.vertex_or_index_count,
      .instance_count = event.instance_count,
      .first_vertex = event.indexed ? 0 : event.start_vertex_or_index,
      .first_instance = event.start_instance,
      .index_count = event.indexed ? event.vertex_or_index_count : 0,
      .first_index = event.indexed ? event.start_vertex_or_index : 0,
      .vertex_offset = event.base_vertex,
      .indexed = event.indexed,
      .dispatch = event.dispatch,
      .indirect = event.indirect,
      .topology = event.topology};
   if (event.type == EventType::Draw || event.type == EventType::LumaPass)
   {
      const Capture::DepthStencil& depth = event.depth_stencil;
      Replies::TraceDepthStencil& depth_reply = reply.depth_stencil.emplace(depth.state_bound, depth.depth_enable, depth.depth_write, depth.depth_func, depth.stencil_enable);
      if (depth.stencil_enable)
      {
         depth_reply.stencil_ref = depth.stencil_ref;
         depth_reply.read_mask = depth.read_mask;
         depth_reply.write_mask = depth.write_mask;
         depth_reply.front = depth.front;
         depth_reply.back = depth.back;
      }
      const Capture::Rasterizer& rasterizer = event.rasterizer;
      reply.rasterizer = Replies::TraceRasterizer{rasterizer.state_bound, rasterizer.cull, rasterizer.fill, rasterizer.front_ccw, rasterizer.depth_bias,
         rasterizer.slope_scaled_depth_bias, rasterizer.depth_clip, rasterizer.scissor_enable, rasterizer.multisample};
      reply.viewport_count = event.viewports.size();
      reply.viewports = event.viewports;
      if (event.viewports_scaled)
      {
         reply.viewports_scaled = true;
         reply.original_viewports = event.original_viewports;
      }
      reply.scissor_count = event.scissor_rects.size();
      reply.scissor_rects = event.scissor_rects;
      if (event.scissors_scaled)
      {
         reply.scissors_scaled = true;
         reply.original_scissor_rects = event.original_scissor_rects;
      }
   }
   for (const Capture::View& view : event.render_targets)
   {
      Replies::TraceView& target = reply.rtvs.emplace_back(ToReply(view));
      target.write_mask = view.write_mask;
      if (!view.blend.empty())
         target.blend = view.blend;
   }
   reply.alpha_to_coverage = event.alpha_to_coverage;
   reply.blend_factor = event.blend_factor;
   for (const Capture::View& view : event.shader_resources)
      reply.srvs.push_back(ToReply(view));
   for (const Capture::View& view : event.unordered_access)
      reply.uavs.push_back(ToReply(view));
   if (event.depth_stencil_view)
      reply.dsv = ToReply(*event.depth_stencil_view);
   // Typed fields, a few hundred at most per buffer
   constexpr size_t MAX_FIELDS = 256;
   for (const Capture::ConstantBuffer& buffer : event.constant_buffers)
   {
      Replies::TraceConstantBuffer& entry = reply.cbs.emplace_back(buffer.slot, std::string(GetStageName(buffer.stage)), buffer.id, buffer.size);
      if (buffer.constant_count)
      {
         entry.first_constant = buffer.first_constant;
         entry.num_constants = buffer.constant_count;
      }
      if (buffer.space != 0)
         entry.space = buffer.space;
      if (!buffer.heap_type.empty())
         entry.heap_type = buffer.heap_type;
      if (buffer.view_size != 0)
         entry.view_size = buffer.view_size;
      if (buffer.contents.empty())
         continue;
      std::span<const std::byte> contents = buffer.contents;
      if (buffer.constant_count && !buffer.contents_ranged)
      {
         const size_t first = std::min<size_t>(size_t(buffer.first_constant) * 16, contents.size());
         contents = contents.subspan(first, std::min<size_t>(size_t(buffer.constant_count) * 16, contents.size() - first));
      }
      uint32_t hash = 0;
      // The shader's layouts are by space 0 slot: another space's buffer is shown raw
      for (const Capture::Shader& shader : event.shaders)
         hash = shader.stage == buffer.stage && buffer.space == 0 ? shader.hash : hash;
      const Cbuffers::Description description = Cbuffers::Describe(hash, buffer.slot, contents);
      entry.layout = description.source;
      entry.fields.emplace();
      for (size_t i = 0; i < std::min(description.fields.size(), MAX_FIELDS); i++)
      {
         const Cbuffers::Field& field = description.fields[i];
         entry.fields->push_back({field.name, field.type, field.offset, field.value});
      }
      if (description.fields.size() > MAX_FIELDS)
         entry.fields_truncated = true;
   }
   for (const Capture::Sampler& sampler : event.samplers)
      reply.samplers.push_back({sampler.slot, sampler.filter, sampler.filter_id, sampler.address_names, sampler.address, sampler.mip_lod_bias,
         sampler.space != 0 ? std::optional<uint32_t>(sampler.space) : std::nullopt});
   if (event.sr)
   {
      const Capture::SrInfo& s = *event.sr;
      reply.sr = Replies::TraceSrInfo{s.backend, s.provider_version, s.render_width, s.render_height, s.output_width, s.output_height, s.dynamic_resolution,
         s.hdr, s.inverted_depth, s.auto_exposure, s.mvs_jittered, s.reset, s.succeeded, s.copy_back, s.jitter_x, s.jitter_y, s.jitter_phases, s.vert_fov,
         s.near_plane, s.far_plane, s.pre_exposure, s.mvs_x_scale, s.mvs_y_scale, s.sharpness, s.velocity_factor};
   }
   return Utils::Json::Write(reply);
}
} // namespace Tools
} // namespace Mcp
} // namespace Trace
} // namespace Luma
#endif
