#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D11/D3D11MirrorsInternal.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <vector>

#include <wrl/client.h>

#include "Draw/D3D11/D3D11Draw.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Utils/Debug.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace Mirrors {
namespace D3D11 {
namespace {
using namespace Utils::Format::DXGI;
using Internal::OwnedResource;
using Internal::ResourceMirrorState;

std::atomic<uint32_t> unhandled_resolves = 0;

// CPU data of the game's format for `subresource` (or `box` of it) of a texture the game made as `original_info`, in `format`
std::optional<Converted> ConvertUpload(const Upgrade::TextureInfo& original_info, DXGI_FORMAT format, UINT subresource, const D3D11_BOX* box, const void* data,
   UINT row_pitch, UINT depth_pitch)
{
   if (format != DXGI_FORMAT_R16G16B16A16_FLOAT)
      return std::nullopt;
   const uint32_t level = subresource % std::max(original_info.levels, 1u);
   const bool volume = original_info.dimension == Upgrade::Dimension::Texture3D;
   const uint32_t width = box ? box->right - box->left : Upgrade::GetMipSize(original_info.width, level);
   const uint32_t height = box ? box->bottom - box->top : Upgrade::GetMipSize(original_info.height, level);
   const uint32_t depth = box ? box->back - box->front : volume ? Upgrade::GetMipSize(original_info.depth_or_layers, level)
                                                                : 1;
   return ConvertToRgba16Float(data, row_pitch, depth_pitch, original_info.format, width, height, depth);
}

// A native CopyResource/CopySubresourceRegion reinterprets raw bits within a compatible typeless format family,
// whereas a shader blit converts values or scales pixels. We use non-sRGB views here so the shader performs a
// raw-value copy without applying hardware sRGB decode/encode.
DXGI_FORMAT GetRawFormat(DXGI_FORMAT format)
{
   switch (format)
   {
   case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
   case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
   case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
   default: return IsTypeless(format) ? GetDefaultTyped(format) : format;
   }
}

ComPtr<ID3D11Texture2D> CreateProxy(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& desc, UINT bind, UINT width = 0, UINT height = 0)
{
   D3D11_TEXTURE2D_DESC proxy = desc;
   if (width > 0)
      proxy.Width = width;
   if (height > 0)
      proxy.Height = height;
   proxy.MipLevels = 1;
   proxy.ArraySize = 1;
   proxy.SampleDesc = {1, 0};
   proxy.Usage = D3D11_USAGE_DEFAULT;
   proxy.BindFlags = bind;
   proxy.CPUAccessFlags = 0;
   proxy.MiscFlags = 0;
   ComPtr<ID3D11Texture2D> texture;
   device->CreateTexture2D(&proxy, nullptr, &texture);
   return texture;
}

struct ConvertingCopyOptions
{
   DXGI_FORMAT resolve_format = DXGI_FORMAT_UNKNOWN;
   bool while_off = false;
   // Written at another size (an in-place texture of another size), at x, y
   Math::uint2 target_size = {};
};

// Converts and/or scales a 2D texture subresource (or partial region) through a shader blit.
// Resolves MSAA sources first into a single-sample temporary because a standard 2D SRV blit cannot sample MSAA directly.
bool ConvertingCopySubresourceRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z,
   ID3D11Resource* source, UINT source_subresource, const D3D11_BOX* box, const ConvertingCopyOptions& options = {})
{
   if (z != 0)
      return false;
   ComPtr<ID3D11Texture2D> source_texture;
   ComPtr<ID3D11Texture2D> destination_texture;
   if (FAILED(source->QueryInterface(IID_PPV_ARGS(&source_texture))) || FAILED(destination->QueryInterface(IID_PPV_ARGS(&destination_texture))))
      return false;
   D3D11_TEXTURE2D_DESC source_desc;
   D3D11_TEXTURE2D_DESC destination_desc;
   source_texture->GetDesc(&source_desc);
   destination_texture->GetDesc(&destination_desc);
   if (destination_desc.SampleDesc.Count != 1)
      return false;
   const UINT src_mips = std::max(source_desc.MipLevels, 1u);
   const UINT dst_mips = std::max(destination_desc.MipLevels, 1u);
   const UINT src_mip = source_subresource % src_mips;
   const UINT src_slice = source_subresource / src_mips;
   const UINT dst_mip = destination_subresource % dst_mips;
   const UINT dst_slice = destination_subresource / dst_mips;
   if (src_slice >= source_desc.ArraySize || dst_slice >= destination_desc.ArraySize)
      return false;
   const UINT src_w = Upgrade::GetMipSize(source_desc.Width, src_mip);
   const UINT src_h = Upgrade::GetMipSize(source_desc.Height, src_mip);
   const UINT dst_w = Upgrade::GetMipSize(destination_desc.Width, dst_mip);
   const UINT dst_h = Upgrade::GetMipSize(destination_desc.Height, dst_mip);
   D3D11_BOX src_box = {0, 0, 0, src_w, src_h, 1};
   if (box)
   {
      if (box->left >= box->right || box->top >= box->bottom || box->front >= box->back || box->right > src_w || box->bottom > src_h || box->back > 1)
         return false;
      src_box = *box;
   }
   const UINT copy_w = src_box.right - src_box.left;
   const UINT copy_h = src_box.bottom - src_box.top;
   const bool full_src_box = src_box.left == 0 && src_box.top == 0 && src_box.front == 0 && copy_w == src_w && copy_h == src_h;
   const bool sized = options.target_size.x > 0 && options.target_size.y > 0;
   const bool full_dst = x == 0 && y == 0 && full_src_box && (!sized || (options.target_size.x == dst_w && options.target_size.y == dst_h));
   const UINT region_w = sized ? options.target_size.x : copy_w;
   const UINT region_h = sized ? options.target_size.y : copy_h;
   if (!full_dst && (x + region_w > dst_w || y + region_h > dst_h))
      return false;

   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   ComPtr<ID3D11Texture2D> read = source_texture;
   UINT read_mip = src_mip;
   UINT read_slice = src_slice;
   UINT read_array_size = source_desc.ArraySize;
   if (source_desc.SampleDesc.Count > 1)
   {
      // Resolve MSAA source first (must be a non-typeless format compatible with the source texture's family)
      D3D11_TEXTURE2D_DESC resolved_desc = source_desc;
      resolved_desc.Format = options.resolve_format != DXGI_FORMAT_UNKNOWN ? options.resolve_format : GetRawFormat(source_desc.Format);
      read = CreateProxy(device.Get(), resolved_desc, D3D11_BIND_SHADER_RESOURCE, src_w, src_h);
      if (!read)
         return false;
      context->ResolveSubresource(read.Get(), 0, source_texture.Get(), source_subresource, resolved_desc.Format);
      read_mip = 0;
      read_slice = 0;
      read_array_size = 1;
      if (!full_src_box)
      {
         ComPtr<ID3D11Texture2D> cropped = CreateProxy(device.Get(), resolved_desc, D3D11_BIND_SHADER_RESOURCE, copy_w, copy_h);
         if (!cropped)
            return false;
         context->CopySubresourceRegion(cropped.Get(), 0, 0, 0, 0, read.Get(), 0, &src_box);
         read = std::move(cropped);
      }
   }
   else if (!full_src_box || !(source_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
   {
      read = CreateProxy(device.Get(), source_desc, D3D11_BIND_SHADER_RESOURCE, copy_w, copy_h);
      if (!read)
         return false;
      context->CopySubresourceRegion(read.Get(), 0, 0, 0, 0, source_texture.Get(), source_subresource, &src_box);
      read_mip = 0;
      read_slice = 0;
      read_array_size = 1;
   }

   const UINT target_w = full_dst ? dst_w : region_w;
   const UINT target_h = full_dst ? dst_h : region_h;
   const bool direct_rtv = full_dst && (destination_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
   ComPtr<ID3D11Texture2D> write = destination_texture;
   UINT write_mip = dst_mip;
   UINT write_slice = dst_slice;
   UINT write_array_size = destination_desc.ArraySize;
   if (!direct_rtv)
   {
      write = CreateProxy(device.Get(), destination_desc, D3D11_BIND_RENDER_TARGET, target_w, target_h);
      if (!write)
         return false;
      write_mip = 0;
      write_slice = 0;
      write_array_size = 1;
   }

   D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {.Format = GetRawFormat(source_desc.Format)};
   if (read_array_size > 1)
   {
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
      srv_desc.Texture2DArray = {read_mip, 1, read_slice, 1};
   }
   else
   {
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D = {read_mip, 1};
   }

   D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {.Format = GetRawFormat(destination_desc.Format)};
   if (write_array_size > 1)
   {
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
      rtv_desc.Texture2DArray = {write_mip, write_slice, 1};
   }
   else
   {
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      rtv_desc.Texture2D = {write_mip};
   }

   ComPtr<ID3D11ShaderResourceView> srv;
   ComPtr<ID3D11RenderTargetView> rtv;
   if (FAILED(device->CreateShaderResourceView(read.Get(), &srv_desc, &srv)) || FAILED(device->CreateRenderTargetView(write.Get(), &rtv_desc, &rtv)))
      return false;

   const bool same_size = copy_w == target_w && copy_h == target_h;
   const Draw::D3D11::CopyOptions draw_options = {.state_mask = Draw::FULL_GRAPHICS, .while_off = options.while_off};
   if (!(same_size ? Draw::D3D11::Copy(context, srv.Get(), rtv.Get(), draw_options) : Draw::D3D11::Scale(context, srv.Get(), rtv.Get(), draw_options)))
      return false;
   if (!same_size)
      Scaling::CountScaledBlit();
   if (write != destination_texture)
      context->CopySubresourceRegion(destination_texture.Get(), destination_subresource, x, y, 0, write.Get(), 0, nullptr);
   return true;
}

bool ConvertingCopy(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
{
   return ConvertingCopySubresourceRegion(context, destination, 0, 0, 0, 0, source, 0, nullptr);
}

DXGI_FORMAT GetFormat(ID3D11Resource* resource)
{
   D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
   resource->GetType(&dimension);
   switch (dimension)
   {
   case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
   {
      D3D11_TEXTURE1D_DESC desc;
      static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
      return desc.Format;
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
   {
      D3D11_TEXTURE2D_DESC desc;
      static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
      return desc.Format;
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
   {
      D3D11_TEXTURE3D_DESC desc;
      static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
      return desc.Format;
   }
   default: return DXGI_FORMAT_UNKNOWN;
   }
}

bool SameDimensions2D(ID3D11Resource* a, ID3D11Resource* b)
{
   ComPtr<ID3D11Texture2D> ta;
   ComPtr<ID3D11Texture2D> tb;
   if (FAILED(a->QueryInterface(IID_PPV_ARGS(&ta))) || FAILED(b->QueryInterface(IID_PPV_ARGS(&tb))))
      return true;
   D3D11_TEXTURE2D_DESC da;
   D3D11_TEXTURE2D_DESC db;
   ta->GetDesc(&da);
   tb->GetDesc(&db);
   return da.Width == db.Width && da.Height == db.Height;
}

uint32_t GetLevels(ID3D11Resource* resource)
{
   const std::optional<Upgrade::TextureInfo> info = Internal::DescribeResource(resource);
   return info ? std::max(info->levels, 1u) : 1u;
}

uint32_t GetSubresourceCount(ID3D11Resource* resource)
{
   const std::optional<Upgrade::TextureInfo> info = Internal::DescribeResource(resource);
   if (!info)
      return 1;
   const uint32_t layers = info->dimension == Upgrade::Dimension::Texture3D ? 1 : info->depth_or_layers;
   return std::max(info->levels, 1u) * std::max(layers, 1u);
}

// A copy between an in-place texture and one whose format it can't be copied to natively, or of another size (the runtime would drop it)
bool IsInPlaceMismatch(ID3D11Resource* destination, ID3D11Resource* source)
{
   if (!HasAnyInPlace() || !destination || !source)
      return false;
   const InPlaceState* destination_state = FindInPlace(destination);
   const InPlaceState* source_state = FindInPlace(source);
   if (!destination_state && !source_state)
      return false;
   if (!AreCopyCompatible(GetFormat(destination), GetFormat(source)))
      return true;
   const bool resized = (destination_state && destination_state->resized) || (source_state && source_state->resized);
   return resized && (!SameDimensions2D(destination, source) || GetSubresourceCount(destination) != GetSubresourceCount(source));
}

// Game subresource index (in original_info.levels) remapped to the upgraded texture's mip count
UINT RemapInPlaceSubresource(const InPlaceState* state, UINT subresource)
{
   if (!state || !state->resized || state->info.levels == state->original_info.levels)
      return subresource;
   const uint32_t game_levels = std::max(state->original_info.levels, 1u);
   return (subresource % game_levels) + (subresource / game_levels) * std::max(state->info.levels, 1u);
}

// Clamps a scaled box to the actual mip dimensions (mip > 0 floors odd sizes, so mip-0 scale can overshoot by 1 texel)
D3D11_BOX ClampBoxToMip(const Scaling::Box& scaled, uint32_t mip_w, uint32_t mip_h)
{
   const UINT left = std::min(scaled.left, mip_w > 0 ? mip_w - 1u : 0u);
   const UINT top = std::min(scaled.top, mip_h > 0 ? mip_h - 1u : 0u);
   const UINT right = std::clamp(scaled.right, left + 1u, std::max(mip_w, 1u));
   const UINT bottom = std::clamp(scaled.bottom, top + 1u, std::max(mip_h, 1u));
   return {left, top, scaled.front, right, bottom, scaled.back};
}

// True: Luma issued it, converted (or skipped it when it couldn't, as the native copy would be dropped)
bool RedirectInPlaceCopy(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
{
   if (!IsInPlaceMismatch(destination, source))
      return false;
   bool converted = true;
   // The game's CopyResource has the same mips and layers on both: an in-place texture with more mips copies the game's
   const uint32_t destination_levels = GetLevels(destination);
   const uint32_t source_levels = GetLevels(source);
   const uint32_t levels = std::min(destination_levels, source_levels);
   const uint32_t layers = GetSubresourceCount(source) / source_levels;
   for (uint32_t layer = 0; layer < layers; layer++)
   {
      for (uint32_t level = 0; level < levels; level++)
         converted &= ConvertingCopySubresourceRegion(context, destination, level + layer * destination_levels, 0, 0, 0, source, level + layer * source_levels, nullptr,
            {.while_off = true});
   }
   ASSERT_ONCE_MSG(converted, "A copy between a texture upgraded in place and one of another format couldn't be converted: it was skipped");
   return true;
}

bool RedirectInPlaceCopyRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z, ID3D11Resource* source,
   UINT source_subresource, const D3D11_BOX* box)
{
   if (!HasAnyInPlace() || !destination || !source)
      return false;
   const InPlaceState* destination_state = FindInPlace(destination);
   const InPlaceState* source_state = FindInPlace(source);
   if (!destination_state && !source_state)
      return false;
   const Math::float2 source_scale = GetInPlaceScale(source_state);
   const Math::float2 destination_scale = GetInPlaceScale(destination_state);
   const bool compatible = AreCopyCompatible(GetFormat(destination), GetFormat(source));
   const bool rescaled = source_scale != Math::float2{1.f, 1.f} || destination_scale != Math::float2{1.f, 1.f};
   if (compatible && !rescaled)
      return false;
   const UINT mapped_dst_subresource = RemapInPlaceSubresource(destination_state, destination_subresource);
   const UINT mapped_src_subresource = RemapInPlaceSubresource(source_state, source_subresource);
   const Upgrade::TextureInfo game_source = source_state ? source_state->original_info : Internal::DescribeResource(source).value_or(Upgrade::TextureInfo{});
   const Upgrade::TextureInfo real_source = source_state ? source_state->info : game_source;
   const Upgrade::TextureInfo real_destination = destination_state ? destination_state->info : Internal::DescribeResource(destination).value_or(Upgrade::TextureInfo{});
   const uint32_t source_level = source_subresource % std::max(game_source.levels, 1u);
   const uint32_t dst_game_levels = destination_state ? std::max(destination_state->original_info.levels, 1u) : std::max(real_destination.levels, 1u);
   const uint32_t destination_level = destination_subresource % dst_game_levels;
   const uint32_t src_mip_w = Upgrade::GetMipSize(real_source.width, source_level);
   const uint32_t src_mip_h = Upgrade::GetMipSize(real_source.height, source_level);
   const uint32_t dst_mip_w = Upgrade::GetMipSize(real_destination.width, destination_level);
   const uint32_t dst_mip_h = Upgrade::GetMipSize(real_destination.height, destination_level);
   // The game's box and offset are in its sizes
   std::optional<D3D11_BOX> source_box;
   if (box)
   {
      const Scaling::Box scaled = Scaling::RescaleBox({box->left, box->top, box->front, box->right, box->bottom, box->back}, source_scale);
      source_box = ClampBoxToMip(scaled, src_mip_w, src_mip_h);
   }
   const UINT destination_x = std::min(UINT(std::lround(double(x) * double(destination_scale.x))), dst_mip_w > 0 ? dst_mip_w - 1u : 0u);
   const UINT destination_y = std::min(UINT(std::lround(double(y) * double(destination_scale.y))), dst_mip_h > 0 ? dst_mip_h - 1u : 0u);
   if (compatible && source_scale == destination_scale)
   {
      context->CopySubresourceRegion(destination, mapped_dst_subresource, destination_x, destination_y, z, source, mapped_src_subresource,
         source_box ? &*source_box : nullptr);
      return true;
   }
   // The region the game copies, in its sizes, at the destination's
   const uint32_t game_w = box ? box->right - box->left : Upgrade::GetMipSize(game_source.width, source_level);
   const uint32_t game_h = box ? box->bottom - box->top : Upgrade::GetMipSize(game_source.height, source_level);
   const uint32_t max_target_w = dst_mip_w > destination_x ? dst_mip_w - destination_x : 1u;
   const uint32_t max_target_h = dst_mip_h > destination_y ? dst_mip_h - destination_y : 1u;
   const Math::uint2 target_size = {std::clamp(uint32_t(std::lround(double(game_w) * double(destination_scale.x))), 1u, max_target_w),
      std::clamp(uint32_t(std::lround(double(game_h) * double(destination_scale.y))), 1u, max_target_h)};
   [[maybe_unused]] const bool converted = ConvertingCopySubresourceRegion(context, destination, mapped_dst_subresource, destination_x, destination_y, z, source,
      mapped_src_subresource, source_box ? &*source_box : nullptr, {.while_off = true, .target_size = target_size});
   ASSERT_ONCE_MSG(converted, "A region copy between a texture upgraded in place and one of another format couldn't be converted: it was skipped");
   return true;
}

// The game's resolve format is of the family it asked for: mapped to the in-place texture's
bool RedirectInPlaceResolve(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source, UINT source_subresource,
   DXGI_FORMAT format)
{
   if (!HasAnyInPlace() || !destination || !source)
      return false;
   const InPlaceState* source_state = FindInPlace(source);
   const InPlaceState* destination_state = FindInPlace(destination);
   if (!source_state && !destination_state)
      return false;
   const UINT mapped_dst_subresource = RemapInPlaceSubresource(destination_state, destination_subresource);
   const UINT mapped_src_subresource = RemapInPlaceSubresource(source_state, source_subresource);
   const DXGI_FORMAT destination_format = GetFormat(destination);
   const DXGI_FORMAT source_format = GetFormat(source);
   const DXGI_FORMAT game_source_format = source_state ? source_state->original_info.format : source_format;
   if (GetTypeless(destination_format) != GetTypeless(source_format) || !SameDimensions2D(destination, source))
   {
      const DXGI_FORMAT resolve_format = GetResolveFormat(format, game_source_format, source_format);
      if (!ConvertingCopySubresourceRegion(context, destination, mapped_dst_subresource, 0, 0, 0, source, mapped_src_subresource, nullptr,
             {.resolve_format = resolve_format, .while_off = true}))
      {
         unhandled_resolves++;
         ASSERT_ONCE_MSG(false, "Resolving between a texture upgraded in place and one of another format failed to convert: it was skipped");
      }
      return true;
   }
   context->ResolveSubresource(destination, mapped_dst_subresource, source, mapped_src_subresource, GetResolveFormat(format, game_source_format, destination_format));
   return true;
}

struct MappedMirrorEntry
{
   ID3D11Resource* resource = nullptr;
   UINT subresource = 0;
   D3D11_MAPPED_SUBRESOURCE mapped = {};
};

struct MappedMirrors
{
   std::vector<MappedMirrorEntry> entries;
};

void WriteToMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT row_pitch, UINT depth_pitch)
{
   const ResourceMirrorState* state = FindMirrorState<OwnedResource>(resource);
   if (!state || !data)
      return;
   const std::optional<Converted> converted = ConvertUpload(state->original_info, state->info.format, subresource, box, data, row_pitch, depth_pitch);
   if (!converted)
   {
      ASSERT_ONCE_MSG(false, "CPU data written to a mirrored texture can't be converted to its mirror's format: the mirror is stale");
      return;
   }
   context->UpdateSubresource(state->mirror.Get(), subresource, box, converted->bytes.data(), converted->row_pitch, converted->depth_pitch);
}

// CPU data of the game's size: into a texture of its size, scaled into the resized one
bool WriteResizedInPlace(ID3D11DeviceContext* context, ID3D11Resource* resource, const InPlaceState& state, UINT subresource, const D3D11_BOX* box, const void* data,
   UINT row_pitch, UINT depth_pitch)
{
   const uint32_t level = subresource % std::max(state.original_info.levels, 1u);
   const uint32_t game_w = box ? box->right - box->left : Upgrade::GetMipSize(state.original_info.width, level);
   const uint32_t game_h = box ? box->bottom - box->top : Upgrade::GetMipSize(state.original_info.height, level);
   std::optional<Converted> converted;
   if (state.info.format != state.original_info.format)
   {
      converted = ConvertUpload(state.original_info, state.info.format, subresource, box, data, row_pitch, depth_pitch);
      if (!converted)
         return false;
   }
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   const D3D11_TEXTURE2D_DESC upload_desc = {.Width = game_w,
      .Height = game_h,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = state.info.format,
      .SampleDesc = {1, 0},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE};
   const D3D11_SUBRESOURCE_DATA upload_data =
      converted ? D3D11_SUBRESOURCE_DATA{converted->bytes.data(), converted->row_pitch, converted->depth_pitch} : D3D11_SUBRESOURCE_DATA{data, row_pitch, depth_pitch};
   ComPtr<ID3D11Texture2D> upload;
   if (!device || FAILED(device->CreateTexture2D(&upload_desc, &upload_data, &upload)))
      return false;
   const Math::float2 scale = GetInPlaceScale(&state);
   const uint32_t dst_mip_w = Upgrade::GetMipSize(state.info.width, level);
   const uint32_t dst_mip_h = Upgrade::GetMipSize(state.info.height, level);
   const UINT x = box ? std::min(UINT(std::lround(double(box->left) * double(scale.x))), dst_mip_w > 0 ? dst_mip_w - 1u : 0u) : 0;
   const UINT y = box ? std::min(UINT(std::lround(double(box->top) * double(scale.y))), dst_mip_h > 0 ? dst_mip_h - 1u : 0u) : 0;
   const uint32_t max_target_w = dst_mip_w > x ? dst_mip_w - x : 1u;
   const uint32_t max_target_h = dst_mip_h > y ? dst_mip_h - y : 1u;
   const Math::uint2 target_size = {std::clamp(uint32_t(std::lround(double(game_w) * double(scale.x))), 1u, max_target_w),
      std::clamp(uint32_t(std::lround(double(game_h) * double(scale.y))), 1u, max_target_h)};
   return ConvertingCopySubresourceRegion(context, resource, RemapInPlaceSubresource(&state, subresource), x, y, 0, upload.Get(), 0, nullptr,
      {.while_off = true, .target_size = target_size});
}

bool WriteInPlace(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT row_pitch, UINT depth_pitch)
{
   const InPlaceState* state = FindInPlace(resource);
   if (!state || !data)
      return false;
   if (state->resized)
   {
      // The game's call would write its texels at the wrong size
      [[maybe_unused]] const bool written = WriteResizedInPlace(context, resource, *state, subresource, box, data, row_pitch, depth_pitch);
      ASSERT_ONCE_MSG(written, "CPU data written to a resized texture upgraded in place couldn't be scaled into it: the write was skipped");
      return true;
   }
   const std::optional<Converted> converted = ConvertUpload(state->original_info, state->info.format, subresource, box, data, row_pitch, depth_pitch);
   // The game's call would read its data as texels of the upgraded format, past its end
   if (!converted)
   {
      ASSERT_ONCE_MSG(false, "CPU data written to a texture upgraded in place can't be converted to its format: the write was skipped");
      return true;
   }
   context->UpdateSubresource(resource, subresource, box, converted->bytes.data(), converted->row_pitch, converted->depth_pitch);
   return true;
}
} // namespace

void PrepareCopyTargets(ID3D11DeviceContext* context, ID3D11Resource*& destination, ID3D11Resource*& source)
{
   if (!IsSwapping())
      return;
   Internal::TryPropagateDirectChain(context, destination, source);
   destination = GetEffectiveResource(destination);
   source = GetEffectiveResource(source);
}

bool RedirectCopy(ID3D11DeviceContext* context, ID3D11Resource* destination, ID3D11Resource* source)
{
   if (!IsSwapping())
      return RedirectInPlaceCopy(context, destination, source);
   Internal::TryPropagateDirectChain(context, destination, source);
   const CopyPlan<ID3D11Resource> plan = PlanCopy(destination, source, &FindMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return RedirectInPlaceCopy(context, destination, source);
   const bool same_size = SameDimensions2D(plan.destination, plan.source);
   if (plan.action == CopyAction::DirectMirrorCopy && same_size)
   {
      context->CopyResource(plan.destination, plan.source);
      return true;
   }
   if (ConvertingCopy(context, plan.destination, plan.source))
      return true;
   ASSERT_ONCE_MSG(false, "A copy between a mirror and a texture of another format couldn't be converted: the game's textures are copied, the mirror is stale");
   return false;
}

bool RedirectCopyRegion(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, UINT x, UINT y, UINT z, ID3D11Resource* source,
   UINT source_subresource, const D3D11_BOX* box, UINT copy_flags)
{
   if (!IsSwapping())
      return RedirectInPlaceCopyRegion(context, destination, destination_subresource, x, y, z, source, source_subresource, box);
   Internal::TryPropagateDirectChain(context, destination, source);
   const CopyPlan<ID3D11Resource> plan = PlanCopy(destination, source, &FindMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return RedirectInPlaceCopyRegion(context, destination, destination_subresource, x, y, z, source, source_subresource, box);
   const bool same_size = SameDimensions2D(plan.destination, plan.source);
   if (plan.action == CopyAction::DirectMirrorCopy && same_size)
   {
      D3D11_BOX scaled_box = {};
      const D3D11_BOX* use_box = box;
      const ResourceMirrorState* src_mirror = FindMirrorState<OwnedResource>(source);
      const ResourceMirrorState* dst_mirror = FindMirrorState<OwnedResource>(destination);
      if (src_mirror && src_mirror->is_scaled && box)
      {
         const Math::float2 scale = {
            float(src_mirror->info.width) / float(std::max(src_mirror->original_info.width, 1u)),
            float(src_mirror->info.height) / float(std::max(src_mirror->original_info.height, 1u)),
         };
         const Scaling::Box rescaled = Scaling::RescaleBox({box->left, box->top, box->front, box->right, box->bottom, box->back}, scale);
         scaled_box = {rescaled.left, rescaled.top, rescaled.front, rescaled.right, rescaled.bottom, rescaled.back};
         use_box = &scaled_box;
         Scaling::CountCopyBoxRescale();
      }
      if (dst_mirror && dst_mirror->is_scaled && (x != 0 || y != 0))
      {
         const Math::float2 scale = {
            float(dst_mirror->info.width) / float(std::max(dst_mirror->original_info.width, 1u)),
            float(dst_mirror->info.height) / float(std::max(dst_mirror->original_info.height, 1u)),
         };
         x = UINT(std::lround(double(x) * double(scale.x)));
         y = UINT(std::lround(double(y) * double(scale.y)));
      }
      ComPtr<ID3D11DeviceContext1> context1;
      if (copy_flags && SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1))))
         context1->CopySubresourceRegion1(plan.destination, destination_subresource, x, y, z, plan.source, source_subresource, use_box, copy_flags);
      else
         context->CopySubresourceRegion(plan.destination, destination_subresource, x, y, z, plan.source, source_subresource, use_box);
      return true;
   }
   if (ConvertingCopySubresourceRegion(context, plan.destination, destination_subresource, x, y, z, plan.source, source_subresource, box))
      return true;
   ASSERT_ONCE_MSG(false, "A region copy between a mirror and a texture of another format couldn't be converted: the game's textures are copied, the mirror is stale");
   return false;
}

bool RedirectResolve(ID3D11DeviceContext* context, ID3D11Resource* destination, UINT destination_subresource, ID3D11Resource* source, UINT source_subresource,
   DXGI_FORMAT format)
{
   if (!IsSwapping())
      return RedirectInPlaceResolve(context, destination, destination_subresource, source, source_subresource, format);
   Internal::TryPropagateDirectChain(context, destination, source);
   const CopyPlan<ID3D11Resource> plan = PlanCopy(destination, source, &FindMirror, &GetFormat);
   if (plan.action == CopyAction::Original)
      return RedirectInPlaceResolve(context, destination, destination_subresource, source, source_subresource, format);
   const DXGI_FORMAT destination_format = GetFormat(plan.destination);
   const DXGI_FORMAT source_format = GetFormat(plan.source);
   // ResolveSubresource requires source and destination to share the same typeless format family (stricter than CopyResource,
   // which allows cross-family copies of equal-bit-size formats). When a mirror crosses families, resolve into a temporary
   // single-sample texture of the source's family and blit-convert into the destination.
   if (GetTypeless(destination_format) != GetTypeless(source_format) || destination_format == DXGI_FORMAT_UNKNOWN)
   {
      const DXGI_FORMAT resolve_format = GetResolveFormat(format, GetFormat(source), source_format);
      if (ConvertingCopySubresourceRegion(context, plan.destination, destination_subresource, 0, 0, 0, plan.source, source_subresource, nullptr,
             {.resolve_format = resolve_format}))
         return true;
      unhandled_resolves++;
      ASSERT_ONCE_MSG(false, "Resolving between a mirror and a texture of another format failed to convert: the game's textures are resolved");
      return false;
   }
   const DXGI_FORMAT resolve_format = GetResolveFormat(format, GetFormat(source), destination_format);
   context->ResolveSubresource(plan.destination, destination_subresource, plan.source, source_subresource, resolve_format);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11RenderTargetView* view, const FLOAT color[4])
{
   if (!IsSwapping())
      return false;
   ID3D11RenderTargetView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearRenderTargetView(mirror, color);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil)
{
   if (!IsSwapping())
      return false;
   ID3D11DepthStencilView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearDepthStencilView(mirror, flags, depth, stencil);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const UINT values[4])
{
   if (!IsSwapping())
      return false;
   ID3D11UnorderedAccessView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearUnorderedAccessViewUint(mirror, values);
   return true;
}

bool RedirectClear(ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view, const FLOAT values[4])
{
   if (!IsSwapping())
      return false;
   ID3D11UnorderedAccessView* mirror = FindMirrorView(view);
   if (!mirror)
      return false;
   context->ClearUnorderedAccessViewFloat(mirror, values);
   return true;
}

bool RedirectClearView(ID3D11DeviceContext* context, ID3D11View* view, const FLOAT color[4], const D3D11_RECT* rects, UINT rect_count)
{
   // The game's rects are in its size
   if (rects && rect_count > 0 && view && HasAnyResizedInPlace())
   {
      ComPtr<ID3D11Resource> resource;
      view->GetResource(&resource);
      ComPtr<ID3D11DeviceContext1> context1;
      if (const InPlaceState* state = FindInPlace(resource.Get()); state && state->resized && SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1))))
      {
         std::vector<D3D11_RECT> scaled(rect_count);
         for (UINT i = 0; i < rect_count; i++)
         {
            const Scaling::Rect rect = Scaling::RescaleRect({rects[i].left, rects[i].top, rects[i].right, rects[i].bottom}, GetInPlaceScale(state));
            scaled[i] = {rect.left, rect.top, rect.right, rect.bottom};
         }
         context1->ClearView(view, color, scaled.data(), rect_count);
         return true;
      }
   }
   if (!IsSwapping())
      return false;
   ID3D11View* mirror = FindMirrorView(view);
   ComPtr<ID3D11DeviceContext1> context1;
   if (!mirror || FAILED(context->QueryInterface(IID_PPV_ARGS(&context1))))
      return false;
   std::vector<D3D11_RECT> scaled_rects;
   const D3D11_RECT* use_rects = rects;
   if (rects && rect_count > 0)
   {
      ID3D11Resource* res = GetViewResource(view);
      if (const ResourceMirrorState* state = FindMirrorState<OwnedResource>(res); state && state->is_scaled)
      {
         const Math::float2 scale = {
            float(state->info.width) / float(std::max(state->original_info.width, 1u)),
            float(state->info.height) / float(std::max(state->original_info.height, 1u)),
         };
         scaled_rects.reserve(rect_count);
         for (UINT i = 0; i < rect_count; i++)
         {
            const Scaling::Rect r = Scaling::RescaleRect({rects[i].left, rects[i].top, rects[i].right, rects[i].bottom}, scale);
            scaled_rects.push_back({r.left, r.top, r.right, r.bottom});
         }
         use_rects = scaled_rects.data();
         Scaling::CountClearRectRescale();
      }
   }
   context1->ClearView(mirror, color, use_rects, rect_count);
   return true;
}

bool WriteSubresource(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box, const void* data, UINT row_pitch, UINT depth_pitch)
{
   if (WriteInPlace(context, resource, subresource, box, data, row_pitch, depth_pitch))
      return true;
   // The game's call still writes the original
   WriteToMirror(context, resource, subresource, box, data, row_pitch, depth_pitch);
   return false;
}

void TrackMappedMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, const D3D11_MAPPED_SUBRESOURCE& mapped)
{
   // Only GPU-only textures are upgraded: never expected
   ASSERT_ONCE_MSG(!FindInPlace(resource), "Game mapped a texture upgraded in place: its CPU data is in the upgraded format, not the game's");
   if (!FindMirrorState<OwnedResource>(resource))
      return;
   if (type == D3D11_MAP_READ || type == D3D11_MAP_READ_WRITE)
      ASSERT_ONCE_MSG(false, "Game mapped a mirrored texture for CPU read: mirror contents are not read back");
   if (type == D3D11_MAP_READ || !mapped.pData)
      return;
   if (MappedMirrors* state = Hooks::Lifetime::GetOrCreateStateCached<MappedMirrors>(context))
   {
      for (MappedMirrorEntry& entry : state->entries)
      {
         if (entry.resource == resource && entry.subresource == subresource)
         {
            entry.mapped = mapped;
            return;
         }
      }
      state->entries.push_back({resource, subresource, mapped});
   }
}

