#pragma once

#include <cstdint>

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

// This thread's scope set aside while it lives (the bypass stays): a call made meanwhile is answered as the game's, even from inside
// Luma's work
struct Suspension
{
   Suspension();
   ~Suspension();
   Suspension(const Suspension&) = delete;
   Suspension& operator=(const Suspension&) = delete;

   uint32_t depth = 0;
};

// Inside a Luma scope, or bypassed: every detour passes the call through untouched
bool IsActive();

// Process wide, any thread: set before unhooking, so detours still in a chain only call their original
void SetBypass(bool bypass);
bool IsBypassed();

// ReShade's work on the presenting thread, from its present event to Luma's Present (its effects, its overlay, the add-ons' present
// handlers): passed through as a Luma scope is. Opening an open window or closing a closed one does nothing.
void BeginThirdPartyWork();
void EndThirdPartyWork();
bool IsThirdPartyWorkOpen();

// Optional callback identifying third-party add-on return addresses whose direct API calls Luma hooks pass through.
using CallerFilter = bool (*)(const void* caller);
void SetCallerFilter(CallerFilter filter);
// Registers/unregisters a loaded DLL (e.g. NGX, FidelityFX, XeSS) whose internal threads bypass Luma hooks.
void AddIgnoredModule(void* module_handle);
void RemoveIgnoredModule(void* module_handle);
bool IsIgnoredCaller(const void* caller);
} // namespace Scope
} // namespace Hooks
} // namespace Luma
