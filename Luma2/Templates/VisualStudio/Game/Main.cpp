// Luma 2.0 game mod template. Search for "###" to customize.
// Neutral by default: uncomment options in Init(), or set LUMA_EXAMPLES=1 to compile the API reference below.
#include "Luma.hpp"

#include <array>
#include <string_view>

#ifndef LUMA_EXAMPLES
#define LUMA_EXAMPLES 0
#endif

#if LUMA_EXAMPLES
#include <format>
#include <optional>
#include <span>
#include <vector>

#include <wrl/client.h>

struct Luma::Game::GameDeviceData
{
   Luma::Math::float2 upscaled_render_resolution = {1.f, 1.f};
   float resolution_scale = 1.f;
};
#endif

using namespace Luma;

namespace {
// ### Update mod identity ###
constexpr std::string_view DESCRIPTION = "Luma for \"$projectname$\". Describe what the mod does here.";
constexpr std::array<std::string_view, 5> CREDITS = {
   "Main:",
   "Your name",
   "Third Party:",
   "ImGui",
   "Luma Framework",
};
constexpr std::array<Game::Link, 1> LINKS = {{
   {"Contribute on Github", "https://github.com/Filoppi/Luma-Framework"},
}};

// Shader CRC32 hashes (from the overlay's Trace tab or "Luma/Dump"):
// constexpr std::array<uint32_t, 1> SHADER_HASHES_TONEMAP = {0x12345678};
} // namespace

