#pragma once

#include <cstdint>

#include "Common/Math.hpp"

// The HLSL types the cbuffer structs shared with the shaders ("Includes/Core/CBuffers.hlsl", a game's "Game/GameCBuffers.hlsl") are written with, for their C++ side
namespace CB {
using namespace Luma::Math;
using uint = uint32_t;
} // namespace CB
