#pragma once

#include <Windows.h>

#include "Common/Api.hpp"
#include "Common/Math.hpp"
#include "Draw/D3D11/D3D11Draw.hpp"
#include "Draw/Draw.hpp"
#include "Game/Callbacks.hpp"
#include "Game/Events.hpp"
#include "Game/Game.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Shaders/Defines.hpp"
#include "UI/Icons.hpp"
#include "UI/Widgets.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Log.hpp"

namespace Luma {
// The game's DllMain calls it. Only registers a loader notification: Luma initializes when d3d11.dll or d3d12.dll loads, still
// under the loader lock (no dll loads, no waiting on other threads).
BOOL DllMain(HMODULE module, DWORD reason, LPVOID reserved);

// Everything Luma does to the image, at once (Settings::Core::ENABLED). Tracking keeps running while it's off, so switching back
// needs no catch-up. Every feature's draw-path entry checks it. Lock-free, any thread.
bool IsEnabled();
} // namespace Luma
