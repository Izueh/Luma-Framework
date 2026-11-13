#pragma once

#include "Hooks/Functions.hpp"
#include "Luma.hpp"

namespace Luma {
namespace Game {
namespace Registry {
// Written only while Init runs, so read without a lock
const Info& GetInfo();
const D3D11::Events& GetD3D11Events();
const D3D12::Events& GetD3D12Events();
const D3D11::Callbacks& GetD3D11Callbacks();
const Panels& GetPanels();
// Null if the game set none for the hash
D3D11::DrawCallback FindDrawCallback(uint32_t hash);
D3D11::DrawCallback GetDrawOrDispatchCallback();
D3D11::DrawCallback GetVariantDrawCallback();
bool HasDrawCallbacks();

// The native functions the game's events and callbacks need hooked
Hooks::D3D11::Functions GetRequestedFunctions();
// Those of its native callbacks alone, which must not include one Core owns (Startup::GetOwnedFunctions)
Hooks::D3D11::Functions GetCallbackFunctions();
void LogDeclarations();
// Forgets the info, events, callbacks, panels and options (tests)
void Reset();

// With each device the game creates, before its callbacks
void CreateDeviceData(IUnknown* device);

// Runs `callback` if the game set it. Handlers are already inside a Luma scope.
template <typename Callback, typename... Arguments>
auto Call(Callback callback, Arguments&&... arguments)
{
   using Result = decltype(callback(std::forward<Arguments>(arguments)...));
   if (!callback)
      return Result();
   return callback(std::forward<Arguments>(arguments)...);
}
} // namespace Registry
} // namespace Game
} // namespace Luma
