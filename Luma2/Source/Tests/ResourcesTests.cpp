#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <span>
#include <cstring>
#include <vector>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "Frame/Frame.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/D3D12/D3D12HeapTail.hpp"
#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/Mirrors.hpp"
#include "Resources/Samplers.hpp"
#include "Resources/Upgrade.hpp"
#include "Trace/D3D11/D3D11Capture.hpp"
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
   // CPU-only heaps and samplers don't get one, nor a heap the tail would take past the device's limit
   CHECK_FALSE(Extend({D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_NONE}));
   CHECK_FALSE(Extend({D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE}));
   CHECK_FALSE(Extend({D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000000, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE}));

   ComPtr<ID3D12DescriptorHeap> heap;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&*extended, IID_PPV_ARGS(&heap))));
   const Stats before = GetStats();
   Track(device.Get(), heap.Get(), game_desc);
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
   // Past the tail, or a heap without one: none
   CHECK(GetRange(heap.Get(), TAIL_SIZE - 8, 16, 3, write).ptr == 0);
   ComPtr<ID3D12DescriptorHeap> plain;
   REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&game_desc, IID_PPV_ARGS(&plain))));
   CHECK_FALSE(GetGameSize(plain.Get()));
   CHECK(GetRange(plain.Get(), 0, 16, 1, write).ptr == 0);

   heap.Reset();
   CHECK(GetStats().heaps == before.heaps);
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
   Mirrors12::CreateMirror(device.Get(), texture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, &clear);
   ID3D12Resource* mirror = Mirrors12::GetMirror(texture.Get());
   REQUIRE(mirror);
   CHECK(Mirrors12::GetOriginal(mirror) == texture.Get());
   CHECK(mirror->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
   CHECK(Resources::Mirrors::GetStats().mirrors == before.mirrors + 1);

   // Barriers on the game's texture are given to the mirror
   D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
   barrier.Transition = {texture.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
   const std::span<const D3D12_RESOURCE_BARRIER> barriers = Mirrors12::AddMirrorBarriers(std::span(&barrier, 1));
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
   Mirrors12::OnCreateView(device.Get(), rtv, Mirrors12::ViewKind::RenderTarget, texture.Get(), &view, true);
   const D3D12_CPU_DESCRIPTOR_HANDLE* targets = &rtv;
   BOOL single_range = TRUE;
   const D3D12_CPU_DESCRIPTOR_HANDLE* depth = nullptr;
   std::array<DXGI_FORMAT, 8> formats;
   Mirrors12::SwapRenderTargets(1, targets, single_range, depth, formats);
   CHECK(targets[0].ptr != rtv.ptr);
   CHECK_FALSE(single_range);
   CHECK(formats[0] == DXGI_FORMAT_R16G16B16A16_FLOAT);
   CHECK(formats[1] == DXGI_FORMAT_UNKNOWN);
   D3D12_CPU_DESCRIPTOR_HANDLE cleared = rtv;
   CHECK(Mirrors12::SwapHandle(cleared));
   CHECK(cleared.ptr == targets[0].ptr);
   // The game writes another view there: the handle is the game's again
   const D3D12_RESOURCE_DESC plain_desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 32, 18, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
   ComPtr<ID3D12Resource> plain;
   REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &plain_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&plain))));
   Mirrors12::CreateMirror(device.Get(), plain.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr);
   CHECK_FALSE(Mirrors12::GetMirror(plain.Get()));
   const D3D12_CPU_DESCRIPTOR_HANDLE other = {rtv.ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)};
   device->CreateRenderTargetView(plain.Get(), nullptr, other);
   Mirrors12::OnCreateView(device.Get(), other, Mirrors12::ViewKind::RenderTarget, plain.Get(), nullptr, true);
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
   Mirrors12::OnCreateView(device.Get(), srv, Mirrors12::ViewKind::ShaderResource, texture.Get(), nullptr, true);
   const uint32_t swapped = Mirrors12::GetStats().swapped_copies;
   device->CopyDescriptorsSimple(1, gpu_heap->GetCPUDescriptorHandleForHeapStart(), srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   Mirrors12::OnCopyDescriptors(device.Get(), gpu_heap->GetCPUDescriptorHandleForHeapStart(), srv, 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, true);
   CHECK(Mirrors12::GetStats().swapped_copies == swapped + 1);
   // A copy between CPU heaps carries the mirror along
   const D3D12_CPU_DESCRIPTOR_HANDLE staged = {srv.ptr + 2 * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)};
   device->CopyDescriptorsSimple(1, staged, srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   const uint32_t descriptors = Mirrors12::GetStats().descriptors;
   Mirrors12::OnCopyDescriptors(device.Get(), staged, srv, 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, true);
   CHECK(Mirrors12::GetStats().descriptors == descriptors + 1);

   // Copies between the game's mirrored textures run on the mirrors
   ComPtr<ID3D12Resource> second;
   REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&second))));
   Mirrors12::CreateMirror(device.Get(), second.Get(), D3D12_RESOURCE_STATE_COPY_DEST, nullptr);
   ID3D12Resource* copy_destination = second.Get();
   ID3D12Resource* copy_source = texture.Get();
   Mirrors12::SwapCopy(copy_destination, copy_source);
   CHECK(copy_destination == Mirrors12::GetMirror(second.Get()));
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
