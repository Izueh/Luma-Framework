#include "Luma.hpp"

#include <array>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <random>
#include <span>
#include <string_view>

#include <wrl/client.h>

using namespace Luma;

struct Luma::Game::GameDeviceData
{
   Math::float2 upscaled_render_resolution = {1.f, 1.f};
   float resolution_scale = 1.f;
   Math::uint4 viewport_rect = {0, 0, 1, 1};
   bool drs_active = false;
   bool found_per_view_globals = false;
   bool drew_sr = false;
   bool is_in_menu = true;
   uint32_t jitterless_frames_count = 0;
   Microsoft::WRL::ComPtr<ID3D11SamplerState> linear_sampler;
};

namespace {
using Game::cb_luma_global_settings;
using Game::GameDeviceData;

constexpr float PI = 3.14159265358979323846f;
constexpr uint32_t CB_PER_VIEW_GLOBAL_SIZE = 4096;
constexpr uint64_t DECODE_MVS_HASH = "Luma_MotionVec_UE4_Decode"_h;
constexpr uint64_t COPY_VS_HASH = "Luma_Copy_VS"_h;

std::atomic<bool> can_sharpen_atomic = true;

// The About text and credits
constexpr std::string_view DESCRIPTION = "Luma for \"Final Fantasy VII Remake\" is developed by Izueh and Pumbo and is open source and free. It adds DLSS and improved HDR "
                                         "tonemapping. Additional thanks to ShortFuse and Musa from the RenoDX team and their HDR mods for Remake and Rebirth, which served "
                                         "as reference. If you enjoy it, consider donating to any of the contributors.";
constexpr std::array<std::string_view, 13> CREDITS = {
   "Main:",
   "Izueh",
   "Pumbo",
   "Acknowledgments:",
   "ShortFuse",
   "Musa",
   "Third Party:",
   "ReShade",
   "ImGui",
   "RenoDX",
   "3Dmigoto",
   "Oklab",
   "DICE (HDR tonemapper)",
};
constexpr std::array<Game::Link, 3> LINKS = {{
   {"Buy Izueh a Coffee on ko-fi", "https://ko-fi.com/izueh"},
   {"Buy Pumbo a Coffee on buymeacoffee", "https://buymeacoffee.com/realfiloppi"},
   {"Buy Pumbo a Coffee on ko-fi", "https://ko-fi.com/realpumbo"},
}};

// The game's shaders by hash, each with a debug name (Init)
constexpr std::array<uint32_t, 1> SHADER_HASHES_TAA = {0x4729683B};
constexpr std::array<uint32_t, 1> SHADER_HASHES_TITLE = {0x5FEE74F9};
constexpr std::array<uint32_t, 1> SHADER_HASHES_DOF = {0xB400FAF6};
constexpr std::array<uint32_t, 1> SHADER_HASHES_MOTION_BLUR = {0xB0F56393};
constexpr std::array<uint32_t, 1> SHADER_HASHES_DOWNSAMPLE_BLOOM = {0x2174B927};
constexpr std::array<uint32_t, 1> SHADER_HASHES_BLOOM = {0x4D6F937E};
constexpr std::array<uint32_t, 1> SHADER_HASHES_MENU_SLOWDOWN = {0x968B821F};
constexpr std::array<uint32_t, 1> SHADER_HASHES_TONEMAP = {0xF68D39B5};
constexpr std::array<uint32_t, 1> SHADER_HASHES_VELOCITY_FLATTEN = {0x4EB2EA5B};
constexpr std::array<uint32_t, 1> SHADER_HASHES_VELOCITY_GATHER = {0xFEE03685};
constexpr std::array<uint32_t, 1> SHADER_HASHES_SSAO_TEMPORAL = {0x04BFE575};
constexpr std::array<uint32_t, 8> SHADER_HASHES_OUTPUT_HDR = {0xA8EB118F, 0x922A71D1, 0x3A4D858E, 0xD950DA01, 0x5CD12E67, 0x3B489929, 0x8D04181D, 0x6846FF90};
constexpr std::array<uint32_t, 8> SHADER_HASHES_OUTPUT_SDR = {0x506D5998, 0xF68D39B5, 0xBBB9CE42, 0x51E2B894, 0x803889E8, 0xD96EF76D, 0x5C2D3A71, 0x66162229};
constexpr std::array<uint32_t, 1> SHADER_HASHES_DOWNSAMPLE_BLOOM_2 = {0xA77F0B56};
constexpr std::array<uint32_t, 2> SHADER_HASHES_UPSCALE_BLOOM = {0xD9E87012, 0x46727E9A};
constexpr std::array<uint32_t, 2> SHADER_HASHES_BLUR_BLOOM = {0xCCD7FA05, 0x69467442};
constexpr std::array<uint32_t, 1> SHADER_HASHES_AMBIENT_OCCLUSION = {0x1D610CBA};
constexpr std::array<uint32_t, 1> SHADER_HASHES_AMBIENT_OCCLUSION_DENOISE_1 = {0x8BD60486};
constexpr std::array<uint32_t, 1> SHADER_HASHES_AMBIENT_OCCLUSION_DENOISE_2 = {0xE6A8D4FB};

Math::float2 ToFloat(Math::uint2 size)
{
   return {float(size.x), float(size.y)};
}

// Parses UE4 CBPerViewGlobal (4096 bytes) on both D3D11 (via Map/Unmap) and D3D12 (via ReadBoundCBuffer)
bool ParsePerViewGlobal(Device device, std::span<const std::byte> bytes)
{
   if (bytes.size() < CB_PER_VIEW_GLOBAL_SIZE)
      return false;
   GameDeviceData* data = Game::GetGameDeviceData(device);
   if (!data || data->found_per_view_globals)
      return false;

   const auto* f = reinterpret_cast<const Math::float4*>(bytes.data());
   const bool valid_res = f[126].x > 0.f && f[126].y > 0.f && f[125].x > 0.f && f[125].y > 0.f &&
      f[126].z == 1.f / f[126].x && f[126].w == 1.f / f[126].y && f[125].z == 1.f / f[125].x && f[125].w == 1.f / f[125].y;
   const bool valid_jitter = std::fabs(f[118].x) <= 1.f && std::fabs(f[118].y) <= 1.f && (std::fabs(f[118].x) > 0.f || std::fabs(f[118].y) > 0.f);
   if (!valid_res || !valid_jitter)
      return false;

   data->found_per_view_globals = true;
   Game::Resources::ConstantCapture::PauseForFrame();
   data->jitterless_frames_count = 0;
   data->is_in_menu = false;

   Math::uint2 render_size = Game::FrameState::Get(device).render;
   if (f[122].x > 0.f && f[122].y > 0.f)
   {
      render_size = {uint32_t(f[122].x + 0.5f), uint32_t(f[122].y + 0.5f)};
      Game::FrameState::SetRenderResolution(device, render_size);
   }

   // Projection matrix rows at float4[24..27]
   const float m11 = f[25].y;
   const float m22 = f[26].z;
   const float m23 = f[26].w;
   const float m32 = f[27].z;
   const float m33 = f[27].w;
   float near_plane = (std::fabs(m22 - m23) > 1e-6f) ? ((m33 - m32) / (m22 - m23)) / 100.f : 0.1f;
   float vert_fov = (std::fabs(m11) > 1e-3f) ? (std::atan(1.f / m11) * 2.f) : (60.f * (PI / 180.f));
   if (near_plane < 0.01f || near_plane > 100.f)
      near_plane = 0.1f;
   if (vert_fov < (10.f * (PI / 180.f)) || vert_fov > (170.f * (PI / 180.f)))
      vert_fov = 60.f * (PI / 180.f);

   Game::FrameState::Camera camera = Game::FrameState::GetCamera(device);
   camera.jitter_pixels = {f[118].x * float(render_size.x) * 0.5f, f[118].y * float(render_size.y) * -0.5f};
   camera.vert_fov = vert_fov;
   camera.near_plane = near_plane;
   camera.far_plane = FLT_MAX;
   camera.camera_cut = f[140].y != 0.f;
   Game::FrameState::SetCamera(device, camera);
   return true;
}

void OnPerViewGlobalWrite(Device device, std::span<const std::byte> bytes)
{
   ParsePerViewGlobal(device, bytes);
}

// Called at every draw with a Luma shader (LumaData is uploaded when its bytes change)
void UpdateLumaInstanceDataCB(CB::LumaInstanceData& data, const GameDeviceData& game_device_data, const FrameState::Resolutions& resolutions)
{
   const Math::float2 render_resolution = ToFloat(resolutions.render);
   data.GameData.RenderResolution = {render_resolution.x, render_resolution.y, 1.0f / render_resolution.x, 1.0f / render_resolution.y};
   data.GameData.OutputResolution = {game_device_data.upscaled_render_resolution.x, game_device_data.upscaled_render_resolution.y,
      1.0f / game_device_data.upscaled_render_resolution.x, 1.0f / game_device_data.upscaled_render_resolution.y};
   data.GameData.ResolutionScale = {game_device_data.resolution_scale, 1.0f / game_device_data.resolution_scale};
   data.GameData.DrewUpscaling = game_device_data.drew_sr ? 1u : 0u;
   data.GameData.ViewportRect = game_device_data.viewport_rect;
}

void OnInitSwapchain(Device device)
{
   GameDeviceData* game_device_data = Game::GetGameDeviceData(device);
   if (!game_device_data)
      return;
   const Math::float2 output_resolution = ToFloat(Game::FrameState::Get(device).output);
   game_device_data->upscaled_render_resolution = output_resolution;
   const bool sharpen_ok = output_resolution == game_device_data->upscaled_render_resolution;
   can_sharpen_atomic.store(sharpen_ok, std::memory_order_relaxed);
   cb_luma_global_settings.GameSettings.can_sharpen = sharpen_ok ? 1.f : 0.f;
   Game::MarkLumaSettingsDirty();
}

void OnPresent(Device device)
{
   if (GameDeviceData* data = Game::GetGameDeviceData(device))
   {
      data->found_per_view_globals = false;
      data->drew_sr = false;
   }
   static std::mt19937 random_generator(uint32_t(std::chrono::system_clock::now().time_since_epoch().count()));
   static auto random_range = static_cast<float>((std::mt19937::max)() - (std::mt19937::min)());
   cb_luma_global_settings.GameSettings.custom_random = static_cast<float>(random_generator() + (std::mt19937::min)()) / random_range;
}

Game::DrawResult OnDrawOutputPass(Device device, bool is_hdr)
{
   if (Game::FrameState::HasPass(device, Game::FrameState::PassMarker::MainPostProcessing) || !Game::FrameState::HasPass(device, Game::FrameState::PassMarker::Taa))
      return Game::DrawResult::None;
   Game::Resources::Scaling::End(device);
   Game::FrameState::MarkPass(device, Game::FrameState::PassMarker::Upscaling);
   Game::FrameState::MarkPass(device, Game::FrameState::PassMarker::MainPostProcessing);
   Game::Display::SetHdr(is_hdr);
   return Game::DrawResult::None;
}

Game::Effects::SR::DrawOverrides PrepareTaaSr(Device device, Math::uint2 render_size, Math::uint2 upscaled_size, Math::uint4 viewport_rect)
{
   Game::FrameState::SetRenderResolution(device, render_size);
   Game::Resources::Scaling::SetTargetResolution(device, upscaled_size);
   Game::Effects::SR::DrawOverrides overrides;
   overrides.output_size = upscaled_size;
   overrides.render_size = render_size;

   if (GameDeviceData* data = Game::GetGameDeviceData(device))
   {
      data->upscaled_render_resolution = ToFloat(upscaled_size);
      data->resolution_scale = float(render_size.x) / float(upscaled_size.x);
      data->viewport_rect = viewport_rect;

      float sr_scale = 1.f;
      if (!data->drs_active && data->resolution_scale == 1.f)
         sr_scale = 1.f;
      else if (data->resolution_scale < 0.5f - FLT_EPSILON)
      {
         sr_scale = data->resolution_scale;
         data->drs_active = true;
      }
      else
      {
         sr_scale = 1.f / 1.5f;
         data->drs_active = true;
      }
      const double aspect = double(upscaled_size.x) / double(upscaled_size.y);
      overrides.settings_render_size = Game::Effects::SR::FindClosestIntegerResolutionForAspectRatio(
         double(upscaled_size.x) * sr_scale, double(upscaled_size.y) * sr_scale, aspect);
      overrides.dynamic_resolution = data->drs_active;

      const Math::float2 output_resolution = ToFloat(Game::FrameState::Get(device).output);
      const bool sharpen_ok = output_resolution == data->upscaled_render_resolution;
      can_sharpen_atomic.store(sharpen_ok, std::memory_order_relaxed);
      const float can_sharpen = sharpen_ok ? 1.f : 0.f;
      if (cb_luma_global_settings.GameSettings.can_sharpen != can_sharpen)
      {
         cb_luma_global_settings.GameSettings.can_sharpen = can_sharpen;
         Game::MarkLumaSettingsDirty();
      }
   }
   return overrides;
}

namespace D3D11Events {
void OnInitSwapchain(IDXGISwapChain*, ID3D11Device* device, bool)
{
   ::OnInitSwapchain(device);
}

void OnPresent(IDXGISwapChain*, ID3D11Device* device)
{
   ::OnPresent(device);
}

Game::DrawResult OnDrawTaa(const Game::D3D11::Call& call)
{
   Microsoft::WRL::ComPtr<ID3D11Device> device;
   call.context->GetDevice(&device);
   if (!device || Game::FrameState::HasPass(device.Get(), Game::FrameState::PassMarker::MainPostProcessing))
      return Game::DrawResult::None;
   Game::FrameState::MarkPass(device.Get(), Game::FrameState::PassMarker::Taa);

   ID3D11Resource* const output_res = Game::D3D11::TagBoundRTV(call, 0, Game::Resources::Database::Sr::OUTPUT);
   ID3D11Resource* const depth_res = Game::D3D11::TagBoundSRV(call, ShaderStage::Pixel, 1, Game::Resources::Database::Sr::DEPTH);
   ID3D11Resource* const color_res = Game::D3D11::TagBoundSRV(call, ShaderStage::Pixel, 2, Game::Resources::Database::Sr::COLOR);
   if (!output_res || !depth_res || !color_res)
      return Game::DrawResult::None;

   Microsoft::WRL::ComPtr<ID3D11Texture2D> output_tex;
   if (FAILED(output_res->QueryInterface(IID_PPV_ARGS(&output_tex))))
      return Game::DrawResult::None;
   D3D11_TEXTURE2D_DESC out_desc = {};
   output_tex->GetDesc(&out_desc);
   const Math::uint2 upscaled_size = {out_desc.Width, out_desc.Height};

   UINT viewport_count = 1;
   D3D11_VIEWPORT viewport = {};
   call.context->RSGetViewports(&viewport_count, &viewport);
   const Math::uint2 render_size = viewport_count > 0 && viewport.Width > 0.f && viewport.Height > 0.f
      ? Math::uint2{uint32_t(viewport.Width + 0.5f), uint32_t(viewport.Height + 0.5f)}
      : upscaled_size;
   const Math::uint4 viewport_rect = viewport_count > 0
      ? Math::uint4{uint32_t(viewport.TopLeftX + 0.5f), uint32_t(viewport.TopLeftY + 0.5f), upscaled_size.x, upscaled_size.y}
      : Math::uint4{0, 0, upscaled_size.x, upscaled_size.y};

   const Game::Effects::SR::DrawOverrides overrides = PrepareTaaSr(device.Get(), render_size, upscaled_size, viewport_rect);
   if (!Game::Effects::SR::IsActive() || !Game::D3D11::HasLumaShader(device.Get(), DECODE_MVS_HASH) || !Game::D3D11::HasLumaShader(device.Get(), COPY_VS_HASH))
      return Game::DrawResult::None;

   // Decode UE4 motion vectors into the managed SR_MOTION_VECTORS texture
   if (!Game::D3D11::EnsureTexture(device.Get(), Game::Resources::Database::Sr::MOTION_VECTORS, upscaled_size.x, upscaled_size.y, DXGI_FORMAT_R16G16_FLOAT,
          D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE))
      return Game::DrawResult::None;
   ID3D11RenderTargetView* const mv_rtv = Game::D3D11::GetRTV(device.Get(), Game::Resources::Database::Sr::MOTION_VECTORS);
   if (!mv_rtv)
      return Game::DrawResult::None;

   {
      Microsoft::WRL::ComPtr<ID3D11RenderTargetView> prev_rtv;
      Microsoft::WRL::ComPtr<ID3D11DepthStencilView> prev_dsv;
      Microsoft::WRL::ComPtr<ID3D11VertexShader> prev_vs;
      Microsoft::WRL::ComPtr<ID3D11PixelShader> prev_ps;
      D3D11_PRIMITIVE_TOPOLOGY prev_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      call.context->OMGetRenderTargets(1, &prev_rtv, &prev_dsv);
      call.context->VSGetShader(&prev_vs, nullptr, nullptr);
      call.context->PSGetShader(&prev_ps, nullptr, nullptr);
      call.context->IAGetPrimitiveTopology(&prev_topology);

      call.context->OMSetRenderTargets(1, &mv_rtv, nullptr);
      call.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
      Game::D3D11::BindLumaShader(call.context, COPY_VS_HASH);
      Game::D3D11::BindLumaShader(call.context, DECODE_MVS_HASH);
      call.context->Draw(4, 0);

      ID3D11RenderTargetView* restore_rtv = prev_rtv.Get();
      call.context->OMSetRenderTargets(1, &restore_rtv, prev_dsv.Get());
      call.context->VSSetShader(prev_vs.Get(), nullptr, 0);
      call.context->PSSetShader(prev_ps.Get(), nullptr, 0);
      call.context->IASetPrimitiveTopology(prev_topology);
   }

   if (Game::Effects::SR::Draw(call, overrides))
   {
      if (GameDeviceData* data = Game::GetGameDeviceData(device.Get()))
      {
         data->drew_sr = true;
         if (data->drs_active && data->found_per_view_globals)
            Game::Resources::Scaling::Begin(device.Get());
      }
      Game::FrameState::MarkPass(device.Get(), Game::FrameState::PassMarker::Upscaling);
      return Game::DrawResult::Replaced;
   }
   return Game::DrawResult::None;
}

Game::DrawResult OnDrawDofAndMotionBlur(const Game::D3D11::Call& call)
{
   Microsoft::WRL::ComPtr<ID3D11Device> device;
   call.context->GetDevice(&device);
   if (!device || Game::FrameState::HasPass(device.Get(), Game::FrameState::PassMarker::MainPostProcessing) || !Game::FrameState::HasPass(device.Get(), Game::FrameState::PassMarker::Taa))
      return Game::DrawResult::None;
   if (GameDeviceData* data = Game::GetGameDeviceData(device.Get()))
   {
      if (!data->linear_sampler)
      {
         D3D11_SAMPLER_DESC desc = {};
         desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
         desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
         desc.MaxLOD = D3D11_FLOAT32_MAX;
         device->CreateSamplerState(&desc, &data->linear_sampler);
      }
      if (data->linear_sampler)
      {
         ID3D11SamplerState* sampler = data->linear_sampler.Get();
         call.context->PSSetSamplers(0, 1, &sampler);
      }
   }
   return Game::DrawResult::None;
}

Game::DrawResult OnDrawOutputHdr(const Game::D3D11::Call& call)
{
   Microsoft::WRL::ComPtr<ID3D11Device> device;
   call.context->GetDevice(&device);
   return ::OnDrawOutputPass(device.Get(), true);
}

Game::DrawResult OnDrawOutputSdr(const Game::D3D11::Call& call)
{
   Microsoft::WRL::ComPtr<ID3D11Device> device;
   call.context->GetDevice(&device);
   return ::OnDrawOutputPass(device.Get(), false);
}
} // namespace D3D11Events

namespace D3D12Events {
Game::DrawResult OnDrawTaa(const Game::D3D12::Call& call)
{
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   if (FAILED(call.list->GetDevice(IID_PPV_ARGS(&device))) || !device)
      return Game::DrawResult::None;
   if (Game::FrameState::HasPass(device.Get(), Game::FrameState::PassMarker::MainPostProcessing))
      return Game::DrawResult::None;
   Game::FrameState::MarkPass(device.Get(), Game::FrameState::PassMarker::Taa);

   // Read UE4 CBPerViewGlobal (b1, 4096 bytes) synchronously from the mapped upload heap
   std::array<std::byte, CB_PER_VIEW_GLOBAL_SIZE> cb1_bytes = {};
   if (Game::D3D12::ReadBoundCBuffer(call, ShaderStage::Pixel, 1, cb1_bytes))
      ParsePerViewGlobal(device.Get(), cb1_bytes);

   ID3D12Resource* const output_res = Game::D3D12::TagBoundRTV(call, 0, Game::Resources::Database::Sr::OUTPUT);
   ID3D12Resource* const depth_res = Game::D3D12::TagBoundSRV(call, ShaderStage::Pixel, 1, Game::Resources::Database::Sr::DEPTH);
   ID3D12Resource* const color_res = Game::D3D12::TagBoundSRV(call, ShaderStage::Pixel, 2, Game::Resources::Database::Sr::COLOR);
   if (!output_res || !depth_res || !color_res)
      return Game::DrawResult::None;

   const D3D12_RESOURCE_DESC out_desc = output_res->GetDesc();
   const Math::uint2 upscaled_size = {uint32_t(out_desc.Width), out_desc.Height};
   const std::optional<D3D12_VIEWPORT> viewport = Game::D3D12::GetViewport(call.list);
   const Math::uint2 render_size = viewport && viewport->Width > 0.f && viewport->Height > 0.f
      ? Math::uint2{uint32_t(viewport->Width + 0.5f), uint32_t(viewport->Height + 0.5f)}
      : Game::FrameState::Get(device.Get()).render;
   const Math::uint4 viewport_rect = viewport
      ? Math::uint4{uint32_t(viewport->TopLeftX + 0.5f), uint32_t(viewport->TopLeftY + 0.5f), upscaled_size.x, upscaled_size.y}
      : Math::uint4{0, 0, upscaled_size.x, upscaled_size.y};

   const Game::Effects::SR::DrawOverrides overrides = PrepareTaaSr(device.Get(), render_size, upscaled_size, viewport_rect);
   if (!Game::Effects::SR::IsActive())
      return Game::DrawResult::None;

   if (!Game::D3D12::EnsureTexture(device.Get(), Game::Resources::Database::Sr::MOTION_VECTORS, upscaled_size.x, upscaled_size.y, DXGI_FORMAT_R16G16_FLOAT,
          D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
      return Game::DrawResult::None;

   if (!Game::D3D12::DrawFullscreenToEntry(call, COPY_VS_HASH, DECODE_MVS_HASH, Game::Resources::Database::Sr::MOTION_VECTORS, DXGI_FORMAT_R16G16_FLOAT,
          upscaled_size))
      return Game::DrawResult::None;

   if (Game::Effects::SR::Draw(call, overrides))
   {
      if (GameDeviceData* data = Game::GetGameDeviceData(device.Get()))
      {
         data->drew_sr = true;
         if (data->drs_active && data->found_per_view_globals)
            Game::Resources::Scaling::Begin(device.Get());
      }
      Game::FrameState::MarkPass(device.Get(), Game::FrameState::PassMarker::Upscaling);
      return Game::DrawResult::Replaced;
   }
   return Game::DrawResult::None;
}

Game::DrawResult OnDrawOutputHdr(const Game::D3D12::Call& call)
{
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   if (SUCCEEDED(call.list->GetDevice(IID_PPV_ARGS(&device))))
      return ::OnDrawOutputPass(device.Get(), true);
   return Game::DrawResult::None;
}

Game::DrawResult OnDrawOutputSdr(const Game::D3D12::Call& call)
{
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   if (SUCCEEDED(call.list->GetDevice(IID_PPV_ARGS(&device))))
      return ::OnDrawOutputPass(device.Get(), false);
   return Game::DrawResult::None;
}

void OnInitSwapchain(IDXGISwapChain*, ID3D12CommandQueue* queue, bool)
{
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))))
      ::OnInitSwapchain(device.Get());
}

