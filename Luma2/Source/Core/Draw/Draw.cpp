#include "Draw/Draw.hpp"

#include <initializer_list>

#include "Game/Registry.hpp"

namespace Luma {
namespace Draw {
namespace {
// Written while Init runs
Recorded recorded = Recorded::None;
} // namespace

void SetOptions(const Options& options)
{
   recorded = options.record_bound_state;
}

Recorded GetRecorded()
{
#if DEVELOPMENT
   return Recorded::All;
#else
   return recorded;
#endif
}

Hooks::D3D11::Functions GetD3D11Functions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   const auto request = [&](std::initializer_list<Hooks::D3D11::Function> requested) {
      for (const Hooks::D3D11::Function function : requested)
         functions.set(size_t(function));
   };
   const Recorded groups = GetRecorded();
   if (Has(groups, Recorded::Viewports))
      request({RSSetViewports, RSSetScissorRects});
   if (Has(groups, Recorded::Blend))
      request({OMSetBlendState});
   if (Has(groups, Recorded::Depth))
      request({OMSetDepthStencilState});
   if (groups != Recorded::None)
      request({ClearState, FinishCommandList, ExecuteCommandList});
   if (Game::Registry::HasDrawCallbacks())
      request({Draw, DrawInstanced, DrawIndexed, DrawIndexedInstanced, DrawAuto, DrawInstancedIndirect, DrawIndexedInstancedIndirect, Dispatch, DispatchIndirect});
   return functions;
}
} // namespace Draw
} // namespace Luma
