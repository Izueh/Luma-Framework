#pragma once

#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Common/Device.hpp"
#include "Common/Math.hpp"
#include "Frame/FrameState.hpp"
#include "Hooks/HookFunctions.hpp"
#include "Resources/ResourceDatabase.hpp"

#if DEVELOPMENT
namespace Luma::Trace::Capture {
struct SrInfo;
}
#endif

namespace Luma {
namespace Effects {
namespace SR {
enum class UserType : uint8_t
{
   None = 0,
   Auto,
   Dlss,
   Fsr3,
   Fsr4,
   XeSS,
   Count,
};

// Matches CB::LumaGlobalSettings::SRType in shaders (do not reorder)
enum class Type : int32_t
{
   None = -1,
   Dlss = 0,
   Fsr3 = 1,
   Fsr4 = 2,
   XeSS = 3,
   Count = 4,
};

constexpr size_t TYPE_COUNT = size_t(Type::Count);

constexpr std::string_view GetTypeName(Type type)
{
   switch (type)
   {
   case Type::Dlss: return "DLSS";
   case Type::Fsr3: return "FSR 3";
   case Type::Fsr4: return "FSR 4";
   case Type::XeSS: return "XeSS";
   default: return "None";
   }
}

constexpr bool AreTypesEqual(UserType user_type, Type type)
{
   switch (user_type)
   {
   case UserType::None: return type == Type::None;
   case UserType::Auto: return false;
   case UserType::Dlss: return type == Type::Dlss;
   case UserType::Fsr3: return type == Type::Fsr3;
   case UserType::Fsr4: return type == Type::Fsr4;
   case UserType::XeSS: return type == Type::XeSS;
   default: return false;
   }
}

struct Options
{
   bool enable = false;
   bool hdr = true;
   bool inverted_depth = false;
   bool mvs_jittered = false;
   bool auto_exposure = false;
   float mvs_x_scale = 1.f;
   float mvs_y_scale = 1.f;
   bool firefly_swatter = false;
};

struct SettingsData
{
   uint32_t output_width = 1;
   uint32_t output_height = 1;
   uint32_t render_width = 1;
   uint32_t render_height = 1;
   bool dynamic_resolution = false;
   bool hdr = true;
   bool inverted_depth = false;
   bool mvs_jittered = false;
   float mvs_x_scale = 1.f;
   float mvs_y_scale = 1.f;
   bool auto_exposure = false;
   uint32_t render_preset = 0;
   bool firefly_swatter = false;

