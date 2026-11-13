#pragma once

#include <cstdint>

#include <d3d11.h>

#include "Luma.hpp"

// The game's draw callbacks (Game::D3D11::OnVariantDraw, OnDraw, OnDrawOrDispatch), run by the draw and dispatch handlers
namespace Luma {
namespace Draw {
namespace D3D11 {
struct CallbackResult
{
   Game::DrawResult result = Game::DrawResult::None;
   // The stages where a callback bound one of Luma's shaders with its requests (BindLumaShader, Draw::GetStageBit bits): it took
   // care of Luma's cbuffers for the call (1.0's updated_cbuffers)
   uint8_t luma_stages = 0;
};

// Inside a Luma scope, before the game's call and before the variants' requests are bound: the variant callback when a variant is
// bound, then the callback of the hash bound on its pixel, vertex, geometry, hull or domain stage (the first with one), else the
// generic one. None while Luma is off, and right away for a draw without a callback.
CallbackResult RunDrawCallbacks(ID3D11DeviceContext* context, const Game::D3D11::DrawCall& draw);
CallbackResult RunDispatchCallbacks(ID3D11DeviceContext* context, const Game::D3D11::DispatchCall& dispatch);

#if DEVELOPMENT
// The trace's action on a shader bound to one of `stages` (Draw::GetStageBit bits) skips the call
bool IsSkippedByTrace(ID3D11DeviceContext* context, uint8_t stages);
#endif
} // namespace D3D11
} // namespace Draw
} // namespace Luma
