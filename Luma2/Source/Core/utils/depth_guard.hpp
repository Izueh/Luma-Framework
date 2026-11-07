#pragma once

#include <cstdint>

namespace Luma {
namespace Utils {
// Counts nested entries through a thread local counter, to only handle the outermost of nested calls to hooked functions
class DepthGuard
{
public:
   explicit DepthGuard(uint32_t& depth) : depth_(depth) { depth_++; }
   ~DepthGuard() { depth_--; }
   DepthGuard(const DepthGuard&) = delete;
   DepthGuard& operator=(const DepthGuard&) = delete;

   bool IsOutermost() const { return depth_ == 1; }

private:
   uint32_t& depth_;
};
} // namespace Utils
} // namespace Luma
