#include "Effects/SR/SuperResolution.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <mutex>
#include <string_view>

#include "Resources/LumaCBuffers.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#if DEVELOPMENT
#include "Trace/FrameCapture.hpp"
#endif

namespace Luma {
namespace Effects {
namespace SR {
namespace {
Options options;
std::mutex runtime_info_mutex; // Never held with another
RuntimeInfo runtime_info;

constexpr std::array<std::string_view, 6> USER_TYPE_LABELS = {"Off", "Auto", "DLSS", "FSR 3", "FSR 4", "XeSS"};
// Matches 1.0 DLSS render preset list (NVSDK_NGX_DLSS_Hint_Render_Preset_Default/E/F/J/K/L/M)
constexpr std::array<std::string_view, 7> DLSS_PRESET_LABELS = {"Default", "E (CNN)", "F (CNN)", "J", "K", "L", "M"};
constexpr std::array<uint32_t, 7> DLSS_PRESET_VALUES = {0u, 5u, 6u, 10u, 11u, 12u, 13u};
constexpr std::array<std::string_view, 3> AUTO_EXPOSURE_LABELS = {"Default", "Force Off", "Force On"};
} // namespace

bool SettingsData::operator==(const SettingsData& other) const
{
   return output_width == other.output_width && output_height == other.output_height && render_width == other.render_width &&
      render_height == other.render_height && dynamic_resolution == other.dynamic_resolution && hdr == other.hdr &&
      inverted_depth == other.inverted_depth && mvs_jittered == other.mvs_jittered && std::fabs(mvs_x_scale - other.mvs_x_scale) < FLT_EPSILON &&
      std::fabs(mvs_y_scale - other.mvs_y_scale) < FLT_EPSILON && auto_exposure == other.auto_exposure && render_preset == other.render_preset &&
      firefly_swatter == other.firefly_swatter;
}

float GetMipLODBias(float render_height, float output_height)
{
   if (render_height <= 0.f || output_height <= 0.f)
      return 0.f;
   return std::log2(render_height / std::max(render_height, output_height)) - 1.f;
}

float GetMipLODBias(Type type, float render_height, float output_height)
{
   const float bias = GetMipLODBias(render_height, output_height);
   return type == Type::XeSS ? (bias + 1.f) : bias;
}

Math::uint2 FindClosestIntegerResolutionForAspectRatio(double target_width, double target_height, double aspect_ratio)
{
   const uint32_t w = uint32_t(std::max<long long>(1ll, std::llround(target_width)));
   const uint32_t h = uint32_t(std::max<long long>(1ll, std::llround(target_height)));
   if (aspect_ratio <= 0.0)
      return {w, h};
   const uint32_t rounded_h = uint32_t(std::max<long long>(1ll, std::llround(double(w) / aspect_ratio)));
   return {w, rounded_h};
}

Type GetAutoType(const std::array<Capabilities, TYPE_COUNT>& backends)
{
   // Same auto preference order as 1.0: DLSS -> FSR 4 (AMD) -> XeSS (Intel) -> FSR 3
   constexpr std::array<Type, 4> ORDER = {Type::Dlss, Type::Fsr4, Type::XeSS, Type::Fsr3};
   for (const Type type : ORDER)
   {
      const Capabilities& cap = backends[size_t(type)];
      if (cap.supported && cap.auto_selectable)
         return type;
   }
   return Type::None;
}

Type ResolveActiveType(UserType user_type, const std::array<Capabilities, TYPE_COUNT>& backends)
{
   if (user_type == UserType::None)
      return Type::None;
   if (user_type == UserType::Auto)
      return GetAutoType(backends);
   Type requested = Type::None;
   switch (user_type)
   {
   case UserType::Dlss: requested = Type::Dlss; break;
   case UserType::Fsr3: requested = Type::Fsr3; break;
   case UserType::Fsr4: requested = Type::Fsr4; break;
   case UserType::XeSS: requested = Type::XeSS; break;
   default: return Type::None;
   }
   return backends[size_t(requested)].supported ? requested : Type::None;
}

SettingsData BuildSettingsData(const Options& opts, const Frame::Resolutions& resolutions, const DrawOverrides& overrides)
{
   const Math::uint2 output = overrides.output_size.value_or(resolutions.output);
   const Math::uint2 render = overrides.settings_render_size.value_or(overrides.render_size.value_or(resolutions.render));
   SettingsData data;
   data.output_width = std::max(1u, output.x);
   data.output_height = std::max(1u, output.y);
   data.render_width = std::max(1u, render.x);
   data.render_height = std::max(1u, render.y);
   data.dynamic_resolution = overrides.dynamic_resolution.value_or(false);
   data.hdr = opts.hdr;
   data.inverted_depth = opts.inverted_depth;
   data.mvs_jittered = opts.mvs_jittered;
   data.mvs_x_scale = opts.mvs_x_scale;
   data.mvs_y_scale = opts.mvs_y_scale;
   data.auto_exposure = opts.auto_exposure;
#if DEVELOPMENT
   const int32_t ae_override = Settings::Get(Settings::Core::SR_AUTO_EXPOSURE).value;
   if (ae_override == 1)
      data.auto_exposure = false;
   else if (ae_override == 2)
      data.auto_exposure = true;
#endif
   data.render_preset = GetDlssRenderPreset();
   data.firefly_swatter = opts.firefly_swatter;
   return data;
}

DrawParams BuildDrawParams(const Frame::Resolutions& resolutions, const Frame::Camera& camera, const Frame::Timing& timing, const DrawOverrides& overrides)
{
   const Math::uint2 render = overrides.render_size.value_or(resolutions.render);
   DrawParams params;
   params.render_width = std::max(1u, render.x);
   params.render_height = std::max(1u, render.y);
   params.jitter_x = camera.jitter_pixels.x;
   params.jitter_y = camera.jitter_pixels.y;
   params.vert_fov = camera.vert_fov;
   params.near_plane = camera.near_plane;
   params.far_plane = camera.far_plane;
   const float dev_pre_exposure = GetCustomPreExposure();
   params.pre_exposure = dev_pre_exposure > 0.f ? dev_pre_exposure : overrides.pre_exposure.value_or(camera.pre_exposure);
   params.time_delta = timing.time_delta;
   params.frame_index = timing.frame_index;
   params.reset = overrides.reset.value_or(camera.camera_cut);
   params.user_sharpness = overrides.user_sharpness.value_or(-1.f);
   params.fsr_velocity_factor = GetFsrVelocityFactor();
   params.debug_view = IsDebugViewEnabled();
   return params;
}

void SetOptions(const Options& value)
{
   options = value;
}

const Options& GetOptions()
{
   return options;
}

void Register()
{
   const std::array settings = {
      Settings::Setting{
         .key = Settings::Core::SR_TYPE.key,
         .label = "Super Resolution",
         .tooltip = "Temporal anti-aliasing and upscaling backend replacing the game's TAA.",
         .category = "Upscaling",
         .default_value = Settings::EnumIndex{uint32_t(UserType::Auto)},
         .enum_labels = USER_TYPE_LABELS,
         .is_visible = []() { return options.enable; },
         .is_option_enabled = [](int32_t index) {
            const RuntimeInfo info = GetRuntimeInfo();
            switch (UserType(index))
            {
            case UserType::None: return true;
            case UserType::Auto: return GetAutoType(info.backends) != Type::None;
            case UserType::Dlss: return info.backends[size_t(Type::Dlss)].supported;
            case UserType::Fsr3: return info.backends[size_t(Type::Fsr3)].supported;
            case UserType::Fsr4: return info.backends[size_t(Type::Fsr4)].supported;
            case UserType::XeSS: return info.backends[size_t(Type::XeSS)].supported;
            default: return false;
            } },
      },
      Settings::Setting{
         .key = Settings::Core::SR_DLSS_PRESET.key,
         .label = "DLSS Preset",
         .tooltip = "DLSS model preset hint (Default lets NVIDIA pick; E/F are legacy CNN, J/K/L/M are Transformer models).",
         .category = "Upscaling",
         .default_value = Settings::EnumIndex{0},
         .enum_labels = DLSS_PRESET_LABELS,
         .is_visible = []() { return options.enable && GetRuntimeInfo().active_type == Type::Dlss; },
      },
      Settings::Setting{
         .key = Settings::Core::SR_VELOCITY_FACTOR.key,
         .label = "FSR Velocity Factor",
         .tooltip = "FSR 3 temporal stability factor for bright pixels (0 = more stable, 1 = default).",
         .category = "Upscaling",
         .visibility = Settings::Visibility::Test,
         .default_value = 1.f,
         .range = Settings::Range{0.f, 1.f},
         .format = "%.2f",
         .is_visible = []() { return options.enable && GetRuntimeInfo().active_type == Type::Fsr3; },
      },
      Settings::Setting{
         .key = Settings::Core::SR_DEBUG_VIEW.key,
         .label = "FSR Debug View",
         .tooltip = "Makes FSR 3/4 draw its internal debug visualization in its output.",
         .category = "Upscaling",
         .visibility = Settings::Visibility::Development,
         .default_value = false,
         .is_visible = []() {
            const Type t = GetRuntimeInfo().active_type;
            return options.enable && (t == Type::Fsr3 || t == Type::Fsr4);
         },
      },
      Settings::Setting{
         .key = Settings::Core::SR_AUTO_EXPOSURE.key,
         .label = "Auto Exposure",
         .tooltip = "Overrides the game's SR auto-exposure flag (Default uses the game's setting).",
         .category = "Upscaling",
         .visibility = Settings::Visibility::Development,
         .default_value = Settings::EnumIndex{0},
         .enum_labels = AUTO_EXPOSURE_LABELS,
         .is_visible = []() { return options.enable && GetRuntimeInfo().active_type != Type::None; },
      },
      Settings::Setting{
         .key = Settings::Core::SR_PRE_EXPOSURE.key,
         .label = "Custom Pre-Exposure",
         .tooltip = "Overrides the pre-exposure passed to SR when > 0.",
         .category = "Upscaling",
         .visibility = Settings::Visibility::Development,
         .default_value = 0.f,
         .range = Settings::Range{0.f, 10.f},
         .format = "%.2f",
         .is_visible = []() { return options.enable && GetRuntimeInfo().active_type != Type::None; },
      },
   };
   Settings::Register(settings, Settings::Kind::Luma);
}

bool IsNeeded()
{
   return options.enable;
}

UserType GetUserType()
{
   const int32_t index = Settings::Get(Settings::Core::SR_TYPE).value;
   return (index >= 0 && index < int32_t(UserType::Count)) ? UserType(index) : UserType::None;
}

uint32_t GetDlssRenderPreset()
{
   const int32_t index = Settings::Get(Settings::Core::SR_DLSS_PRESET).value;
   return (index >= 0 && size_t(index) < DLSS_PRESET_VALUES.size()) ? DLSS_PRESET_VALUES[size_t(index)] : 0u;
}

float GetFsrVelocityFactor()
{
   return std::clamp(Settings::Get(Settings::Core::SR_VELOCITY_FACTOR), 0.f, 1.f);
}

bool IsDebugViewEnabled()
{
#if DEVELOPMENT
   return Settings::Get(Settings::Core::SR_DEBUG_VIEW);
#else
   return false;
#endif
}

float GetCustomPreExposure()
{
#if DEVELOPMENT
   return Settings::Get(Settings::Core::SR_PRE_EXPOSURE);
#else
   return 0.f;
#endif
}

Hooks::D3D12::Functions GetD3D12Functions()
{
   Hooks::D3D12::Functions functions;
   if (!IsNeeded())
      return functions;
   using enum Hooks::D3D12::Function;
   for (const Hooks::D3D12::Function fn : {CreateResource, CreateDescriptorHeap, CreateView, CopyDescriptors, SetDescriptorHeaps, SetRootSignature,
           SetRootArguments, SetPipelineState, OMSetRenderTargets, ResourceBarrier, Reset, ClearState})
      functions.set(size_t(fn));
   return functions;
}

void PublishRuntimeInfo(const RuntimeInfo& info)
{
   std::scoped_lock lock(runtime_info_mutex);
   runtime_info = info;
}

RuntimeInfo GetRuntimeInfo()
{
   std::scoped_lock lock(runtime_info_mutex);
   return runtime_info;
}

bool IsActive()
{
   return options.enable && GetUserType() != UserType::None && GetRuntimeInfo().active_type != Type::None;
}

void UpdateSamplerLodBias(IUnknown* device, Type active_type, bool taa_detected, uint32_t render_height, uint32_t output_height)
{
   if (active_type != Type::None && taa_detected && render_height > 0 && output_height > 0)
      Resources::Samplers::SetLodBiasOffset(device, GetMipLODBias(active_type, float(render_height), float(output_height)));
   else
      Resources::Samplers::SetLodBiasOffset(device, 0.f);
}

void PublishRecord(const Record& record)
{
   PublishRuntimeInfo({.enabled = IsNeeded(),
      .user_type = GetUserType(),
      .active_type = record.active_type,
      .drew_this_frame = record.drew_this_frame,
      .backends = record.capabilities,
      .settings = record.last_settings,
      .last_draw = record.last_draw,
      .jitter_phases = record.last_jitter_phases});
}

void ChooseActiveType(Record& record)
{
   record.active_type = ResolveActiveType(GetUserType(), record.capabilities);
   PublishRecord(record);
}

void UpdateAtPresent(Record& record, IUnknown* device)
{
   const Type next_type = ResolveActiveType(GetUserType(), record.capabilities);
   if (next_type != record.active_type)
   {
      record.active_type = next_type;
      record.force_reset = true;
   }
   const uint32_t sr_type_value = uint32_t(record.active_type == Type::None ? -1 : int32_t(record.active_type));
   if (Resources::LumaCBuffers::cb_luma_global_settings.SRType != sr_type_value)
   {
      Resources::LumaCBuffers::cb_luma_global_settings.SRType = sr_type_value;
      Resources::LumaCBuffers::MarkSettingsDirty();
   }
   const Frame::Resolutions resolutions = Frame::Get(device);
   UpdateSamplerLodBias(device, record.active_type, Frame::HasPass(device, Frame::PassMarker::Taa), resolutions.render.y, resolutions.output.y);
   // Before the draw is forgotten: the dev tools see the frame that just ended
   PublishRecord(record);
   record.drew_this_frame = false;
}

std::optional<Prepared> PrepareDraw(const Record& record, IUnknown* device, const DrawOverrides& overrides)
{
   if (record.active_type == Type::None || record.active_type == Type::Count)
      return std::nullopt;
   const Frame::Resolutions resolutions = Frame::Get(device);
   Prepared prepared = {BuildSettingsData(GetOptions(), resolutions, overrides),
      BuildDrawParams(resolutions, Frame::GetCamera(device), Frame::GetTiming(device), overrides)};
   prepared.params.reset |= record.force_reset;
   const uint32_t min_res = record.capabilities[size_t(record.active_type)].min_resolution;
   if (prepared.settings.output_width < min_res || prepared.settings.output_height < min_res || prepared.params.render_width < min_res ||
      prepared.params.render_height < min_res)
      return std::nullopt;
   return prepared;
}

void FinishDraw(Record& record, IUnknown* device, const Prepared& prepared, bool succeeded)
{
   if (!succeeded)
   {
      record.force_reset = true;
      return;
   }
   record.force_reset = false;
   record.drew_this_frame = true;
   record.last_settings = prepared.settings;
   record.last_draw = prepared.params;
   Frame::MarkPass(device, Frame::PassMarker::Sr);
   PublishRecord(record);
}

#if DEVELOPMENT
Trace::Capture::SrInfo DescribeDraw(const Record& record, const Prepared& prepared, bool succeeded, bool copy_back)
{
   const SettingsData& settings = prepared.settings;
   const DrawParams& params = prepared.params;
   return {
      .backend = std::string(GetTypeName(record.active_type)),
      .provider_version = record.capabilities[size_t(record.active_type)].provider_version,
      .render_width = params.render_width,
      .render_height = params.render_height,
      .output_width = settings.output_width,
      .output_height = settings.output_height,
      .dynamic_resolution = settings.dynamic_resolution,
      .hdr = settings.hdr,
      .inverted_depth = settings.inverted_depth,
      .auto_exposure = settings.auto_exposure,
      .mvs_jittered = settings.mvs_jittered,
      .reset = params.reset,
      .succeeded = succeeded,
      .copy_back = copy_back,
      .jitter_x = params.jitter_x,
      .jitter_y = params.jitter_y,
      .jitter_phases = record.last_jitter_phases,
      .vert_fov = params.vert_fov,
      .near_plane = params.near_plane,
      .far_plane = params.far_plane,
      .pre_exposure = params.pre_exposure,
      .mvs_x_scale = settings.mvs_x_scale,
      .mvs_y_scale = settings.mvs_y_scale,
      .sharpness = params.user_sharpness,
      .velocity_factor = params.fsr_velocity_factor,
   };
}
#endif
} // namespace SR
} // namespace Effects
} // namespace Luma
