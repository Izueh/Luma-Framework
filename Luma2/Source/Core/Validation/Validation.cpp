#include "Validation/Validation.hpp"

#include <array>
#include <atomic>
#include <format>
#include <mutex>
#include <unordered_map>

#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Validation {
namespace {
#if DEVELOPMENT
const std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::DEBUG_LAYER.key,
      .label = "Debug Layer",
      .tooltip = "D3D11/D3D12: enables the API debug layer and logs validation messages to Luma.log (requires Windows Graphics Tools).",
      .category = "Validation",
      .visibility = Settings::Visibility::Development,
      .default_value = DEBUG_LAYER,
      .requires_restart = true,
   },
   Settings::Setting{
      .key = Settings::Core::GPU_VALIDATION.key,
      .label = "GPU Validation",
      .tooltip = "D3D12: enables the debug layer with GPU-based validation (slow; for finding GPU errors with Luma's changes).",
      .category = "Validation",
      .visibility = Settings::Visibility::Development,
      .default_value = GPU_VALIDATION,
      .requires_restart = true,
   },
};
#endif
std::atomic<bool> active_debug_layer = DEBUG_LAYER;
std::atomic<bool> active_gpu_validation = GPU_VALIDATION;
} // namespace

void Register()
{
#if DEVELOPMENT
   Settings::Register(SETTINGS, Settings::Kind::Development);
#endif
}

bool WantsDebugLayer()
{
#if DEVELOPMENT
   return DEBUG_LAYER || (Settings::Find(Settings::Core::DEBUG_LAYER.key) && Settings::Get(Settings::Core::DEBUG_LAYER));
#else
   return DEBUG_LAYER;
#endif
}

bool WantsGpuValidation()
{
#if DEVELOPMENT
   return GPU_VALIDATION || (Settings::Find(Settings::Core::GPU_VALIDATION.key) && Settings::Get(Settings::Core::GPU_VALIDATION));
#else
   return GPU_VALIDATION;
#endif
}

bool IsDebugLayerOn()
{
   return active_debug_layer.load(std::memory_order_relaxed);
}

bool IsGpuValidationOn()
{
   return active_gpu_validation.load(std::memory_order_relaxed);
}

void SetActiveLayers(ActiveLayers layers)
{
   active_debug_layer = layers.debug_layer;
   active_gpu_validation = layers.gpu_validation;
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

constexpr Log::Level GetLogLevel(Severity severity)
{
   switch (severity)
   {
   case Severity::Corruption:
   case Severity::Error: return Log::Level::Error;
   case Severity::Warning: return Log::Level::Warning;
   default: return Log::Level::Info;
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
   Log::Write(GetLogLevel(severity), std::format("{} validation [{} #{}]{}: {}", Luma::GetName(api), GetName(severity), id, reports == LOGGED_PER_ID ? " (last time logged, still counted)" : "", text));
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
