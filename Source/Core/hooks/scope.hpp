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

bool IsActive();
} // namespace Scope
} // namespace Hooks
} // namespace Luma
