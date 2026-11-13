#pragma once

#include <cstdint>

#include "Common/Api.hpp"
#include "Hooks/Functions.hpp"

// Saving and restoring a context's state around Luma's own passes, what the game bound (recorded at bind), and the passes
// themselves. The API-agnostic part: which state, and what's recorded.
namespace Luma {
namespace Draw {
constexpr uint8_t GetStageBit(ShaderStage stage)
{
   return uint8_t(1u << uint32_t(stage));
}

inline constexpr uint8_t GRAPHICS_STAGES = GetStageBit(ShaderStage::Vertex) | GetStageBit(ShaderStage::Hull) | GetStageBit(ShaderStage::Domain) |
   GetStageBit(ShaderStage::Geometry) | GetStageBit(ShaderStage::Pixel);
inline constexpr uint8_t COMPUTE_STAGES = GetStageBit(ShaderStage::Compute);

struct Range
{
   uint32_t first = 0;
   uint32_t count = 0;
};

// The state a StateStack saves and restores. 1.0's DrawStateStack had three fixed modes, kept as presets below; a pass names
// the groups it changes, so nothing it doesn't touch is copied. Stage masks are GetStageBit() bits.
struct StateMask
{
   uint8_t shaders = 0;
   uint8_t shader_resources = 0;
   Range shader_resource_slots = {0, 128};
   uint8_t constant_buffers = 0;
   Range constant_buffer_slots = {0, 14};
   uint8_t samplers = 0;
   Range sampler_slots = {0, 16};
   // The pixel stage's bit is the output merger's (saved with the targets), the compute stage's is the compute shader's
   uint8_t unordered_access = 0;
   bool targets = false; // Render targets and depth stencil
   bool blend = false;
   bool depth = false; // The depth stencil state and stencil reference
   bool rasterizer = false;
   bool viewports = false; // And scissor rects
   bool input_assembler = false; // Topology and input layout
};

inline constexpr uint8_t VERTEX_AND_PIXEL = GetStageBit(ShaderStage::Vertex) | GetStageBit(ShaderStage::Pixel);
inline constexpr uint8_t PIXEL = GetStageBit(ShaderStage::Pixel);

// 1.0's SimpleGraphics: the first 3 pixel shader resources and the first sampler, no unordered access views
inline constexpr StateMask SIMPLE_GRAPHICS = {.shaders = VERTEX_AND_PIXEL,
   .shader_resources = PIXEL,
   .shader_resource_slots = {0, 3},
   .constant_buffers = VERTEX_AND_PIXEL,
   .samplers = PIXEL,
   .sampler_slots = {0, 1},
   .targets = true,
   .blend = true,
   .depth = true,
   .rasterizer = true,
   .viewports = true,
   .input_assembler = true};
// 1.0's FullGraphics
inline constexpr StateMask FULL_GRAPHICS = {.shaders = VERTEX_AND_PIXEL,
   .shader_resources = PIXEL,
   .constant_buffers = VERTEX_AND_PIXEL,
   .samplers = PIXEL,
   .unordered_access = PIXEL,
   .targets = true,
   .blend = true,
   .depth = true,
   .rasterizer = true,
   .viewports = true,
   .input_assembler = true};
// 1.0's Compute
inline constexpr StateMask COMPUTE = {.shaders = COMPUTE_STAGES,
   .shader_resources = COMPUTE_STAGES,
   .constant_buffers = COMPUTE_STAGES,
   .samplers = COMPUTE_STAGES,
   .unordered_access = COMPUTE_STAGES};
// What a fullscreen pixel pass changes (1.0's DrawCustomPixelShader), Luma's cbuffers on both of its stages included: the default
inline constexpr StateMask FULLSCREEN_PASS = {.shaders = VERTEX_AND_PIXEL,
   .shader_resources = PIXEL,
   .shader_resource_slots = {0, 1},
   .constant_buffers = VERTEX_AND_PIXEL,
   .samplers = PIXEL,
   .sampler_slots = {0, 1},
   .targets = true,
   .blend = true,
   .depth = true,
   .rasterizer = true,
   .viewports = true,
   .input_assembler = true};

// The groups of the bound-state cache: what the game binds, recorded at bind so draw callbacks (and trace) read it without asking
// the API. Views and buffers aren't in it: the API unbinds them on its own (a resource bound as an output leaves the input slots
// it was in), which a record would miss. The shaders bound per stage are Variants' record.
enum class Recorded : uint8_t
{
   None = 0,
   Viewports = 1 << 0, // And scissor rects
   Blend = 1 << 1,
   Depth = 1 << 2,
   All = Viewports | Blend | Depth,
};

constexpr Recorded operator|(Recorded a, Recorded b)
{
   return Recorded(uint8_t(a) | uint8_t(b));
}

constexpr bool Has(Recorded groups, Recorded group)
{
   return (uint8_t(groups) & uint8_t(group)) != 0;
}

// Game::Options::draw
struct Options
{
   // The groups of the bound-state cache the game's callbacks read (Draw::D3D11::GetBound)
   Recorded record_bound_state = Recorded::None;
};

// Init only (Game::SetOptions)
void SetOptions(const Options& options);
// What the game declared, everything in Development (trace reads it)
Recorded GetRecorded();
// The native functions to hook: the binds of the recorded groups (and what resets a context's state), the draws when the game
// has draw callbacks (the shader binds they need are Shaders')
Hooks::D3D11::Functions GetD3D11Functions();
} // namespace Draw
} // namespace Luma
