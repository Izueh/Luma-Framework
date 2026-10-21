#pragma once

#include <d3d11.h>
#include <utility>
#include "../../../Core/includes/com_ptr.h"

enum class FFXVRainComposition { AfterAA, AfterDoF };

// Extract native rain at its original depth-tested draw, preserving background
// motion vectors and stencil. Reinsert it after AA or after the game DoF.
struct FFXVRainLayer
{
   ComPtr<ID3D11Texture2D> texture;
   ComPtr<ID3D11ShaderResourceView> srv;
   ComPtr<ID3D11RenderTargetView> rtv;
   ComPtr<ID3D11BlendState> capture_blend, composite_blend;
   ComPtr<ID3D11SamplerState> sampler;
   ComPtr<ID3D11DepthStencilState> composite_depth;
   ComPtr<ID3D11Buffer> constants;
   // Retain these until composition, even if shaders are reloaded mid-frame.
   ComPtr<ID3D11VertexShader> composite_vs;
   ComPtr<ID3D11PixelShader> composite_ps;
   UINT width = 0, height = 0;
   FFXVRainComposition composition = FFXVRainComposition::AfterAA;
   bool pending = false;
   bool capture_closed = false;
   bool resolved = false;
   float jitter_x = 0.f, jitter_y = 0.f; // UV, native TAA jitter

   void ResetFrame()
   {
      pending = capture_closed = resolved = false;
      jitter_x = jitter_y = 0.f;
      composite_vs.reset();
      composite_ps.reset();
   }

   bool WantsComposition(FFXVRainComposition timing) const
   {
      return pending && resolved && composition == timing;
   }

