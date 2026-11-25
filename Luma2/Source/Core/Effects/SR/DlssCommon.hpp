#pragma once

#include <cstdint>

#include "Effects/SR/SuperResolution.hpp"

struct NVSDK_NGX_Parameter;

namespace Luma {
namespace Effects {
namespace SR {
// Shared NGX DLSS parameter helpers (API-agnostic: no D3D11/D3D12 headers)
namespace DlssCommon {
inline constexpr const char* PROJECT_ID = "d8238c51-1f2f-438d-a309-38c16e33c716";
inline constexpr const char* ENGINE_VERSION = "1.0";

int SelectPerfQualityMode(NVSDK_NGX_Parameter* capabilities, const SettingsData& settings);
int BuildCreateFlags(const SettingsData& settings);
void ApplyRuntimeParams(NVSDK_NGX_Parameter* runtime_params, const SettingsData& settings);
int ComputeJitterPhases(const SettingsData& settings);
} // namespace DlssCommon
} // namespace SR
} // namespace Effects
} // namespace Luma