// API reference (compiled only when LUMA_EXAMPLES=1; delete once you know what you need).
namespace Examples {
#if LUMA_EXAMPLES
namespace {
using Game::cb_luma_global_settings;
using Game::GameDeviceData;
using Settings::EnumIndex;
using Settings::Key;
using Settings::Range;
using Settings::Setting;
using Settings::Visibility;
using Settings::Sources::Binding;

constexpr std::array<uint32_t, 1> EXAMPLE_HASHES = {0xDEADBEEF};

// Cbuffers & per-frame events

// Called at every draw with a Luma shader: fills the game's part of LumaData ("Resources/<Game>/Shaders/Game/GameCBuffers.hlsl").
// LumaData is uploaded only when its bytes change. Any thread.
void UpdateLumaInstanceData(CB::LumaInstanceData& data, const GameDeviceData& game_device_data, const Frame::Resolutions& resolutions)
{
   data.GameData.dummy = float(resolutions.render.x) * game_device_data.resolution_scale;
}

void OnInitSwapchain(Device device)
{
   GameDeviceData* game_device_data = Game::GetGameDeviceData(device);
   if (!game_device_data)
      return;
   const Frame::Resolutions resolutions = Game::Frame::Get(device);
   game_device_data->upscaled_render_resolution = {float(resolutions.output.x), float(resolutions.output.y)};
   Game::Frame::SetRenderResolution(device, resolutions.output);
   // Only a change made outside Present needs this so draws see it before the next Present
   Game::MarkLumaSettingsDirty();
}

// Settings (saved in "Luma/Luma.toml" by category; keys are unique and don't start with "Luma." or "Defines.")

constexpr std::array<std::string_view, 3> EXAMPLE_MODES = {"Off", "Low", "High"};

bool IsExampleToggleOn()
{
   return Settings::Get<bool>("ExampleToggle");
}

// Const, not constexpr: a setting with a string default can't be constexpr
const std::array SETTINGS = {
   // Float slider bound to LumaSettings cbuffer through `parse` (0-100 on screen, 0-1 in the shader)
   Setting{
      .key = "ExampleSlider",
      .label = "Example Slider",
      .tooltip = "A float slider bound to a field of the game's LumaSettings cbuffer. Default is 50.",
      .category = "Example",
      .default_value = 50.f,
      .range = Range{0.f, 100.f},
      .format = "%.0f",
      .source = Binding{&cb_luma_global_settings.GameSettings.dummy, [](float value) { return value * 0.01f; }},
      .live = true,
   },
   // Bool toggle (0 or 1 in the cbuffer)
   Setting{
      .key = "ExampleToggle",
      .label = "Example Toggle",
      .tooltip = "A bool, 0 or 1 in the shader.",
      .category = "Example",
      .default_value = true,
      .source = Binding{&cb_luma_global_settings.GameSettings.dummy},
   },
   // Enum (index of its label)
   Setting{
      .key = "ExampleMode",
      .label = "Example Mode",
      .tooltip = "An enum, shown as a combo or as segments. The shader gets the index.",
      .category = "Example",
      .default_value = EnumIndex{1},
      .enum_labels = EXAMPLE_MODES,
      .source = Binding{&cb_luma_global_settings.GameSettings.dummy},
   },
   // Int with no cbuffer source: read with Settings::Get<int32_t>("ExampleCount") or a change listener
   Setting{
      .key = "ExampleCount",
      .label = "Example Count",
      .category = "Example",
      .default_value = 3,
      .range = Range{0, 10},
      .is_enabled = &IsExampleToggleOn,
   },
   // Key (Windows virtual key code)
   Setting{
      .key = "ExampleKey",
      .label = "Example Key",
      .tooltip = "A keyboard key, as a Windows virtual key code.",
      .category = "Example",
      .default_value = Key{VK_F8},
   },
   // String
   Setting{
      .key = "ExampleText",
      .label = "Example Text",
      .category = "Example",
      .default_value = std::string("text"),
      .is_visible = &IsExampleToggleOn,
   },
   // Shown in Test and Development builds only (Release is the default)
   Setting{
      .key = "ExampleTest",
      .label = "Example Test Setting",
      .category = "Example",
      .visibility = Visibility::Test,
      .default_value = false,
   },
   // Shown in Development builds only (for quick experiments, prefer DevSetting01..10 in Core/CBuffers.hlsl)
   Setting{
      .key = "ExampleDevelopment",
      .label = "Example Development Setting",
      .category = "Example",
      .visibility = Visibility::Development,
      .default_value = false,
   },
};

void OnSettingChanged(const Setting& setting, const Settings::Value& /*value*/)
{
   if (setting.key == "ExampleCount")
   {
      // React to it here, or read it where it's used
   }
}

// Custom overlay panels (plain settings go through the registry above)

void DrawSettingsPanel()
{
   UI::Widgets::Caption("A custom settings panel, drawn below the registry's settings.");
   if (UI::Widgets::Button("Example Button", UI::Widgets::ButtonKind::Secondary, "Shows a toast"))
      UI::Widgets::Toast(UI::Widgets::Tone::Info, "Example Button clicked");
}

void DrawInfoPanel()
{
   UI::Widgets::Property("Example Property", std::format("{}", Settings::Get<int32_t>("ExampleCount")));
}

void DrawAboutPanel()
{
   UI::Widgets::Paragraph("Extra about text for the mod.");
}

void DrawDevelopmentPanel()
{
   UI::Widgets::Caption("Development tools of the mod.");
}

// Draw callbacks (return None to run the game's call, Skip to drop it, Replaced if the callback drew instead)

constexpr bool RUN_EXAMPLE = false;

Game::DrawResult OnExampleDrawD3D11(const Game::D3D11::Call& call)
{
   if (!RUN_EXAMPLE || call.draw == nullptr)
      return Game::DrawResult::None;
   const uint32_t pixel_hash = call.hashes[size_t(ShaderStage::Pixel)];
   if (pixel_hash == EXAMPLE_HASHES[0])
   {
      // Custom textures loaded from Resources/<Game>/Textures/ are looked up by filename hash:
      // ID3D11ShaderResourceView* noise = Game::D3D11::GetTexture(device, "fast_noise"_h);
      Game::D3D11::BindLumaShader(call.context, "Luma_Example_PS"_h, {.custom_data_3 = 1.f});
      Game::D3D11::RunOriginal(call);
      return Game::DrawResult::Replaced;
   }
   Game::D3D11::UseOriginal(call, ShaderStage::Pixel);
   Game::Display::SetHdr(true);
   return Game::DrawResult::None;
}

Game::DrawResult OnExampleDrawD3D12(const Game::D3D12::Call& call)
{
   if (!RUN_EXAMPLE || call.draw == nullptr)
      return Game::DrawResult::None;
   Game::D3D12::RunOriginal(call);
   Game::D3D12::UseOriginal(call);
   return Game::DrawResult::Skip;
}

template <typename Call>
Game::DrawResult OnVariantDraw(const Call&)
{
   return Game::DrawResult::None;
}

template <typename Call>
Game::DrawResult OnDrawOrDispatch(const Call&)
{
   return Game::DrawResult::None;
}

// D3D11 events

namespace D3D11Events {
void OnInitDevice(ID3D11Device* /*device*/) {}

void OnDestroyDevice(ID3D11Device* /*device*/) {}

void OnCreateSwapchain(Game::SwapchainDesc& /*desc*/) {}

void OnInitSwapchain(IDXGISwapChain*, ID3D11Device* device, bool /*resize*/) { ::Examples::OnInitSwapchain(device); }

void OnCreateShader(ID3D11Device* /*device*/, ShaderStage /*stage*/, uint32_t /*hash*/, std::span<const uint8_t> /*bytecode*/, ID3D11DeviceChild* /*shader*/) {}

void OnCreateBuffer(ID3D11Device*, D3D11_BUFFER_DESC& /*desc*/, const D3D11_SUBRESOURCE_DATA*& /*initial_data*/) {}

void OnInitBuffer(ID3D11Device*, const D3D11_BUFFER_DESC& /*desc*/, const D3D11_SUBRESOURCE_DATA* /*initial_data*/, ID3D11Buffer* /*buffer*/) {}

void OnCreateTexture2D(ID3D11Device*, D3D11_TEXTURE2D_DESC1& /*desc*/, const D3D11_SUBRESOURCE_DATA*& /*initial_data*/) {}

void OnInitTexture2D(ID3D11Device*, const D3D11_TEXTURE2D_DESC1& /*desc*/, const D3D11_SUBRESOURCE_DATA* /*initial_data*/, ID3D11Texture2D* /*texture*/) {}

void OnResourceDestroyed(ID3D11Device*, const ID3D11Resource* /*resource*/) {}

void OnInitShaderResourceView(ID3D11Device*, ID3D11Resource* /*resource*/, const D3D11_SHADER_RESOURCE_VIEW_DESC1* /*desc*/, ID3D11ShaderResourceView* /*view*/) {}

void OnInitRenderTargetView(ID3D11Device*, ID3D11Resource* /*resource*/, const D3D11_RENDER_TARGET_VIEW_DESC1* /*desc*/, ID3D11RenderTargetView* /*view*/) {}

void OnInitUnorderedAccessView(ID3D11Device*, ID3D11Resource* /*resource*/, const D3D11_UNORDERED_ACCESS_VIEW_DESC1* /*desc*/, ID3D11UnorderedAccessView* /*view*/) {}

void OnInitDepthStencilView(ID3D11Device*, ID3D11Resource* /*resource*/, const D3D11_DEPTH_STENCIL_VIEW_DESC* /*desc*/, ID3D11DepthStencilView* /*view*/) {}

void OnViewDestroyed(ID3D11Device*, const ID3D11View* /*view*/) {}

void OnCreateSamplerState(ID3D11Device*, D3D11_SAMPLER_DESC& /*desc*/) {}

bool OnBindRenderTargets(ID3D11DeviceContext*, UINT /*count*/, ID3D11RenderTargetView** /*views*/, ID3D11DepthStencilView*& /*depth_stencil*/) { return false; }

bool OnBindRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext*, UINT /*count*/, ID3D11RenderTargetView** /*views*/, ID3D11DepthStencilView*& /*depth_stencil*/,
   UINT /*uav_start*/, UINT /*uav_count*/, ID3D11UnorderedAccessView** /*uavs*/, const UINT* /*initial_counts*/) { return false; }

void OnMap(ID3D11DeviceContext*, ID3D11Resource* /*resource*/, UINT /*subresource*/, D3D11_MAP /*type*/, UINT /*flags*/, D3D11_MAPPED_SUBRESOURCE& /*mapped*/) {}

bool OnUnmap(ID3D11DeviceContext*, ID3D11Resource* /*resource*/, UINT /*subresource*/) { return false; }

void OnUnmapped(ID3D11DeviceContext*, ID3D11Resource* /*resource*/, UINT /*subresource*/) {}

bool OnUpdateSubresource(ID3D11DeviceContext*, ID3D11Resource* /*resource*/, UINT /*subresource*/, const D3D11_BOX* /*box*/, const void* /*data*/, UINT /*row_pitch*/,
   UINT /*depth_pitch*/, UINT /*copy_flags*/) { return false; }

bool OnCopyResource(ID3D11DeviceContext*, ID3D11Resource* /*destination*/, ID3D11Resource* /*source*/) { return false; }

bool OnCopySubresourceRegion(ID3D11DeviceContext*, ID3D11Resource* /*destination*/, UINT /*destination_subresource*/, UINT /*x*/, UINT /*y*/, UINT /*z*/,
   ID3D11Resource* /*source*/, UINT /*source_subresource*/, const D3D11_BOX* /*box*/, UINT /*copy_flags*/) { return false; }

bool OnResolveSubresource(ID3D11DeviceContext*, ID3D11Resource* /*destination*/, UINT /*destination_subresource*/, ID3D11Resource* /*source*/, UINT /*source_subresource*/,
   DXGI_FORMAT /*format*/) { return false; }

bool OnClearRenderTargetView(ID3D11DeviceContext*, ID3D11RenderTargetView* /*view*/, const FLOAT /*color*/[4]) { return false; }

bool OnClearView(ID3D11DeviceContext*, ID3D11View* /*view*/, const FLOAT /*color*/[4], const D3D11_RECT* /*rects*/, UINT /*rect_count*/) { return false; }

void OnBindConstantBuffers(ID3D11DeviceContext*, ShaderStage /*stage*/, UINT /*start*/, UINT /*count*/, ID3D11Buffer* const* /*buffers*/) {}

void OnBindViewports(ID3D11DeviceContext*, UINT /*count*/, const D3D11_VIEWPORT* /*viewports*/) {}

void OnBindScissorRects(ID3D11DeviceContext*, UINT /*count*/, const D3D11_RECT* /*rects*/) {}

void OnBindBlendState(ID3D11DeviceContext*, ID3D11BlendState* /*state*/, const FLOAT /*blend_factor*/[4], UINT /*sample_mask*/) {}

void OnBindDepthStencilState(ID3D11DeviceContext*, ID3D11DepthStencilState* /*state*/, UINT /*stencil_ref*/) {}

void OnClearState(ID3D11DeviceContext*) {}

void OnFinishCommandList(ID3D11DeviceContext*, BOOL /*restore*/, ID3D11CommandList* /*list*/) {}

bool OnExecuteCommandList(ID3D11DeviceContext*, ID3D11CommandList* /*list*/, BOOL /*restore*/) { return false; }
} // namespace D3D11Events

// D3D12 events

namespace D3D12Events {
void OnInitDevice(ID3D12Device* /*device*/) {}

void OnDestroyDevice(ID3D12Device* /*device*/) {}

void OnCreateSwapchain(Game::SwapchainDesc& /*desc*/) {}

void OnInitSwapchain(IDXGISwapChain*, ID3D12CommandQueue* queue, bool /*resize*/)
{
   Microsoft::WRL::ComPtr<ID3D12Device> device;
   if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))))
      ::Examples::OnInitSwapchain(device.Get());
}
} // namespace D3D12Events

