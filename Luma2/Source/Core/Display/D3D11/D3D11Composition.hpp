#pragma once

#include <d3d11.h>

#include "Display/Composition.hpp"

// Display composition on D3D11: Luma's fullscreen pass on the immediate context, the game's state restored after it
namespace Luma {
namespace Display {
namespace Composition {
namespace D3D11 {
// Inside a Luma scope. `source` (the game's image) into the whole of `target` (a real back buffer), whether Luma is on or off (off,
// the shader passes the game's image through). False when Luma's shaders aren't compiled yet.
bool Compose(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, ID3D11Texture2D* target, const Pass& pass);
} // namespace D3D11
} // namespace Composition
} // namespace Display
} // namespace Luma
