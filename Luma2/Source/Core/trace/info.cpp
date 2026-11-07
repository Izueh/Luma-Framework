#include "trace/info.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>

#include <Windows.h>

#include "game/game.hpp"
#include "hooks/hooks.hpp"
#include "hooks/device_proxy.hpp"
#include "ui/overlay.hpp"
#include "utils/modules.hpp"
#include "utils/strings.hpp"
#include "validation/validation.hpp"

namespace Luma {
namespace Trace {
namespace {
// Older than this, the game isn't presenting (minimized, paused, a loading screen, a debugger)
constexpr uint64_t PRESENTING_MS = 1000;

std::atomic<uint64_t> last_present_ms = 0;
std::atomic<uint64_t> frame_count = 0;

// Never held with another lock
std::mutex mutex;
std::vector<SwapchainInfo> swapchains;
} // namespace

void SetSwapchain(SwapchainInfo info)
{
   if constexpr (!ENABLED)
      return;
   std::scoped_lock lock(mutex);
   const auto it = std::ranges::find(swapchains, info.id, &SwapchainInfo::id);
   if (it != swapchains.end())
      *it = std::move(info);
   else
      swapchains.push_back(std::move(info));
}

void RemoveSwapchain(const void* swapchain)
{
   if constexpr (!ENABLED)
      return;
   std::scoped_lock lock(mutex);
   std::erase_if(swapchains, [swapchain](const SwapchainInfo& info) { return info.id == reinterpret_cast<uint64_t>(swapchain); });
}

void OnPresent()
{
   if constexpr (!ENABLED)
      return;
   last_present_ms.store(GetTickCount64(), std::memory_order_relaxed);
   frame_count.fetch_add(1, std::memory_order_relaxed);
}

Status GetStatus()
{
   Status status;
   const Game::Info& info = Game::GetInfo();
   status.game = std::string(info.name);
   status.version = std::string(info.version);
   status.pid = GetCurrentProcessId();
   status.bits = uint32_t(sizeof(void*) * 8);
   status.exe_path = Utils::Strings::ToUtf8(Utils::Modules::GetPath(nullptr));
   status.luma_path = Utils::Strings::ToUtf8(Utils::Modules::SelfDirectory());
   if (const uint64_t last_present = last_present_ms.load(std::memory_order_relaxed))
   {
      status.ms_since_present = int64_t(GetTickCount64() - last_present);
      status.presenting = uint64_t(status.ms_since_present) < PRESENTING_MS;
   }
   status.frame_count = frame_count.load(std::memory_order_relaxed);
   {
      std::scoped_lock lock(mutex);
      status.swapchains = swapchains;
   }
   if (!status.swapchains.empty())
      status.api = status.swapchains.front().api;

   status.overlay_open = UI::IsOpen();
   const Hooks::Counts hooks = Hooks::GetCounts();
   status.counts = {Hooks::DeviceProxy::GetCount(), uint32_t(status.swapchains.size()), hooks.declared, hooks.installed, hooks.failed};

   const Validation::Counts validation = Validation::GetCounts();
   status.validation = {Validation::DEBUG_LAYER, Validation::GPU_VALIDATION, validation.corruptions, validation.errors, validation.warnings};

   MEMORYSTATUSEX memory = {sizeof(MEMORYSTATUSEX)};
   if (GlobalMemoryStatusEx(&memory))
   {
      status.virtual_used_mb = (memory.ullTotalVirtual - memory.ullAvailVirtual) >> 20;
      status.virtual_total_mb = memory.ullTotalVirtual >> 20;
      status.system_memory_load_percent = memory.dwMemoryLoad;
   }
   return status;
}
} // namespace Trace
} // namespace Luma
