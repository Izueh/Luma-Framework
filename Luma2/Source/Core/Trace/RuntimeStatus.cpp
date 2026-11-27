#include "Trace/RuntimeStatus.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>

#include <Windows.h>

#include <format>
#include <set>

#include "Common/Api.hpp"
#include "ReShade/ReShade.hpp"
#include "Effects/SR/SuperResolution.hpp"
#include "Game/GameRegistry.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D12/D3D12Descriptors.hpp"
#include "Resources/D3D12/D3D12HeapTail.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Resources/ResourceBindings.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Shaders/D3D11/D3D11Variants.hpp"
#include "Shaders/D3D12/D3D12Database.hpp"
#include "Shaders/D3D12/D3D12PipelineLibrary.hpp"
#include "Shaders/D3D12/D3D12RootSignatures.hpp"
#include "Shaders/D3D12/D3D12Variants.hpp"
#include "Shaders/ShaderInvariants.hpp"
#include "Hooks/HookManager.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "Hooks/ThirdPartyModules.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderPatching.hpp"
#include "Shaders/ShaderStore.hpp"
#include "System/JobQueue.hpp"
#include "Shaders/Shaders.hpp"
#include "Swapchain/DXGI/DXGISwapchain.hpp"
#include "Shaders/ShaderVariants.hpp"
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

bool HasD3D12Device()
{
   return std::ranges::any_of(Shaders::GetStores(), [](const std::shared_ptr<Shaders::Store>& store) { return store->api == Api::D3D12; });
}
} // namespace

void SetSwapchain(SwapchainInfo info)
{
   std::scoped_lock lock(mutex);
   const auto it = std::ranges::find(swapchains, info.id, &SwapchainInfo::id);
   if (it != swapchains.end())
      *it = std::move(info);
   else
      swapchains.push_back(std::move(info));
}

