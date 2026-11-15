#pragma once

#include <cstdint>
#include <optional>
#include <vector>
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

struct ShadersInfo
{
   bool loaded = true; // False after "Unload Shaders"
   uint32_t replaced = 0; // Game shaders with live objects and a replacement
   bool has_compilation_errors = false;
   std::string job; // The oldest running job, empty when idle
};

// Resources::Mirrors and Samplers
struct ResourcesInfo
{
   bool upgrades = false; // Rules exist
   bool samplers = false; // Sampler upgrades are on
   uint32_t mirrors = 0;
   uint64_t mirror_mb = 0;
   uint32_t views = 0;
   float lod_bias_offset = 0.f;
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
};

struct D3D12Info
{
   uint32_t pipelines = 0; // The game's, alive
   D3D12VariantsInfo variant_pipelines;
   D3D12LibraryInfo luma_pipeline_library; // Luma/Cache/Pipelines/Library.bin
   uint32_t extended_root_signatures = 0;
   uint32_t unextended_root_signatures = 0;
   uint64_t cbuffer_ring_bytes = 0; // Luma's cbuffers' upload memory
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
   ShadersInfo shaders;
   PatchesInfo patches;
   ResourcesInfo resources;
   JobsInfo jobs;
   ValidationInfo validation;
   std::optional<D3D12Info> d3d12; // Once a D3D12 device was created
   uint64_t virtual_used_mb = 0;
   uint64_t virtual_total_mb = 0;
   uint32_t system_memory_load_percent = 0;
   std::vector<std::string> ignored_args; // Arguments the tool doesn't take
};

// A swapchain was created or resized
void SetSwapchain(SwapchainInfo info);
void RemoveSwapchain(const void* swapchain);
void RecordPresent();

// Gathered on the asking thread
Status GetStatus();
} // namespace Trace
} // namespace Luma
