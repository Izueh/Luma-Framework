#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Not in an anonymous namespace: glaze can't reflect types with internal linkage
namespace Luma {
namespace Trace {
#if DEVELOPMENT
inline constexpr bool ENABLED = true;
#else
inline constexpr bool ENABLED = false;
#endif

struct SwapchainInfo
{
   uint64_t id = 0; // The swapchain's address
   std::string api;
   uint32_t width = 0;
   uint32_t height = 0;
   uint32_t format = 0; // DXGI_FORMAT
   uint32_t buffers = 0;
};

struct CountsInfo
{
   uint32_t proxies = 0; // Devices the game holds
   uint32_t swapchains = 0;
   uint32_t hooks_declared = 0;
   uint32_t hooks_installed = 0;
   uint32_t hooks_failed = 0;
};

struct HooksInfo
{
   uint32_t intact = 0;
   uint32_t chained = 0;
   uint32_t restored = 0;
   uint32_t detoured_before = 0;
   uint32_t neutralised = 0; // Left in another hooker's chain by an unload
   std::vector<std::string> overlays;
   std::string present_order; // "not hooked", "alone", "luma outermost", "overlay outermost"
   std::string present_overlay;
};

struct ShadersInfo
{
   bool loaded = true; // False after "Unload Shaders"
   uint32_t replaced = 0; // Game shaders with live objects and a replacement
   bool has_compilation_errors = false;
   std::string job; // The oldest running job, empty when idle
};

struct ChainsInfo
{
   std::string mode;
   uint32_t seeds = 0;
   uint32_t direct_propagations = 0;
   uint32_t indirect_propagations = 0;
   uint32_t deferred_queued = 0;
};

struct ScalingInfo
{
   std::string mode;
   bool active = false;
   uint32_t activating_thread = 0;
   uint32_t scaled_mirrors = 0;
   uint32_t pool_size = 0;
   uint32_t invalidations = 0;
   uint32_t viewport_rescales = 0;
   uint32_t scissor_rescales = 0;
   uint32_t copy_box_rescales = 0;
   uint32_t clear_rect_rescales = 0;
   uint32_t scaled_blits = 0;
   uint32_t cross_thread_overlaps = 0;
};

// Resources::Mirrors, Samplers, Chains and Scaling
struct ResourcesInfo
{
   bool upgrades = false; // Rules exist
   bool samplers = false; // Sampler upgrades are on
   uint32_t mirrors = 0;
   uint64_t mirror_mb = 0;
   uint32_t views = 0;
   float lod_bias_offset = 0.f;
   ChainsInfo chains;
   ScalingInfo scaling;
};

// Swapchain and Display: what the game asked for and what Luma presents, of the swapchain presented last
struct OutputInfo
{
   std::string mode; // "Mirror", "InPlace"
   std::string output_mode; // "SDR", "HDR10", "scRGB"
   std::string color_space; // The real swapchain's
   std::string game_color_space; // What the game set, kept as what its image is in; empty if it never did
   uint32_t game_format = 0; // DXGI_FORMAT
   uint32_t real_format = 0;
   uint32_t game_width = 0;
   uint32_t game_height = 0;
   uint32_t real_width = 0;
   uint32_t real_height = 0;
   uint32_t game_buffers = 0;
   uint32_t real_buffers = 0;
   bool flip_model = false;
   bool tearing = false;
   bool borderless = false;
   bool game_fullscreen = false;
   bool composed = false; // The last present ran display composition
   bool wrapped = false; // The game holds a proxy of the swapchain
   bool hdr_supported = false;
   bool hdr_enabled = false; // Windows HDR on the display the window is on
   float peak_nits = 0.f;
   float sdr_white_nits = 0.f;
   uint32_t display_mode = 0; // LumaSettings: 0 SDR, 1 HDR, 2 SDR on HDR
   float peak_white_nits = 0.f;
   float game_paper_white_nits = 0.f;
   float ui_paper_white_nits = 0.f;
};

// What the ReShade module reports
struct ReShadeInfo
{
   std::string load; // Whether ReShade is loaded and by whom, or why it isn't
   std::string bridge; // Whether Luma's present work runs at ReShade's present event, before its effects
   std::string mirror_swapchain_view; // Mirror: whether ReShade's own reads get the real swapchain, and the output switches through its wrapper
};

// Shaders::Patching: recipes and the game's bytecode patches
struct PatchesInfo
{
   bool needed = false;
   bool loaded = true; // False after "Unload Patches"
   bool running = false;
   uint32_t recipes = 0;
   uint32_t patched = 0;
   uint32_t unmatched = 0;
   uint32_t failed = 0;
   uint32_t pending = 0;
   uint32_t broken_recipes = 0;
};

struct JobsInfo
{
   uint32_t threads = 0; // The most that run at once (Luma.Advanced.WorkerThreads)
   uint32_t running = 0;
   uint32_t queued = 0;
};

struct ValidationInfo
{
   bool debug_layer = false; // Whether this build turns it on
   bool gpu_validation = false;
   uint64_t corruptions = 0;
   uint64_t errors = 0;
   uint64_t warnings = 0;
};

struct D3D12VariantsInfo
{
   uint32_t tracked = 0; // Game pipelines that can get a variant (their desc copied)
   uint32_t ready = 0;
   uint32_t pending = 0;
   uint32_t failed = 0;
};

struct D3D12LibraryInfo
{
   uint32_t hits = 0;
   uint32_t misses = 0;
   uint32_t stored = 0;
   uint32_t format_sets = 0;
};

struct D3D12MirrorsInfo
{
   uint32_t descriptors = 0;
   uint32_t samplers = 0;
   uint32_t swapped_copies = 0;
   uint32_t direct_writes = 0;
   uint32_t format_variants = 0;
   uint32_t warmed_format_variants = 0;
   uint32_t converted_copies = 0;
   uint32_t unhandled_copies = 0;
   uint32_t uav_clears = 0;
   uint32_t unhandled_clears = 0;
   uint32_t aliasing_barriers = 0;
};

struct D3D12Info
{
   uint32_t pipelines = 0;
   D3D12VariantsInfo variant_pipelines;
   D3D12LibraryInfo luma_pipeline_library;
   uint32_t extended_root_signatures = 0;
   uint32_t unextended_root_signatures = 0;
   uint32_t largest_extended_root_signature_dwords = 0;
   uint32_t luma_root_signature_dwords = 0;
   std::vector<uint32_t> unextended_root_signature_dwords;
   uint32_t rejected_variants = 0;
   uint64_t cbuffer_ring_bytes = 0;
   uint32_t heap_tails = 0;
   uint32_t heaps_without_tail = 0;
   uint32_t heap_tails_without_ring = 0;
   uint32_t heap_tails_over_tier_2 = 0;
   uint32_t heaps_over_tier_2_refused = 0;
   uint32_t heap_ring_blocks = 0;
   uint32_t heap_ring_full = 0;
   uint32_t view_tables = 0;
   uint32_t luma_textures = 0;
   uint32_t texture_tables = 0;
   D3D12MirrorsInfo mirrors;
   // Where Luma's cbuffers are: "space100", with the variants compiled as SM5.1
   std::string register_model;
   // Development builds: the descriptor heaps and resources trace knows
   uint32_t descriptor_heaps = 0;
   uint32_t resources = 0;
   // Development builds, from the game's shaders' declarations: the register spaces they use (Luma's must not be one), and how
   // many are bindless (SM6.6 heap indexing, or unbounded arrays) so trace can't list all they read
   std::vector<uint32_t> game_register_spaces;
   uint32_t heap_indexed_shaders = 0;
   uint32_t unbounded_array_shaders = 0;
};

struct UnimplementedInfo
{
   uint32_t enhanced_barrier_resources = 0;
   uint32_t reserved_resources = 0;
   uint32_t anisotropic_static_samplers = 0;
   uint32_t pipelines_without_desc = 0;
   uint32_t stream_output_variants = 0;
   uint32_t resolves_between_formats = 0;
};

struct SrInfo
{
   bool enabled = false;
   std::string active_type;
   bool drew_this_frame = false;
   uint32_t render_width = 0;
   uint32_t render_height = 0;
   uint32_t output_width = 0;
   uint32_t output_height = 0;
   std::vector<std::string> supported_backends;
};

struct Status
{
   bool ok = true;
   uint32_t luma = 2;
   std::string game;
   std::string version;
   uint32_t pid = 0;
   uint32_t bits = 0;
   std::string exe_path;
   std::string luma_path;
   std::string log_path; // Read by the MCP bridge for luma_log
   std::string api; // Of the first swapchain
   bool presenting = false;
   int64_t ms_since_present = -1; // -1 before the first present
   uint64_t frame_count = 0;
   std::vector<SwapchainInfo> swapchains;
   bool overlay_open = false;
   CountsInfo counts;
   HooksInfo hooks;
   ReShadeInfo reshade;
   ShadersInfo shaders;
   PatchesInfo patches;
   ResourcesInfo resources;
   SrInfo sr;
   std::optional<OutputInfo> output; // Once a swapchain presented through Luma
   JobsInfo jobs;
   ValidationInfo validation;
   std::optional<D3D12Info> d3d12; // Once a D3D12 device was created
   UnimplementedInfo unimplemented;
   uint64_t virtual_used_mb = 0;
   uint64_t virtual_total_mb = 0;
   uint32_t system_memory_load_percent = 0;
   std::vector<std::string> ignored_args; // Arguments the tool doesn't take
};

// Lightweight summary for the overlay header and status bar (avoids full status gathering on every frame)
struct OverlaySummary
{
   std::optional<SwapchainInfo> primary_swapchain;
   uint32_t hooks_installed = 0;
   uint32_t hooks_failed = 0;
   uint32_t broken_recipes = 0;
   uint32_t failed_patches = 0;
   uint64_t validation_errors = 0;
   uint64_t validation_corruptions = 0;
};

// A swapchain was created or resized
void SetSwapchain(SwapchainInfo info);
void RemoveSwapchain(const void* swapchain);
void RecordPresent();

// Lightweight overlay summary (safe to call every frame in all build configurations)
OverlaySummary GetOverlaySummary();
Status GetStatus();
std::string DescribeRootSignatures();
} // namespace Trace
} // namespace Luma
