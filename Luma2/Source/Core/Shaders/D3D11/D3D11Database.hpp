#pragma once

#include <span>

#include <d3d11.h>

#include "Common/ShaderStage.hpp"
#include "Shaders/ShaderDatabase.hpp"

namespace Luma {
namespace Shaders {
namespace Database {
namespace D3D11 {
// The game created `object`: its shader is found or added, and the object attached to it. Its hash.
uint32_t TrackShader(ID3D11Device* device, ShaderStage stage, std::span<const uint8_t> bytecode, ID3D11DeviceChild* object);
// The shader of one of the game's objects, null for one Luma didn't see created
const Shader* FindShader(ID3D11DeviceChild* object);
} // namespace D3D11
} // namespace Database
} // namespace Shaders
} // namespace Luma
