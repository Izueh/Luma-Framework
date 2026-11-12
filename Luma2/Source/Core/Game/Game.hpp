#pragma once

#include "Hooks/Functions.hpp"
#include "Luma.hpp"

namespace Luma {
namespace Game {
// Written only while Init runs, so read without a lock
const Info& GetInfo();
const D3D11::Callbacks& GetD3D11();
const D3D12::Callbacks& GetD3D12();
const Panels& GetPanels();

// The native functions the game's callbacks need hooked
Hooks::D3D11::Functions GetRequestedFunctions();
void LogDeclarations();
// Forgets the info, callbacks and panels (tests)
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
} // namespace Game
} // namespace Luma
