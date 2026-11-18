#pragma once

namespace Luma {
namespace Hooks {
namespace Scope {
class Guard
{
public:
   Guard();
   ~Guard();
   Guard(const Guard&) = delete;
   Guard& operator=(const Guard&) = delete;
};

// Inside a Luma scope, or bypassed: every detour passes the call through untouched
bool IsActive();

// Process wide, any thread: set before unhooking, so detours still in a chain only call their original
void SetBypass(bool bypass);
bool IsBypassed();
} // namespace Scope
} // namespace Hooks
} // namespace Luma
