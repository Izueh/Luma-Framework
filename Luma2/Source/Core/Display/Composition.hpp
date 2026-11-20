#pragma once

#include <cstdint>
#include <optional>

#include "Common/Math.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Utils/Hash.hpp"

// Display composition (Guidelines.md section 12): the pass that writes the game's image into the real back buffer, ending with the
// encode for the output mode. The part every API shares: what a composition does this present, and the "LumaComposition" cbuffer its shader reads.
namespace Luma {
namespace Display {
namespace Composition {
inline constexpr uint64_t VERTEX_SHADER = Utils::Hash::Name("Luma_Copy_VS");
inline constexpr uint64_t PIXEL_SHADER = Utils::Hash::Name("Luma_DisplayComposition");

// CB::LumaCompositionData::OutputEncoding
enum class Encoding : uint32_t
{
   Linear, // scRGB
   Pq, // HDR10
   Srgb, // SDR on an 8 or 10 bit buffer
};

// What decides a composition, from the swapchain
struct Inputs
{
   Encoding encoding = Encoding::Linear;
   // The game asked for a linear back buffer (float or sRGB)
   bool game_linear = false;
   // Mirror: composition is the only writer of the real buffer, it always runs
   bool always = false;
   // Native: the real buffer is the game's own, nothing is composed
   bool off = false;
   // The game's image over the real buffer's size
   Math::float2 source_scale = {1.f, 1.f};
};

struct Pass
{
   Resources::Database::CustomData custom;
   CB::LumaCompositionData data = {};
};

// The stages of Guidelines.md section 12, each switchable on its own (a setting) and an identity when off. The encode for the output mode
// is the last and can't be switched off. Bits of CB::LumaCompositionData::Stages, as STAGE_* in the shader.
enum class Stage : uint32_t
{
   Decode, // The game's encoding to linear
   UI, // UI composite and its paper white
   GammaCorrection,
   GamutMapping,
   Scale, // From the game's size to the real buffer's
   DevelopmentViews, // Debug draw, SDR/HDR split
   Count,
};

// The "LumaComposition" cbuffer entry and the stages' settings. Init only, with Luma's other cbuffers, before the settings load.
void Register();
// Presenting thread: what this present's composition does, empty when nothing would change the image (in place only) or composition is off
std::optional<Pass> Plan(const Inputs& inputs);
// Presenting thread, before drawing `pass`: what its cbuffer is written with
void SetCurrent(const Pass& pass);
} // namespace Composition
} // namespace Display
} // namespace Luma