void OnPresent(IDXGISwapChain*, ID3D12CommandQueue* queue)
{
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))))
      ::OnPresent(device.Get());
}
} // namespace D3D12Events

using Settings::EnumIndex;
using Settings::Range;
using Settings::Setting;
using Settings::Sources::Binding;

bool IsHdrMode()
{
   return cb_luma_global_settings.DisplayMode == 1;
}

bool IsRcasVisible()
{
   return Game::Effects::SR::IsActive();
}

bool IsRcasEnabled()
{
   return Game::Effects::SR::IsActive() && can_sharpen_atomic.load(std::memory_order_relaxed);
}

constexpr std::array<std::string_view, 2> TONEMAP_TYPES = {"Vanilla", "Neutwo"};

const std::array SETTINGS = {
   Setting{
      .key = "FXBloom",
      .label = "Bloom",
      .tooltip = "Bloom strength multiplier. Default is 50.",
      .category = "Post Processing",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_bloom, [](float value) { return value * 0.02f; }},
   },
   Setting{
      .key = "FXVignette",
      .label = "Vignette",
      .tooltip = "Vignette strength multiplier. Default is 50.",
      .category = "Post Processing",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_vignette, [](float value) { return value * 0.02f; }},
   },
   Setting{
      .key = "FXFilmGrain",
      .label = "Film Grain",
      .tooltip = "Film grain strength multiplier. Default is 50, for Vanilla look set to 0.",
      .category = "Post Processing",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_film_grain_strength, [](float value) { return value * 0.02f; }},
   },
   Setting{
      .key = "FXRCAS",
      .label = "Sharpness",
      .tooltip = "RCAS strength multiplier. Default is 50, for Vanilla look set to 0.",
      .category = "Post Processing",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_sharpness_strength, [](float value) { return value * 0.01f; }},
      .is_enabled = &IsRcasEnabled,
      .is_visible = &IsRcasVisible,
   },
   Setting{
      .key = "FXHDRVideos",
      .label = "HDR Videos",
      .tooltip = "Enable or disable HDR video playback. Default is On.",
      .category = "Post Processing",
      .default_value = true,
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_hdr_videos},
      .is_visible = &IsHdrMode,
   },
   Setting{
      .key = "TonemapType",
      .label = "Tone Map Type",
      .tooltip = "Tone mapping algorithm to use",
      .category = "Color Grading",
      .default_value = EnumIndex{1},
      .enum_labels = TONEMAP_TYPES,
      .source = Binding{&cb_luma_global_settings.GameSettings.tonemap_type},
      .is_enabled = &IsHdrMode,
      .is_visible = &IsHdrMode,
   },
   Setting{
      .key = "CustomLUTStrength",
      .label = "LUT Strength",
      .tooltip = "LUT strength multiplier.",
      .category = "Color Grading",
      .default_value = 100.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.custom_lut_strength, [](float value) { return value * 0.01f; }},
      .is_visible = &IsHdrMode,
   },
   Setting{
      .key = "ColorGradeHueCorrection",
      .label = "Hue Shift",
      .category = "Color Grading",
      .default_value = 0.3f,
      .range = Range{0.f, 1.f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.hue_correction_strength},
      .is_visible = &IsHdrMode,
   },
   Setting{
      .key = "ColorGradeExposure",
      .label = "Exposure",
      .category = "Color Grading",
      .default_value = 1.f,
      .range = Range{0.f, 2.f},
      .format = "%.2f",
      .source = Binding{&cb_luma_global_settings.GameSettings.exposure},
      .is_visible = &IsHdrMode,
   },
   Setting{
      .key = "ColorGradeHighlights",
      .label = "Highlights",
      .category = "Color Grading",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.highlights, [](float value) { return value * 0.02f; }},
      .is_visible = &IsHdrMode,
   },
   Setting{
      .key = "ColorGradeShadows",
      .label = "Shadows",
      .category = "Color Grading",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.shadows, [](float value) { return value * 0.02f; }},
      .is_visible = &IsHdrMode,
   },
   Setting{
      .key = "ColorGradeContrast",
      .label = "Contrast",
      .category = "Color Grading",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.contrast, [](float value) { return value * 0.02f; }},
      .is_visible = &IsHdrMode,
   },
   Setting{
      .key = "ColorGradeSaturation",
      .label = "Saturation",
      .category = "Color Grading",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.saturation, [](float value) { return value * 0.02f; }},
      .is_visible = &IsHdrMode,
   },
   Setting{
      .key = "ColorGradeHighlightSaturation",
      .label = "Highlight Saturation",
      .category = "Color Grading",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.highlight_saturation, [](float value) { return value * 0.02f; }},
      .is_visible = &IsHdrMode,
   },
   Setting{
      .key = "ColorGradeBlowout",
      .label = "Blowout",
      .category = "Color Grading",
      .default_value = 20.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.blowout, [](float value) { return value * 0.02f; }},
      .is_visible = &IsHdrMode,
   },
   Setting{
      .key = "ColorGradeFlare",
      .label = "Flare",
      .category = "Color Grading",
      .default_value = 0.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.flare, [](float value) { return value * 0.02f; }},
      .is_visible = &IsHdrMode,
   },
};
} // namespace

