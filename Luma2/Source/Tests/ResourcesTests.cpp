#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "Frame/Frame.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/Mirrors.hpp"
#include "Resources/Samplers.hpp"
#include "Resources/Upgrade.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"

using Microsoft::WRL::ComPtr;
using namespace Luma;
using Resources::Upgrade::Dimension;
using Resources::Upgrade::SizeFilters;
using Resources::Upgrade::TextureDesc;

namespace {
// FF7R's 1.0 configuration (main.cpp 2081 to 2106)
Resources::Upgrade::Rules GetFF7RRules()
{
   return {
      .formats = {DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R11G11B10_FLOAT},
      .size_filters = SizeFilters::SwapchainResolution | SizeFilters::SwapchainAspectRatio | SizeFilters::CustomAspectRatio,
      .custom_aspect_ratios = {16.f / 9.f},
      .lut = {.size = 32, .dimensions = Resources::Upgrade::LutDimensions::_3D},
   };
}

struct RulesFixture
{
   RulesFixture() { Resources::Upgrade::SetRules(GetFF7RRules()); }

   ~RulesFixture() { Resources::Upgrade::SetRules({}); }
};

TextureDesc Target(uint32_t width, uint32_t height, DXGI_FORMAT format)
{
   return {.width = width, .height = height, .format = format, .render_target = true};
}

// The output is a 21:9 window, so 16:9 targets only match through the custom aspect ratio
constexpr Resources::Upgrade::FrameState STATE = {.render = {3440, 1440}, .output = {3440, 1440}, .display = {3440, 1440}};
} // namespace

TEST_CASE("Upgrade: FF7R's rules match the textures 1.0 upgraded")
{
   RulesFixture rules;
   using Resources::Upgrade::Match;
   // The output's size and aspect ratio, and 16:9 at any size within a pixel
   CHECK(Match(Target(3440, 1440, DXGI_FORMAT_R11G11B10_FLOAT), STATE, false)->format == DXGI_FORMAT_R16G16B16A16_FLOAT);
   CHECK(Match(Target(1720, 720, DXGI_FORMAT_R10G10B10A2_UNORM), STATE, false));
   CHECK(Match(Target(1920, 1080, DXGI_FORMAT_R10G10B10A2_TYPELESS), STATE, false));
   CHECK(Match(Target(1921, 1080, DXGI_FORMAT_R11G11B10_FLOAT), STATE, false));
   CHECK_FALSE(Match(Target(1924, 1080, DXGI_FORMAT_R11G11B10_FLOAT), STATE, false));
   CHECK_FALSE(Match(Target(512, 512, DXGI_FORMAT_R11G11B10_FLOAT), STATE, false));
   // Formats not in the list, and what the GPU doesn't write
   CHECK_FALSE(Match(Target(3440, 1440, DXGI_FORMAT_R8G8B8A8_UNORM), STATE, false));
   TextureDesc read_only = Target(3440, 1440, DXGI_FORMAT_R11G11B10_FLOAT);
   read_only.render_target = false;
   CHECK_FALSE(Match(read_only, STATE, false));
   TextureDesc dynamic = Target(3440, 1440, DXGI_FORMAT_R11G11B10_FLOAT);
   dynamic.gpu_only = false;
   CHECK_FALSE(Match(dynamic, STATE, false));
   // Arrays aren't 2D targets
   TextureDesc array = Target(3440, 1440, DXGI_FORMAT_R11G11B10_FLOAT);
   array.depth_or_layers = 2;
   CHECK_FALSE(Match(array, STATE, false));
   // The 3D LUT, written by a compute shader, 32 on each side, one mip
   TextureDesc lut = {.dimension = Dimension::Texture3D, .width = 32, .height = 32, .depth_or_layers = 32, .format = DXGI_FORMAT_R10G10B10A2_UNORM, .unordered_access = true};
   CHECK(Match(lut, STATE, false)->format == DXGI_FORMAT_R16G16B16A16_FLOAT);
   lut.levels = 2;
   CHECK_FALSE(Match(lut, STATE, false));
   lut.levels = 1;
   lut.width = 16;
   CHECK_FALSE(Match(lut, STATE, false));
   // No rules, no mirrors
   Resources::Upgrade::SetRules({});
   CHECK_FALSE(Resources::Upgrade::IsNeeded());
   CHECK_FALSE(Match(Target(3440, 1440, DXGI_FORMAT_R11G11B10_FLOAT), STATE, false));
}

