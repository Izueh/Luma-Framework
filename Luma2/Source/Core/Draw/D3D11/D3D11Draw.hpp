#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "Draw/Draw.hpp"
#include "Resources/ResourceDatabase.hpp"

// Draw on D3D11: the state stack, the bound-state cache, and Luma's fullscreen passes. Games reach these through Luma.hpp.
namespace Luma {
namespace Draw {
namespace D3D11 {
struct StageState
{
   Microsoft::WRL::ComPtr<ID3D11DeviceChild> shader;
   std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> shader_resources;
   std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> constant_buffers;
   // The D3D11.1 ranges, when the context has them
   std::vector<UINT> first_constants;
   std::vector<UINT> constant_counts;
   std::vector<Microsoft::WRL::ComPtr<ID3D11SamplerState>> samplers;
   std::vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>> unordered_access;
};

// A context's state saved before a pass and restored after it, in the groups of `mask`
struct StateStack
{
   StateMask mask = FULLSCREEN_PASS;
   std::array<StageState, 6> stages;
   std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets;
   Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil;
   UINT render_target_count = 0; // Up to the last one bound, the output merger's views start after it
   Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
   std::array<FLOAT, 4> blend_factor = {1.f, 1.f, 1.f, 1.f};
   UINT sample_mask = UINT_MAX;
   Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;
   UINT stencil_ref = 0;
   Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer;
   std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
   UINT viewport_count = 0;
   std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissor_rects = {};
   UINT scissor_count = 0;
   D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
   Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
};

// Inside a Luma scope, as everything Luma calls on a context
void Save(ID3D11DeviceContext* context, StateStack& stack);
void Restore(ID3D11DeviceContext* context, const StateStack& stack);

// What the game bound last on a context, in the groups recorded (Draw::GetRecorded). Calls made inside a Luma scope (Luma's own,
// a draw callback's) aren't seen: a pass restoring what it changed keeps it true.
struct Bound
{
   std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
   UINT viewport_count = 0;
   std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissor_rects = {};
   UINT scissor_count = 0;
   // Valid while bound: the context holds them
   ID3D11BlendState* blend = nullptr;
   std::array<FLOAT, 4> blend_factor = {1.f, 1.f, 1.f, 1.f};
   UINT sample_mask = UINT_MAX;
   ID3D11DepthStencilState* depth = nullptr;
   UINT stencil_ref = 0;
};

// Null for a context without private data
const Bound* GetBound(ID3D11DeviceContext* context);

// The handlers' records
void RecordViewports(ID3D11DeviceContext* context, UINT count, const D3D11_VIEWPORT* viewports);
void RecordScissorRects(ID3D11DeviceContext* context, UINT count, const D3D11_RECT* rects);
void RecordBlendState(ID3D11DeviceContext* context, ID3D11BlendState* state, const FLOAT blend_factor[4], UINT sample_mask);
void RecordDepthStencilState(ID3D11DeviceContext* context, ID3D11DepthStencilState* state, UINT stencil_ref);
// ClearState, or a command list finished or executed without restoring the state
void ForgetBoundState(ID3D11DeviceContext* context);

// A pass drawing over a whole target with Luma's custom shaders: a 4 vertex strip, `source` at t0, `sampler` at s0, Luma's
// cbuffers on both stages (LumaData with `custom`). The state of FULLSCREEN_PASS is saved and restored around it.
struct FullscreenPass
{
   std::string_view name; // In trace
   uint64_t vertex_shader = 0; // "Luma_Copy_VS"_h
   uint64_t pixel_shader = 0;
   ID3D11ShaderResourceView* source = nullptr;
   ID3D11RenderTargetView* target = nullptr;
   ID3D11SamplerState* sampler = nullptr; // Optional
   ID3D11BlendState* blend = nullptr; // Null for the default states
   ID3D11DepthStencilState* depth = nullptr;
   // Without alpha the blend factor's alpha is 0
   bool alpha = true;
   Resources::Database::CustomData custom;
   // Drawn while Luma is off too: display composition, the only writer of the real back buffer
   bool while_off = false;
   // State groups saved and restored around the pass
   StateMask state_mask = FULLSCREEN_PASS;
};

// Inside a Luma scope. False, drawing nothing, while Luma is off (but `while_off`), a shader isn't compiled or the target isn't a 2D
// texture's.
bool DrawFullscreen(ID3D11DeviceContext* context, const FullscreenPass& pass);
// `source` into the whole of `target`, at the same size (point, "Luma_Copy_VS/PS") or another one with the same aspect
// (bilinear, "Luma_Scale_VS/PS")
bool Copy(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target, StateMask state_mask = FULLSCREEN_PASS);
bool Scale(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target, StateMask state_mask = FULLSCREEN_PASS);
} // namespace D3D11
} // namespace Draw
} // namespace Luma
