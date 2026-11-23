#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "Settings/SettingsRegistry.hpp"

// The ids of every setting Core owns, the only place their keys are written. Games reach them through Luma.hpp.
namespace Luma {
namespace Settings {
namespace Core {
// Everything Luma does to the image, at once (Settings::IsEnabled)
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
// What the real swapchain presents (Swapchain::OutputMode, Auto first): applied at the next present
inline constexpr Id<Settings::EnumIndex> OUTPUT_MODE = {"Luma.Display.OutputMode"};
// LumaSettings' white levels in HDR, in nits: a peak of 0 is the display's
inline constexpr Id<float> PEAK_WHITE = {"Luma.Display.PeakWhite"};
inline constexpr Id<float> PAPER_WHITE = {"Luma.Display.PaperWhite"};
inline constexpr Id<float> UI_PAPER_WHITE = {"Luma.Display.UIPaperWhite"};
// Test builds: SDR content presented in the HDR display mode
inline constexpr Id<bool> SDR_ON_HDR = {"Luma.Display.SdrOnHdr"};
// Display composition pipeline & stage settings (written live to LumaSettings.DisplayComposition)
inline constexpr Id<Settings::EnumIndex> COMPOSITION_GAMMA_CORRECTION = {"Luma.Display.GammaCorrectionType"};
inline constexpr Id<Settings::EnumIndex> COMPOSITION_POST_PROCESS_SPACE = {"Luma.Development.Composition.PostProcessSpaceType"};
inline constexpr Id<bool> COMPOSITION_EARLY_DISPLAY_ENCODING = {"Luma.Development.Composition.EarlyDisplayEncoding"};
inline constexpr Id<Settings::EnumIndex> COMPOSITION_VANILLA_ENCODING = {"Luma.Development.Composition.VanillaEncodingType"};
inline constexpr Id<Settings::EnumIndex> COMPOSITION_GAMUT_MAPPING = {"Luma.Development.Composition.GamutMappingType"};
inline constexpr Id<Settings::EnumIndex> COMPOSITION_UI_DRAW_TYPE = {"Luma.Development.Composition.UIDrawType"};
inline constexpr Id<bool> COMPOSITION_TEST_2X_ZOOM = {"Luma.Development.Composition.Test2xZoom"};
inline constexpr Id<Settings::EnumIndex> COMPOSITION_SPLIT_VIEW_MODE = {"Luma.Development.Composition.SplitViewMode"};
inline constexpr Id<bool> COMPOSITION_SPLIT_VIEW_NATIVE = {"Luma.Development.Composition.SplitViewNativeImpl"};
// Development builds: display composition's stage bypasses (Display::Composition::Stage, in its order)
inline constexpr std::array<Id<bool>, 6> COMPOSITION_STAGES = {{
   {"Luma.Development.Composition.StageDecode"},
   {"Luma.Development.Composition.StageUI"},
   {"Luma.Development.Composition.StageGammaCorrection"},
   {"Luma.Development.Composition.StageGamutMapping"},
   {"Luma.Development.Composition.StageScale"},
   {"Luma.Development.Composition.StageDevelopmentViews"},
}};
// Test builds: the game is told the display is in HDR (Display::Spoofing), a game's default
inline constexpr Id<bool> REPORT_HDR = {"Luma.Display.ReportHdr"};
// HDR10 metadata (the display's peak) on an HDR10 swapchain
inline constexpr Id<bool> HDR_METADATA = {"Luma.Display.HdrMetadata"};
// Test builds: Swapchain::Mode (the game's choice first), from the game's next swapchain
inline constexpr Id<Settings::EnumIndex> SWAPCHAIN_MODE = {"Luma.Swapchain.Mode"};
// Test builds: the real swapchain uses the flip model (HDR needs it), from the game's next swapchain
inline constexpr Id<bool> SWAPCHAIN_FLIP_MODEL = {"Luma.Swapchain.FlipModel"};
// Test builds: tearing allowed (VRR, uncapped frame rates in a window), from the game's next swapchain
inline constexpr Id<bool> SWAPCHAIN_TEARING = {"Luma.Swapchain.Tearing"};
// Test builds: the game gets a proxy of the swapchain (Hooks::SwapchainProxy), from the game's next swapchain
inline constexpr Id<bool> SWAPCHAIN_WRAP = {"Luma.Swapchain.Wrap"};
// Test builds: exclusive fullscreen replaced by a borderless window (the game's choice first)
inline constexpr Id<Settings::EnumIndex> SWAPCHAIN_FULLSCREEN = {"Luma.Swapchain.Fullscreen"};
// Development builds: the real swapchain's size over the game's (composition scales), from the game's next swapchain or resize
inline constexpr Id<float> SWAPCHAIN_SCALE = {"Luma.Development.SwapchainScale"};
// Test builds: "ReShade64.dll" next to Luma is loaded, from the game's next start
inline constexpr Id<bool> LOAD_RESHADE = {"Luma.ReShade.Load"};
// Test builds: Luma registers as a ReShade add-on, so its present work runs before ReShade's effects, from the game's next start
inline constexpr Id<bool> RESHADE_BRIDGE = {"Luma.ReShade.Bridge"};
// The most background jobs (System::Jobs) that run at once
inline constexpr Id<int32_t> WORKER_THREADS = {"Luma.Advanced.WorkerThreads"};
// Luma's mirrors of the game's textures swapped in (Resources::Mirrors), Development
inline constexpr Id<bool> UPGRADES = {"Luma.Resources.Upgrades"};
// Luma's mirrors of the game's samplers swapped in (Resources::Samplers), Development
inline constexpr Id<bool> SAMPLERS = {"Luma.Resources.Samplers"};
// Development builds: replaces the sampler LOD bias offset (0 until SR sets one)
inline constexpr Id<bool> SAMPLERS_CUSTOM_BIAS = {"Luma.Development.Samplers.CustomBias"};
inline constexpr Id<float> SAMPLERS_BIAS = {"Luma.Development.Samplers.Bias"};
// Compiles the shaders as soon as a define changes
inline constexpr Id<bool> AUTO_COMPILE = {"Luma.Shaders.AutoCompile"};
// Development and Test builds: writes the game's shaders to Luma/Dump
inline constexpr Id<bool> DUMP_SHADERS = {"Luma.Development.DumpShaders"};
// Development builds: D3D11/D3D12 debug layer, from the game's next start
inline constexpr Id<bool> DEBUG_LAYER = {"Luma.Development.DebugLayer"};
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
} // namespace Defines
} // namespace Shaders
} // namespace Luma
