#pragma once

#include <array>
#include <utility>

#include "Common/Api.hpp"
#include "Game/Game.hpp"
#include "Game/GameEvents.hpp"
#include "Hooks/HookFunctions.hpp"

namespace Luma {
namespace Game {
namespace Registry {
// Written only while Init runs, so read without a lock
const Info& GetInfo();
const D3D11::Events& GetD3D11Events();
const D3D12::Events& GetD3D12Events();
const Panels& GetPanels();
ApiSet GetSupportedApis();
// Null if the game set none for the hash (OnShaderDraw)
D3D11::DrawCallback FindDrawCallback(uint32_t hash);
// Any draw event: OnShaderDraw, on_variant_draw or on_draw_or_dispatch
bool HasDrawCallbacks();
D3D12::DrawCallback FindD3D12DrawCallback(uint32_t hash);
bool HasD3D12DrawCallbacks();
// The native D3D12 functions the game's draw events need hooked
Hooks::D3D12::Functions GetRequestedD3D12Functions();

// The native functions the game's events need hooked
Hooks::DXGI::Functions GetRequestedDXGIFunctions();
Hooks::D3D11::Functions GetRequestedFunctions();
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

template <Api api>
const auto& GetEvents()
{
   if constexpr (api == Api::D3D11)
      return GetD3D11Events();
   else
   {
      static_assert(api == Api::D3D12);
      return GetD3D12Events();
   }
}

template <Api api>
auto FindDrawCallback(uint32_t hash)
{
   if constexpr (api == Api::D3D11)
      return FindDrawCallback(hash);
   else
   {
      static_assert(api == Api::D3D12);
      return FindD3D12DrawCallback(hash);
   }
}

template <Api api>
bool HasDrawCallbacks()
{
   if constexpr (api == Api::D3D11)
      return HasDrawCallbacks();
   else
   {
      static_assert(api == Api::D3D12);
      return HasD3D12DrawCallbacks();
   }
}

template <Api api, typename Call, typename RefreshVariantStagesFn>
DrawResult DispatchDrawEvents(Call& call, Luma::Shaders::StageSet callback_stages, RefreshVariantStagesFn&& refresh_variant_stages)
{
   constexpr std::array ORDER = {ShaderStage::Pixel, ShaderStage::Vertex, ShaderStage::Geometry, ShaderStage::Hull, ShaderStage::Domain, ShaderStage::Compute};
   const auto& events = GetEvents<api>();
   const auto variant_callback = call.variant_stages.Any() ? events.on_variant_draw : nullptr;
   decltype(events.on_draw_or_dispatch) callback = nullptr;
   for (const ShaderStage stage : ORDER)
   {
      const uint32_t hash = call.hashes[size_t(stage)];
      if (!callback_stages.Has(stage) || !hash)
         continue;
      if ((callback = FindDrawCallback<api>(hash)))
         break;
   }
   if (!callback)
      callback = events.on_draw_or_dispatch;
   if (!variant_callback && !callback)
      return DrawResult::None;
   if (variant_callback)
   {
      if (const DrawResult result = variant_callback(call); result != DrawResult::None)
         return result;
      call.variant_stages = refresh_variant_stages();
   }
   return callback ? callback(call) : DrawResult::None;
}
} // namespace Registry
} // namespace Game
} // namespace Luma
