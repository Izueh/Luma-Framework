#include "Trace/Info.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>

#include <Windows.h>

#include "Game/Registry.hpp"
#include "Hooks/Hooks.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "Shaders/Database.hpp"
#include "Shaders/Jobs.hpp"
#include "Shaders/Shaders.hpp"
#include "Shaders/Variants.hpp"
#include "System/Paths.hpp"
#include "UI/Overlay.hpp"
#include "System/Modules.hpp"
#include "Utils/Strings.hpp"
#include "Validation/Validation.hpp"

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

void RecordPresent()
{
   if constexpr (!ENABLED)
      return;
   last_present_ms.store(GetTickCount64(), std::memory_order_relaxed);
   frame_count.fetch_add(1, std::memory_order_relaxed);
}

Status GetStatus()
{
   Status status;
   const Game::Info& info = Game::Registry::GetInfo();
   status.game = std::string(info.name);
   status.version = std::string(info.version);
   status.pid = GetCurrentProcessId();
   status.bits = uint32_t(sizeof(void*) * 8);
   status.exe_path = Utils::Strings::ToUtf8(System::Modules::GetPath(nullptr));
   status.luma_path = Utils::Strings::ToUtf8(System::Modules::SelfDirectory());
   status.log_path = Utils::Strings::ToUtf8(System::Paths::GetLog());
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

   status.shaders.loaded = Shaders::Variants::IsLoaded();
   for (const Shaders::Database::Shader* shader : Shaders::Database::GetShaders())
      status.shaders.replaced += shader->objects && Shaders::Variants::HasVariant(shader->hash);
   status.shaders.has_compilation_errors = Shaders::GetLastBuild().failed != 0;
   status.shaders.job = Shaders::Jobs::GetStatus().name;

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
