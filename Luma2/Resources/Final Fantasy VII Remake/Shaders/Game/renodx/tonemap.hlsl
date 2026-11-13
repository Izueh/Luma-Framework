// ported from renodx 
// https://github.com/clshortfuse/renodx/blob/main/src/shaders/reno_drt.hlsl
// https://github.com/clshortfuse/renodx/blob/main/src/shaders/tonemap.hlsl
#ifndef SRC_SHADERS_RENO_DRT_HLSL_
#define SRC_SHADERS_RENO_DRT_HLSL_

#include "Includes/Color/Color.hlsl"
#include "Includes/Core/Math.hlsl"
#include "Includes/Tonemap/ACES.hlsl"


namespace renodx {
namespace tonemap {

struct Config {
  float type;
  float peak_nits;
  float game_nits;
  float gamma_correction;
  float exposure;
  float highlights;
  float shadows;
  float contrast;
  float saturation;
  float mid_gray_value;
  float mid_gray_nits;
  float reno_drt_highlights;
  float reno_drt_shadows;
  float reno_drt_contrast;
  float reno_drt_saturation;
  float reno_drt_dechroma;
  float reno_drt_flare;
  float hue_correction_type;
  float hue_correction_strength;
  float3 hue_correction_color;
  int reno_drt_hue_correction_method;
  int reno_drt_tone_map_method;
  int reno_drt_working_color_space;
  bool reno_drt_per_channel;
  float reno_drt_blowout;
  float reno_drt_clamp_color_space;
  float reno_drt_clamp_peak;
  float reno_drt_white_clip;
};

namespace config {
namespace type {
static const float VANILLA = 0.f;
static const float NONE = 1.f;
static const float ACES = 2.f;
static const float RENODRT = 3.f;
}  // namespace type

namespace hue_correction_type {
static const float INPUT = 0.f;
static const float CLAMPED = 1.f;
static const float CUSTOM = 2.f;
}  // namespace hue_correction_type

Config Create(
    float type = config::type::VANILLA,
    float peak_nits = 203.f,
    float game_nits = 203.f,
    float gamma_correction = 0,
    float exposure = 1.f,
    float highlights = 1.f,
    float shadows = 1.f,
    float contrast = 1.f,
    float saturation = 1.f,
    float mid_gray_value = 0.18f,
    float mid_gray_nits = 18.f,
    float reno_drt_highlights = 1.f,
    float reno_drt_shadows = 1.f,
    float reno_drt_contrast = 1.f,
    float reno_drt_saturation = 1.f,
    float reno_drt_dechroma = 0.5f,
    float reno_drt_flare = 0.f,
    float hue_correction_type = config::hue_correction_type::INPUT,
    float hue_correction_strength = 1.f,
    float3 hue_correction_color = 0,
    uint reno_drt_hue_correction_method = 0,
    uint reno_drt_tone_map_method = 0,
    uint reno_drt_working_color_space = 0u,
    bool reno_drt_per_channel = false,
    float reno_drt_blowout = 0,
    float reno_drt_clamp_color_space = 2.f,
    float reno_drt_clamp_peak = 1.f,
    float reno_drt_white_clip = 100.f) {
  const Config tm_config = {
    type,
    peak_nits,
    game_nits,
    gamma_correction,
    exposure,
    highlights,
    shadows,
    contrast,
    saturation,
    mid_gray_value,
    mid_gray_nits,
    reno_drt_highlights,
    reno_drt_shadows,
    reno_drt_contrast,
    reno_drt_saturation,
    reno_drt_dechroma,
    reno_drt_flare,
    hue_correction_type,
    hue_correction_strength,
    hue_correction_color,
    reno_drt_hue_correction_method,
    reno_drt_tone_map_method,
    reno_drt_working_color_space,
    reno_drt_per_channel,
    reno_drt_blowout,
    reno_drt_clamp_color_space,
    reno_drt_clamp_peak,
    reno_drt_white_clip
  };
  return tm_config;
}

float3 ApplyACES(float3 color, Config tm_config) {
  static const float ACES_MID_GRAY = 0.10f;
  static const float ACES_MIN = 0.0001f;
  const float mid_gray_scale = (tm_config.mid_gray_value / ACES_MID_GRAY);

  float aces_min = ACES_MIN / tm_config.game_nits;
  float aces_max = (tm_config.peak_nits / tm_config.game_nits);

  // [branch]
  // if (tm_config.gamma_correction != 0.f) {
  //   aces_max = renodx::color::correct::Gamma(
  //       aces_max,
  //       tm_config.gamma_correction > 0.f,
  //       abs(tm_config.gamma_correction) == 1.f ? 2.2f : 2.4f);
  //   aces_min = renodx::color::correct::Gamma(
  //       aces_min,
  //       tm_config.gamma_correction > 0.f,
  //       abs(tm_config.gamma_correction) == 1.f ? 2.2f : 2.4f);
  // } else {
  //   // noop
  // }
  aces_max /= mid_gray_scale;
  aces_min /= mid_gray_scale;

  color = ACES::RGCAndRRTAndODT(color, aces_min * 48.f, aces_max * 48.f);
  color /= 48.f;
  color *= mid_gray_scale;

  return color;
}

} // namespace config
}  // namespace tonemap
}  // namespace renodx

#endif  // SRC_SHADERS_RENO_DRT_HLSL_