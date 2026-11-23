#include "Resources/ResourceUpgrades.hpp"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>

#include "Utils/Debug.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"

namespace Luma {
namespace Resources {
namespace Upgrade {
namespace {
Rules rules;
bool set = false;

// `width` x `height` is one of the mips of `base_width` x `base_height`
bool IsMipOf(uint32_t base_width, uint32_t base_height, uint32_t width, uint32_t height)
{
   if (width == 0 || height == 0 || base_width == 0 || base_height == 0 || base_width < width || base_height < height)
      return false;
   const bool valid_width = (base_width >> (std::countr_zero(base_width) - std::countr_zero(width))) == width;
   const bool valid_height = (base_height >> (std::countr_zero(base_height) - std::countr_zero(height))) == height;
   return valid_width && valid_height;
}

bool Contains(const std::vector<DXGI_FORMAT>& formats, DXGI_FORMAT format)
{
   return std::ranges::find(formats, format) != formats.end();
}

// The 2D size filters, in their bit order
bool MatchesSize(const TextureInfo& desc, const FrameState& state)
{
   const SizeFilters filters = rules.size_filters;
   if (filters == SizeFilters::All)
      return true;
   Math::uint2 render = state.render;
   Math::uint2 output = state.output;
   Math::uint2 display = state.display;
   // A texture aligned to 4 pixels is compared with the resolutions padded the same way
   if (Has(filters, SizeFilters::PadTo4Px) && desc.width % 4 == 0 && desc.height % 4 == 0)
   {
      for (Math::uint2* size : {&render, &output, &display})
         *size = {(size->x + 3u) & ~3u, (size->y + 3u) & ~3u};
   }
   bool matches = false;
   if (Has(filters, SizeFilters::DisplayResolution))
      matches |= desc.width == display.x && desc.height == display.y;
   if (Has(filters, SizeFilters::SwapchainResolution))
      matches |= desc.width == output.x && desc.height == output.y;
   if (Has(filters, SizeFilters::SwapchainResolutionWidth))
      matches |= desc.width == output.x;
   if (Has(filters, SizeFilters::SwapchainResolutionHeight))
      matches |= desc.height == output.y;
   if (Has(filters, SizeFilters::RenderResolution))
      matches |= desc.width == render.x && desc.height == render.y;
   // Cubes were let through by their layers: only with the Cubes filter, at any size
   if (!Has(filters, SizeFilters::Cubes))
      matches &= !desc.cube;
   else
      matches |= desc.cube;
   // Scaled from the smallest dimension, which leaves more threshold (engines round downscales in several ways)
   const float threshold = float(rules.aspect_ratio_pixel_threshold);
   const float width = float(desc.width);
   const float height = float(desc.height);
   const float min_ratio = desc.width <= desc.height ? (width - threshold) / height : width / (height + threshold);
   const float max_ratio = desc.width <= desc.height ? (width + threshold) / height : width / (height - threshold);
   const auto within = [&](float ratio) { return ratio >= min_ratio - FLT_EPSILON && ratio <= max_ratio + FLT_EPSILON; };
   if (Has(filters, SizeFilters::SwapchainAspectRatio))
      matches |= within(float(output.x) / float(output.y));
   if (Has(filters, SizeFilters::RenderAspectRatio))
      matches |= within(float(render.x) / float(render.y));
   if (Has(filters, SizeFilters::DisplayAspectRatio))
      matches |= within(float(display.x) / float(display.y));
   if (Has(filters, SizeFilters::CustomAspectRatio))
      matches |= std::ranges::any_of(rules.custom_aspect_ratios, within);
   if (Has(filters, SizeFilters::CustomSize))
      matches |= std::ranges::any_of(rules.custom_sizes, [&](const Math::uint2& size) { return desc.width == size.x && desc.height == size.y; });
   if (Has(filters, SizeFilters::Mips))
   {
      const Math::uint2 largest = output.y >= render.y ? output : render;
      matches |= IsMipOf(largest.x, largest.y, desc.width, desc.height);
   }
   if (Has(filters, SizeFilters::No1Px))
      matches &= desc.width != 1 || desc.height != 1;
   return matches;
}

bool IsLut(const TextureInfo& desc)
{
   const uint32_t size = rules.lut.size;
   if (size == UINT32_MAX)
      return false;
   switch (rules.lut.dimensions)
   {
   case LutDimensions::_1D: return desc.dimension == Dimension::Texture1D && desc.width == size && desc.depth_or_layers == 1 && desc.levels == 1;
   case LutDimensions::_3D:
      return desc.dimension == Dimension::Texture3D && desc.width == size && desc.height == size && desc.depth_or_layers == size && desc.levels == 1;
   default:
   {
      // 2D: the slices side by side, width = size squared
      const bool lut_2d = desc.dimension == Dimension::Texture2D && desc.width == size * size && desc.height == size && desc.depth_or_layers == 1 && desc.levels == 1 &&
         desc.samples == 1;
      const bool lut_3d = rules.lut.dimensions == LutDimensions::_2DAnd3D && desc.dimension == Dimension::Texture3D && desc.width == size && desc.height == size &&
         desc.depth_or_layers == size && desc.levels == 1;
      return lut_2d || lut_3d;
   }
   }
}
} // namespace

void SetRules(const Rules& value)
{
   rules = value;
   set = !rules.formats.empty() || !rules.depth_formats.empty();
   // A size change is a scaled mirror, not built yet
   if (!rules.custom_dimensions.empty())
      LUMA_UNIMPLEMENTED("Resources::Upgrade custom dimensions (Resources::Scaling)");
}

const Rules& GetRules()
{
   return rules;
}

bool IsNeeded()
{
   return set;
}

std::optional<TextureInfo> Match(const TextureInfo& desc, const FrameState& state, [[maybe_unused]] bool has_initial_data)
{
   // Only what the GPU writes: render targets and UAVs (writable shader resources need chains, not built yet), and depth
   const bool color = desc.render_target || desc.unordered_access;
   if (!set || !desc.gpu_only || (!color && !desc.depth_stencil))
      return std::nullopt;
   const bool format_matches = desc.depth_stencil ? Contains(rules.depth_formats, desc.format) : Contains(rules.formats, desc.format);
   if (!format_matches)
      return std::nullopt;
   // 2D textures (cubes are 2D arrays of 6) by size, LUTs by their own shape; depth textures never are LUTs
   bool matches = desc.dimension == Dimension::Texture2D && (desc.depth_or_layers == 1 || desc.cube) && MatchesSize(desc, state);
   if (!desc.depth_stencil)
      matches |= IsLut(desc);
   if (!matches)
      return std::nullopt;
   TextureInfo mirror = desc;
   mirror.format = GetBestFormat(desc);
   return mirror;
}

DXGI_FORMAT GetBestFormat(const TextureInfo& desc)
{
   if (desc.depth_stencil)
   {
      // A stencil is kept
      switch (desc.format)
      {
      case DXGI_FORMAT_R24G8_TYPELESS:
      case DXGI_FORMAT_D24_UNORM_S8_UINT:
      case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32G8X24_TYPELESS;
      // Typeless at the deepest depth, so it can be read as a shader resource too
      default: return DXGI_FORMAT_R32_TYPELESS;
      }
   }
   return DXGI_FORMAT_R16G16B16A16_FLOAT;
}

DXGI_FORMAT GetBestViewFormat(DXGI_FORMAT view_format, ViewKind kind, [[maybe_unused]] DXGI_FORMAT original_format, DXGI_FORMAT mirror_format)
{
   using namespace Utils::Format::DXGI;
   if (view_format == DXGI_FORMAT_UNKNOWN)
      return DXGI_FORMAT_UNKNOWN;
   switch (mirror_format)
   {
   case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
   case DXGI_FORMAT_R32_TYPELESS:
   case DXGI_FORMAT_R32_FLOAT:
   case DXGI_FORMAT_D32_FLOAT: return kind == ViewKind::DepthStencil ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_R32_FLOAT;
   case DXGI_FORMAT_R32G8X24_TYPELESS:
   case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      if (kind == ViewKind::DepthStencil)
         return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
      // The game's view read the depth or the stencil of a depth stencil texture: the same part of the mirror
      return view_format == DXGI_FORMAT_X24_TYPELESS_G8_UINT || view_format == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT ? DXGI_FORMAT_X32_TYPELESS_G8X24_UINT
                                                                                                                   : DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
   default:
   {
      // Another family: a typed mirror format replaces the view's; a typeless one takes the view's encoding in the mirror's family
      ASSERT_ONCE(IsTypeless(original_format) || GetTypeless(view_format) == GetTypeless(original_format));
      if (!IsTypeless(mirror_format))
         return mirror_format;
      const DXGI_FORMAT remapped = GetRemapped(view_format, mirror_format);
      return IsTypeless(remapped) ? GetDefaultTyped(mirror_format) : remapped;
   }
   }
}
} // namespace Upgrade
} // namespace Resources
} // namespace Luma
