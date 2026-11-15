#include "Trace/Capture.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <mutex>
#include <utility>

#include "Utils/Log.hpp"

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

bool Matches(const Event& event, const Filter& filter)
{
   const bool has_shader_filter = filter.stage || filter.hash || !filter.text.empty();
   if (event.type == EventType::Present)
      return !has_shader_filter && !filter.replaced_only && !filter.cancelled_only;
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
   const auto contains = [&](std::string_view text) {
      return !std::ranges::search(text, filter.text, [](char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); }).empty();
   };
   if (event.type == EventType::LumaPass && contains(event.name))
      return true;
   return std::ranges::any_of(event.shaders, [&](const Shader& shader) {
      if (!shader.luma_shader.empty())
         return contains(shader.luma_shader);
      return contains(std::format("0x{:08X}", shader.hash)) || contains(shader.name);
   });
}

Hooks::D3D11::Functions GetD3D11Functions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   for (const Hooks::D3D11::Function function :
      {Draw, DrawInstanced, DrawIndexed, DrawIndexedInstanced, DrawAuto, DrawInstancedIndirect, DrawIndexedInstancedIndirect, Dispatch, DispatchIndirect})
      functions.set(size_t(function));
   return functions;
}

Hooks::D3D12::Functions GetD3D12Functions()
{
   using enum Hooks::D3D12::Function;
   Hooks::D3D12::Functions functions;
   for (const Hooks::D3D12::Function function : {CreateRootSignature, CreatePipelineState, CreateDescriptorHeap, CreateView, CopyDescriptors, CreateResource,
           Reset, ClearState, SetPipelineState, SetRootSignature, OMSetRenderTargets, SetDescriptorHeaps, SetRootArguments, SetFixedState, ResourceBarrier, Draw, Dispatch,
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

void AdvanceFrame()
{
   std::scoped_lock lock(mutex);
   if (capturing)
   {
      capturing = false;
      events.push_back({.type = EventType::Present, .name = "Present"});
      auto frame = std::make_shared<Frame>(++count, std::move(events));
      Utils::Log::Info("Trace: captured {} calls", frame->events.size() - 1);
      last = std::move(frame);
      events = {};
   }
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
} // namespace Capture
} // namespace Trace
} // namespace Luma
#endif
