#pragma once

#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

#include <dxgiformat.h>

#include "Common/Math.hpp"

// Texture format upgrades as rules: which of the game's textures get a mirror (Resources::Mirrors) and in what desc. Shared by D3D11 and D3D12, which both describe textures with DXGI
// formats: the rules name them (Guidelines.md section 4, typed declarations). Set once, in Init (Game::Resources::SetUpgrades).
namespace Luma {
namespace Resources {
namespace Upgrade {
enum class SizeFilters : uint32_t
{
   // No bit: every size
   All = 0,
   // The output (swapchain) resolution
   SwapchainResolution = 1 << 0,
   // The render resolution (TAA, SR)
   RenderResolution = 1 << 1,
   // The output's aspect ratio, within the pixel threshold: also catches render resolution targets and bloom-like downscales
   SwapchainAspectRatio = 1 << 2,
   RenderAspectRatio = 1 << 3,
   // Rules::custom_aspect_ratios: games that render 16:9 internally on any window
   CustomAspectRatio = 1 << 4,
   // Rules::custom_sizes
   CustomSize = 1 << 5,
   // Power of two mips of the larger of render and output resolution
   Mips = 1 << 6,
   // Cubes of any size
   Cubes = 1 << 7,
   SwapchainResolutionWidth = 1 << 8,
   SwapchainResolutionHeight = 1 << 9,
   DisplayResolution = 1 << 10,
   DisplayAspectRatio = 1 << 11,
   // Resolutions padded to multiples of 4 pixels per axis (Unreal) for textures that are
   PadTo4Px = 1 << 12,
   // Never 1x1 textures
   No1Px = 1 << 13,
};

constexpr SizeFilters operator|(SizeFilters a, SizeFilters b)
{
   return SizeFilters(uint32_t(a) | uint32_t(b));
}

constexpr bool Has(SizeFilters filters, SizeFilters filter)
{
   return (uint32_t(filters) & uint32_t(filter)) != 0;
}

enum class LutDimensions : uint8_t
{
   _1D,
   _2D,
   _3D,
   _2DAnd3D,
};

struct Lut
{
   // 1D: the width; 2D: the height (width is size squared); 3D: every side. UINT32_MAX: no LUT.
   uint32_t size = UINT32_MAX;
   LutDimensions dimensions = LutDimensions::_2D;
};

// A texture matching `filter` (width, height, depth or layers, samples; 0 ignores one) and
// `format` (UNKNOWN: any) gets `size` (0 keeps one)
struct CustomDimensions
{
   Math::uint4 filter = {};
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   Math::uint4 size = {};
};

struct Rules
{
   // Render targets and UAVs of these formats get RGBA16F mirrors
   std::vector<DXGI_FORMAT> formats;
   // Depth textures of these get 32 bit depth (keeping a stencil)
   std::vector<DXGI_FORMAT> depth_formats;
   // For 2D textures (LUTs and cubes apart)
   SizeFilters size_filters = SizeFilters::SwapchainResolution | SizeFilters::SwapchainAspectRatio;
   std::vector<float> custom_aspect_ratios = {16.f / 9.f};
   std::vector<Math::uint2> custom_sizes;
   uint32_t aspect_ratio_pixel_threshold = 1;
   Lut lut;
   // A size change, a scaled mirror (declared, not applied yet)
   std::vector<CustomDimensions> custom_dimensions;
};

void SetRules(const Rules& rules);
const Rules& GetRules();
// There are rules: mirrors and their hooks are needed
bool IsNeeded();

enum class Dimension : uint8_t
{
   Texture1D,
   Texture2D,
   Texture3D,
};

struct TextureInfo
{
   Dimension dimension = Dimension::Texture2D;
   uint32_t width = 1;
   uint32_t height = 1;
   uint32_t depth_or_layers = 1;
   uint32_t levels = 1;
   uint32_t samples = 1;
   DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   bool render_target = false;
   bool unordered_access = false;
   bool depth_stencil = false;
   bool cube = false;
   bool gpu_only = true;

   bool operator==(const TextureInfo&) const = default;
};

struct FrameState
{
   Math::uint2 render = {1, 1};
   Math::uint2 output = {1, 1};
   Math::uint2 display = {1, 1};
};

std::optional<TextureInfo> Match(const TextureInfo& info, const FrameState& state, bool has_initial_data);
DXGI_FORMAT GetBestFormat(const TextureInfo& info);
enum class ViewKind : uint8_t
{
   ShaderResource,
   RenderTarget,
   UnorderedAccess,
   DepthStencil,
};
// The format of the mirror's view for a game's view of the original. An unknown view format
// (the resource's default view) stays unknown.
DXGI_FORMAT GetBestViewFormat(DXGI_FORMAT view_format, ViewKind kind, DXGI_FORMAT original_format, DXGI_FORMAT mirror_format);
} // namespace Upgrade
} // namespace Resources
} // namespace Luma