void RemoveSwapchain(const void* swapchain)
{
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

OverlaySummary GetOverlaySummary()
{
   OverlaySummary summary;
   {
      std::scoped_lock lock(mutex);
      if (!swapchains.empty())
         summary.primary_swapchain = swapchains.front();
   }
   const Hooks::Counts hooks = Hooks::GetCounts();
   summary.hooks_installed = hooks.installed;
   summary.hooks_failed = hooks.failed;
   const Shaders::Patching::Status patches = Shaders::Patching::GetStatus(Shaders::GetShownStore().get());
   summary.broken_recipes = patches.broken_recipes;
   summary.failed_patches = patches.failed;
   const Validation::Counts validation = Validation::GetCounts();
   summary.validation_errors = validation.errors;
   summary.validation_corruptions = validation.corruptions;
   return summary;
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
   status.luma_path = Utils::Strings::ToUtf8(System::Modules::GetSelfDirectory());
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
   status.reshade = {ReShade::DescribeLoadState(), ReShade::DescribeBridge(), ReShade::DescribeMirrorSwapchainView()};

   for (const Api api : {Api::D3D11, Api::D3D12})
   {
      if (Game::Registry::GetSupportedApis().Has(api))
         status.supported_apis.emplace_back(GetName(api));
   }
   const std::shared_ptr<Shaders::Store> shown = Shaders::GetShownStore();
   for (const std::shared_ptr<Shaders::Store>& store : Shaders::GetStores())
   {
      const Shaders::BuildInfo build = Shaders::GetLastBuild(*store);
      status.devices.push_back({.api = std::string(GetName(store->api)),
         .shown = store == shown,
         .shaders = {.build = build.id,
            .compiled = build.compiled,
            .cached = build.cached,
            .failed = build.failed,
            .patched = Shaders::Patching::GetStatus(store.get()).patched}});
   }
   status.shaders.loaded = Shaders::Variants::IsLoaded();
   if (shown)
   {
      for (const Shaders::Database::Shader* shader : Shaders::Database::GetShaders())
         status.shaders.replaced += shader->objects && Shaders::Variants::HasVariant(*shown, shader->hash);
   }
   status.shaders.has_compilation_errors = Shaders::GetLastBuild().failed != 0;
   const Shaders::Patching::Status patching = Shaders::Patching::GetStatus(Shaders::GetShownStore().get());
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
   const Resources::Chains::Stats chains = Resources::Chains::GetStats();
   const Resources::Scaling::Stats scaling = Resources::Scaling::GetStats();
   status.resources = {
      .upgrades = Resources::Mirrors::IsNeeded(),
      .samplers = Resources::Samplers::IsNeeded(),
      .method = Resources::Upgrade::GetRules().method == Resources::Upgrade::Method::InPlace ? "InPlace" : "Mirror",
      .mirrors = mirrors.mirrors,
      .mirror_mb = mirrors.bytes >> 20,
      .views = mirrors.views,
      .in_place = mirrors.in_place,
      .in_place_saved_mb = mirrors.in_place_saved_bytes >> 20,
      .in_place_resized = mirrors.in_place_resized,
      .lod_bias_offset = Resources::Samplers::GetShownLodBiasOffset(),
      .chains = {
         .mode = std::string(Resources::Chains::GetModeName(chains.mode)),
         .seeds = chains.seeds,
         .direct_propagations = chains.direct_propagations,
         .indirect_propagations = chains.indirect_propagations,
         .deferred_queued = chains.deferred_queued,
      },
      .scaling = {
         .mode = std::string(Resources::Scaling::GetModeName(scaling.mode)),
         .active = scaling.active,
         .activating_thread = scaling.activating_thread,
         .scaled_mirrors = scaling.scaled_mirrors,
         .pool_size = scaling.pool_size,
         .invalidations = scaling.invalidations,
         .viewport_rescales = scaling.viewport_rescales,
         .scissor_rescales = scaling.scissor_rescales,
         .copy_box_rescales = scaling.copy_box_rescales,
         .clear_rect_rescales = scaling.clear_rect_rescales,
         .scaled_blits = scaling.scaled_blits,
         .cross_thread_overlaps = scaling.cross_thread_overlaps,
      },
   };
   const Effects::SR::RuntimeInfo sr_info = Effects::SR::GetRuntimeInfo();
   status.sr = {
      .enabled = sr_info.enabled,
      .active_type = std::string(Effects::SR::GetTypeName(sr_info.active_type)),
      .drew_this_frame = sr_info.drew_this_frame,
      .render_width = sr_info.last_draw.render_width ? sr_info.last_draw.render_width : sr_info.settings.render_width,
      .render_height = sr_info.last_draw.render_height ? sr_info.last_draw.render_height : sr_info.settings.render_height,
      .output_width = sr_info.settings.output_width,
      .output_height = sr_info.settings.output_height,
   };
   for (size_t i = 0; i < Effects::SR::TYPE_COUNT; i++)
   {
      if (sr_info.backends[i].supported)
         status.sr.supported_backends.emplace_back(Effects::SR::GetTypeName(Effects::SR::Type(i)));
   }
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
         .wrapped = output.wrapped,
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

   if (HasD3D12Device())
   {
      D3D12Info& d3d12 = status.d3d12.emplace();
      const Shaders::Database::D3D12::Stats database = Shaders::Database::D3D12::GetStats();
      d3d12.pipelines = database.pipelines;
      d3d12.extended_root_signatures = database.extended_signatures;
      d3d12.unextended_root_signatures = database.unextended_signatures;
      d3d12.largest_extended_root_signature_dwords = database.largest_extended_dwords;
      d3d12.luma_root_signature_dwords = Shaders::RootSignatures::D3D12::LUMA_DWORDS;
      d3d12.unextended_root_signature_dwords = database.unextended_dwords;
#if DEVELOPMENT || TEST
      d3d12.rejected_variants = Shaders::Invariants::GetRejectedCount();
#endif
      const Shaders::Variants::D3D12::Stats variants = Shaders::Variants::D3D12::GetStats();
      d3d12.variant_pipelines = {variants.pipelines, variants.ready, variants.pending, variants.failed};
      const Shaders::PipelineLibrary::D3D12::Stats library = Shaders::PipelineLibrary::D3D12::GetStats();
      d3d12.luma_pipeline_library = {library.hits, library.misses, library.stored, library.format_sets};
      const Resources::Database::D3D12::Stats resources = Resources::Database::D3D12::GetStats();
      d3d12.cbuffer_ring_bytes = resources.ring_bytes;
      d3d12.luma_textures = resources.textures;
      d3d12.texture_tables = resources.tables;
      d3d12.view_tables = resources.view_tables;
      const Resources::HeapTail::D3D12::Stats tails = Resources::HeapTail::D3D12::GetStats();
      d3d12.heap_tails = tails.heaps;
      d3d12.heaps_without_tail = tails.refused;
      d3d12.heap_tails_without_ring = tails.fixed_only;
      d3d12.heap_tails_over_tier_2 = tails.over_tier_2;
      d3d12.heaps_over_tier_2_refused = tails.over_tier_2_refused;
      d3d12.heap_ring_blocks = tails.ring_blocks;
      d3d12.heap_ring_full = tails.ring_full;
      const Resources::Mirrors::D3D12::Stats mirrored = Resources::Mirrors::D3D12::GetStats();
      d3d12.mirrors = {mirrored.descriptors, mirrored.samplers, mirrored.swapped_copies, mirrored.direct_writes, variants.format_variants,
         variants.warmed_format_variants, mirrored.converted_copies, mirrored.unhandled_copies, mirrored.uav_clears, mirrored.unhandled_clears,
         mirrored.aliasing_barriers, mirrored.in_place_converted_copies, mirrored.in_place_unhandled_copies, mirrored.in_place_unasked_placed,
         mirrored.in_place_unfit_placed};
      status.unimplemented.enhanced_barrier_resources = mirrored.layout_resources;
      status.unimplemented.reserved_resources = mirrored.reserved_resources;
      status.unimplemented.anisotropic_static_samplers = Shaders::RootSignatures::D3D12::GetAnisotropicStaticSamplerSignatures();
      status.unimplemented.pipelines_without_desc = variants.draws_without_desc;
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
         d3d12.unbounded_array_shaders += Shaders::Container::HasUnbounded(bindings);
      }
      d3d12.game_register_spaces.assign(spaces.begin(), spaces.end());
#endif
   }
   status.unimplemented.stream_output_variants = Shaders::Variants::D3D11::GetStreamOutputVariants();
   status.unimplemented.resolves_between_formats = Resources::Mirrors::D3D11::GetUnhandledResolves();

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

std::string DescribeRootSignatures()
{
   if (!HasD3D12Device())
      return {};
   const Shaders::Database::D3D12::Stats database = Shaders::Database::D3D12::GetStats();
   std::string text = std::format("D3D12 root signatures: {} extended (largest {} DWORDs, and Luma's {} at most), {} not", database.extended_signatures,
      database.largest_extended_dwords, Shaders::RootSignatures::D3D12::LUMA_DWORDS, database.unextended_signatures);
   for (size_t i = 0; i < database.unextended_dwords.size(); i++)
      text += std::format("{}{}", i == 0 ? " (DWORDs: " : ", ", database.unextended_dwords[i]);
   if (!database.unextended_dwords.empty())
      text += ")";
#if DEVELOPMENT || TEST
   if (const uint32_t rejected = Shaders::Invariants::GetRejectedCount())
      text += std::format("; {} variant{} refused by D3D12's invariants", rejected, rejected == 1 ? "" : "s");
#endif
   return text;
}
} // namespace Trace
} // namespace Luma
