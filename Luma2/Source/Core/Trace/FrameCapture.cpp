#include "Trace/FrameCapture.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <mutex>
#include <utility>

#include "Log/Log.hpp"

namespace Luma {
namespace Trace {
namespace Capture {
namespace {
std::atomic<bool> capturing = false;
std::atomic<bool> requested = false;
// Never held with another lock
std::mutex mutex;
std::vector<Event> events;
std::shared_ptr<const Frame> last;
uint64_t count = 0;
// Of the capture running, so contents read back late don't land in the next one
uint64_t started = 0;
std::string tracked_resource;
} // namespace

const Shader* GetPrimaryShader(const Event& event)
{
   for (const ShaderStage stage : {ShaderStage::Pixel, ShaderStage::Vertex, ShaderStage::Compute})
   {
      if (const auto it = std::ranges::find(event.shaders, stage, &Shader::stage); it != event.shaders.end())
         return &*it;
   }
   return event.shaders.empty() ? nullptr : &event.shaders.front();
}

uint32_t GetInstance(const Frame& frame, size_t index, ShaderStage stage, uint32_t hash)
{
   uint32_t instance = 0;
   for (size_t i = 0; i < index && i < frame.events.size(); i++)
   {
      for (const Shader& shader : frame.events[i].shaders)
         instance += shader.stage == stage && shader.hash == hash && shader.luma_shader.empty();
   }
   return instance;
}

bool IsResourceEvent(const Event& event)
{
   return event.type == EventType::Copy || event.type == EventType::Clear || event.type == EventType::CpuWrite || event.type == EventType::CpuRead;
}

ResourceUsage GetResourceUsage(const Event& event, std::string_view resource_id)
{
   if (resource_id.empty())
      return ResourceUsage::None;
   bool read = false;
   bool write = false;
   if (IsResourceEvent(event))
   {
      if (event.type == EventType::Copy)
      {
         read |= event.source && event.source->id == resource_id;
         write |= event.target && event.target->id == resource_id;
      }
      else if (event.type == EventType::CpuRead)
      {
         read |= event.target && event.target->id == resource_id;
      }
      else
      {
         write |= event.target && event.target->id == resource_id;
      }
   }
   else
   {
      for (const View& view : event.shader_resources)
         read |= view.resource.id == resource_id;
      for (const ConstantBuffer& cb : event.constant_buffers)
         read |= cb.id == resource_id;
      for (const View& view : event.render_targets)
         write |= view.resource.id == resource_id;
      if (event.depth_stencil_view && event.depth_stencil_view->resource.id == resource_id)
      {
         if (event.depth_stencil.depth_write || event.depth_stencil.stencil_enable)
            write = true;
         else
            read = true;
      }
      for (const View& view : event.unordered_access)
      {
         if (view.resource.id == resource_id)
         {
            read = true;
            write = true;
         }
      }
   }
   if (read && write)
      return ResourceUsage::ReadWrite;
   if (write)
      return ResourceUsage::Write;
   if (read)
      return ResourceUsage::Read;
   return ResourceUsage::None;
}

void SetTrackedResource(std::string resource_id)
{
   std::scoped_lock lock(mutex);
   tracked_resource = std::move(resource_id);
}

std::string GetTrackedResource()
{
   std::scoped_lock lock(mutex);
   return tracked_resource;
}

namespace {
// A mirror swapped in, or upgraded in place
bool IsUpgraded(const Resource& resource)
{
   return resource.mirror.has_value() || resource.mirror_info.has_value() || resource.in_place_from.has_value();
}
} // namespace

bool HasUpgradedResource(const Event& event)
{
   const auto any_view = [](const std::vector<View>& views) {
      return std::ranges::any_of(views, [](const View& v) { return IsUpgraded(v.resource); });
   };
   return any_view(event.render_targets) || any_view(event.shader_resources) || any_view(event.unordered_access) ||
      (event.depth_stencil_view && IsUpgraded(event.depth_stencil_view->resource)) || (event.source && IsUpgraded(*event.source)) ||
      (event.target && IsUpgraded(*event.target));
}

bool IsScaledEvent(const Event& event)
{
   if (event.viewports_scaled || event.scissors_scaled || event.copy_scaled)
      return true;
   // A scaled mirror, or a texture resized in place
   const auto is_scaled = [](const Resource& resource) { return (resource.mirror_info && resource.mirror_info->is_scaled) || resource.in_place_game_size.has_value(); };
   const auto any_scaled = [&](const std::vector<View>& views) { return std::ranges::any_of(views, [&](const View& v) { return is_scaled(v.resource); }); };
   return any_scaled(event.render_targets) || any_scaled(event.shader_resources) || any_scaled(event.unordered_access) ||
      (event.depth_stencil_view && is_scaled(event.depth_stencil_view->resource)) || (event.source && is_scaled(*event.source)) ||
      (event.target && is_scaled(*event.target));
}

bool Matches(const Event& event, const Filter& filter)
{
   if (filter.upgraded_only && !HasUpgradedResource(event))
      return false;
   if (filter.scaled_only && !IsScaledEvent(event))
      return false;
   const auto contains = [&](std::string_view text) {
      return !std::ranges::search(text, filter.text, [](char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); }).empty();
   };
   const auto matches_resource = [&](const Resource& resource) { return contains(resource.id) || contains(resource.debug_name); };
   const bool has_shader_filter = filter.stage || filter.hash || !filter.text.empty();
   if (event.type == EventType::Present)
      return !has_shader_filter && !filter.replaced_only && !filter.cancelled_only;
   if (event.type == EventType::ThirdPartyWork)
   {
      if (!filter.third_party)
         return false;
      if (event.shaders.empty())
         return !filter.stage && !filter.hash && !filter.replaced_only && !filter.cancelled_only && (filter.text.empty() || contains(event.name));
      const Shader* primary = GetPrimaryShader(event);
      if (filter.stage && std::ranges::none_of(event.shaders, [&](const Shader& shader) { return shader.stage == *filter.stage; }))
         return false;
      if (filter.hash && std::ranges::none_of(event.shaders, [&](const Shader& shader) { return shader.hash == *filter.hash; }))
         return false;
      if (filter.replaced_only && (!primary || primary->variants.empty()))
         return false;
      if (filter.cancelled_only && event.outcome == Outcome::Ran)
         return false;
      if (filter.text.empty() || contains(event.name))
         return true;
      return std::ranges::any_of(event.shaders, [&](const Shader& shader) { return contains(std::format("0x{:08X}", shader.hash)) || contains(shader.name); });
   }
   if (IsResourceEvent(event))
   {
      if (filter.stage || filter.hash || filter.replaced_only || filter.cancelled_only)
         return false;
      if (event.type == EventType::Copy && !filter.copies)
         return false;
      if (event.type == EventType::Clear && !filter.clears)
         return false;
      if (event.type == EventType::CpuWrite && !filter.cpu_writes)
         return false;
      if (event.type == EventType::CpuRead && !filter.cpu_reads)
         return false;
      if ((event.type == EventType::CpuWrite || event.type == EventType::CpuRead) && event.target && event.target->type == "Buffer" && !filter.buffer_writes)
         return false;
      if (filter.text.empty())
         return true;
      return contains(event.name) || (event.cpu_access && contains(event.cpu_access->map_type)) || (event.source && matches_resource(*event.source)) ||
         (event.target && matches_resource(*event.target));
   }
   if (event.type == EventType::LumaPass && !filter.luma_passes)
      return false;
   const Shader* primary = GetPrimaryShader(event);
   if (event.type == EventType::Draw && primary && primary->stage == ShaderStage::Vertex && !filter.include_vertex_only)
      return false;
   if (filter.stage && std::ranges::none_of(event.shaders, [&](const Shader& shader) { return shader.stage == *filter.stage; }))
      return false;
   if (filter.hash && std::ranges::none_of(event.shaders, [&](const Shader& shader) { return shader.hash == *filter.hash; }))
      return false;
   if (filter.replaced_only && (!primary || primary->variants.empty()))
      return false;
   if (filter.cancelled_only && event.outcome == Outcome::Ran)
      return false;
   if (filter.text.empty())
      return true;
   if (event.type == EventType::LumaPass && contains(event.name))
      return true;
   const bool shader_match = std::ranges::any_of(event.shaders, [&](const Shader& shader) {
      if (!shader.luma_shader.empty())
         return contains(shader.luma_shader);
      return contains(std::format("0x{:08X}", shader.hash)) || contains(shader.name);
   });
   if (shader_match)
      return true;
   const auto views_match = [&](std::span<const View> views) {
      return std::ranges::any_of(views, [&](const View& view) { return matches_resource(view.resource); });
   };
   if (views_match(event.render_targets) || views_match(event.shader_resources) || views_match(event.unordered_access))
      return true;
   if (event.depth_stencil_view && matches_resource(event.depth_stencil_view->resource))
      return true;
   return std::ranges::any_of(event.constant_buffers, [&](const ConstantBuffer& cb) { return contains(cb.id); });
}

Hooks::D3D11::Functions GetD3D11Functions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   for (const Hooks::D3D11::Function function :
      {Draw, DrawInstanced, DrawIndexed, DrawIndexedInstanced, DrawAuto, DrawInstancedIndirect, DrawIndexedInstancedIndirect, Dispatch, DispatchIndirect,
         // Copies, clears and CPU writes
         Map, UpdateSubresource, CopyResource, CopySubresourceRegion, ResolveSubresource, ClearRenderTargetView, ClearDepthStencilView,
         ClearUnorderedAccessViewUint, ClearUnorderedAccessViewFloat, ClearView})
      functions.set(size_t(function));
   return functions;
}

Hooks::D3D12::Functions GetD3D12Functions()
{
   using enum Hooks::D3D12::Function;
   Hooks::D3D12::Functions functions;
   for (const Hooks::D3D12::Function function : {CreateRootSignature, CreatePipelineState, CreateDescriptorHeap, CreateView, CopyDescriptors, CreateResource,
           Reset, ClearState, SetPipelineState, SetRootSignature, OMSetRenderTargets, SetDescriptorHeaps, SetRootArguments, SetFixedState, ResourceBarrier, Copy, Clear, Draw, Dispatch,
           ExecuteIndirect, ExecuteBundle})
      functions.set(size_t(function));
   return functions;
}

bool IsCapturing()
{
   return capturing.load(std::memory_order_relaxed);
}

void Request()
{
   requested = true;
}

void FinishFrame()
{
   std::scoped_lock lock(mutex);
   if (capturing)
   {
      capturing = false;
      events.push_back({.type = EventType::Present, .name = "Present"});
      auto frame = std::make_shared<Frame>(++count, std::move(events));
      Log::Info("Trace: captured {} calls", frame->events.size() - 1);
      last = std::move(frame);
      events = {};
   }
}

void StartRequestedFrame()
{
   std::scoped_lock lock(mutex);
   if (requested.exchange(false))
   {
      // Calls that saw the last capture running and were added after it ended
      events.clear();
      started++;
      capturing = true;
   }
}

Added Add(Event event)
{
   std::scoped_lock lock(mutex);
   events.push_back(std::move(event));
   return {started, events.size() - 1};
}

void SetContents(const Added& event, size_t constant_buffer, std::vector<std::byte> contents)
{
   std::scoped_lock lock(mutex);
   if (event.capture == started && event.index < events.size() && constant_buffer < events[event.index].constant_buffers.size())
      events[event.index].constant_buffers[constant_buffer].contents = std::move(contents);
}

std::shared_ptr<const Frame> GetLast()
{
   std::scoped_lock lock(mutex);
   return last;
}

void ClearLast()
{
   std::scoped_lock lock(mutex);
   last.reset();
}

bool IsAnyStageSkipped(std::span<const Shaders::Database::Shader* const> shaders, Shaders::StageSet stages)
{
   for (size_t stage = 0; stage < shaders.size(); stage++)
   {
      if (stages.Has(ShaderStage(stage)) && shaders[stage] && shaders[stage]->skip_draws.load(std::memory_order_relaxed))
         return true;
   }
   return false;
}
} // namespace Capture
} // namespace Trace
} // namespace Luma
#endif
