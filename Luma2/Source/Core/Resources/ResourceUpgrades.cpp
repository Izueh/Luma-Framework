#include "Resources/ResourceUpgrades.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cfloat>
#include <cmath>

#include "Log/Log.hpp"
#include "Notifications/Notifications.hpp"
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

// Shadow maps and other square textures pass the aspect ratio filter of a square output. Publishing skips 1x1, which games
// often have for a few frames at boot.
void WarnAboutSquareOutput(SizeFilters filters, Math::uint2 output)
{
   static std::atomic<bool> warned = false;
   if (!Has(filters, SizeFilters::SwapchainAspectRatio) || Has(filters, SizeFilters::No1Px) || output.x != output.y || output.x == 0)
      return;
#if !DEVELOPMENT && !TEST
   if (output.x == 1)
      return;
#endif
   if (!warned.exchange(true))
      Notifications::Post(Notifications::Kind::Warning, "Your current game output resolution has an aspect ratio of 1:1 (a squared resolution), that might cause issues "
                                                        "with texture upgrades by aspect ratio, given that shadow maps and other things are often rendered in squared "
                                                        "textures.");
}

// The 2D size filters, in their bit order
bool MatchesSize(const TextureInfo& desc, const FrameState& state)
{
   const SizeFilters filters = rules.size_filters;
   if (filters == SizeFilters::All)
      return true;
   WarnAboutSquareOutput(filters, state.output);
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

uint32_t GetMaxMipLevels(uint32_t width, uint32_t height, uint32_t depth)
{
   const uint32_t max_dimension = std::max({width, height, depth, 1u});
   return uint32_t(std::floor(std::log2(double(max_dimension)))) + 1u;
}

bool ApplyCustomDimensions(const TextureInfo& desc, bool has_initial_data, TextureInfo& upgraded)
{
   if (has_initial_data || rules.custom_dimensions.empty())
      return false;
   for (const CustomDimensions& rule : rules.custom_dimensions)
   {
      if (rule.format != DXGI_FORMAT_UNKNOWN && rule.format != desc.format)
         continue;
      if ((rule.filter.x > 0 && rule.filter.x != desc.width) || (rule.filter.y > 0 && rule.filter.y != desc.height) ||
         (rule.filter.z > 0 && rule.filter.z != desc.depth_or_layers) || (rule.filter.w > 0 && rule.filter.w != desc.samples))
         continue;

      const bool wants_width = rule.size.x > 0 && rule.size.x != desc.width;
      const bool supports_height = (desc.dimension == Dimension::Texture2D || desc.dimension == Dimension::Texture3D) && !desc.cube;
      const bool wants_height = rule.size.y > 0 && rule.size.y != desc.height;
      if (wants_height && !supports_height)
         continue;
      const bool supports_depth_layers = !desc.cube;
      const bool wants_depth_layers = rule.size.z > 0 && rule.size.z != desc.depth_or_layers;
      if (wants_depth_layers && !supports_depth_layers)
         continue;
      const bool supports_samples = desc.dimension == Dimension::Texture2D && !desc.cube && !desc.unordered_access && desc.levels == 1;
      const bool wants_samples = rule.size.w > 0 && rule.size.w != desc.samples;
      if (wants_samples && !supports_samples)
         continue;

      bool resized = false;
      if (wants_width)
      {
         upgraded.width = rule.size.x;
         resized = true;
      }
      if (wants_height)
      {
         upgraded.height = rule.size.y;
         resized = true;
      }
      if (wants_depth_layers)
      {
         upgraded.depth_or_layers = rule.size.z;
         resized = true;
      }
      if (wants_samples)
      {
         upgraded.samples = rule.size.w;
         resized = true;
      }
      if (!resized)
         break;

      if (desc.cube)
         upgraded.height = upgraded.width;

      if (upgraded.levels > 1)
      {
         const bool is_3d = desc.dimension == Dimension::Texture3D;
         const uint32_t original_max = GetMaxMipLevels(desc.width, desc.height, is_3d ? desc.depth_or_layers : 0);
         const uint32_t upgraded_max = GetMaxMipLevels(upgraded.width, upgraded.height, is_3d ? upgraded.depth_or_layers : 0);
         if (desc.levels == original_max || upgraded.levels > upgraded_max)
            upgraded.levels = upgraded_max;
      }
      return true;
   }
   return false;
}

// A game texture of a typed format can have views with no desc (the texture's format), so it stays typed: a typeless best format takes
// its default typed one (for depth, the D format)
DXGI_FORMAT GetInPlaceFormat(const TextureInfo& game, DXGI_FORMAT upgraded)
{
   using namespace Utils::Format::DXGI;
   if (IsTypeless(game.format) || !IsTypeless(upgraded))
      return upgraded;
   if (game.depth_stencil)
   {
      switch (upgraded)
      {
      case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
      case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_D32_FLOAT;
      default: break;
      }
   }
   return GetDefaultTyped(upgraded);
}

// Mips resolved to a count (0 is a full chain), so the game's and the texture's can be told apart
std::optional<UpgradeInfo> GetInPlaceUpgradeInfo(UpgradeInfo info)
{
   TextureInfo& game = info.game;
   TextureInfo& upgraded = info.upgraded;
   if (game.levels == 0)
      game.levels = GetMaxMipLevels(game.width, game.height, game.dimension == Dimension::Texture3D ? game.depth_or_layers : 1u);
   if (upgraded.levels == 0)
      upgraded.levels = GetMaxMipLevels(upgraded.width, upgraded.height, upgraded.dimension == Dimension::Texture3D ? upgraded.depth_or_layers : 1u);
   const bool resized = upgraded.width != game.width || upgraded.height != game.height || upgraded.levels != game.levels;
   if (upgraded.samples != game.samples || upgraded.depth_or_layers != game.depth_or_layers ||
      (resized && (game.dimension != Dimension::Texture2D || upgraded.levels < game.levels)))
      return std::nullopt;
   upgraded.format = GetInPlaceFormat(game, upgraded.format);
   if (!resized && upgraded.format == game.format)
      return std::nullopt;
   info.resized = resized;
   info.method = Method::InPlace;
   return info;
}
} // namespace

