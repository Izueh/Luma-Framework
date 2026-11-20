#include "hooks/scope.hpp"

#include <atomic>
#include <cstdint>

namespace Luma {
namespace Hooks {
namespace Scope {
namespace {
thread_local uint32_t depth = 0;
thread_local bool third_party_work = false;
std::atomic<bool> bypass = false;
} // namespace

Guard::Guard()
{
   depth++;
}

Guard::~Guard()
{
   depth--;
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

// One level of the scope depth, so IsActive stays a single check
void BeginThirdPartyWork()
{
   if (!third_party_work)
   {
      third_party_work = true;
      depth++;
   }
}

void EndThirdPartyWork()
{
   if (third_party_work)
   {
      third_party_work = false;
      depth--;
   }
}

bool IsThirdPartyWorkOpen()
{
   return third_party_work;
}
} // namespace Scope
} // namespace Hooks
} // namespace Luma
