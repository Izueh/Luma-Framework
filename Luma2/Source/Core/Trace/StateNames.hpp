#pragma once

#if DEVELOPMENT
#include <cstdint>
#include <format>
#include <string>

// The names trace gives pipeline state values. D3D11's and D3D12's enums share the same numeric values (blend, comparison,
// stencil op, cull, fill, address, filter), so both APIs' captures format identically.
namespace Luma {
namespace Trace {
namespace StateNames {
std::string GetBlendName(uint32_t value);
std::string GetBlendOpName(uint32_t value);
std::string GetComparisonName(uint32_t value);
std::string GetStencilOpName(uint32_t value);
std::string GetCullName(uint32_t value);
std::string GetFillName(uint32_t value);
std::string GetAddressName(uint32_t value);
std::string GetFilterName(uint32_t value);

// A render target's blend ("SRC_ALPHA ADD INV_SRC_ALPHA / alpha ONE ADD ZERO"), empty when off
template <typename TargetBlend>
std::string DescribeBlend(const TargetBlend& blend)
{
   if (!blend.BlendEnable)
      return {};
   return std::format("{} {} {} / alpha {} {} {}", GetBlendName(blend.SrcBlend), GetBlendOpName(blend.BlendOp), GetBlendName(blend.DestBlend),
      GetBlendName(blend.SrcBlendAlpha), GetBlendOpName(blend.BlendOpAlpha), GetBlendName(blend.DestBlendAlpha));
}

// "func fail depth_fail pass"
template <typename Face>
std::string DescribeStencilFace(const Face& face)
{
   return std::format("{} {} {} {}", GetComparisonName(face.StencilFunc), GetStencilOpName(face.StencilFailOp),
      GetStencilOpName(face.StencilDepthFailOp), GetStencilOpName(face.StencilPassOp));
}
} // namespace StateNames
} // namespace Trace
} // namespace Luma
#endif
