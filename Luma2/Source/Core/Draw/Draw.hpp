#pragma once

#include <cstdint>

#include "Common/ShaderStage.hpp"
#include "Hooks/HookFunctions.hpp"
#include "Shaders/ShaderStages.hpp"

// Saving and restoring a context's state around Luma's own passes, what the game bound (recorded at bind), and the passes
// themselves. The API-agnostic part: which state, and what's recorded.
namespace Luma {
namespace Draw {
struct Range
{
   uint32_t first = 0;
   uint32_t count = 0;
};

// The state a StateStack saves and restores, with three presets below: a pass names the groups it changes, so nothing it
// doesn't touch is copied.
struct StateMask
{
   Shaders::StageSet shaders;
   Shaders::StageSet shader_resources;
   Range shader_resource_slots = {0, 128};
   Shaders::StageSet constant_buffers;
   Range constant_buffer_slots = {0, 14};
   Shaders::StageSet samplers;
   Range sampler_slots = {0, 16};
   // The pixel stage is the output merger's (saved with the targets), the compute stage is the compute shader's
   Shaders::StageSet unordered_access;
   bool targets = false; // Render targets and depth stencil
   bool blend = false;
   bool depth = false; // The depth stencil state and stencil reference
   bool rasterizer = false;
   bool viewports = false; // And scissor rects
   bool input_assembler = false; // Topology and input layout
};

inline constexpr Shaders::StageSet VERTEX_AND_PIXEL = {ShaderStage::Vertex, ShaderStage::Pixel};
inline constexpr Shaders::StageSet PIXEL = {ShaderStage::Pixel};

// The first 3 pixel shader resources and the first sampler, no unordered access views
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
// Every slot of the graphics stages, the output merger's unordered access views included
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
inline constexpr StateMask COMPUTE = {.shaders = Shaders::COMPUTE_STAGES,
   .shader_resources = Shaders::COMPUTE_STAGES,
   .constant_buffers = Shaders::COMPUTE_STAGES,
   .samplers = Shaders::COMPUTE_STAGES,
   .unordered_access = Shaders::COMPUTE_STAGES};
// What a fullscreen pixel pass changes, Luma's cbuffers on both of its stages included: the default
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

// Game::Options::draw: the groups of the bound-state cache the game's callbacks read (Draw::D3D11::GetBound). What the game binds is
// recorded at bind so draw callbacks (and trace) read it without asking the API. Views and buffers aren't in it: the API unbinds
// them on its own (a resource bound as an output leaves the input slots it was in), which a record would miss. The shaders bound per
// stage are Variants' record.
struct Options
{
   bool viewports = false; // And scissor rects
   bool blend = false;
   bool depth = false;

   bool Any() const { return viewports || blend || depth; }
};

// Init only (Game::SetOptions)
void SetOptions(const Options& options);
// What the game declared, everything in Development (trace reads it)
Options GetRecorded();
// The native functions to hook: the binds of the recorded groups, and what resets a context's state
Hooks::D3D11::Functions GetD3D11Functions();
} // namespace Draw
} // namespace Luma