TEST_CASE("Upgrade: the size filters and the mirror's view formats as 1.0's")
{
   Resources::Upgrade::Rules rules = GetFF7RRules();
   rules.size_filters = SizeFilters::Mips | SizeFilters::No1Px;
   rules.depth_formats = {DXGI_FORMAT_R24G8_TYPELESS};
   Resources::Upgrade::SetRules(rules);
   using Resources::Upgrade::Match;
   const Resources::Upgrade::FrameState state = {.render = {1024, 512}, .output = {1024, 512}};
   CHECK(Match(Target(256, 128, DXGI_FORMAT_R11G11B10_FLOAT), state, false));
   CHECK_FALSE(Match(Target(256, 100, DXGI_FORMAT_R11G11B10_FLOAT), state, false));
   CHECK_FALSE(Match(Target(1, 1, DXGI_FORMAT_R11G11B10_FLOAT), state, false));
   // Depth keeps its stencil, typeless so it can be read
   TextureDesc depth = {.width = 512, .height = 256, .format = DXGI_FORMAT_R24G8_TYPELESS, .depth_stencil = true};
   CHECK(Match(depth, state, false)->format == DXGI_FORMAT_R32G8X24_TYPELESS);
   Resources::Upgrade::SetRules({});

   using Resources::Upgrade::GetBestViewFormat;
   using Resources::Upgrade::ViewKind;
   CHECK(GetBestViewFormat(DXGI_FORMAT_R10G10B10A2_UNORM, ViewKind::ShaderResource, DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R16G16B16A16_FLOAT) ==
      DXGI_FORMAT_R16G16B16A16_FLOAT);
   CHECK(GetBestViewFormat(DXGI_FORMAT_UNKNOWN, ViewKind::RenderTarget, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT) == DXGI_FORMAT_UNKNOWN);
   CHECK(GetBestViewFormat(DXGI_FORMAT_D24_UNORM_S8_UINT, ViewKind::DepthStencil, DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R32G8X24_TYPELESS) ==
      DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
   CHECK(GetBestViewFormat(DXGI_FORMAT_X24_TYPELESS_G8_UINT, ViewKind::ShaderResource, DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R32G8X24_TYPELESS) ==
      DXGI_FORMAT_X32_TYPELESS_G8X24_UINT);
   CHECK(GetBestViewFormat(DXGI_FORMAT_R24_UNORM_X8_TYPELESS, ViewKind::ShaderResource, DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R32G8X24_TYPELESS) ==
      DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
}

TEST_CASE("DXGI formats: copy compatibility and CPU data converted to RGBA16F")
{
   using namespace Utils::Format::DXGI;
   CHECK(AreCopyCompatible(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_TYPELESS));
   CHECK(AreCopyCompatible(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM));
   CHECK_FALSE(AreCopyCompatible(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT));
   // 1.0 took two formats without a typeless family as compatible
   CHECK_FALSE(AreCopyCompatible(DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R9G9B9E5_SHAREDEXP));
   CHECK(AreCopyCompatible(DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT));

   // 1.0 in each channel: exponent 15 with fp16's bias, no mantissa
   const uint32_t one_r11g11b10 = 0x3C0u | (0x3C0u << 11) | (0x1E0u << 22);
   // 2 texels of 1.0 and then half of the green, in rows of 3 texels (a padded pitch)
   const std::array<uint32_t, 6> rows = {one_r11g11b10, (0x380u << 11), 0xDEADBEEF, 0, 0, 0xDEADBEEF};
   const std::optional<Converted> converted = ConvertToRgba16Float(rows.data(), 12, 24, DXGI_FORMAT_R11G11B10_FLOAT, 2, 2, 1);
   REQUIRE(converted);
   CHECK(converted->row_pitch == 16);
   std::array<uint16_t, 16> halves = {};
   REQUIRE(converted->bytes.size() == sizeof(halves));
   std::memcpy(halves.data(), converted->bytes.data(), sizeof(halves));
   CHECK(halves[0] == 0x3C00);
   CHECK(halves[1] == 0x3C00);
   CHECK(halves[2] == 0x3C00);
   CHECK(halves[3] == 0x3C00);
   CHECK(halves[4] == 0);
   CHECK(halves[5] == 0x3800); // 0.5
   CHECK(halves[8] == 0);

   const uint32_t rgb10a2 = 1023u | (0u << 10) | (1023u << 20) | (3u << 30);
   const std::optional<Converted> unorm = ConvertToRgba16Float(&rgb10a2, 4, 4, DXGI_FORMAT_R10G10B10A2_UNORM, 1, 1, 1);
   REQUIRE(unorm);
   std::memcpy(halves.data(), unorm->bytes.data(), 8);
   CHECK(halves[0] == 0x3C00);
   CHECK(halves[1] == 0);
   CHECK(halves[2] == 0x3C00);
   CHECK(halves[3] == 0x3C00);

   const std::array<uint8_t, 4> bgra = {0, 0, 255, 255};
   const std::optional<Converted> swizzled = ConvertToRgba16Float(bgra.data(), 4, 4, DXGI_FORMAT_B8G8R8A8_UNORM, 1, 1, 1);
   REQUIRE(swizzled);
   std::memcpy(halves.data(), swizzled->bytes.data(), 8);
   CHECK(halves[0] == 0x3C00);
   CHECK(halves[2] == 0);
   CHECK_FALSE(ConvertToRgba16Float(bgra.data(), 4, 4, DXGI_FORMAT_R32_FLOAT, 1, 1, 1));
}

TEST_CASE("Samplers: 1.0's mirror desc for anisotropic samplers")
{
   using Resources::Samplers::Desc;
   using Resources::Samplers::GetMirrorDesc;
   constexpr uint32_t ANISOTROPIC = 0x55;
   constexpr uint32_t COMPARISON_ANISOTROPIC = 0xd5;
   constexpr uint32_t LINEAR = 0x15;
   const std::optional<Desc> sharper = GetMirrorDesc({.filter = ANISOTROPIC, .mip_lod_bias = 0.f, .max_anisotropy = 4, .min_lod = 0.f}, -1.f);
   REQUIRE(sharper);
   CHECK(sharper->max_anisotropy == 16);
   CHECK(sharper->mip_lod_bias == -1.f);
   CHECK(sharper->min_lod == 0.f);
   // A positive minimum LOD is lowered by the sharper bias
   CHECK(GetMirrorDesc({.filter = ANISOTROPIC, .max_anisotropy = 16, .min_lod = 2.f}, -1.f)->min_lod == 1.f);
   CHECK(GetMirrorDesc({.filter = COMPARISON_ANISOTROPIC, .max_anisotropy = 2}, 0.f)->max_anisotropy == 16);
   // Not anisotropic: untouched (1.0's Publishing branch); nothing to change: none, D3D11 would give the game's own sampler
   CHECK_FALSE(GetMirrorDesc({.filter = LINEAR}, -1.f));
   CHECK_FALSE(GetMirrorDesc({.filter = ANISOTROPIC, .max_anisotropy = 16}, 0.f));
   // Out of the API's range: clamped
   CHECK(GetMirrorDesc({.filter = ANISOTROPIC, .mip_lod_bias = -15.f}, -4.f)->mip_lod_bias == -16.f);
}

TEST_CASE("Mirrors D3D11: a matching texture gets a mirror, its views mirrored ones, CPU writes reach it, and both go with the texture")
{
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
   Frame::CreateDeviceState(device.Get());
   Frame::SetOutputResolution(device.Get(), {64, 36});
   RulesFixture rules;
   const Resources::Mirrors::Stats before = Resources::Mirrors::GetStats();

   const D3D11_TEXTURE2D_DESC1 desc = {.Width = 32,
      .Height = 18,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = DXGI_FORMAT_R11G11B10_FLOAT,
      .SampleDesc = {1, 0},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE};
   ComPtr<ID3D11Texture2D> texture;
   REQUIRE(SUCCEEDED(device->CreateTexture2D(reinterpret_cast<const D3D11_TEXTURE2D_DESC*>(&desc), nullptr, &texture)));
   Resources::Mirrors::D3D11::CreateMirror(device.Get(), desc, nullptr, texture.Get());
   ID3D11Resource* mirror = Resources::Mirrors::D3D11::GetMirror(texture.Get());
   REQUIRE(mirror);
   CHECK(Resources::Mirrors::D3D11::GetOriginal(mirror) == texture.Get());
   D3D11_TEXTURE2D_DESC mirror_desc;
   static_cast<ID3D11Texture2D*>(mirror)->GetDesc(&mirror_desc);
   CHECK(mirror_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
   CHECK(mirror_desc.Width == 32);
   CHECK(Resources::Mirrors::GetStats().mirrors == before.mirrors + 1);

   // A view of the game's texture gets the same view of the mirror, which a bind swaps in
   ComPtr<ID3D11RenderTargetView> view;
   const D3D11_RENDER_TARGET_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_R11G11B10_FLOAT, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D};
   REQUIRE(SUCCEEDED(device->CreateRenderTargetView(texture.Get(), &view_desc, &view)));
   D3D11_RENDER_TARGET_VIEW_DESC1 view_desc1 = {};
   std::memcpy(&view_desc1, &view_desc, sizeof(view_desc));
   Resources::Mirrors::D3D11::CreateMirrorView(device.Get(), texture.Get(), &view_desc1, view.Get());
   std::array<ID3D11RenderTargetView*, 2> bound = {view.Get(), nullptr};
   Resources::Mirrors::D3D11::SwapViews(bound.data(), UINT(bound.size()));
   REQUIRE(bound[0] != view.Get());
   CHECK(bound[1] == nullptr);
   ComPtr<ID3D11Resource> bound_resource;
   bound[0]->GetResource(&bound_resource);
   CHECK(bound_resource.Get() == mirror);

   // CPU data reaches the mirror in its format
   const uint32_t one = 0x3C0u | (0x3C0u << 11) | (0x1E0u << 22);
   const std::vector<uint32_t> data(32 * 18, one);
   Resources::Mirrors::D3D11::WriteToMirror(context.Get(), texture.Get(), 0, nullptr, data.data(), 32 * 4, 32 * 18 * 4);
   D3D11_TEXTURE2D_DESC staging_desc = mirror_desc;
   staging_desc.Usage = D3D11_USAGE_STAGING;
   staging_desc.BindFlags = 0;
   staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
   staging_desc.MiscFlags = 0;
   ComPtr<ID3D11Texture2D> staging;
   REQUIRE(SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, &staging)));
   context->CopyResource(staging.Get(), mirror);
   D3D11_MAPPED_SUBRESOURCE mapped = {};
   REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
   uint16_t texel[4];
   std::memcpy(texel, static_cast<const uint8_t*>(mapped.pData) + mapped.RowPitch * 17 + 31 * 8, sizeof(texel));
   context->Unmap(staging.Get(), 0);
   CHECK(texel[0] == 0x3C00);
   CHECK(texel[2] == 0x3C00);
   CHECK(texel[3] == 0x3C00);

   // A texture the rules don't match has none
   D3D11_TEXTURE2D_DESC1 other = desc;
   other.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   ComPtr<ID3D11Texture2D> plain;
   REQUIRE(SUCCEEDED(device->CreateTexture2D(reinterpret_cast<const D3D11_TEXTURE2D_DESC*>(&other), nullptr, &plain)));
   Resources::Mirrors::D3D11::CreateMirror(device.Get(), other, nullptr, plain.Get());
   CHECK(Resources::Mirrors::D3D11::GetMirror(plain.Get()) == nullptr);

   // The mirror and its view go with the game's objects
   bound = {};
   bound_resource.Reset();
   view.Reset();
   texture.Reset();
   CHECK(Resources::Mirrors::GetStats().mirrors == before.mirrors);
   CHECK(Resources::Mirrors::GetStats().views == before.views);
   CHECK(Resources::Mirrors::GetStats().bytes == before.bytes);
}
