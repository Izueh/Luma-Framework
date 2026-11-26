#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <span>
#include <cstring>
#include <thread>
#include <vector>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "Draw/D3D11/D3D11Draw.hpp"
#include "Draw/Draw.hpp"
#include "Frame/FrameState.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D11/D3D11Samplers.hpp"
#include "Resources/D3D12/D3D12HeapTail.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/D3D12/D3D12ResourceDatabase.hpp"
#include "Shaders/D3D12/D3D12Database.hpp"
#include "Shaders/D3D12/D3D12RootSignatures.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Trace/D3D11/D3D11Capture.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"

using Microsoft::WRL::ComPtr;
using namespace Luma;
using Resources::Upgrade::Dimension;
using Resources::Upgrade::SizeFilters;
using Resources::Upgrade::TextureInfo;

namespace {
// FF7R's upgrade rules
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

TextureInfo Target(uint32_t width, uint32_t height, DXGI_FORMAT format)
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
   TextureInfo read_only = Target(3440, 1440, DXGI_FORMAT_R11G11B10_FLOAT);
   read_only.render_target = false;
   CHECK_FALSE(Match(read_only, STATE, false));
   TextureInfo dynamic = Target(3440, 1440, DXGI_FORMAT_R11G11B10_FLOAT);
   dynamic.gpu_only = false;
   CHECK_FALSE(Match(dynamic, STATE, false));
   // Arrays aren't 2D targets
   TextureInfo array = Target(3440, 1440, DXGI_FORMAT_R11G11B10_FLOAT);
   array.depth_or_layers = 2;
   CHECK_FALSE(Match(array, STATE, false));
   // The 3D LUT, written by a compute shader, 32 on each side, one mip
   TextureInfo lut = {.dimension = Dimension::Texture3D, .width = 32, .height = 32, .depth_or_layers = 32, .format = DXGI_FORMAT_R10G10B10A2_UNORM, .unordered_access = true};
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
   TextureInfo depth = {.width = 512, .height = 256, .format = DXGI_FORMAT_R24G8_TYPELESS, .depth_stencil = true};
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
   // A typeless mirror takes the view's encoding in the mirror's family
   CHECK(GetBestViewFormat(DXGI_FORMAT_R8G8B8A8_UNORM, ViewKind::ShaderResource, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R16G16B16A16_TYPELESS) ==
      DXGI_FORMAT_R16G16B16A16_UNORM);
   CHECK(GetBestViewFormat(DXGI_FORMAT_R8G8B8A8_SNORM, ViewKind::ShaderResource, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R16G16B16A16_TYPELESS) ==
      DXGI_FORMAT_R16G16B16A16_SNORM);
   CHECK(GetBestViewFormat(DXGI_FORMAT_R8G8B8A8_UINT, ViewKind::UnorderedAccess, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R16G16B16A16_TYPELESS) ==
      DXGI_FORMAT_R16G16B16A16_UINT);
   CHECK(GetBestViewFormat(DXGI_FORMAT_R8G8B8A8_SINT, ViewKind::UnorderedAccess, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R16G16B16A16_TYPELESS) ==
      DXGI_FORMAT_R16G16B16A16_SINT);
   CHECK(GetBestViewFormat(DXGI_FORMAT_R11G11B10_FLOAT, ViewKind::ShaderResource, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R16G16B16A16_TYPELESS) ==
      DXGI_FORMAT_R16G16B16A16_FLOAT);
   // An encoding the mirror's family doesn't have (sRGB on 16-bit) falls back to the mirror's default typed format
   CHECK(GetBestViewFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, ViewKind::ShaderResource, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R16G16B16A16_TYPELESS) ==
      DXGI_FORMAT_R16G16B16A16_FLOAT);
}

TEST_CASE("DXGI formats: typeless families, encodings and remapping")
{
   using namespace Utils::Format::DXGI;
   CHECK(GetTypeless(DXGI_FORMAT_BC1_UNORM_SRGB) == DXGI_FORMAT_BC1_TYPELESS);
   CHECK(GetTypeless(DXGI_FORMAT_BC2_UNORM) == DXGI_FORMAT_BC2_TYPELESS);
   CHECK(GetTypeless(DXGI_FORMAT_BC3_UNORM_SRGB) == DXGI_FORMAT_BC3_TYPELESS);
   CHECK(GetTypeless(DXGI_FORMAT_BC4_SNORM) == DXGI_FORMAT_BC4_TYPELESS);
   CHECK(GetTypeless(DXGI_FORMAT_BC5_UNORM) == DXGI_FORMAT_BC5_TYPELESS);
   CHECK(GetTypeless(DXGI_FORMAT_BC6H_SF16) == DXGI_FORMAT_BC6H_TYPELESS);
   CHECK(GetTypeless(DXGI_FORMAT_BC7_UNORM_SRGB) == DXGI_FORMAT_BC7_TYPELESS);
   CHECK(AreCopyCompatible(DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC1_UNORM_SRGB));
   CHECK(AreCopyCompatible(DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB));
   CHECK_FALSE(AreCopyCompatible(DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC7_UNORM));

   CHECK(GetEncoding(DXGI_FORMAT_R16G16B16A16_TYPELESS) == Encoding::Typeless);
   CHECK(GetEncoding(DXGI_FORMAT_R10G10B10A2_UNORM) == Encoding::Unorm);
   CHECK(GetEncoding(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) == Encoding::UnormSrgb);
   CHECK(GetEncoding(DXGI_FORMAT_R8G8_SNORM) == Encoding::Snorm);
   CHECK(GetEncoding(DXGI_FORMAT_R16_UINT) == Encoding::Uint);
   CHECK(GetEncoding(DXGI_FORMAT_R32_SINT) == Encoding::Sint);
   CHECK(GetEncoding(DXGI_FORMAT_R11G11B10_FLOAT) == Encoding::UFloat);
   CHECK(GetEncoding(DXGI_FORMAT_R16G16B16A16_FLOAT) == Encoding::SFloat);

   CHECK(GetRemapped(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_TYPELESS) == DXGI_FORMAT_R16G16B16A16_UNORM);
   CHECK(GetRemapped(DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R16G16B16A16_FLOAT) == DXGI_FORMAT_R16G16B16A16_SNORM);
   CHECK(GetRemapped(DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R32_TYPELESS) == DXGI_FORMAT_R32_UINT);
   CHECK(GetRemapped(DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R32_TYPELESS) == DXGI_FORMAT_R32_SINT);
   CHECK(GetRemapped(DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC7_TYPELESS) == DXGI_FORMAT_BC7_UNORM_SRGB);
   CHECK(GetRemapped(DXGI_FORMAT_BC6H_UF16, DXGI_FORMAT_R16G16B16A16_TYPELESS) == DXGI_FORMAT_R16G16B16A16_FLOAT);
   CHECK(GetTyped(DXGI_FORMAT_R32_TYPELESS, Encoding::Unorm, true) == DXGI_FORMAT_UNKNOWN);
}

