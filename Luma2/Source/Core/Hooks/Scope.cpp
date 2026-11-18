#include "Hooks/Scope.hpp"

#include <atomic>
#include <cstdint>

namespace Luma {
namespace Hooks {
namespace Scope {
namespace {
thread_local uint32_t depth = 0;
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
} // namespace Scope
} // namespace Hooks
} // namespace Luma
