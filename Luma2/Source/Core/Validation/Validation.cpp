#include "Validation/Validation.hpp"

#include <array>
#include <atomic>
#include <format>
#include <mutex>
#include <unordered_map>

#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Utils/Log.hpp"

namespace Luma {
namespace Validation {
namespace {
#if DEVELOPMENT
const std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::GPU_VALIDATION.key,
      .label = "GPU Validation",
      .tooltip = "D3D12: the debug layer with GPU-based validation, from the game's next start (the debug layer can't be turned on once a device exists). "
                 "Slow: for finding what the GPU does wrong with Luma's changes.",
      .category = "Development",
      .visibility = Settings::Visibility::Development,
      .default_value = false,
   },
};
#endif
std::atomic<bool> d3d12_debug_layer = DEBUG_LAYER;
std::atomic<bool> d3d12_gpu_validation = GPU_VALIDATION;
} // namespace

void Register()
{
#if DEVELOPMENT
   Settings::Register(SETTINGS, Settings::Kind::Development);
#endif
}

bool IsDebugLayerOn()
{
   return DEBUG_LAYER || d3d12_debug_layer;
}

bool IsGpuValidationOn()
{
   return d3d12_gpu_validation;
}

void SetD3D12Layers(bool debug_layer, bool gpu_validation)
{
   d3d12_debug_layer = debug_layer;
   d3d12_gpu_validation = gpu_validation;
}

namespace {
constexpr uint64_t LOGGED_PER_ID = 5;

// Never held with another lock
std::mutex mutex;
Counts counts;
std::unordered_map<int64_t, uint64_t> seen; // (api, id) -> reports

constexpr std::string_view GetName(Severity severity)
{
   switch (severity)
   {
   case Severity::Corruption: return "CORRUPTION";
   case Severity::Error: return "ERROR";
   case Severity::Warning: return "WARNING";
   default: return "INFO";
   }
}

constexpr Utils::Log::Level GetLogLevel(Severity severity)
{
   switch (severity)
   {
   case Severity::Corruption:
   case Severity::Error: return Utils::Log::Level::Error;
   case Severity::Warning: return Utils::Log::Level::Warning;
   default: return Utils::Log::Level::Info;
   }
}
} // namespace

void Report(Api api, Severity severity, int32_t id, std::string_view text)
{
   uint64_t reports;
   {
      std::scoped_lock lock(mutex);
      switch (severity)
      {
      case Severity::Corruption: counts.corruptions++; break;
      case Severity::Error: counts.errors++; break;
      case Severity::Warning: counts.warnings++; break;
      default: break;
      }
      reports = ++seen[(int64_t(api) << 32) | uint32_t(id)];
   }
   if (reports > LOGGED_PER_ID)
      return;
   Utils::Log::Write(GetLogLevel(severity), std::format("{} validation [{} #{}]{}: {}", Luma::GetName(api), GetName(severity), id, reports == LOGGED_PER_ID ? " (last time logged, still counted)" : "", text));
}

Counts GetCounts()
{
   std::scoped_lock lock(mutex);
   return counts;
}

void Reset()
{
   std::scoped_lock lock(mutex);
   counts = {};
   seen.clear();
}
} // namespace Validation
} // namespace Luma