TEST_CASE("Draw D3D11: EXTENDED_GRAPHICS saves and restores vertex buffers, index buffer and geometry shader")
{
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));

   const D3D11_BUFFER_DESC vb_desc = {.ByteWidth = 64, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
   const D3D11_BUFFER_DESC ib_desc = {.ByteWidth = 64, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_INDEX_BUFFER};
   ComPtr<ID3D11Buffer> vb0;
   ComPtr<ID3D11Buffer> vb1;
   ComPtr<ID3D11Buffer> ib;
   REQUIRE(SUCCEEDED(device->CreateBuffer(&vb_desc, nullptr, &vb0)));
   REQUIRE(SUCCEEDED(device->CreateBuffer(&vb_desc, nullptr, &vb1)));
   REQUIRE(SUCCEEDED(device->CreateBuffer(&ib_desc, nullptr, &ib)));

   ID3D11Buffer* vbs[2] = {vb0.Get(), vb1.Get()};
   const UINT strides[2] = {16, 32};
   const UINT offsets[2] = {4, 8};
   context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
   context->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R16_UINT, 12);

   Draw::D3D11::StateStack stack;
   stack.mask = Draw::EXTENDED_GRAPHICS;
   Draw::D3D11::Save(context.Get(), stack);

   ID3D11Buffer* null_vbs[2] = {nullptr, nullptr};
   const UINT zero_strides[2] = {0, 0};
   const UINT zero_offsets[2] = {0, 0};
   context->IASetVertexBuffers(0, 2, null_vbs, zero_strides, zero_offsets);
   context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);

   Draw::D3D11::Restore(context.Get(), stack);

   ComPtr<ID3D11Buffer> restored_vb0;
   ComPtr<ID3D11Buffer> restored_vb1;
   ID3D11Buffer* restored_vbs[2] = {};
   UINT restored_strides[2] = {};
   UINT restored_offsets[2] = {};
   context->IAGetVertexBuffers(0, 2, restored_vbs, restored_strides, restored_offsets);
   restored_vb0.Attach(restored_vbs[0]);
   restored_vb1.Attach(restored_vbs[1]);
   CHECK(restored_vb0.Get() == vb0.Get());
   CHECK(restored_vb1.Get() == vb1.Get());
   CHECK(restored_strides[0] == 16);
   CHECK(restored_strides[1] == 32);
   CHECK(restored_offsets[0] == 4);
   CHECK(restored_offsets[1] == 8);

   ComPtr<ID3D11Buffer> restored_ib;
   DXGI_FORMAT restored_format = DXGI_FORMAT_UNKNOWN;
   UINT restored_ib_offset = 0;
   context->IAGetIndexBuffer(&restored_ib, &restored_format, &restored_ib_offset);
   CHECK(restored_ib.Get() == ib.Get());
   CHECK(restored_format == DXGI_FORMAT_R16_UINT);
   CHECK(restored_ib_offset == 12);
}

TEST_CASE("DXGI formats: copy compatibility and CPU data converted to RGBA16F")
{
   using namespace Utils::Format::DXGI;
   CHECK(AreCopyCompatible(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_TYPELESS));
   CHECK(AreCopyCompatible(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM));
   CHECK_FALSE(AreCopyCompatible(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT));
   // Two formats without a typeless family are not compatible
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

TEST_CASE("Samplers: mirror desc for anisotropic samplers")
{
   using Resources::Samplers::GetMirrorDesc;
   const std::optional<D3D11_SAMPLER_DESC> sharper = GetMirrorDesc(
      D3D11_SAMPLER_DESC{.Filter = D3D11_FILTER_ANISOTROPIC, .MipLODBias = 0.f, .MaxAnisotropy = 4, .MinLOD = 0.f}, -1.f);
   REQUIRE(sharper);
   CHECK(sharper->MaxAnisotropy == 16);
   CHECK(sharper->MipLODBias == -1.f);
   CHECK(sharper->MinLOD == 0.f);
   CHECK(GetMirrorDesc(D3D11_SAMPLER_DESC{.Filter = D3D11_FILTER_ANISOTROPIC, .MaxAnisotropy = 16, .MinLOD = 2.f}, -1.f)->MinLOD == 1.f);
   CHECK(GetMirrorDesc(D3D12_SAMPLER_DESC{.Filter = D3D12_FILTER_COMPARISON_ANISOTROPIC, .MaxAnisotropy = 2}, 0.f)->MaxAnisotropy == 16);
   CHECK_FALSE(GetMirrorDesc(D3D11_SAMPLER_DESC{.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR}, -1.f));
   CHECK_FALSE(GetMirrorDesc(D3D11_SAMPLER_DESC{.Filter = D3D11_FILTER_ANISOTROPIC, .MaxAnisotropy = 16}, 0.f));
   CHECK(GetMirrorDesc(D3D12_SAMPLER_DESC{.Filter = D3D12_FILTER_ANISOTROPIC, .MipLODBias = -15.f}, -4.f)->MipLODBias == -16.f);
}

TEST_CASE("Samplers D3D11: a sampler's mirror is made at its creation, and for a new bias at the present, never at a bind")
{
   ComPtr<ID3D11Device> device;
   REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr)));

   struct Bias
   {
      ID3D11Device* device;
      ~Bias() { Resources::Samplers::SetLodBiasOffset(device, 0.f); }
   } bias{device.Get()};

   Resources::Samplers::SetLodBiasOffset(device.Get(), -1.f);
   const D3D11_SAMPLER_DESC desc = {.Filter = D3D11_FILTER_ANISOTROPIC, .AddressU = D3D11_TEXTURE_ADDRESS_WRAP, .AddressV = D3D11_TEXTURE_ADDRESS_WRAP, .AddressW = D3D11_TEXTURE_ADDRESS_WRAP, .MaxAnisotropy = 4, .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
   ComPtr<ID3D11SamplerState> sampler;
   REQUIRE(SUCCEEDED(device->CreateSamplerState(&desc, &sampler)));
   Resources::Samplers::D3D11::TrackSampler(device.Get(), desc, sampler.Get());
   ID3D11SamplerState* bound = sampler.Get();
   Resources::Samplers::D3D11::SwapSamplers(&bound, 1);
   REQUIRE(bound != sampler.Get());
   D3D11_SAMPLER_DESC mirror = {};
   bound->GetDesc(&mirror);
   CHECK(mirror.MaxAnisotropy == 16);
   CHECK(mirror.MipLODBias == -1.f);

   // A new bias: the bind keeps the mirror it had until the present makes the new one
   Resources::Samplers::SetLodBiasOffset(device.Get(), -2.f);
   ID3D11SamplerState* before_present = sampler.Get();
   Resources::Samplers::D3D11::SwapSamplers(&before_present, 1);
   CHECK(before_present == bound);
   Resources::Samplers::D3D11::RefreshMirrors(device.Get());
   ID3D11SamplerState* after_present = sampler.Get();
   Resources::Samplers::D3D11::SwapSamplers(&after_present, 1);
   after_present->GetDesc(&mirror);
   CHECK(mirror.MipLODBias == -2.f);
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
   ID3D11Resource* mirror = Resources::Mirrors::D3D11::FindMirror(texture.Get());
   REQUIRE(mirror);
   CHECK(Resources::Mirrors::D3D11::FindOriginal(mirror) == texture.Get());
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
#if DEVELOPMENT
   // Trace describes a mirror as the game's texture (it recursed into the mirror until the stack ran out)
   Trace::Capture::D3D11::AddWrite(context.Get(), "Mirror", mirror);
#endif

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
   CHECK(Resources::Mirrors::D3D11::FindMirror(plain.Get()) == nullptr);

   // The mirror and its view go with the game's objects
   bound = {};
   bound_resource.Reset();
   view.Reset();
   texture.Reset();
   CHECK(Resources::Mirrors::GetStats().mirrors == before.mirrors);
   CHECK(Resources::Mirrors::GetStats().views == before.views);
   CHECK(Resources::Mirrors::GetStats().bytes == before.bytes);
}

