#include "Effects/RCAS/D3D11/D3D11Rcas.hpp"

#include <algorithm>

#include <wrl/client.h>

#include "Draw/D3D11/D3D11Draw.hpp"
#include "Hooks/HookScope.hpp"
#include "Hooks/ObjectLifetime.hpp"
#include "Resources/D3D11/D3D11ResourceDatabase.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Utils/Hash.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Effects {
namespace RCAS {
namespace D3D11 {
namespace {
constexpr uint64_t COPY_VS_HASH = Utils::Hash::Name("Luma_Copy_VS");
constexpr uint64_t RCAS_PS_HASH = Utils::Hash::Name("Luma_RCAS_PS");

struct DeviceState
{
   ComPtr<ID3D11Buffer> params_cb;
};

struct SharpenParams
{
   float width = 1.f;
   float height = 1.f;
   float sharpness = 0.f;
   float unused = 0.f;
};
} // namespace

bool Draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target, float sharpness)
{
   if (!Settings::IsEnabled() || !context || !source || !target || sharpness <= 0.f)
      return false;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   if (!device)
      return false;

   const Shaders::D3D11::Live* vs = Resources::Database::D3D11::GetShader(device.Get(), COPY_VS_HASH);
   const Shaders::D3D11::Live* ps = Resources::Database::D3D11::GetShader(device.Get(), RCAS_PS_HASH);
   if (!vs || !vs->object || !ps || !ps->object)
      return false;

   ComPtr<ID3D11Resource> target_res;
   target->GetResource(&target_res);
   ComPtr<ID3D11Texture2D> target_tex;
   if (!target_res || FAILED(target_res.As(&target_tex)))
      return false;
   D3D11_TEXTURE2D_DESC desc = {};
   target_tex->GetDesc(&desc);

   Hooks::Scope::Guard scope;
   DeviceState* state = Hooks::Lifetime::GetOrCreateStateCached<DeviceState>(device.Get());
   if (!state)
      return false;
   if (!state->params_cb)
   {
      const D3D11_BUFFER_DESC cb_desc = {sizeof(SharpenParams), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
      if (FAILED(device->CreateBuffer(&cb_desc, nullptr, &state->params_cb)))
         return false;
   }

   D3D11_MAPPED_SUBRESOURCE mapped = {};
   if (FAILED(context->Map(state->params_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
      return false;
   *static_cast<SharpenParams*>(mapped.pData) = {float(desc.Width), float(desc.Height), std::clamp(sharpness, 0.f, 1.f), 0.f};
   context->Unmap(state->params_cb.Get(), 0);

   Luma::Draw::D3D11::StateStack stack{.mask = Luma::Draw::FULLSCREEN_PASS};
   Luma::Draw::D3D11::Save(context, stack);

   const D3D11_VIEWPORT vp = {0.f, 0.f, float(desc.Width), float(desc.Height), 0.f, 1.f};
   context->RSSetViewports(1, &vp);
   context->OMSetRenderTargets(1, &target, nullptr);
   context->OMSetBlendState(nullptr, nullptr, UINT_MAX);
   context->OMSetDepthStencilState(nullptr, 0);
   context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   context->IASetInputLayout(nullptr);
   context->VSSetShader(static_cast<ID3D11VertexShader*>(vs->object.Get()), nullptr, 0);
   context->PSSetShader(static_cast<ID3D11PixelShader*>(ps->object.Get()), nullptr, 0);
   ID3D11Buffer* cb = state->params_cb.Get();
   context->PSSetConstantBuffers(0, 1, &cb);
   context->PSSetShaderResources(0, 1, &source);
   context->Draw(4, 0);

   Luma::Draw::D3D11::Restore(context, stack);
   return true;
}
} // namespace D3D11
} // namespace RCAS
} // namespace Effects
} // namespace Luma