void Luma::Game::Init()
{
   SetInfo({
      .version = "2.0.0",
      .description = DESCRIPTION,
      .credits = CREDITS,
      .links = LINKS,
   });

   Shaders::SetDebugName("TAA", SHADER_HASHES_TAA);
   Shaders::SetDebugName("Title", SHADER_HASHES_TITLE);
   Shaders::SetDebugName("DoF", SHADER_HASHES_DOF);
   Shaders::SetDebugName("Motion Blur", SHADER_HASHES_MOTION_BLUR);
   Shaders::SetDebugName("Downsample Bloom", SHADER_HASHES_DOWNSAMPLE_BLOOM);
   Shaders::SetDebugName("Downsample Bloom", SHADER_HASHES_DOWNSAMPLE_BLOOM_2);
   Shaders::SetDebugName("Upscale Bloom", SHADER_HASHES_UPSCALE_BLOOM);
   Shaders::SetDebugName("Blur Bloom", SHADER_HASHES_BLUR_BLOOM);
   Shaders::SetDebugName("Apply Bloom", SHADER_HASHES_BLOOM);
   Shaders::SetDebugName("Slowdown Menu", SHADER_HASHES_MENU_SLOWDOWN);
   Shaders::SetDebugName("Upscale and Tonemap", SHADER_HASHES_TONEMAP);
   Shaders::SetDebugName("Velocity Flatten", SHADER_HASHES_VELOCITY_FLATTEN);
   Shaders::SetDebugName("Velocity Gather", SHADER_HASHES_VELOCITY_GATHER);
   Shaders::SetDebugName("Ambient Occlusion", SHADER_HASHES_AMBIENT_OCCLUSION);
   Shaders::SetDebugName("Ambient Occlusion Temporal (SSAO algorithm)", SHADER_HASHES_SSAO_TEMPORAL);
   Shaders::SetDebugName("Ambient Occlusion Denoise 1", SHADER_HASHES_AMBIENT_OCCLUSION_DENOISE_1);
   Shaders::SetDebugName("Ambient Occlusion Denoise 2", SHADER_HASHES_AMBIENT_OCCLUSION_DENOISE_2);
   Shaders::SetDebugName("Output HDR", SHADER_HASHES_OUTPUT_HDR);
   Shaders::SetDebugName("Output SDR", SHADER_HASHES_OUTPUT_SDR);
   Shaders::RedirectHashes("Output_HDR", SHADER_HASHES_OUTPUT_HDR);
   Shaders::RedirectHashes("Output_SDR", SHADER_HASHES_OUTPUT_SDR);

   Shaders::AddDefine({
      .name = "SSAO_ALGORITHM",
      .label = "SSAO Algorithm",
      .tooltip = "AO algorithm\n0 - Original game SSAO\n1 - VBAO",
      .default_value = 1,
      .range = Range{0, 1},
   });
   Settings::Register(SETTINGS);
   Settings::SetDefault(Settings::Core::PAPER_WHITE, 250.f);
   Settings::SetDefault(Settings::Core::UI_PAPER_WHITE, 250.f);
   Settings::SetDefault(Settings::Core::LINK_OS_WHITE_LEVEL, false);

   SetOptions({
      .cbuffers = {
         .settings_slot = 13,
         .data_slot = 12,
         .update_luma_instance_data = &UpdateLumaInstanceDataCB,
      },
      .bindings = {.static_samplers = true},
      .textures = {.load = true},
      .samplers = {.upgrade = true},
      .scaling = {.mode = Resources::Scaling::Mode::ViewportsOnly},
      .swapchain = {.mode = Display::Swapchain::Mode::Mirror, .hdr_output = Display::Swapchain::OutputMode::Hdr10},
      .composition = {
         .post_process_space = 1,
         .early_display_encoding = true,
         .vanilla_encoding = 1,
         .gamma_correction = 1,
         .ui_draw_type = 1,
      },
      .sr = {
         .enable = true,
         .hdr = true,
         .inverted_depth = true,
         .firefly_swatter = true,
      },
      .constant_capture = {.enable = true},
      .device_data = GameDeviceDataType::Of<GameDeviceData>(),
   });
   Resources::ConstantCapture::RegisterWriteCallback(CB_PER_VIEW_GLOBAL_SIZE, &OnPerViewGlobalWrite);
   Resources::SetUpgrades({
      .formats = {DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R11G11B10_FLOAT},
      .size_filters = Resources::Upgrade::SizeFilters::SwapchainResolution | Resources::Upgrade::SizeFilters::SwapchainAspectRatio |
         Resources::Upgrade::SizeFilters::CustomAspectRatio,
      .custom_aspect_ratios = {16.f / 9.f},
      .lut = {.size = 32, .dimensions = Resources::Upgrade::LutDimensions::_3D},
   });
   SetEvents(D3D11::Events{
      .on_init_swapchain = &D3D11Events::OnInitSwapchain,
      .on_present = &D3D11Events::OnPresent,
   });
   SetEvents(D3D12::Events{
      .on_init_swapchain = &D3D12Events::OnInitSwapchain,
      .on_present = &D3D12Events::OnPresent,
   });
   D3D11::OnShaderDraw(SHADER_HASHES_TAA, &D3D11Events::OnDrawTaa);
   D3D11::OnShaderDraw(SHADER_HASHES_DOF, &D3D11Events::OnDrawDofAndMotionBlur);
   D3D11::OnShaderDraw(SHADER_HASHES_MOTION_BLUR, &D3D11Events::OnDrawDofAndMotionBlur);
   D3D11::OnShaderDraw(SHADER_HASHES_OUTPUT_HDR, &D3D11Events::OnDrawOutputHdr);
   D3D11::OnShaderDraw(SHADER_HASHES_OUTPUT_SDR, &D3D11Events::OnDrawOutputSdr);
   D3D12::OnShaderDraw(SHADER_HASHES_TAA, &D3D12Events::OnDrawTaa);
   D3D12::OnShaderDraw(SHADER_HASHES_OUTPUT_HDR, &D3D12Events::OnDrawOutputHdr);
   D3D12::OnShaderDraw(SHADER_HASHES_OUTPUT_SDR, &D3D12Events::OnDrawOutputSdr);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
   return Luma::DllMain(module, reason, reserved);
}