TEST_CASE("Heap tail D3D12: shader-visible heaps get Luma's descriptors after the game's, written once per key")
{
   using namespace Resources::HeapTail::D3D12;
   ComPtr<IDXGIFactory4> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   ComPtr<IDXGIAdapter> adapter;
   REQUIRE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
   ComPtr<ID3D12Device> device;
   REQUIRE(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));

   const D3D12_DESCRIPTOR_HEAP_DESC game_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
   const std::optional<D3D12_DESCRIPTOR_HEAP_DESC> extended = Extend(game_desc);
   REQUIRE(extended);
   CHECK(extended->NumDescriptors == 16 + TAIL_SIZE);
   CHECK(TAIL_SIZE == FIXED_SIZE + RING_SIZE);
   // CPU-only heaps and samplers don't get one. A heap at the tier 2 limit is tried with its tail: the device answers.
   CHECK_FALSE(Extend({D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_NONE}));
   CHECK_FALSE(Extend({D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE}));
   // Refused with the ring: the fixed section alone is tried next, then nothing
   const D3D12_DESCRIPTOR_HEAP_DESC large = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000000, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
   const D3D12_DESCRIPTOR_HEAP_DESC large_extended = *Extend(large);
   CHECK(large_extended.NumDescriptors == 1000000 + TAIL_SIZE);
   const Stats answered = GetStats();
   const std::optional<D3D12_DESCRIPTOR_HEAP_DESC> fixed = RecordExtension(device.Get(), large, large_extended, E_INVALIDARG);
   REQUIRE(fixed);
   CHECK(fixed->NumDescriptors == 1000000 + FIXED_SIZE);
   CHECK(GetStats().refused == answered.refused);
   CHECK_FALSE(RecordExtension(device.Get(), large, *fixed, E_INVALIDARG));
   CHECK(GetStats().refused == answered.refused + 1);
   CHECK(GetStats().over_tier_2_refused == answered.over_tier_2_refused + 2);
   CHECK_FALSE(RecordExtension(device.Get(), large, *fixed, S_OK));
   CHECK(GetStats().fixed_only == answered.fixed_only + 1);

   ComPtr<ID3D12DescriptorHeap> heap;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&*extended, IID_PPV_ARGS(&heap))));
   const Stats before = GetStats();
   Track(device.Get(), heap.Get(), game_desc, TAIL_SIZE);
   CHECK(GetStats().heaps == before.heaps + 1);
   CHECK(GetGameSize(heap.Get()) == 16u);

   const UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   int writes = 0;
   D3D12_CPU_DESCRIPTOR_HANDLE written = {};
   const auto write = [&](D3D12_CPU_DESCRIPTOR_HANDLE start, uint32_t) {
      writes++;
      written = start;
   };
   const D3D12_GPU_DESCRIPTOR_HANDLE first = GetRange(heap.Get(), 32, 16, 1, write);
   CHECK(first.ptr == heap->GetGPUDescriptorHandleForHeapStart().ptr + UINT64(increment) * (16 + 32));
   CHECK(written.ptr == heap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(increment) * (16 + 32));
   CHECK(GetRange(heap.Get(), 32, 16, 1, write).ptr == first.ptr);
   CHECK(writes == 1);
   GetRange(heap.Get(), 48, 16, 2, write);
   CHECK(writes == 2);
   // Past the fixed section (the ring is allocated per list), or a heap without a tail: none
   CHECK(GetRange(heap.Get(), FIXED_SIZE - 8, 16, 3, write).ptr == 0);
   ComPtr<ID3D12DescriptorHeap> plain;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&game_desc, IID_PPV_ARGS(&plain))));
   CHECK_FALSE(GetGameSize(plain.Get()));
   CHECK(GetRange(plain.Get(), 0, 16, 1, write).ptr == 0);

   // A heap with the fixed section alone has no ring
   D3D12_DESCRIPTOR_HEAP_DESC fixed_desc = game_desc;
   fixed_desc.NumDescriptors += FIXED_SIZE;
   ComPtr<ID3D12DescriptorHeap> fixed_heap;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&fixed_desc, IID_PPV_ARGS(&fixed_heap))));
   Track(device.Get(), fixed_heap.Get(), game_desc, FIXED_SIZE);
   CHECK(GetRange(fixed_heap.Get(), 0, 16, 1, write).ptr != 0);
   ComPtr<ID3D12CommandAllocator> allocator;
   ComPtr<ID3D12GraphicsCommandList> list;
   REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
   REQUIRE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
   CHECK_FALSE(AllocateRing(list.Get(), fixed_heap.Get(), 4));

   heap.Reset();
   fixed_heap.Reset();
   CHECK(GetStats().heaps == before.heaps);
}

