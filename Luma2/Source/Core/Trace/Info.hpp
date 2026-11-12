#pragma once

#include <cstdint>
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

struct ValidationInfo
{
   bool debug_layer = false; // Whether this build turns it on
   bool gpu_validation = false;
   uint64_t corruptions = 0;
   uint64_t errors = 0;
   uint64_t warnings = 0;
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
   std::string api; // Of the first swapchain
   bool presenting = false;
   int64_t ms_since_present = -1; // -1 before the first present
   uint64_t frame_count = 0;
   std::vector<SwapchainInfo> swapchains;
   bool overlay_open = false;
   CountsInfo counts;
   ValidationInfo validation;
   uint64_t virtual_used_mb = 0;
   uint64_t virtual_total_mb = 0;
   uint32_t system_memory_load_percent = 0;
   std::vector<std::string> ignored_args; // Arguments the tool doesn't take
};

// A swapchain was created or resized
void SetSwapchain(SwapchainInfo info);
void RemoveSwapchain(const void* swapchain);
void OnPresent();

// Gathered on the asking thread
Status GetStatus();
} // namespace Trace
} // namespace Luma
