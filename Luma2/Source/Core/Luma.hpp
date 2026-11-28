#pragma once

#include <Windows.h>

#include "Common/Api.hpp"
#include "Common/Math.hpp"
#include "Draw/D3D11/D3D11Draw.hpp"
#include "Draw/Draw.hpp"
#include "Game/GameEvents.hpp"
#include "Game/Game.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Shaders/ShaderDefines.hpp"
#include "UI/Icons.hpp"
#include "UI/Widgets.hpp"
#include "Utils/Debug.hpp"
#include "Log/Log.hpp"
#include "Notifications/Notifications.hpp"

namespace Luma {
// The game's DllMain calls it. Only registers a loader notification: Luma initializes when a graphics dll loads, still
// under the loader lock (no dll loads, no waiting on other threads).
BOOL DllMain(HMODULE module, DWORD reason, LPVOID reserved);
} // namespace Luma