void SetRules(const Rules& value)
{
   rules = value;
   set = !rules.formats.empty() || !rules.depth_formats.empty() || !rules.custom_dimensions.empty();
}

const Rules& GetRules()
{
   return rules;
}

bool IsNeeded()
{
   return set;
}

std::optional<UpgradeInfo> GetUpgradeInfo(const TextureInfo& game, const FrameState& state, const UpgradeOptions& options)
{
   const bool writable_sr = options.allow_writable_sr && game.shader_resource && !options.has_initial_data;
   const bool color = game.render_target || game.unordered_access || writable_sr;
   if ((!set && !options.force_format) || !game.gpu_only || (!color && !game.depth_stencil))
      return std::nullopt;

   UpgradeInfo info = {.game = game, .upgraded = game};
   info.resized = ApplyCustomDimensions(game, options.has_initial_data, info.upgraded);

   const bool format_matches =
      options.force_format || (game.depth_stencil ? Contains(rules.depth_formats, game.format) : (color && Contains(rules.formats, game.format)));
   if (!format_matches && !info.resized)
      return std::nullopt;

   // 2D textures (cubes are 2D arrays of 6) by size, LUTs by their own shape; depth textures never are LUTs
   bool format_upgrade = format_matches && game.dimension == Dimension::Texture2D && (game.depth_or_layers == 1 || game.cube) && MatchesSize(game, state);
   if (format_matches && !game.depth_stencil)
      format_upgrade |= IsLut(game);
   if (!format_upgrade && !info.resized)
      return std::nullopt;
   if (format_upgrade)
      info.upgraded.format = GetBestFormat(info.upgraded);
   if (!info.resized && info.upgraded.format == info.game.format)
      return std::nullopt;
   if (rules.method == Method::InPlace && !options.mirror)
   {
      if (std::optional<UpgradeInfo> in_place = GetInPlaceUpgradeInfo(info))
         return in_place;
      // If InPlace rejected solely because GetInPlaceFormat kept the same format with no resize, don't create a redundant mirror
      if (!info.resized && GetInPlaceFormat(info.game, info.upgraded.format) == info.game.format)
         return std::nullopt;
   }
   return info;
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

uint32_t GetMipSize(uint32_t size, uint32_t level)
{
   return std::max(size >> level, 1u);
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