// D3D11 native callbacks

namespace D3D11Callbacks {
void PreCreateBlendState(ID3D11Device*, D3D11_BLEND_DESC1& /*desc*/) {}

void PostIASetVertexBuffers(ID3D11DeviceContext*, UINT /*start*/, UINT /*count*/, ID3D11Buffer* const* /*buffers*/, const UINT* /*strides*/, const UINT* /*offsets*/) {}

void PostIASetIndexBuffer(ID3D11DeviceContext*, ID3D11Buffer* /*buffer*/, DXGI_FORMAT /*format*/, UINT /*offset*/) {}

bool PreCopyStructureCount(ID3D11DeviceContext*, ID3D11Buffer* /*destination*/, UINT /*destination_offset*/, ID3D11UnorderedAccessView* /*source*/) { return false; }

void PostCreateDeferredContext(ID3D11Device*, ID3D11DeviceContext* /*context*/) {}

void OnDeferredContextDestroyed(ID3D11Device*, const ID3D11DeviceContext* /*context*/) {}
} // namespace D3D11Callbacks

// Bytecode patching

std::optional<std::vector<uint8_t>> PatchShader(ShaderStage /*stage*/, uint32_t /*hash*/, std::span<const uint8_t> /*code*/, std::span<const uint8_t> /*container*/)
{
   return std::nullopt;
}
} // namespace