   bool Prepare(ID3D11Device* device, UINT w, UINT h)
   {
      if (!capture_blend)
      {
         D3D11_BLEND_DESC desc = {};
         auto& rt = desc.RenderTarget[0];
         rt.BlendEnable = TRUE;
         rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
         rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
         rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
         rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
         desc.IndependentBlendEnable = TRUE;
         desc.RenderTarget[1] = rt;
         desc.RenderTarget[1].BlendEnable = FALSE;
         desc.RenderTarget[1].RenderTargetWriteMask = 0;
         if (FAILED(device->CreateBlendState(&desc, capture_blend.put()))) return false;
      }
      if (!composite_blend)
      {
         D3D11_BLEND_DESC desc;
         capture_blend->GetDesc(&desc);
         // The scene's alpha can carry unrelated post-processing information.
         desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
         if (FAILED(device->CreateBlendState(&desc, composite_blend.put()))) return false;
      }
      if (!sampler)
      {
         D3D11_SAMPLER_DESC desc = {};
         desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
         desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
         desc.MaxLOD = D3D11_FLOAT32_MAX;
         if (FAILED(device->CreateSamplerState(&desc, sampler.put()))) return false;
      }
      if (!composite_depth)
      {
         D3D11_DEPTH_STENCIL_DESC desc = {};
         desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
         if (FAILED(device->CreateDepthStencilState(&desc, composite_depth.put()))) return false;
      }
      if (!constants)
      {
         D3D11_BUFFER_DESC desc = {};
         desc.ByteWidth = 16;
         desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         if (FAILED(device->CreateBuffer(&desc, nullptr, constants.put()))) return false;
      }
      if (texture && width == w && height == h) return true;
      if (pending || !w || !h) return false;
      D3D11_TEXTURE2D_DESC desc = {};
      desc.Width = w;
      desc.Height = h;
      desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
      desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
      ComPtr<ID3D11Texture2D> next;
      ComPtr<ID3D11ShaderResourceView> next_srv;
      ComPtr<ID3D11RenderTargetView> next_rtv;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, next.put())) ||
          FAILED(device->CreateShaderResourceView(next.get(), nullptr, next_srv.put())) ||
          FAILED(device->CreateRenderTargetView(next.get(), nullptr, next_rtv.put()))) return false;
      texture = std::move(next);
      srv = std::move(next_srv);
      rtv = std::move(next_rtv);
      width = w;
      height = h;
      return true;
   }

   template<class Draw>
   bool Capture(ID3D11Device* device, ID3D11DeviceContext* context,
      ID3D11VertexShader* vs, ID3D11PixelShader* ps, Draw&& draw, FFXVRainComposition timing = FFXVRainComposition::AfterAA)
   {
      if (capture_closed || !vs || !ps) return false;
      ComPtr<ID3D11RenderTargetView> originals[8];
      ID3D11RenderTargetView* raw[8] = {};
      ComPtr<ID3D11DepthStencilView> dsv;
      context->OMGetRenderTargets(8, raw, dsv.put());
      for (UINT i = 0; i < 8; ++i)
         *originals[i].put() = raw[i];
      for (UINT i = 2; i < 8; ++i)
         if (raw[i]) return false;
      if (!raw[0] || !raw[1] || !dsv) return false;
      ComPtr<ID3D11Resource> resource;
      ComPtr<ID3D11Texture2D> scene;
      raw[0]->GetResource(resource.put());
      if (FAILED(resource->QueryInterface(scene.put()))) return false;
      D3D11_TEXTURE2D_DESC desc;
      scene->GetDesc(&desc);
      D3D11_RENDER_TARGET_VIEW_DESC view;
      raw[0]->GetDesc(&view);
      if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || view.Texture2D.MipSlice != 0)
         return false;

      ComPtr<ID3D11BlendState> blend;
      FLOAT factor[4];
      UINT sample_mask;
      context->OMGetBlendState(blend.put(), factor, &sample_mask);
      if (!blend) return false;
      D3D11_BLEND_DESC blend_desc;
      blend->GetDesc(&blend_desc);
      const auto& color = blend_desc.RenderTarget[0];
      if (blend_desc.AlphaToCoverageEnable || !color.BlendEnable || color.SrcBlend != D3D11_BLEND_ONE ||
          color.DestBlend != D3D11_BLEND_INV_SRC_ALPHA || color.BlendOp != D3D11_BLEND_OP_ADD || (color.RenderTargetWriteMask & 7) != 7)
         return false;
      ComPtr<ID3D11DepthStencilState> depth, read_only_depth;
      UINT reference;
      context->OMGetDepthStencilState(depth.put(), &reference);
      if (!depth) return false;
      D3D11_DEPTH_STENCIL_DESC depth_desc;
      depth->GetDesc(&depth_desc);
      if (depth_desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ZERO) return false;
      depth_desc.StencilWriteMask = 0;
      if (FAILED(device->CreateDepthStencilState(&depth_desc, read_only_depth.put()))) return false;
      if (!Prepare(device, desc.Width, desc.Height)) return false;

      if (!pending)
      {
         const FLOAT clear[4] = {};
         context->ClearRenderTargetView(rtv.get(), clear);
         composition = timing;
         composite_vs = vs;
         composite_ps = ps;
      }
      ID3D11RenderTargetView* targets[] = {rtv.get(), raw[1]};
      // Disable velocity writes and accumulate premultiplied color/opacity.
      context->OMSetRenderTargets(2, targets, dsv.get());
      context->OMSetBlendState(capture_blend.get(), factor, sample_mask);
      context->OMSetDepthStencilState(read_only_depth.get(), reference);
      draw();
      pending = true;
      context->OMSetRenderTargets(8, raw, dsv.get());
      context->OMSetBlendState(blend.get(), factor, sample_mask);
      context->OMSetDepthStencilState(depth.get(), reference);
      return true;
   }

   // Caller saves/restores graphics and compute state. Scene must be the actual
   // downstream input after upscale-chain substitution, including native fallback.
   bool Composite(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* scene_srv)
   {
      if (!pending || !scene_srv) return false;
      ComPtr<ID3D11Resource> resource;
      ComPtr<ID3D11Texture2D> scene;
      scene_srv->GetResource(resource.put());
      if (FAILED(resource->QueryInterface(scene.put())) || scene.get() == texture.get()) return false;
      D3D11_TEXTURE2D_DESC desc;
      scene->GetDesc(&desc);
      D3D11_SHADER_RESOURCE_VIEW_DESC source_view;
      scene_srv->GetDesc(&source_view);
      if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || source_view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || source_view.Texture2D.MostDetailedMip != 0)
         return false;
      D3D11_RENDER_TARGET_VIEW_DESC view = {};
      view.Format = source_view.Format;
      view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      ComPtr<ID3D11RenderTargetView> target;
      if (FAILED(device->CreateRenderTargetView(scene.get(), &view, target.put()))) return false;
      // Raw-copy fallback is still jittered; native TAA and SR outputs are stable.
      const float data[4] = {1.f / desc.Width, 1.f / desc.Height, resolved ? jitter_x : 0.f, resolved ? jitter_y : 0.f};
      context->UpdateSubresource(constants.get(), 0, nullptr, data, 0, 0);
      context->OMSetRenderTargets(0, nullptr, nullptr);
      ID3D11ShaderResourceView* nulls[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
      context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nulls);
      context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nulls);
      ID3D11Buffer* cb = constants.get();
      context->PSSetConstantBuffers(0, 1, &cb);
      ID3D11ShaderResourceView* input = srv.get();
      context->PSSetShaderResources(0, 1, &input);
      ID3D11SamplerState* sampling = sampler.get();
      context->PSSetSamplers(0, 1, &sampling);
      ID3D11RenderTargetView* output = target.get();
      context->OMSetRenderTargets(1, &output, nullptr);
      context->OMSetBlendState(composite_blend.get(), nullptr, 0xFFFFFFFF);
      context->OMSetDepthStencilState(composite_depth.get(), 0);
      context->IASetInputLayout(nullptr);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
      context->RSSetState(nullptr);
      context->RSSetScissorRects(0, nullptr);
      D3D11_VIEWPORT viewport = {0, 0, float(desc.Width), float(desc.Height), 0, 1};
      context->RSSetViewports(1, &viewport);
      context->VSSetShader(composite_vs.get(), nullptr, 0);
      context->PSSetShader(composite_ps.get(), nullptr, 0);
      // This fullscreen composition does not use the consumer's geometry or
      // tessellation stages.
      ComPtr<ID3D11GeometryShader> gs;
      ComPtr<ID3D11HullShader> hs;
      ComPtr<ID3D11DomainShader> ds;
      context->GSGetShader(gs.put(), nullptr, nullptr);
      context->HSGetShader(hs.put(), nullptr, nullptr);
      context->DSGetShader(ds.put(), nullptr, nullptr);
      context->GSSetShader(nullptr, nullptr, 0);
      context->HSSetShader(nullptr, nullptr, 0);
      context->DSSetShader(nullptr, nullptr, 0);
      context->Draw(4, 0);
      context->GSSetShader(gs.get(), nullptr, 0);
      context->HSSetShader(hs.get(), nullptr, 0);
      context->DSSetShader(ds.get(), nullptr, 0);
      // Restoring compute inputs must not conflict with the scene RTV we just
      // used; the intercepted consumer can immediately bind the scene as an SRV.
      context->OMSetRenderTargets(0, nullptr, nullptr);
      pending = false;
      capture_closed = true;
      return true;
   }
};
