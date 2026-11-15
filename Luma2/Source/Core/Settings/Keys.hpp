#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "Settings/Registry.hpp"

// The ids of every setting Core owns, the only place their keys are written. Games reach them through Luma.hpp.
namespace Luma {
namespace Settings {
namespace Core {
// Everything Luma does to the image, at once (Luma::IsEnabled)
inline constexpr Id<bool> ENABLED = {"Luma.Enabled"};
// Switches ENABLED
inline constexpr Id<Key> ENABLED_KEY = {"Luma.EnabledKey"};
// Opens and closes the overlay
inline constexpr Id<Key> OVERLAY_KEY = {"Luma.Overlay.Key"};
inline constexpr Id<float> OVERLAY_FONT_SIZE = {"Luma.Overlay.FontSize"};
// Of the overlay's window background, in percent
inline constexpr Id<float> OVERLAY_OPACITY = {"Luma.Overlay.Opacity"};
// The overlay's white on an HDR swapchain, in nits
inline constexpr Id<float> OVERLAY_HDR_PAPER_WHITE = {"Luma.Overlay.HdrPaperWhite"};
// The most background jobs (System::Jobs) that run at once
inline constexpr Id<int32_t> WORKER_THREADS = {"Luma.Advanced.WorkerThreads"};
// Luma's mirrors of the game's textures swapped in (Resources::Mirrors), Development
inline constexpr Id<bool> UPGRADES = {"Luma.Resources.Upgrades"};
// Luma's mirrors of the game's samplers swapped in (Resources::Samplers), Development
inline constexpr Id<bool> SAMPLERS = {"Luma.Resources.Samplers"};
// Development builds: replaces the sampler LOD bias offset (0 until SR sets one, Step 7), 1.0's custom_texture_mip_lod_bias_offset
inline constexpr Id<bool> SAMPLERS_CUSTOM_BIAS = {"Luma.Development.Samplers.CustomBias"};
inline constexpr Id<float> SAMPLERS_BIAS = {"Luma.Development.Samplers.Bias"};
// Compiles the shaders as soon as a define changes
inline constexpr Id<bool> AUTO_COMPILE = {"Luma.Shaders.AutoCompile"};
// Development and Test builds: writes the game's shaders to Luma/Dump
inline constexpr Id<bool> DUMP_SHADERS = {"Luma.Development.DumpShaders"};
// Development builds: D3D12's debug layer with GPU-based validation, from the game's next start
inline constexpr Id<bool> GPU_VALIDATION = {"Luma.Development.GpuValidation"};
// Development builds: DevSetting01 to 10 of LumaSettings
inline constexpr std::array<Id<float>, 10> DEV_SETTINGS = {{
   {"Luma.Development.Setting01"},
   {"Luma.Development.Setting02"},
   {"Luma.Development.Setting03"},
   {"Luma.Development.Setting04"},
   {"Luma.Development.Setting05"},
   {"Luma.Development.Setting06"},
   {"Luma.Development.Setting07"},
   {"Luma.Development.Setting08"},
   {"Luma.Development.Setting09"},
   {"Luma.Development.Setting10"},
}};
} // namespace Core
} // namespace Settings

namespace Shaders {
namespace Defines {
// One of Luma's shader defines: its setting and the name it's compiled with. Its key is "Defines.<name>", as a game's.
template <typename T>
struct Id : Settings::Id<T>
{
   std::string_view name;
};

// 1.0's shader_defines_data
inline constexpr Id<int32_t> POST_PROCESS_SPACE_TYPE = {{"Defines.POST_PROCESS_SPACE_TYPE"}, "POST_PROCESS_SPACE_TYPE"};
inline constexpr Id<bool> EARLY_DISPLAY_ENCODING = {{"Defines.EARLY_DISPLAY_ENCODING"}, "EARLY_DISPLAY_ENCODING"};
inline constexpr Id<int32_t> VANILLA_ENCODING_TYPE = {{"Defines.VANILLA_ENCODING_TYPE"}, "VANILLA_ENCODING_TYPE"};
inline constexpr Id<int32_t> GAMMA_CORRECTION_TYPE = {{"Defines.GAMMA_CORRECTION_TYPE"}, "GAMMA_CORRECTION_TYPE"};
inline constexpr Id<int32_t> GAMUT_MAPPING_TYPE = {{"Defines.GAMUT_MAPPING_TYPE"}, "GAMUT_MAPPING_TYPE"};
inline constexpr Id<int32_t> UI_DRAW_TYPE = {{"Defines.UI_DRAW_TYPE"}, "UI_DRAW_TYPE"};
// Development builds
inline constexpr Id<bool> TEST_2X_ZOOM = {{"Defines.TEST_2X_ZOOM"}, "TEST_2X_ZOOM"};
inline constexpr Id<int32_t> TEST_SDR_HDR_SPLIT_VIEW_MODE = {{"Defines.TEST_SDR_HDR_SPLIT_VIEW_MODE"}, "TEST_SDR_HDR_SPLIT_VIEW_MODE"};
inline constexpr Id<bool> TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL = {{"Defines.TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL"}, "TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL"};
} // namespace Defines
} // namespace Shaders
} // namespace Luma