TEST_CASE("Heap tail D3D12: a table of pass views goes into the ring, written again only when it changes, freed once its list executed")
{
   namespace Database12 = Resources::Database::D3D12;
   namespace Tail12 = Resources::HeapTail::D3D12;
   ComPtr<IDXGIFactory4> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   ComPtr<IDXGIAdapter> adapter;
   REQUIRE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
   ComPtr<ID3D12Device> device;
   REQUIRE(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
   Database12::CreateDeviceObjects(device.Get());

   // A game signature with nothing, extended with Luma's table
   const auto serialize = reinterpret_cast<PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE>(
      GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12SerializeVersionedRootSignature"));
   REQUIRE(serialize);
   D3D12_VERSIONED_ROOT_SIGNATURE_DESC game_signature = {.Version = D3D_ROOT_SIGNATURE_VERSION_1_1};
   ComPtr<ID3DBlob> game_blob;
   REQUIRE(SUCCEEDED(serialize(&game_signature, &game_blob, nullptr)));
   const std::span<const uint8_t> game_bytes(static_cast<const uint8_t*>(game_blob->GetBufferPointer()), game_blob->GetBufferSize());
   const Shaders::RootSignatures::D3D12::Extended extended = Shaders::RootSignatures::D3D12::Extend(game_bytes);
   REQUIRE(extended.table);
   ComPtr<ID3D12RootSignature> root_signature;
   REQUIRE(SUCCEEDED(device->CreateRootSignature(0, extended.blob.data(), extended.blob.size(), IID_PPV_ARGS(&root_signature))));
   const Shaders::Database::D3D12::RootLayout* layout =
      Shaders::Database::D3D12::TrackRootSignature(game_bytes, root_signature.Get(), extended.luma_parameter, extended.table, extended.static_samplers);
   REQUIRE(layout);
   // Trace lists Luma's static samplers with the game's (none here), when the mod turned them on
   CHECK(layout->static_samplers == 0);
   CHECK(layout->static_sampler_descs.size() == (extended.static_samplers ? Resources::Bindings::D3D12_STATIC_SAMPLERS : 0));

   const D3D12_DESCRIPTOR_HEAP_DESC game_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
   const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = *Tail12::Extend(game_desc);
   ComPtr<ID3D12DescriptorHeap> heap;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap))));
   Tail12::Track(device.Get(), heap.Get(), game_desc, Tail12::TAIL_SIZE);

   const D3D12_COMMAND_QUEUE_DESC queue_desc = {D3D12_COMMAND_LIST_TYPE_DIRECT};
   ComPtr<ID3D12CommandQueue> queue;
   ComPtr<ID3D12CommandAllocator> allocator;
   ComPtr<ID3D12GraphicsCommandList> list;
   REQUIRE(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));
   REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
   REQUIRE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
   list->SetGraphicsRootSignature(root_signature.Get());
   Database12::SetRootSignature(list.Get(), false, root_signature.Get());
   ID3D12DescriptorHeap* const heaps[] = {heap.Get()};
   list->SetDescriptorHeaps(1, heaps);
   Database12::SetDescriptorHeaps(list.Get(), 1, heaps);

   const D3D12_HEAP_PROPERTIES default_heap = {D3D12_HEAP_TYPE_DEFAULT};
   const D3D12_RESOURCE_DESC texture_desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN,
      D3D12_RESOURCE_FLAG_NONE};
   ComPtr<ID3D12Resource> texture;
   REQUIRE(SUCCEEDED(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
      IID_PPV_ARGS(&texture))));

   const Database12::Stats before = Database12::GetStats();
   const Tail12::Stats ring_before = Tail12::GetStats();
   Database12::TableViews views = {};
   views[15].resource = texture.Get();
   CHECK(Database12::BindTableViews(list.Get(), false, Database12::NO_TABLE, views));
   CHECK(Database12::GetStats().view_tables == before.view_tables + 1);
   CHECK(Tail12::GetStats().ring_blocks == ring_before.ring_blocks + 1);
   // The same views: the same descriptors
   CHECK(Database12::BindTableViews(list.Get(), false, Database12::NO_TABLE, views));
   CHECK(Database12::GetStats().view_tables == before.view_tables + 1);
   // Another view: written again, from the block the list holds
   views[15].desc = {DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
   views[15].desc.Texture2D.MipLevels = 1;
   CHECK(Database12::BindTableViews(list.Get(), false, Database12::NO_TABLE, views));
   CHECK(Database12::GetStats().view_tables == before.view_tables + 2);
   CHECK(Tail12::GetStats().ring_blocks == ring_before.ring_blocks + 1);
   // A ring allocation of a heap without a tail: none
   ComPtr<ID3D12DescriptorHeap> plain;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&game_desc, IID_PPV_ARGS(&plain))));
   CHECK_FALSE(Tail12::AllocateRing(list.Get(), plain.Get(), 4));

   // Executed and reset: the block is free once the GPU passed the execution
   REQUIRE(SUCCEEDED(list->Close()));
   ID3D12CommandList* const lists[] = {list.Get()};
   queue->ExecuteCommandLists(1, lists);
   Hooks::Lifetime::D3D12::MoveReferencesToQueue(queue.Get(), 1, lists);
   ComPtr<ID3D12Fence> fence;
   REQUIRE(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
   REQUIRE(SUCCEEDED(queue->Signal(fence.Get(), 1)));
   const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
   REQUIRE(SUCCEEDED(fence->SetEventOnCompletion(1, event)));
   WaitForSingleObject(event, INFINITE);
   CloseHandle(event);
   Hooks::Lifetime::D3D12::ForgetReferences(list.Get());
   Tail12::ForgetList(list.Get());
   Database12::ForgetList(list.Get());
   CHECK(Tail12::GetStats().ring_blocks == ring_before.ring_blocks + 1);
   Hooks::Lifetime::D3D12::MoveReferencesToQueue(queue.Get(), 0, nullptr);
   CHECK(Tail12::GetStats().ring_blocks == ring_before.ring_blocks);
   Database12::ReleaseDeviceObjects(device.Get());
}