// Must run while the game's Map() pointer (mapped.pData) is still valid, before the native Unmap() invalidates it.
void CommitMappedMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   MappedMirrors* state = Hooks::Lifetime::GetState<MappedMirrors>(context);
   if (!state || state->entries.empty())
      return;
   for (size_t i = 0; i < state->entries.size(); i++)
   {
      if (state->entries[i].resource == resource && state->entries[i].subresource == subresource)
      {
         const D3D11_MAPPED_SUBRESOURCE mapped = state->entries[i].mapped;
         state->entries.erase(state->entries.begin() + ptrdiff_t(i));
         WriteToMirror(context, resource, subresource, nullptr, mapped.pData, mapped.RowPitch, mapped.DepthPitch);
         return;
      }
   }
}

// Discards a tracked mapping without reading mapped.pData (used when a game event took ownership of Unmap).
void DiscardMappedMirror(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
   MappedMirrors* state = Hooks::Lifetime::GetState<MappedMirrors>(context);
   if (!state || state->entries.empty())
      return;
   for (size_t i = 0; i < state->entries.size(); i++)
   {
      if (state->entries[i].resource == resource && state->entries[i].subresource == subresource)
      {
         state->entries.erase(state->entries.begin() + ptrdiff_t(i));
         return;
      }
   }
}

uint32_t GetUnhandledResolves()
{
   return unhandled_resolves.load();
}
} // namespace D3D11
} // namespace Mirrors
} // namespace Resources
} // namespace Luma