   bool operator==(const SettingsData& other) const;
};

struct Capabilities
{
   bool initialized = false;
   bool supported = false;
   bool auto_selectable = true;
   uint32_t min_resolution = 32;
   std::string provider_version;
};

struct DrawParams
{
   uint32_t render_width = 0;
   uint32_t render_height = 0;
   float jitter_x = 0.f;
   float jitter_y = 0.f;
   float vert_fov = 0.f;
   float near_plane = 0.01f;
   float far_plane = 1000.f;
   float pre_exposure = 0.f;
   float time_delta = 1.f / 60.f;
   uint64_t frame_index = 0;
   bool reset = false;
   float user_sharpness = -1.f;
   float fsr_velocity_factor = 1.f;
   bool debug_view = false;
};

struct DrawOverrides
{
   std::optional<Math::uint2> output_size;
   std::optional<Math::uint2> render_size;
   std::optional<Math::uint2> settings_render_size; // Midpoint render size for DRS
   std::optional<bool> dynamic_resolution;
   std::optional<bool> reset;
   std::optional<float> pre_exposure;
   std::optional<float> user_sharpness;
};

template <typename ResourceT>
struct DrawInputs
{
   ResourceT* output_color = nullptr;
   ResourceT* source_color = nullptr;
   ResourceT* motion_vectors = nullptr;
   ResourceT* depth_buffer = nullptr;
   ResourceT* exposure = nullptr;
   ResourceT* bias_mask = nullptr;
   ResourceT* transparency_alpha = nullptr;
};

// Compile-time contract satisfied by every per-API SR backend State struct (no virtual inheritance)
template <typename StateT, typename ContextT, typename ResourceT>
concept SrBackend = requires(StateT& state, ContextT* ctx, const SettingsData& settings, const DrawInputs<ResourceT>& inputs, const DrawParams& params) {
   { state.capabilities } -> std::convertible_to<const Capabilities&>;
   { UpdateSettings(state, ctx, settings) } -> std::same_as<bool>;
   { Evaluate(state, ctx, inputs, params) } -> std::same_as<bool>;
   { GetJitterPhases(state) } -> std::same_as<int>;
   { ReleaseResources(state) } -> std::same_as<void>;
};

struct RuntimeInfo
{
   bool enabled = false;
   UserType user_type = UserType::None;
   Type active_type = Type::None;
   bool drew_this_frame = false;
   bool suppressed = false;
   std::array<Capabilities, TYPE_COUNT> backends = {};
   SettingsData settings = {};
   DrawParams last_draw = {};
   int jitter_phases = 8;
};

// Pure helpers
constexpr int GetDefaultJitterPhases()
{
   return 8;
}

constexpr float HaltonSequence(uint32_t index, uint32_t base)
{
   index += 1;
   float result = 0.f;
   const float inv_base = 1.f / float(base);
   float fraction = inv_base;
   while (index > 0)
   {
      result += float(index % base) * fraction;
      index /= base;
      fraction *= inv_base;
   }
   return result - 0.5f;
}

// A device's choice of provider and what its draws did, the same on both APIs. Each API keeps its providers' states and native
// resources itself.
struct Record
{
   std::array<Capabilities, TYPE_COUNT> capabilities = {};
   Type active_type = Type::None;
   bool drew_this_frame = false;
   // A provider change or a failed draw: the next draw that succeeds starts the provider's history again
   bool force_reset = false;
   SettingsData last_settings;
   DrawParams last_draw;
   int last_jitter_phases = GetDefaultJitterPhases();
};

struct Prepared
{
   SettingsData settings;
   DrawParams params;
};

// Each input by its Resources::Database key, in this order, through `resolve(key)`
template <typename Resource, typename Resolve>
DrawInputs<Resource> GatherInputs(Resolve&& resolve)
{
   namespace Keys = Resources::Database::Sr;
   DrawInputs<Resource> inputs;
   inputs.source_color = resolve(Keys::COLOR);
   inputs.depth_buffer = resolve(Keys::DEPTH);
   inputs.motion_vectors = resolve(Keys::MOTION_VECTORS);
   inputs.output_color = resolve(Keys::OUTPUT);
   inputs.exposure = resolve(Keys::EXPOSURE);
   inputs.bias_mask = resolve(Keys::REACTIVE);
   inputs.transparency_alpha = resolve(Keys::TRANSPARENCY);
   return inputs;
}

template <typename Resource>
bool HasRequiredInputs(const DrawInputs<Resource>& inputs)
{
   return inputs.source_color && inputs.depth_buffer && inputs.motion_vectors && inputs.output_color;
}

void PublishRecord(const Record& record);
// Once the providers are initialized
void ChooseActiveType(Record& record);
// At the device's present: the user's choice applied, SRType and the sampler LOD bias set, the record published, the frame's draw
// forgotten
void UpdateAtPresent(Record& record, IUnknown* device);
// Empty without an active provider, or below its minimum size
std::optional<Prepared> PrepareDraw(const Record& record, IUnknown* device, const DrawOverrides& overrides);
void FinishDraw(Record& record, IUnknown* device, const Prepared& prepared, bool succeeded);
#if DEVELOPMENT
Trace::Capture::SrInfo DescribeDraw(const Record& record, const Prepared& prepared, bool succeeded, bool copy_back);
#endif

float GetMipLODBias(float render_height, float output_height);
float GetMipLODBias(Type type, float render_height, float output_height);
Math::uint2 FindClosestIntegerResolutionForAspectRatio(double target_width, double target_height, double aspect_ratio);

Type GetAutoType(const std::array<Capabilities, TYPE_COUNT>& backends);
Type ResolveActiveType(UserType user_type, const std::array<Capabilities, TYPE_COUNT>& backends);
SettingsData BuildSettingsData(const Options& options, const Frame::Resolutions& resolutions, const DrawOverrides& overrides);
DrawParams BuildDrawParams(const Frame::Resolutions& resolutions, const Frame::Camera& camera, const Frame::Timing& timing, const DrawOverrides& overrides);

void SetOptions(const Options& options);
const Options& GetOptions();
void Register();
bool IsNeeded();
UserType GetUserType();
uint32_t GetDlssRenderPreset();
float GetFsrVelocityFactor();
bool IsDebugViewEnabled();
float GetCustomPreExposure();

Hooks::D3D12::Functions GetD3D12Functions();

// Runtime status snapshot (any thread) for UI, luma_status, and luma_sr_state
void PublishRuntimeInfo(const RuntimeInfo& info);
RuntimeInfo GetRuntimeInfo();

// Convenience query for game settings predicates (e.g. FF7R FXRCAS visibility)
bool IsActive();

// Updates sampler LOD bias offset from current SR type and resolutions (unless Development custom bias is active)
void UpdateSamplerLodBias(IUnknown* device, Type active_type, bool taa_detected, uint32_t render_height, uint32_t output_height);
} // namespace SR
} // namespace Effects
} // namespace Luma
