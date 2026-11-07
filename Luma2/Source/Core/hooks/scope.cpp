#include "hooks/scope.hpp"

#include <cstdint>

namespace Luma {
namespace Hooks {
namespace Scope {
namespace {
thread_local uint32_t depth = 0;
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
   return depth != 0;
}
} // namespace Scope
} // namespace Hooks
} // namespace Luma
