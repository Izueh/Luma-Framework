#include "Trace/Info.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>

#include <Windows.h>

#include <format>
#include <set>

#include "Common/Api.hpp"
#include "Game/Registry.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Resources/D3D12/D3D12HeapTail.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Resources/Bindings.hpp"
#include "Resources/Mirrors.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/Samplers.hpp"
#include "Shaders/D3D12/D3D12Database.hpp"
#include "Shaders/D3D12/D3D12PipelineLibrary.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"
#include "Hooks/Hooks.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "Hooks/ThirdPartyModules.hpp"
#include "Shaders/Database.hpp"
#include "Shaders/Patching.hpp"
#include "System/Jobs.hpp"
#include "Shaders/Shaders.hpp"
#include "Swapchain/DXGI/DXGISwapchain.hpp"
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
   const Hooks::Verification verification = Hooks::GetLastVerification();
   const Hooks::PresentChain present = Hooks::GetPresentChain();
   constexpr std::string_view PRESENT_ORDERS[] = {"not hooked", "alone", "luma outermost", "overlay outermost"};
   status.hooks = {.intact = verification.intact,
      .chained = verification.chained,
      .restored = verification.restored,
      .detoured_before = verification.detoured_before,
      .neutralised = hooks.neutralised,
      .overlays = Hooks::ThirdPartyModules::GetLoaded(),
      .present_order = std::string(PRESENT_ORDERS[size_t(present.order)]),
      .present_overlay = present.overlay};

   status.shaders.loaded = Shaders::Variants::IsLoaded();
   for (const Shaders::Database::Shader* shader : Shaders::Database::GetShaders())
      status.shaders.replaced += shader->objects && Shaders::Variants::HasVariant(shader->hash);
   status.shaders.has_compilation_errors = Shaders::GetLastBuild().failed != 0;
   const Shaders::Patching::Status patching = Shaders::Patching::GetStatus();
   status.patches = {.needed = patching.needed,
      .loaded = patching.loaded,
      .running = patching.running,
      .recipes = patching.recipes,
      .patched = patching.patched,
      .unmatched = patching.unmatched,
      .failed = patching.failed,
      .pending = patching.pending,
      .broken_recipes = patching.broken_recipes};
   const Resources::Mirrors::Stats mirrors = Resources::Mirrors::GetStats();
   status.resources = {.upgrades = Resources::Mirrors::IsNeeded(),
      .samplers = Resources::Samplers::IsNeeded(),
      .mirrors = mirrors.mirrors,
      .mirror_mb = mirrors.bytes >> 20,
      .views = mirrors.views,
      .lod_bias_offset = Resources::Samplers::GetLodBiasOffset()};
   if (const Swapchain::DXGI::Status output = Swapchain::DXGI::GetStatus(); output.known)
   {
      const CB::LumaGlobalSettings& settings = Resources::LumaCBuffers::cb_luma_global_settings;
      status.output = OutputInfo{
         .mode = output.mode,
         .output_mode = output.output_mode,
         .color_space = std::string(Swapchain::DXGI::GetName(output.color_space)),
         .game_color_space = output.game_color_space ? std::string(Swapchain::DXGI::GetName(*output.game_color_space)) : std::string(),
         .game_format = uint32_t(output.game_format),
         .real_format = uint32_t(output.real_format),
         .game_width = output.game_width,
         .game_height = output.game_height,
         .real_width = output.real_width,
         .real_height = output.real_height,
         .game_buffers = output.game_buffers,
         .real_buffers = output.real_buffers,
         .flip_model = output.flip_model,
         .tearing = output.tearing,
         .borderless = output.borderless,
         .game_fullscreen = output.game_fullscreen,
         .composed = output.composed,
         .hdr_supported = output.hdr_supported,
         .hdr_enabled = output.hdr_enabled,
         .peak_nits = output.peak_nits,
         .sdr_white_nits = output.sdr_white_nits,
         // Written on the presenting thread: a torn read only mixes two frames' values
         .display_mode = settings.DisplayMode,
         .peak_white_nits = settings.PeakWhiteNits,
         .game_paper_white_nits = settings.GamePaperWhiteNits,
         .ui_paper_white_nits = settings.UIPaperWhiteNits,
      };
   }
   const System::Jobs::Status jobs = System::Jobs::GetStatus();
   status.shaders.job = jobs.name;
   status.jobs = {jobs.threads, jobs.running, jobs.queued};

   if (Shaders::GetApi() == Api::D3D12)
   {
      D3D12Info& d3d12 = status.d3d12.emplace();
      const Shaders::Database::D3D12::Stats database = Shaders::Database::D3D12::GetStats();
      d3d12.pipelines = database.pipelines;
      d3d12.extended_root_signatures = database.extended_signatures;
      d3d12.unextended_root_signatures = database.unextended_signatures;
      const Shaders::Variants::D3D12::Stats variants = Shaders::Variants::D3D12::GetStats();
      d3d12.variant_pipelines = {variants.pipelines, variants.ready, variants.pending, variants.failed};
      const Shaders::PipelineLibrary::D3D12::Stats library = Shaders::PipelineLibrary::D3D12::GetStats();
      d3d12.luma_pipeline_library = {library.hits, library.misses, library.stored};
      const Resources::Database::D3D12::Stats resources = Resources::Database::D3D12::GetStats();
      d3d12.cbuffer_ring_bytes = resources.ring_bytes;
      d3d12.luma_textures = resources.textures;
      d3d12.texture_tables = resources.tables;
      const Resources::HeapTail::D3D12::Stats tails = Resources::HeapTail::D3D12::GetStats();
      d3d12.heap_tails = tails.heaps;
      d3d12.heaps_without_tail = tails.refused;
      const Resources::Mirrors::D3D12::Stats mirrored = Resources::Mirrors::D3D12::GetStats();
      d3d12.mirrors = {mirrored.descriptors, mirrored.samplers, mirrored.swapped_copies, mirrored.direct_writes, variants.format_variants, mirrored.unhandled_copies,
         mirrored.unhandled_clears, mirrored.layout_resources, mirrored.aliasing_barriers};
      d3d12.register_model = std::format("space{}", Resources::Bindings::GetOptions().d3d12_space);
#if DEVELOPMENT
      const Resources::Descriptors::D3D12::Stats descriptors = Resources::Descriptors::D3D12::GetStats();
      d3d12.descriptor_heaps = descriptors.heaps;
      d3d12.resources = descriptors.resources;
      std::set<uint32_t> spaces;
      for (const Shaders::Database::Shader* shader : Shaders::Database::GetShaders())
      {
         const Shaders::Container::Bindings& bindings = shader->bindings;
         for (const Shaders::Container::Binding& binding : bindings.bindings)
            spaces.insert(binding.space);
         d3d12.heap_indexed_shaders += bindings.resource_heap_indexed || bindings.sampler_heap_indexed;
         d3d12.unbounded_array_shaders += bindings.HasUnbounded();
      }
      d3d12.game_register_spaces.assign(spaces.begin(), spaces.end());
#endif
   }

   const Validation::Counts validation = Validation::GetCounts();
   status.validation = {Validation::IsDebugLayerOn(), Validation::IsGpuValidationOn(), validation.corruptions, validation.errors, validation.warnings};

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
