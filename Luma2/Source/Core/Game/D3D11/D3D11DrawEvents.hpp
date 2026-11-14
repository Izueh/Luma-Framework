#pragma once

#include <d3d11.h>

#include "Game/Events.hpp"
#include "Shaders/Stages.hpp"

// The game's draw events (Game::D3D11::Events::on_variant_draw and on_draw_or_dispatch, Game::D3D11::OnShaderDraw), run by the draw
// and dispatch handlers where 1.0's Core ran OnDrawOrDispatch
namespace Luma {
namespace Game {
namespace DrawEvents {
namespace D3D11 {
struct Result
{
   DrawResult result = DrawResult::None;
   // The stages where an event bound one of Luma's shaders with its requests (BindLumaShader): it took care of Luma's cbuffers for
   // the call (1.0's updated_cbuffers)
   Luma::Shaders::StageSet luma_stages;
};

// Inside a Luma scope, before the game's call and before the variants' requests are bound: the variant event when a variant is
// bound, then the callback of the hash bound on its pixel, vertex, geometry, hull or domain stage (the first with one), else the
// generic event. None while Luma is off, and right away for a call without an event.
Result Run(ID3D11DeviceContext* context, const Game::D3D11::DrawCall& draw);
Result Run(ID3D11DeviceContext* context, const Game::D3D11::DispatchCall& dispatch);
} // namespace D3D11
} // namespace DrawEvents
} // namespace Game
} // namespace Luma
