#pragma once

namespace Luma {
namespace Draw {
// Step 1 fills this: true once a game declares bound state groups or Luma runs its own passes
inline bool IsNeeded()
{
   return false;
}
} // namespace Draw
} // namespace Luma