TEST_CASE("Mirrors D3D12: a matching texture gets a mirror, its views swap at render target binds and copies into shader-visible heaps")
{
   namespace Mirrors12 = Resources::Mirrors::D3D12;
   ComPtr<IDXGIFactory4> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   ComPtr<IDXGIAdapter> adapter;
   REQUIRE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
   ComPtr<ID3D12Device> device;
   REQUIRE(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
   Frame::CreateDeviceState(device.Get());
   Frame::SetOutputResolution(device.Get(), {64, 36});
   RulesFixture rules;
   const Resources::Mirrors::Stats before = Resources::Mirrors::GetStats();

   const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
   const D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 32, 18, 1, 1, DXGI_FORMAT_R11G11B10_FLOAT, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
   const D3D12_CLEAR_VALUE clear = {DXGI_FORMAT_R11G11B10_FLOAT, {0.f, 0.f, 0.f, 1.f}};
   ComPtr<ID3D12Resource> texture;
   REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&texture))));
   Mirrors12::CreateMirror(device.Get(), texture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, false);
   ID3D12Resource* mirror = Mirrors12::FindMirror(texture.Get());
   REQUIRE(mirror);
   CHECK(Mirrors12::FindOriginal(mirror) == texture.Get());
   CHECK(mirror->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
   CHECK(Resources::Mirrors::GetStats().mirrors == before.mirrors + 1);

   // Barriers on the game's texture are given to the mirror
   D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
   barrier.Transition = {texture.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
   const std::span<const D3D12_RESOURCE_BARRIER> barriers = Mirrors12::AddMirrorBarriers(nullptr, std::span(&barrier, 1));
   REQUIRE(barriers.size() == 2);
   CHECK(barriers[0].Transition.pResource == texture.Get());
   CHECK(barriers[1].Transition.pResource == mirror);

   // A render target view of it: the bind gets the mirror's, with its format
   const D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2};
   ComPtr<ID3D12DescriptorHeap> rtv_heap;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap))));
   Mirrors12::TrackHeap(device.Get(), rtv_heap.Get(), rtv_desc);
   const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
   const D3D12_RENDER_TARGET_VIEW_DESC view = {DXGI_FORMAT_R11G11B10_FLOAT, D3D12_RTV_DIMENSION_TEXTURE2D};
   device->CreateRenderTargetView(texture.Get(), &view, rtv);
   Mirrors12::TrackView(device.Get(), rtv, Mirrors12::ViewKind::RenderTarget, texture.Get(), &view, true);
   const D3D12_CPU_DESCRIPTOR_HANDLE* targets = &rtv;
   BOOL single_range = TRUE;
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth = nullptr;
   Shaders::Database::D3D12::TargetFormats formats;
   Mirrors12::SwapRenderTargets(nullptr, 1, targets, single_range, depth, formats);
   CHECK(targets[0].ptr != rtv.ptr);
   CHECK_FALSE(single_range);
   CHECK(formats.render_targets[0] == DXGI_FORMAT_R16G16B16A16_FLOAT);
   CHECK(formats.render_targets[1] == DXGI_FORMAT_UNKNOWN);
   CHECK(formats.depth == DXGI_FORMAT_UNKNOWN);
   D3D12_CPU_DESCRIPTOR_HANDLE cleared = rtv;
   CHECK(Mirrors12::SwapHandle(cleared));
   CHECK(cleared.ptr == targets[0].ptr);
   // The game writes another view there: the handle is the game's again
   const D3D12_RESOURCE_DESC plain_desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 32, 18, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
   ComPtr<ID3D12Resource> plain;
   REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &plain_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&plain))));
   Mirrors12::CreateMirror(device.Get(), plain.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, false);
   CHECK_FALSE(Mirrors12::FindMirror(plain.Get()));
   const D3D12_CPU_DESCRIPTOR_HANDLE other = {rtv.ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)};
   device->CreateRenderTargetView(plain.Get(), nullptr, other);
   Mirrors12::TrackView(device.Get(), other, Mirrors12::ViewKind::RenderTarget, plain.Get(), nullptr, true);
   cleared = other;
   CHECK_FALSE(Mirrors12::SwapHandle(cleared));

   // A shader resource view in a CPU heap, copied into a shader-visible one: the copy is followed by the mirror's view
   const D3D12_DESCRIPTOR_HEAP_DESC cpu_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4};
   const D3D12_DESCRIPTOR_HEAP_DESC gpu_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
   ComPtr<ID3D12DescriptorHeap> cpu_heap;
   ComPtr<ID3D12DescriptorHeap> gpu_heap;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&cpu_desc, IID_PPV_ARGS(&cpu_heap))));
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&gpu_desc, IID_PPV_ARGS(&gpu_heap))));
   Mirrors12::TrackHeap(device.Get(), cpu_heap.Get(), cpu_desc);
   Mirrors12::TrackHeap(device.Get(), gpu_heap.Get(), gpu_desc);
   const D3D12_CPU_DESCRIPTOR_HANDLE srv = cpu_heap->GetCPUDescriptorHandleForHeapStart();
   device->CreateShaderResourceView(texture.Get(), nullptr, srv);
   Mirrors12::TrackView(device.Get(), srv, Mirrors12::ViewKind::ShaderResource, texture.Get(), nullptr, true);
   const uint32_t swapped = Mirrors12::GetStats().swapped_copies;
   device->CopyDescriptorsSimple(1, gpu_heap->GetCPUDescriptorHandleForHeapStart(), srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   Mirrors12::SwapCopiedDescriptors(device.Get(), gpu_heap->GetCPUDescriptorHandleForHeapStart(), srv, 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, true);
   CHECK(Mirrors12::GetStats().swapped_copies == swapped + 1);
   // A copy between CPU heaps carries the mirror along
   const D3D12_CPU_DESCRIPTOR_HANDLE staged = {srv.ptr + 2 * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)};
   device->CopyDescriptorsSimple(1, staged, srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   const uint32_t descriptors = Mirrors12::GetStats().descriptors;
   Mirrors12::SwapCopiedDescriptors(device.Get(), staged, srv, 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, true);
   CHECK(Mirrors12::GetStats().descriptors == descriptors + 1);

   // Copies between the game's mirrored textures run on the mirrors
   ComPtr<ID3D12Resource> second;
   REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&second))));
   Mirrors12::CreateMirror(device.Get(), second.Get(), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, false);
   ID3D12Resource* copy_destination = second.Get();
   ID3D12Resource* copy_source = texture.Get();
   Mirrors12::CopyRegion copy_region = {};
   CHECK_FALSE(Mirrors12::SwapCopy(nullptr, copy_destination, copy_source, copy_region));
   CHECK(copy_destination == Mirrors12::FindMirror(second.Get()));
   CHECK(copy_source == mirror);

   // The mirrors and their handles go with the game's objects
   cpu_heap.Reset();
   rtv_heap.Reset();
   texture.Reset();
   second.Reset();
   CHECK(Resources::Mirrors::GetStats().mirrors == before.mirrors);
   CHECK(Resources::Mirrors::GetStats().views == before.views);
   Mirrors12::ReleaseDeviceObjects(device.Get());
}

TEST_CASE("Mirrors D3D12: a depth mirror swaps at the depth bind with its format; reserved and barrier layout textures are counted, not mirrored")
{
   namespace Mirrors12 = Resources::Mirrors::D3D12;
   ComPtr<IDXGIFactory4> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   ComPtr<IDXGIAdapter> adapter;
   REQUIRE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
   ComPtr<ID3D12Device> device;
   REQUIRE(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
   Frame::CreateDeviceState(device.Get());
   Frame::SetOutputResolution(device.Get(), {64, 36});
   RulesFixture reset_rules;
   Resources::Upgrade::Rules rules = GetFF7RRules();
   rules.depth_formats = {DXGI_FORMAT_R24G8_TYPELESS};
   Resources::Upgrade::SetRules(rules);

   const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
   const D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 32, 18, 1, 1, DXGI_FORMAT_R24G8_TYPELESS, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN,
      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL};
   D3D12_CLEAR_VALUE clear = {DXGI_FORMAT_D24_UNORM_S8_UINT};
   clear.DepthStencil = {1.f, 0};
   ComPtr<ID3D12Resource> texture;
   REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&texture))));
   Mirrors12::CreateMirror(device.Get(), texture.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, false);
   ID3D12Resource* mirror = Mirrors12::FindMirror(texture.Get());
   REQUIRE(mirror);
   CHECK(mirror->GetDesc().Format == DXGI_FORMAT_R32G8X24_TYPELESS);

   const D3D12_DESCRIPTOR_HEAP_DESC dsv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1};
   ComPtr<ID3D12DescriptorHeap> dsv_heap;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&dsv_heap))));
   Mirrors12::TrackHeap(device.Get(), dsv_heap.Get(), dsv_desc);
   const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
   const D3D12_DEPTH_STENCIL_VIEW_DESC view = {DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_DSV_DIMENSION_TEXTURE2D};
   device->CreateDepthStencilView(texture.Get(), &view, dsv);
   Mirrors12::TrackView(device.Get(), dsv, Mirrors12::ViewKind::DepthStencil, texture.Get(), &view, true);
   const D3D12_CPU_DESCRIPTOR_HANDLE* targets = nullptr;
   BOOL single_range = FALSE;
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth = &dsv;
   Shaders::Database::D3D12::TargetFormats formats;
   Mirrors12::SwapRenderTargets(nullptr, 0, targets, single_range, depth, formats);
   REQUIRE(depth);
   CHECK(depth->ptr != dsv.ptr);
   CHECK(formats.depth == DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
   CHECK(formats.Any());

   // A texture the rules match, reserved or with a barrier layout: counted, not mirrored
   const Mirrors12::Stats counted = Mirrors12::GetStats();
   ComPtr<ID3D12Resource> other;
   REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&other))));
   Mirrors12::CreateMirror(device.Get(), other.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr, true);
   Mirrors12::CreateMirror(device.Get(), other.Get(), std::nullopt, nullptr, false);
   CHECK_FALSE(Mirrors12::FindMirror(other.Get()));
   CHECK(Mirrors12::GetStats().reserved_resources == counted.reserved_resources + 1);
   CHECK(Mirrors12::GetStats().layout_resources == counted.layout_resources + 1);

   dsv_heap.Reset();
   texture.Reset();
   Mirrors12::ReleaseDeviceObjects(device.Get());
}