void Register(Game::Options& options)
{
   Game::Shaders::SetDebugName("Example", EXAMPLE_HASHES);
   Game::Shaders::RedirectHashes("Example", EXAMPLE_HASHES);
   Game::Shaders::AddDefine({
      .name = "EXAMPLE_DEFINE",
      .label = "Example Define",
      .tooltip = "A shader define of the game.\n0 - Off\n1 - On",
      .default_value = 1,
      .range = Range{0, 1},
      .visibility = Visibility::Release,
   });

   Settings::Register(SETTINGS);
   Settings::AddChangeListener(&OnSettingChanged);

   options.cbuffers.update_luma_instance_data = &UpdateLumaInstanceData;
   options.device_data = Game::GameDeviceDataType::Of<GameDeviceData>();
   options.patching.patch_sync = nullptr;
   options.patching.patch_async = &PatchShader;

   Game::D3D11::Events d3d11_events = {
      .on_init_device = &D3D11Events::OnInitDevice,
      .on_destroy_device = &D3D11Events::OnDestroyDevice,
      .on_create_swapchain = &D3D11Events::OnCreateSwapchain,
      .on_init_swapchain = &D3D11Events::OnInitSwapchain,
      .on_create_shader = &D3D11Events::OnCreateShader,
      .on_create_buffer = &D3D11Events::OnCreateBuffer,
      .on_init_buffer = &D3D11Events::OnInitBuffer,
      .on_create_texture_2d = &D3D11Events::OnCreateTexture2D,
      .on_init_texture_2d = &D3D11Events::OnInitTexture2D,
      .on_resource_destroyed = &D3D11Events::OnResourceDestroyed,
      .on_init_shader_resource_view = &D3D11Events::OnInitShaderResourceView,
      .on_init_render_target_view = &D3D11Events::OnInitRenderTargetView,
      .on_init_unordered_access_view = &D3D11Events::OnInitUnorderedAccessView,
      .on_init_depth_stencil_view = &D3D11Events::OnInitDepthStencilView,
      .on_view_destroyed = &D3D11Events::OnViewDestroyed,
      .on_create_sampler_state = &D3D11Events::OnCreateSamplerState,
      .on_bind_render_targets = &D3D11Events::OnBindRenderTargets,
      .on_bind_render_targets_and_unordered_access_views = &D3D11Events::OnBindRenderTargetsAndUnorderedAccessViews,
      .on_map = &D3D11Events::OnMap,
      .on_unmap = &D3D11Events::OnUnmap,
      .on_unmapped = &D3D11Events::OnUnmapped,
      .on_update_subresource = &D3D11Events::OnUpdateSubresource,
      .on_copy_resource = &D3D11Events::OnCopyResource,
      .on_copy_subresource_region = &D3D11Events::OnCopySubresourceRegion,
      .on_resolve_subresource = &D3D11Events::OnResolveSubresource,
      .on_clear_render_target_view = &D3D11Events::OnClearRenderTargetView,
      .on_clear_view = &D3D11Events::OnClearView,
      .on_bind_constant_buffers = &D3D11Events::OnBindConstantBuffers,
      .on_bind_viewports = &D3D11Events::OnBindViewports,
      .on_bind_scissor_rects = &D3D11Events::OnBindScissorRects,
      .on_bind_blend_state = &D3D11Events::OnBindBlendState,
      .on_bind_depth_stencil_state = &D3D11Events::OnBindDepthStencilState,
      .on_clear_state = &D3D11Events::OnClearState,
      .on_variant_draw = &OnVariantDraw<Game::D3D11::Call>,
      .on_draw_or_dispatch = &OnDrawOrDispatch<Game::D3D11::Call>,
      .on_finish_command_list = &D3D11Events::OnFinishCommandList,
      .on_execute_command_list = &D3D11Events::OnExecuteCommandList,
   };
   Game::D3D12::Events d3d12_events = {
      .on_init_device = &D3D12Events::OnInitDevice,
      .on_destroy_device = &D3D12Events::OnDestroyDevice,
      .on_create_swapchain = &D3D12Events::OnCreateSwapchain,
      .on_init_swapchain = &D3D12Events::OnInitSwapchain,
      .on_variant_draw = &OnVariantDraw<Game::D3D12::Call>,
      .on_draw_or_dispatch = &OnDrawOrDispatch<Game::D3D12::Call>,
   };
   Game::D3D11::Callbacks d3d11_callbacks = {
      .pre_create_blend_state = &D3D11Callbacks::PreCreateBlendState,
      .post_ia_set_vertex_buffers = &D3D11Callbacks::PostIASetVertexBuffers,
      .post_ia_set_index_buffer = &D3D11Callbacks::PostIASetIndexBuffer,
      .pre_copy_structure_count = &D3D11Callbacks::PreCopyStructureCount,
      .post_create_deferred_context = &D3D11Callbacks::PostCreateDeferredContext,
      .on_deferred_context_destroyed = &D3D11Callbacks::OnDeferredContextDestroyed,
   };

   Game::D3D11::OnShaderDraw(EXAMPLE_HASHES, &OnExampleDrawD3D11);
   Game::D3D12::OnShaderDraw(EXAMPLE_HASHES, &OnExampleDrawD3D12);

   Game::SetEvents(d3d11_events);
   Game::SetEvents(d3d12_events);
   Game::SetCallbacks(d3d11_callbacks);

   Game::SetPanels({
      .settings = &DrawSettingsPanel,
      .info = &DrawInfoPanel,
      .about = &DrawAboutPanel,
      .development = &DrawDevelopmentPanel,
   });
}
#else
void Register(Game::Options&) {}
#endif
} // namespace Examples

