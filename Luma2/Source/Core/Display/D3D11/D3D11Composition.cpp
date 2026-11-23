#include "Display/D3D11/D3D11Composition.hpp"

#include <array>

#include <wrl/client.h>

#include "Draw/D3D11/D3D11Draw.hpp"
#if DEVELOPMENT
#include "Trace/DebugDraw.hpp"
#endif
#include "Utils/DXGI/DXGIFormat.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Display {
namespace Composition {
namespace D3D11 {
namespace {
// The UI and the debug draw views the shader reads (t1 to t8): unbound, so it doesn't take the game's for them
constexpr Draw::Range EXTRA_VIEWS = {1, 8};
} // namespace

bool Compose(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, ID3D11Texture2D* target, const Pass& pass)
{
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   D3D11_TEXTURE2D_DESC desc;
   target->GetDesc(&desc);
   // The shader encodes: an sRGB view would encode again (a bitblt swapchain the game asked sRGB for)
   const DXGI_FORMAT typeless = Utils::Format::DXGI::GetTypeless(desc.Format);
   const D3D11_RENDER_TARGET_VIEW_DESC view_desc = {
      .Format = typeless != DXGI_FORMAT_UNKNOWN ? Utils::Format::DXGI::GetDefaultTyped(typeless) : desc.Format,
      .ViewDimension = desc.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
   };
   ComPtr<ID3D11RenderTargetView> view;
   if (FAILED(device->CreateRenderTargetView(target, &view_desc, &view)))
      return false;

   Draw::D3D11::StateStack stack;
   stack.mask = {.shader_resources = Draw::PIXEL, .shader_resource_slots = EXTRA_VIEWS};
   Draw::D3D11::Save(context, stack);
   constexpr std::array<ID3D11ShaderResourceView*, EXTRA_VIEWS.count> NONE = {};
   context->PSSetShaderResources(EXTRA_VIEWS.first, EXTRA_VIEWS.count, NONE.data());
   Resources::Database::CustomData custom = pass.custom;
#if DEVELOPMENT
   Trace::DebugDraw::BindD3D11(context, custom.custom_data_2, custom.custom_data_3);
#endif
   SetCurrent(pass);
   const bool drawn = Draw::D3D11::DrawFullscreen(context, {
                                                              .name = "Luma Display Composition",
                                                              .vertex_shader = VERTEX_SHADER,
                                                              .pixel_shader = PIXEL_SHADER,
                                                              .source = source,
                                                              .target = view.Get(),
                                                              .custom = custom,
                                                              .while_off = true,
                                                           });
   Draw::D3D11::Restore(context, stack);
   return drawn;
}
} // namespace D3D11
} // namespace Composition
} // namespace Display
} // namespace Luma