TEST_CASE("Mirrors D3D12: a UAV clear of a mirrored texture clears the mirror through its view in the bound heap's ring")
{
   namespace Mirrors12 = Resources::Mirrors::D3D12;
   namespace Database12 = Resources::Database::D3D12;
   namespace Tail12 = Resources::HeapTail::D3D12;
   ComPtr<IDXGIFactory4> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   ComPtr<IDXGIAdapter> adapter;
   REQUIRE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
   ComPtr<ID3D12Device> device;
   REQUIRE(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
   Frame::CreateDeviceState(device.Get());
   Frame::SetOutputResolution(device.Get(), {64, 36});
   Database12::CreateDeviceObjects(device.Get());
   RulesFixture rules;

   const D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
   const D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 32, 18, 1, 1, DXGI_FORMAT_R11G11B10_FLOAT, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
   ComPtr<ID3D12Resource> texture;
   REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&texture))));
   Mirrors12::CreateMirror(device.Get(), texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, false);
   ID3D12Resource* mirror = Mirrors12::FindMirror(texture.Get());
   REQUIRE(mirror);

   // The game's UAV in a CPU heap, and the shader-visible heap it's bound with
   const D3D12_DESCRIPTOR_HEAP_DESC cpu_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1};
   ComPtr<ID3D12DescriptorHeap> cpu_heap;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&cpu_desc, IID_PPV_ARGS(&cpu_heap))));
   Mirrors12::TrackHeap(device.Get(), cpu_heap.Get(), cpu_desc);
   const D3D12_CPU_DESCRIPTOR_HANDLE game_cpu = cpu_heap->GetCPUDescriptorHandleForHeapStart();
   device->CreateUnorderedAccessView(texture.Get(), nullptr, nullptr, game_cpu);
   Mirrors12::TrackView(device.Get(), game_cpu, Mirrors12::ViewKind::UnorderedAccess, texture.Get(), nullptr, true);
   const D3D12_DESCRIPTOR_HEAP_DESC game_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
   const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = *Tail12::Extend(game_desc);
   ComPtr<ID3D12DescriptorHeap> visible;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&visible))));
   Tail12::Track(device.Get(), visible.Get(), game_desc, Tail12::TAIL_SIZE);
   Mirrors12::TrackHeap(device.Get(), visible.Get(), game_desc);
   device->CopyDescriptorsSimple(1, visible->GetCPUDescriptorHandleForHeapStart(), game_cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

   ComPtr<ID3D12CommandAllocator> allocator;
   ComPtr<ID3D12GraphicsCommandList> list;
   REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
   REQUIRE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
   ID3D12DescriptorHeap* const heaps[] = {visible.Get()};
   list->SetDescriptorHeaps(1, heaps);
   Database12::SetDescriptorHeaps(list.Get(), 1, heaps);

   const Mirrors12::Stats before = Mirrors12::GetStats();
   D3D12_GPU_DESCRIPTOR_HANDLE gpu = visible->GetGPUDescriptorHandleForHeapStart();
   D3D12_CPU_DESCRIPTOR_HANDLE cpu = game_cpu;
   ID3D12Resource* resource = texture.Get();
   Mirrors12::SwapClearUnorderedAccess(list.Get(), gpu, cpu, resource);
   CHECK(resource == mirror);
   CHECK(cpu.ptr != game_cpu.ptr);
   const UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   CHECK(gpu.ptr >= visible->GetGPUDescriptorHandleForHeapStart().ptr + UINT64(increment) * (game_desc.NumDescriptors + Tail12::FIXED_SIZE));
   CHECK(Mirrors12::GetStats().uav_clears == before.uav_clears + 1);
   const FLOAT values[4] = {1.f, 0.f, 0.f, 1.f};
   list->ClearUnorderedAccessViewFloat(gpu, cpu, resource, values, 0, nullptr);
   CHECK(SUCCEEDED(list->Close()));

   // Without a heap with a tail bound: counted, the game's clear runs
   REQUIRE(SUCCEEDED(list->Reset(allocator.Get(), nullptr)));
   Hooks::Lifetime::D3D12::ForgetReferences(list.Get());
   Tail12::ForgetList(list.Get());
   Database12::ForgetList(list.Get());
   gpu = visible->GetGPUDescriptorHandleForHeapStart();
   cpu = game_cpu;
   resource = texture.Get();
   Mirrors12::SwapClearUnorderedAccess(list.Get(), gpu, cpu, resource);
   CHECK(resource == texture.Get());
   CHECK(Mirrors12::GetStats().unhandled_clears == before.unhandled_clears + 1);
   CHECK(SUCCEEDED(list->Close()));

   cpu_heap.Reset();
   texture.Reset();
   Mirrors12::ReleaseDeviceObjects(device.Get());
   Database12::ReleaseDeviceObjects(device.Get());
}

TEST_CASE("Upgrade: custom_dimensions overrides dimensions, clamps mip count, and respects guards")
{
   struct Cleanup
   {
      ~Cleanup() { Resources::Upgrade::SetRules({}); }
   } cleanup;

   Resources::Upgrade::Rules rules;
   rules.formats = {DXGI_FORMAT_R11G11B10_FLOAT};
   rules.size_filters = SizeFilters{};
   rules.custom_dimensions = {
      {.filter = {1280, 720, 0, 0}, .format = DXGI_FORMAT_R11G11B10_FLOAT, .size = {2560, 1440, 0, 0}},
      {.filter = {512, 512, 0, 0}, .format = DXGI_FORMAT_UNKNOWN, .size = {128, 128, 0, 0}},
      {.filter = {256, 256, 0, 1}, .format = DXGI_FORMAT_R11G11B10_FLOAT, .size = {0, 0, 0, 4}},
   };
   Resources::Upgrade::SetRules(rules);

   const Resources::Upgrade::FrameState state = {.render = {1280, 720}, .output = {2560, 1440}, .display = {2560, 1440}};
   TextureInfo tex = Target(1280, 720, DXGI_FORMAT_R11G11B10_FLOAT);
   tex.levels = 1;
   bool resized = false;
   const std::optional<TextureInfo> matched = Resources::Upgrade::Match(tex, state, false, false, false, &resized);
   REQUIRE(matched);
   CHECK(matched->width == 2560);
   CHECK(matched->height == 1440);
   CHECK(matched->format == DXGI_FORMAT_R16G16B16A16_FLOAT);
   CHECK(resized);

   // Shrink clamps mip count to max valid mips for 128x128 (8 mips) and preserves format when format isn't in rules.formats
   TextureInfo shrunk = Target(512, 512, DXGI_FORMAT_R8G8B8A8_UNORM);
   shrunk.levels = 10;
   resized = false;
   const std::optional<TextureInfo> shrunk_result = Resources::Upgrade::Match(shrunk, state, false, false, false, &resized);
   REQUIRE(shrunk_result);
   CHECK(shrunk_result->width == 128);
   CHECK(shrunk_result->height == 128);
   CHECK(shrunk_result->levels == 8);
   CHECK(shrunk_result->format == DXGI_FORMAT_R8G8B8A8_UNORM);
   CHECK(resized);

   // Cube textures cannot change height independently, and UAV textures cannot add MSAA samples
   TextureInfo cube = Target(1280, 720, DXGI_FORMAT_R11G11B10_FLOAT);
   cube.cube = true;
   resized = false;
   const std::optional<TextureInfo> cube_match = Resources::Upgrade::Match(cube, state, false, false, false, &resized);
   CHECK_FALSE(resized);
   if (cube_match)
   {
      CHECK(cube_match->width == 1280);
      CHECK(cube_match->height == 720);
   }

   TextureInfo uav_tex = Target(256, 256, DXGI_FORMAT_R11G11B10_FLOAT);
   uav_tex.unordered_access = true;
   resized = false;
   const std::optional<TextureInfo> uav_match = Resources::Upgrade::Match(uav_tex, state, false, false, false, &resized);
   CHECK_FALSE(resized);
   if (uav_match)
      CHECK(uav_match->samples == 1);
}

