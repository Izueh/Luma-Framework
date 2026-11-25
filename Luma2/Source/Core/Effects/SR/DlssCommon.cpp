#include "Effects/SR/DlssCommon.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#if defined(LUMA_USE_NGX) && LUMA_USE_NGX
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#endif

namespace Luma {
namespace Effects {
namespace SR {
namespace DlssCommon {
int SelectPerfQualityMode([[maybe_unused]] NVSDK_NGX_Parameter* capabilities, [[maybe_unused]] const SettingsData& settings)
{
#if defined(LUMA_USE_NGX) && LUMA_USE_NGX
   constexpr int NUM_PERF_QUALITY_MODES = 6;
   int quality_mode = int(NVSDK_NGX_PerfQuality_Value_Balanced);
   uint32_t best_delta = (std::numeric_limits<uint32_t>::max)();
   for (int i = 0; i < NUM_PERF_QUALITY_MODES; ++i)
   {
      unsigned int optimal_w = 0, optimal_h = 0, min_w = 0, max_w = 0, min_h = 0, max_h = 0;
      float sharpness = 0.f;
      const auto value = static_cast<NVSDK_NGX_PerfQuality_Value>(i);
      const NVSDK_NGX_Result res =
         NGX_DLSS_GET_OPTIMAL_SETTINGS(capabilities, settings.output_width, settings.output_height, value, &optimal_w, &optimal_h, &max_w, &max_h, &min_w, &min_h, &sharpness);
      if (NVSDK_NGX_SUCCEED(res) && optimal_w != 0 && optimal_h != 0)
      {
         if (value == NVSDK_NGX_PerfQuality_Value_DLAA)
         {
            optimal_w = max_w = settings.output_width;
            optimal_h = max_h = settings.output_height;
         }
         else if (max_w == 0 || max_h == 0)
         {
            max_w = optimal_w;
            max_h = optimal_h;
         }
         const uint32_t delta = uint32_t(std::abs(int(settings.render_width) - int(optimal_w)) + std::abs(int(settings.render_height) - int(optimal_h)));
         const bool in_range = settings.render_width >= min_w && settings.render_width <= max_w && settings.render_height >= min_h && settings.render_height <= max_h;
         if (!settings.dynamic_resolution && optimal_w == settings.render_width && optimal_h == settings.render_height)
         {
            quality_mode = i;
            break;
         }
         if (in_range && delta < best_delta)
         {
            quality_mode = i;
            best_delta = delta;
         }
      }
   }
   if ((!settings.dynamic_resolution || best_delta == (std::numeric_limits<uint32_t>::max)()) && settings.render_width >= settings.output_width &&
      settings.render_height >= settings.output_height)
      quality_mode = int(NVSDK_NGX_PerfQuality_Value_DLAA);
   return quality_mode;
#else
   return 0;
#endif
}

int BuildCreateFlags([[maybe_unused]] const SettingsData& settings)
{
#if defined(LUMA_USE_NGX) && LUMA_USE_NGX
   return NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | (settings.inverted_depth ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0) |
      (settings.mvs_jittered ? NVSDK_NGX_DLSS_Feature_Flags_MVJittered : 0) | (settings.auto_exposure ? NVSDK_NGX_DLSS_Feature_Flags_AutoExposure : 0) |
      (settings.hdr ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0);
#else
   return 0;
#endif
}

void ApplyRuntimeParams([[maybe_unused]] NVSDK_NGX_Parameter* runtime_params, [[maybe_unused]] const SettingsData& settings)
{
#if defined(LUMA_USE_NGX) && LUMA_USE_NGX
   const auto preset = static_cast<NVSDK_NGX_DLSS_Hint_Render_Preset>(settings.render_preset);
   NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, preset);
   NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, preset);
   NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, preset);
   NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, preset);
   NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, preset);
   NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, preset);
   if (settings.firefly_swatter)
      NVSDK_NGX_Parameter_SetUI(runtime_params, NVSDK_NGX_EParameter_Hint_UseFireflySwatter, 1);
#endif
}

int ComputeJitterPhases(const SettingsData& settings)
{
   if (settings.render_height == 0)
      return GetDefaultJitterPhases();
   const float ratio = std::max(float(settings.output_height) / float(settings.render_height), 1.f);
   return int(std::lrintf(float(GetDefaultJitterPhases()) * ratio * ratio));
}
} // namespace DlssCommon
} // namespace SR
} // namespace Effects
} // namespace Luma
