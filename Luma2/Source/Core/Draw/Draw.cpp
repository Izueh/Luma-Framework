#include "Draw/Draw.hpp"

#include <initializer_list>

namespace Luma {
namespace Draw {
namespace {
// Written while Init runs
Options recorded;
} // namespace

void SetOptions(const Options& options)
{
   recorded = options;
}

Options GetRecorded()
{
#if DEVELOPMENT
   return {.viewports = true, .blend = true, .depth = true};
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
   const Options groups = GetRecorded();
   if (groups.viewports)
      request({RSSetViewports, RSSetScissorRects});
   if (groups.blend)
      request({OMSetBlendState});
   if (groups.depth)
      request({OMSetDepthStencilState});
   if (groups.Any())
      request({ClearState, FinishCommandList, ExecuteCommandList});
   return functions;
}
} // namespace Draw
} // namespace Luma