TEST_CASE("Chains: seed matching and propagation eligibility")
{
   struct Cleanup
   {
      ~Cleanup() { Resources::Chains::SetOptions({}); }
   } cleanup;

   Resources::Chains::Options options;
   options.mode = Resources::Chains::Mode::DirectAndIndirect;
   options.seeds = {
      {0x11223344, {.rtv_slots = {0}, .scale = true}},
      {0xAABBCCDD, {.uav_slots = {1}, .scale = false}},
   };
   Resources::Chains::SetOptions(options);

   CHECK(Resources::Chains::IsNeeded());
   CHECK(Resources::Chains::HasScaleSeeds());
   REQUIRE(Resources::Chains::FindSeed(0x11223344));
   CHECK(Resources::Chains::FindSeed(0x11223344)->scale);
   CHECK_FALSE(Resources::Chains::FindSeed(0x99887766));
   REQUIRE(Resources::Chains::FindSeed(0xAABBCCDD));
   CHECK_FALSE(Resources::Chains::FindSeed(0xAABBCCDD)->scale);

   // Propagation accepts writable GPU-only 2D RGBA/float targets matching aspect ratio, rejects integer/depth/arrays
   TextureInfo source = Target(1280, 720, DXGI_FORMAT_R16G16B16A16_FLOAT);
   TextureInfo ok = Target(1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM);
   CHECK(Resources::Chains::CanPropagate(source, ok));
   TextureInfo int_tex = Target(1280, 720, DXGI_FORMAT_R8G8B8A8_UINT);
   CHECK_FALSE(Resources::Chains::CanPropagate(source, int_tex));
   TextureInfo depth_tex = {.width = 1280, .height = 720, .format = DXGI_FORMAT_R24G8_TYPELESS, .depth_stencil = true};
   CHECK_FALSE(Resources::Chains::CanPropagate(source, depth_tex));
   TextureInfo array_tex = ok;
   array_tex.depth_or_layers = 2;
   CHECK_FALSE(Resources::Chains::CanPropagate(source, array_tex));
}

TEST_CASE("Scaling: pure math and thread-affine Begin/End with cross-thread overlap detection")
{
   struct Cleanup
   {
      ~Cleanup() { Resources::Scaling::SetOptions({}); }
   } cleanup;

   Resources::Scaling::SetOptions({.mode = Resources::Scaling::Mode::ResourcesAndViewports});
   const Resources::Upgrade::FrameState state_2x = {.render = {1280, 720}, .output = {2560, 1440}, .display = {2560, 1440}};
   const Resources::Upgrade::FrameState state_1x = {.render = {2560, 1440}, .output = {2560, 1440}, .display = {2560, 1440}};
   CHECK(Resources::Scaling::ShouldScaleTexture(Target(1280, 720, DXGI_FORMAT_R11G11B10_FLOAT), state_2x, true, true));
   CHECK(Resources::Scaling::ShouldScaleTexture(Target(640, 360, DXGI_FORMAT_R11G11B10_FLOAT), state_2x, true, true));
   CHECK_FALSE(Resources::Scaling::ShouldScaleTexture(Target(1280, 720, DXGI_FORMAT_R11G11B10_FLOAT), state_2x, true, false));
   CHECK_FALSE(Resources::Scaling::ShouldScaleTexture(Target(1280, 720, DXGI_FORMAT_R11G11B10_FLOAT), state_1x, true, true));
   CHECK_FALSE(Resources::Scaling::ShouldScaleTexture(Target(256, 256, DXGI_FORMAT_R11G11B10_FLOAT), state_2x, true, true));

   const Math::uint2 scaled = Resources::Scaling::ComputeScaledSize({640, 360}, {1280, 720}, {2560, 1440});
   CHECK(scaled.x == 1280);
   CHECK(scaled.y == 720);

   const Resources::Scaling::Viewport vp = Resources::Scaling::RescaleViewport({10.f, 20.f, 1280.f, 720.f, 0.f, 1.f}, {2.f, 2.f});
   CHECK(vp.x == doctest::Approx(20.f));
   CHECK(vp.y == doctest::Approx(40.f));
   CHECK(vp.width == doctest::Approx(2560.f));
   CHECK(vp.height == doctest::Approx(1440.f));

   const Resources::Scaling::Rect rect = Resources::Scaling::RescaleRect({10, 20, 100, 200}, {2.f, 2.f});
   CHECK(rect.left == 20);
   CHECK(rect.top == 40);
   CHECK(rect.right == 200);
   CHECK(rect.bottom == 400);

   const Resources::Scaling::Box box = Resources::Scaling::RescaleBox({10, 20, 0, 100, 200, 1}, {2.f, 2.f});
   CHECK(box.left == 20);
   CHECK(box.top == 40);
   CHECK(box.right == 200);
   CHECK(box.bottom == 400);

   // Thread-affine Begin/End on a real COM device
   ComPtr<ID3D11Device> device;
   REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr)));
   Resources::Scaling::Begin(device.Get());
   CHECK(Resources::Scaling::IsActive(device.Get()));
   CHECK(Resources::Scaling::IsActiveOnCurrentThread(device.Get()));
   CHECK_FALSE(Resources::Scaling::CheckCrossThreadOverlap(device.Get()));

   bool worker_active_on_thread = true;
   bool worker_overlap = false;
   std::thread worker([&] {
      worker_active_on_thread = Resources::Scaling::IsActiveOnCurrentThread(device.Get());
      worker_overlap = Resources::Scaling::CheckCrossThreadOverlap(device.Get());
   });
   worker.join();
   CHECK_FALSE(worker_active_on_thread);
   CHECK(worker_overlap);

   Resources::Scaling::End(device.Get());
   CHECK_FALSE(Resources::Scaling::IsActive(device.Get()));
   Resources::Scaling::OnPresent(device.Get());
}

