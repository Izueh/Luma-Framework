#include "validation/validation.hpp"

#include <format>
#include <mutex>
#include <unordered_map>

#include "utils/log.hpp"

namespace Luma {
namespace Validation {
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