// Runs under the loader lock: only declare options/hooks here (no DLL loads, threads, or D3D objects).
void Luma::Game::Init()
{
   SetInfo({
      // Defaults to the Visual Studio project name (LUMA_PROJECT_NAME) when left empty
      // .name = "$projectname$",
      .version = "1.0.0",
      .description = DESCRIPTION,
      .credits = CREDITS,
      .links = LINKS,
      // .api = Api::D3D11, // Restrict to D3D11 or D3D12 (unset handles both)
   });

   // Shaders::SetDebugName("Tonemap", SHADER_HASHES_TONEMAP);
   // Shaders::RedirectHashes("Tonemap", SHADER_HASHES_TONEMAP);

   // Shaders::AddDefine({.name = "TONEMAP_TYPE", .label = "Tonemap Type", .tooltip = "0 - Vanilla SDR\n1 - Luma HDR", .default_value = 1, .range = Settings::Range{0, 1}});
   // Settings::Register(SETTINGS);
   // Settings::AddChangeListener(&OnSettingChanged);

   // Override Core defaults when the game needs non-default behavior:
   // Settings::SetDefault(Settings::Core::REPORT_HDR, true);
   // Settings::SetDefault(Settings::Core::HDR_METADATA, true);
   // Settings::SetDefault(Settings::Core::OUTPUT_MODE, Settings::EnumIndex{1}); // 0 SDR, 1 HDR10, 2 scRGB
   // Settings::SetDefault(Settings::Core::SWAPCHAIN_FLIP_MODEL, true);
   // Settings::SetDefault(Settings::Core::SWAPCHAIN_TEARING, true);
   // Settings::SetDefault(Settings::Core::SUPPRESS_FOCUS_LOSS, false);
   // Settings::SetDefault(Settings::Core::LOAD_RESHADE, true);

   Options options = {
      .cbuffers = {
         .settings_slot = 13,
         .data_slot = 12,
         // .update_luma_instance_data = &UpdateLumaInstanceData,
         // .restore_after_draw = false,
      },
      // .draw = {.viewports = false, .blend = false, .depth = false},
      // .bindings = {.d3d12_space = 100},
      // .textures = {.load = false},
      // .samplers = {.upgrade = false, .upgrade_comparison = false, .force_upgrade_linear = false},
      // .patching = {.patch_sync = nullptr, .patch_async = nullptr},
      .swapchain = {
         .mode = Swapchain::Mode::Mirror, // Mirror, InPlace, or Native
         .hdr_output = Swapchain::OutputMode::Hdr10, // Sdr, Hdr10, or ScRgb
         // .borderless = true,
      },
      // ### Update display composition defaults for the game ###
      .composition = {
         .post_process_space = 0, // 0 Gamma, 1 Linear, 2 Linear until UI
         .early_display_encoding = false,
         .vanilla_encoding = 0, // 0 sRGB, 1 Gamma 2.2
         .gamma_correction = 1, // 0 sRGB, 1 Gamma 2.2, 2..4 hybrid
         .gamut_mapping = 0, // 0 None, 1 Auto, 2 SDR BT.709, 3 HDR BT.2020
         .ui_draw_type = 0, // 0 Raw, 1 Direct Custom, 2 Direct Inverse, 3 Separate
      },
      // .device_data = GameDeviceDataType::Of<GameDeviceData>(),
   };

   Examples::Register(options);
   SetOptions(options);

   // Texture format upgrades (16-bit float mirrors):
   // Resources::SetUpgrades({
   //    .formats = {
   //       DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_TYPELESS,
   //       DXGI_FORMAT_R11G11B10_FLOAT,
   //       // DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_TYPELESS,
   //       // DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_TYPELESS,
   //    },
   //    // .depth_formats = {},
   //    .size_filters = Resources::Upgrade::SizeFilters::SwapchainResolution | Resources::Upgrade::SizeFilters::SwapchainAspectRatio,
   //    // Also available: RenderResolution, RenderAspectRatio, CustomAspectRatio, CustomDimensions, All
   //    // .custom_aspect_ratios = {16.f / 9.f},
   //    // .custom_dimensions = {{1920, 1080}},
   //    // .aspect_ratio_pixel_threshold = 1,
   //    // .lut = {.size = UINT32_MAX, .dimensions = Resources::Upgrade::LutDimensions::_2D},
   // });

   // SetEvents(D3D11::Events{.on_init_swapchain = ..., .on_present = ...});
   // SetEvents(D3D12::Events{.on_init_swapchain = ..., .on_present = ...});
   // D3D11::OnShaderDraw(SHADER_HASHES_TONEMAP, &OnTonemapDraw);
   // D3D12::OnShaderDraw(SHADER_HASHES_TONEMAP, &OnTonemapDraw);
   // SetCallbacks(D3D11::Callbacks{...});
   // SetPanels({.settings = ..., .info = ..., .about = ..., .development = ...});
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
   return Luma::DllMain(module, reason, reserved);
}
