#include "Trace/Readback.hpp"

#if DEVELOPMENT
#include <atomic>
#include <format>
#include <mutex>
#include <thread>

namespace Luma {
namespace Trace {
namespace Readback {
namespace {
// A pass not reached in this many presents after its copy was asked for is reported missing
constexpr uint32_t MAX_MISSED_FRAMES = 3;

std::atomic<bool> armed = false;
std::atomic<uint32_t> calls = 0;
std::atomic<bool> copied = false;
// Never held with another lock
std::mutex mutex;
std::optional<Target> target;
std::optional<Result> result;
uint32_t missed = 0;
} // namespace

Result Read(const Target& requested, std::chrono::steady_clock::time_point deadline)
{
   {
      std::scoped_lock lock(mutex);
      if (target)
         return {.error = "another readback is running"};
      target = requested;
      result.reset();
      missed = 0;
      calls = 0;
      copied = false;
      armed = true;
   }
   while (true)
   {
      {
         std::scoped_lock lock(mutex);
         if (result)
         {
            Result finished = std::move(*result);
            result.reset();
            target.reset();
            armed = false;
            return finished;
         }
      }
      if (std::chrono::steady_clock::now() > deadline)
         break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
   }
   std::scoped_lock lock(mutex);
   target.reset();
   result.reset();
   armed = false;
   return {.error = "timed out: the game isn't presenting (minimized, paused or loading), or the pass's command list wasn't executed"};
}

bool IsArmed()
{
   return armed.load(std::memory_order_relaxed);
}

std::optional<Target> GetTarget()
{
   std::scoped_lock lock(mutex);
   return target;
}

bool CountCall()
{
   std::optional<Target> armed_target = GetTarget();
   // Once copied, later calls of the frame don't count: the next frame's instance would be read again otherwise
   return armed_target && !copied.load(std::memory_order_relaxed) && calls.fetch_add(1, std::memory_order_relaxed) == armed_target->instance;
}

void MarkCopied()
{
   copied = true;
}

void Finish(Result finished)
{
   std::scoped_lock lock(mutex);
   if (target && !result)
      result = std::move(finished);
}

void OnPresent()
{
   calls = 0;
   std::scoped_lock lock(mutex);
   if (!target || result || target->view == ViewKind::Swapchain || copied)
      return;
   if (++missed >= MAX_MISSED_FRAMES)
      result = Result{.error = std::format("the call wasn't reached, or had no {} at slot {}, in {} frames (check \"instance\" and \"slot\")", GetName(target->view),
                         target->slot, MAX_MISSED_FRAMES)};
}

std::string_view GetName(ViewKind view)
{
   switch (view)
   {
   case ViewKind::RenderTarget: return "rtv";
   case ViewKind::ShaderResource: return "srv";
   case ViewKind::UnorderedAccess: return "uav";
   case ViewKind::Depth: return "depth";
   case ViewKind::Stencil: return "stencil";
   default: return "swapchain";
   }
}

std::optional<ViewKind> ParseViewKind(std::string_view name)
{
   for (const ViewKind view : {ViewKind::RenderTarget, ViewKind::ShaderResource, ViewKind::UnorderedAccess, ViewKind::Depth, ViewKind::Stencil, ViewKind::Swapchain})
   {
      if (GetName(view) == name)
         return view;
   }
   return std::nullopt;
}
} // namespace Readback
} // namespace Trace
} // namespace Luma
#endif
