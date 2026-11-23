#pragma once

#include <d3d12.h>

#include "Game/GameEvents.hpp"

// The game's D3D12 draw events (Game::D3D12::Events::on_variant_draw and on_draw_or_dispatch, Game::D3D12::OnShaderDraw), run by
// the draw, dispatch and ExecuteIndirect handlers as D3D11's
namespace Luma {
namespace Game {
namespace DrawEvents {
namespace D3D12 {
// Inside a Luma scope, before the game's call and before the variant's requests are bound: the variant event when a variant
// pipeline is bound, then the callback of the hash on the pipeline's pixel, vertex, geometry, hull or domain stage (the first with
// one), else the generic event. None while Luma is off, and right away for a call without an event.
DrawResult Run(ID3D12GraphicsCommandList* list, const Game::D3D12::DrawCall& draw);
DrawResult Run(ID3D12GraphicsCommandList* list, const Game::D3D12::DispatchCall& dispatch);
} // namespace D3D12
} // namespace DrawEvents
} // namespace Game
} // namespace Luma
