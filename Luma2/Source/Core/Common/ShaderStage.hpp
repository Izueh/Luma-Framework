#pragma once

#include <cstdint>

namespace Luma {
// One callback with a stage argument stands for the per-stage copies of a native function (VSSetConstantBuffers...). Here, not under
// Shaders, because Hooks (below the features) names stages too.
enum class ShaderStage : uint8_t
{
   Vertex,
   Hull,
   Domain,
   Geometry,
   Pixel,
   Compute,
   Amplification,
   Mesh,
};
} // namespace Luma
