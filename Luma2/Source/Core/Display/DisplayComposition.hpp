#pragma once

#include <cstdint>
#include <optional>

#include "Common/Math.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Utils/Hash.hpp"

// Display composition: the pass that writes the game's image into the real back buffer, ending with the
// encode for the output mode, plus the runtime composition settings stored in LumaSettings.DisplayComposition.
namespace Luma {
namespace Display {
namespace Composition {
inline constexpr uint64_t VERTEX_SHADER = Utils::Hash::Name("Luma_Copy_VS");
inline constexpr uint64_t PIXEL_SHADER = Utils::Hash::Name("Luma_DisplayComposition");

// Game::Options::composition: default pipeline space and composition parameters for a game.
struct Options
{
   int32_t post_process_space = 0; // 0 Gamma, 1 Linear, 2 Linear until UI
   bool early_display_encoding = false;
   int32_t vanilla_encoding = 0; // 0 sRGB, 1 Gamma 2.2
   int32_t gamma_correction = 1; // 0 sRGB, 1 Gamma 2.2, 2..4 hybrid modes
   int32_t gamut_mapping = 0; // 0 None, 1 Auto, 2 SDR BT.709, 3 HDR BT.2020
   int32_t ui_draw_type = 0; // 0 Raw, 1 Direct Custom, 2 Direct Inverse, 3 Separate
};

// CB::LumaCompositionData::OutputEncoding
enum class Encoding : uint32_t
{
   Linear, // scRGB
   Pq, // HDR10
   Srgb, // SDR on an 8 or 10 bit buffer (gamma 2.2 encoded)
};

// What decides a composition, from the swapchain
struct Inputs
{
   Encoding encoding = Encoding::Linear;
   // The game asked for a linear back buffer (float or sRGB)
   bool game_linear = false;
   // The game set an HDR10 PQ color space on its swapchain
   bool game_pq = false;
   // Mirror: composition is the only writer of the real buffer, it always runs
   bool always = false;
   // Native: the real buffer is the game's own, nothing is composed
   bool off = false;
   // Active development debug-draw view requires composition
   bool debug_draw_active = false;
   // The game's image over the real buffer's size
   Math::float2 source_scale = {1.f, 1.f};
};

struct Pass
{
   Resources::Database::CustomData custom;
   CB::LumaCompositionData data = {};
};

// Each composition stage can be toggled off independently in Development and becomes an identity when off.
// The output encode is always the final step. Bits match STAGE_* in Luma_DisplayComposition.hlsl.
enum class Stage : uint32_t
{
   Decode, // The game's encoding to linear
   UI, // UI composite and its paper white
   GammaCorrection,
   GamutMapping,
   Scale, // From the game's size to the real buffer's
   DevelopmentViews, // Debug draw, SDR/HDR split, 2x zoom
   Count,
};

// Init only (Game::SetOptions)
void SetOptions(const Options& options);
const Options& GetOptions();
// Registers the composition settings before Game::Init so the game can override defaults
void RegisterSettings();
// Registers the "LumaComposition" cbuffer database entry after Game::Init
void RegisterResources();
// Presenting thread: what this present's composition does, empty when nothing would change the image (in place only) or composition is off
std::optional<Pass> Plan(const Inputs& inputs);
// Presenting thread, before drawing `pass`: what its cbuffer is written with
void SetCurrent(const Pass& pass);
// Whether Luma is enabled and at least one game shader variant is loaded
bool IsModActive();
} // namespace Composition
} // namespace Display
} // namespace Luma
