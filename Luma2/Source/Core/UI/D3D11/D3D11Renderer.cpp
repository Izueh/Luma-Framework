#include "UI/D3D11/D3D11Renderer.hpp"

#include <array>
#include <cstring>
#include <optional>

#include <d3d11_1.h>
#include <wrl/client.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>

#include "Utils/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace UI {
namespace D3D11 {
namespace {
bool initialized = false;
// The encoding pixel shader, compiled once, and its constants
ComPtr<ID3D11PixelShader> encoding_shader;
ComPtr<ID3D11Buffer> encoding_constants;
std::optional<Encoding::Constants> uploaded_constants;
bool encoding_failed = false;

void BindEncodingShader(const ImDrawList*, const ImDrawCmd*)
{
   ID3D11DeviceContext* context = ImGui_ImplDX11_GetRenderState()->DeviceContext;
   context->PSSetShader(encoding_shader.Get(), nullptr, 0);
   ID3D11Buffer* buffers[] = {encoding_constants.Get()};
   context->PSSetConstantBuffers(Encoding::CONSTANTS_REGISTER, 1, buffers);
}

// False if the shader isn't available. Uploads the constants when they change.
bool PrepareEncoding(ID3D11Device* device, ID3D11DeviceContext* context, const Encoding::Output& output)
{
   if (!encoding_shader && !encoding_failed)
   {
      const ComPtr<ID3DBlob> blob = Encoding::CompilePixelShader("ps_4_0");
      const D3D11_BUFFER_DESC desc = {sizeof(Encoding::Constants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
      encoding_failed = !blob || FAILED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &encoding_shader)) ||
         FAILED(device->CreateBuffer(&desc, nullptr, &encoding_constants));
   }
   if (encoding_failed)
      return false;
   const Encoding::Constants constants = Encoding::GetConstants(output);
   if (!uploaded_constants || std::memcmp(&*uploaded_constants, &constants, sizeof(constants)) != 0)
   {
      context->UpdateSubresource(encoding_constants.Get(), 0, nullptr, &constants, 0, 0);
      uploaded_constants = constants;
   }
   return true;
}
} // namespace

bool NeedsRecreation(const DXGI_SWAP_CHAIN_DESC&)
{
   return false;
}

bool NewFrame(ID3D11Device* device, const DXGI_SWAP_CHAIN_DESC&)
{
   if (!initialized)
   {
      ComPtr<ID3D11DeviceContext> immediate_context;
      device->GetImmediateContext(&immediate_context);
      if (!ImGui_ImplDX11_Init(device, immediate_context.Get()))
      {
         Utils::Log::Error("UI: ImGui's D3D11 renderer failed to initialize on device {}", static_cast<void*>(device));
         return false;
      }
      initialized = true;
   }
   ImGui_ImplDX11_NewFrame();
   return true;
}

void Render(ID3D11Device* device, IDXGISwapChain* swapchain, ImDrawData* data, const Encoding::Output& output)
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

   // ImGui's backend doesn't restore the pixel shader's constant buffers: the game's at the encoding's slot, with its D3D11.1 range
   const bool encoding = output.kind != Encoding::Kind::Sdr && PrepareEncoding(device, context.Get(), output);
   ComPtr<ID3D11DeviceContext1> context1;
   context->QueryInterface(IID_PPV_ARGS(&context1));
   ComPtr<ID3D11Buffer> game_constants;
   UINT first_constant = 0;
   UINT constant_count = 0;
   if (encoding)
   {
      if (context1)
         context1->PSGetConstantBuffers1(Encoding::CONSTANTS_REGISTER, 1, &game_constants, &first_constant, &constant_count);
      else
         context->PSGetConstantBuffers(Encoding::CONSTANTS_REGISTER, 1, &game_constants);
      Encoding::InsertCallback(data, &BindEncodingShader);
   }
   ID3D11RenderTargetView* views[] = {view.Get()};
   context->OMSetRenderTargets(1, views, nullptr);
   ImGui_ImplDX11_RenderDrawData(data);

   if (encoding)
   {
      ID3D11Buffer* buffers[] = {game_constants.Get()};
      if (context1 && game_constants)
         context1->PSSetConstantBuffers1(Encoding::CONSTANTS_REGISTER, 1, buffers, &first_constant, &constant_count);
      else
         context->PSSetConstantBuffers(Encoding::CONSTANTS_REGISTER, 1, buffers);
   }
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
   encoding_shader.Reset();
   encoding_constants.Reset();
   uploaded_constants.reset();
   encoding_failed = false;
   initialized = false;
}
} // namespace D3D11
} // namespace UI
} // namespace Luma
