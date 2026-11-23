#include "Hooks/HookScope.hpp"

#include <atomic>
#include <cstdint>

namespace Luma {
namespace Hooks {
namespace Scope {
namespace {
thread_local uint32_t depth = 0;
thread_local bool third_party_work = false;
std::atomic<bool> bypass = false;
std::atomic<CallerFilter> caller_filter = nullptr;
} // namespace

Guard::Guard()
{
   depth++;
}

Guard::~Guard()
{
   depth--;
}

Suspension::Suspension() : depth(Scope::depth)
{
   Scope::depth = 0;
}

Suspension::~Suspension()
{
   Scope::depth = depth;
}

bool IsActive()
{
   return depth != 0 || bypass.load(std::memory_order_relaxed);
}

void SetBypass(bool value)
{
   bypass.store(value, std::memory_order_seq_cst);
}

bool IsBypassed()
{
   return bypass.load(std::memory_order_relaxed);
}

// One level of the scope's depth, so IsActive stays one check on the hot paths
void BeginThirdPartyWork()
{
   if (third_party_work)
      return;
   third_party_work = true;
   depth++;
}

void EndThirdPartyWork()
{
   if (!third_party_work)
      return;
   third_party_work = false;
   depth--;
}

bool IsThirdPartyWorkOpen()
{
   return third_party_work;
}

void SetCallerFilter(CallerFilter filter)
{
   caller_filter.store(filter, std::memory_order_release);
}

bool IsIgnoredCaller(const void* caller)
{
   if (!caller)
      return false;
   const CallerFilter filter = caller_filter.load(std::memory_order_acquire);
   return filter && filter(caller);
}
} // namespace Scope
} // namespace Hooks
} // namespace Luma