TEST_CASE("Mirrors D3D11: scaled viewports, invalidation, and recycle pool")
{
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
   Frame::CreateDeviceState(device.Get());
   Frame::SetOutputResolution(device.Get(), {128, 72});
   Frame::SetRenderResolution(device.Get(), {64, 36});
   const bool was_enabled = Settings::IsEnabled();
   if (!Settings::Find(Settings::Core::ENABLED.key))
   {
      static constexpr std::array ENABLED_SETTING = {Settings::Setting{.key = Settings::Core::ENABLED.key, .label = "Enabled", .default_value = true}};
      Settings::Register(ENABLED_SETTING, Settings::Kind::Luma);
   }
   Settings::Set(Settings::Core::ENABLED, true);

   struct Cleanup
   {
      ID3D11Device* dev;
      bool prev_enabled;

      ~Cleanup()
      {
         Resources::Scaling::End(dev);
         Resources::Scaling::SetOptions({});
         Resources::Upgrade::SetRules({});
         Resources::Mirrors::D3D11::ReleaseDeviceObjects(dev);
         Settings::Set(Settings::Core::ENABLED, prev_enabled);
      }
   } cleanup{device.Get(), was_enabled};

   Resources::Scaling::SetOptions({.mode = Resources::Scaling::Mode::ResourcesAndViewports});
   Resources::Scaling::SetTargetResolution(device.Get(), {128, 72});
   Resources::Scaling::Begin(device.Get());
   Resources::Upgrade::SetRules({
      .formats = {DXGI_FORMAT_R11G11B10_FLOAT},
      .size_filters = SizeFilters::RenderResolution,
      .custom_dimensions = {{.filter = {64, 36, 0, 0}, .format = DXGI_FORMAT_R11G11B10_FLOAT, .size = {128, 72, 0, 0}}},
   });

   const D3D11_TEXTURE2D_DESC1 desc = {.Width = 64,
      .Height = 36,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = DXGI_FORMAT_R11G11B10_FLOAT,
      .SampleDesc = {1, 0},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE};
   ComPtr<ID3D11Texture2D> tex;
   REQUIRE(SUCCEEDED(device->CreateTexture2D(reinterpret_cast<const D3D11_TEXTURE2D_DESC*>(&desc), nullptr, &tex)));
   Resources::Mirrors::D3D11::CreateMirror(device.Get(), desc, nullptr, tex.Get());
   ID3D11Resource* mirror = Resources::Mirrors::D3D11::FindMirror(tex.Get());
   REQUIRE(mirror);
   D3D11_TEXTURE2D_DESC mirror_desc = {};
   static_cast<ID3D11Texture2D*>(mirror)->GetDesc(&mirror_desc);
   CHECK(mirror_desc.Width == 128);
   CHECK(mirror_desc.Height == 72);

   // Viewport rescaling on PrepareDrawOrDispatch when bound to scaled mirror
   ComPtr<ID3D11RenderTargetView> rtv;
   REQUIRE(SUCCEEDED(device->CreateRenderTargetView(tex.Get(), nullptr, &rtv)));
   Resources::Mirrors::D3D11::CreateMirrorView(device.Get(), tex.Get(), nullptr, rtv.Get());
   ID3D11RenderTargetView* bound_rtv = rtv.Get();
   Resources::Mirrors::D3D11::SwapViews(&bound_rtv, 1);
   context->OMSetRenderTargets(1, &bound_rtv, nullptr);
   const D3D11_VIEWPORT vp = {0.f, 0.f, 64.f, 36.f, 0.f, 1.f};
   context->RSSetViewports(1, &vp);
   Resources::Mirrors::D3D11::OnBindViewports(context.Get(), 1, &vp);
   Resources::Mirrors::D3D11::PrepareDrawOrDispatch(context.Get(), false);

   UINT vp_count = 1;
   D3D11_VIEWPORT active_vp = {};
   context->RSGetViewports(&vp_count, &active_vp);
   CHECK(active_vp.Width == doctest::Approx(128.f));
   CHECK(active_vp.Height == doctest::Approx(72.f));

   // Invalidation retires scaled mirrors; OnPresent advances retirement and recycles
   const uint32_t invalidations_before = Resources::Scaling::GetStats().invalidations;
   Resources::Mirrors::D3D11::InvalidateScaledMirrors(device.Get());
   CHECK(Resources::Scaling::GetStats().invalidations == invalidations_before + 1);
   CHECK(Resources::Mirrors::D3D11::FindMirror(tex.Get()) == nullptr);
   Resources::Mirrors::D3D11::OnPresent(device.Get());
   Resources::Mirrors::D3D11::OnPresent(device.Get());
   Resources::Mirrors::D3D11::OnPresent(device.Get());
   Resources::Mirrors::D3D11::ForgetContextState(context.Get());
}

TEST_CASE("Mirrors D3D12: ViewportsOnly expansion scales sub-rect viewports and restores on End")
{
   namespace Mirrors12 = Resources::Mirrors::D3D12;
   ComPtr<IDXGIFactory4> factory;
   REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
   ComPtr<IDXGIAdapter> adapter;
   REQUIRE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
   ComPtr<ID3D12Device> device;
   REQUIRE(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
   Frame::CreateDeviceState(device.Get());
   Frame::SetOutputResolution(device.Get(), {128, 72});
   Frame::SetRenderResolution(device.Get(), {64, 36});
   const bool was_enabled = Settings::IsEnabled();
   if (!Settings::Find(Settings::Core::ENABLED.key))
   {
      static constexpr std::array ENABLED_SETTING = {Settings::Setting{.key = Settings::Core::ENABLED.key, .label = "Enabled", .default_value = true}};
      Settings::Register(ENABLED_SETTING, Settings::Kind::Luma);
   }
   Settings::Set(Settings::Core::ENABLED, true);

   struct Cleanup
   {
      ID3D12Device* dev;
      bool prev_enabled;

      ~Cleanup()
      {
         Resources::Scaling::End(dev);
         Resources::Scaling::SetOptions({});
         Mirrors12::ReleaseDeviceObjects(dev);
         Settings::Set(Settings::Core::ENABLED, prev_enabled);
      }
   } cleanup{device.Get(), was_enabled};

   Resources::Scaling::SetOptions({.mode = Resources::Scaling::Mode::ViewportsOnly});
   Resources::Scaling::SetTargetResolution(device.Get(), {128, 72});
   Resources::Scaling::Begin(device.Get());

   ComPtr<ID3D12CommandAllocator> allocator;
   ComPtr<ID3D12GraphicsCommandList> list;
   REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
   REQUIRE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));

   const D3D12_VIEWPORT game_vp = {0.f, 0.f, 64.f, 36.f, 0.f, 1.f};
   UINT count = 1;
   const D3D12_VIEWPORT* vps = &game_vp;
   Mirrors12::RecordViewports(list.Get(), count, vps);
   REQUIRE(vps != nullptr);
   CHECK(vps[0].Width == doctest::Approx(128.f));
   CHECK(vps[0].Height == doctest::Approx(72.f));

   const std::optional<Mirrors12::ListScalingSnapshot> snap = Mirrors12::GetListScalingSnapshot(list.Get());
   REQUIRE(snap);
   CHECK(snap->viewports_scaled);
   CHECK(snap->game_viewport_count == 1);
   CHECK(snap->game_viewports[0].Width == doctest::Approx(64.f));

   Mirrors12::ForgetListState(list.Get());
}
