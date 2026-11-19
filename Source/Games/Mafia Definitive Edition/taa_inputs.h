#pragma once

#include <d3d11.h>
#include "../../Core/includes/com_ptr.h"

namespace MafiaDE
{
   // NGX takes a resource, not a view, so the view must expose that resource's full mip 0.
   inline bool ReadResolveInput(ID3D11ShaderResourceView* view, UINT width, UINT height,
      bool motion_vectors, ComPtr<ID3D11Resource>& resource)
   {
      resource = nullptr;
      if (view == nullptr || width == 0 || height == 0) return false;

      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
      view->GetDesc(&view_desc);
      if (view_desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || view_desc.Texture2D.MostDetailedMip != 0) return false;

      ComPtr<ID3D11Resource> candidate;
      view->GetResource(candidate.put());
      ComPtr<ID3D11Texture2D> texture;
      if (FAILED(candidate->QueryInterface(texture.put()))) return false;
      D3D11_TEXTURE2D_DESC desc = {};
      texture->GetDesc(&desc);
      if (desc.Width != width || desc.Height != height || desc.MipLevels != 1 || desc.ArraySize != 1 || desc.SampleDesc.Count != 1) return false;

      // The verified game inputs and RenoDX's R11 colour clones are typed float resources.
      const bool fp16 = view_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
      const bool r11 = view_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT && desc.Format == DXGI_FORMAT_R11G11B10_FLOAT;
      if (!(motion_vectors ? fp16 : (r11 || fp16))) return false;

      resource = std::move(candidate);
      return true;
   }
} // namespace MafiaDE
