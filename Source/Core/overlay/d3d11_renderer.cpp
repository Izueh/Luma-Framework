#include "overlay/d3d11_renderer.hpp"

#include <array>

#include <d3d11.h>
#include <wrl/client.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>

#include "utils/log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Overlay {
namespace D3D11 {
namespace {
bool initialized = false;
} // namespace

bool NewFrame(ID3D11Device* device)
{
   if (!initialized)
   {
      ComPtr<ID3D11DeviceContext> immediate_context;
      device->GetImmediateContext(&immediate_context);
      if (!ImGui_ImplDX11_Init(device, immediate_context.Get()))
      {
         Utils::Log::Error("Overlay: ImGui's D3D11 renderer failed to initialize on device {}", static_cast<void*>(device));
         return false;
      }
      initialized = true;
   }
   ImGui_ImplDX11_NewFrame();
   return true;
}

void Render(ID3D11Device* device, IDXGISwapChain* swapchain, ImDrawData* data)
{
   // Buffer 0 is always the one being presented. A view held across frames would make the game's ResizeBuffers fail.
   ComPtr<ID3D11Texture2D> buffer;
   ComPtr<ID3D11RenderTargetView> view;
   if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer))) || FAILED(device->CreateRenderTargetView(buffer.Get(), nullptr, &view)))
      return;

   ComPtr<ID3D11DeviceContext> context;
   device->GetImmediateContext(&context);
   // ImGui's backend restores the rest of the state it changes
   std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_views = {};
   ID3D11DepthStencilView* game_depth = nullptr;
   context->OMGetRenderTargets(UINT(game_views.size()), game_views.data(), &game_depth);

   ID3D11RenderTargetView* views[] = {view.Get()};
   context->OMSetRenderTargets(1, views, nullptr);
   ImGui_ImplDX11_RenderDrawData(data);

   context->OMSetRenderTargets(UINT(game_views.size()), game_views.data(), game_depth);
   for (ID3D11RenderTargetView* game_view : game_views)
   {
      if (game_view)
         game_view->Release();
   }
   if (game_depth)
      game_depth->Release();
}

void Shutdown()
{
   if (!initialized)
      return;
   ImGui_ImplDX11_Shutdown();
   initialized = false;
}
} // namespace D3D11
} // namespace Overlay
} // namespace Luma
