#include "Swapchain/D3D11/D3D11Swapchain.hpp"

#include <d3d11_4.h>

#include "Display/D3D11/D3D11Composition.hpp"
#include "Resources/D3D11/D3D11Mirrors.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Utils/DXGI/DXGIFormat.hpp"
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Swapchain {
namespace D3D11 {
namespace {
// A texture like `source` composition can read, without its targets
bool PrepareCopy(ID3D11Device* device, ID3D11Texture2D* source, Composition& composition)
{
   D3D11_TEXTURE2D_DESC desc;
   source->GetDesc(&desc);
   if (composition.copy)
   {
      D3D11_TEXTURE2D_DESC copy_desc;
      composition.copy->GetDesc(&copy_desc);
      if (copy_desc.Width == desc.Width && copy_desc.Height == desc.Height && copy_desc.Format == desc.Format)
         return true;
   }
   composition.copy.Reset();
   desc.SampleDesc = {1, 0};
   desc.Usage = D3D11_USAGE_DEFAULT;
   desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
   desc.CPUAccessFlags = 0;
   desc.MiscFlags = 0;
   return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &composition.copy));
}

// The view composition reads `resource` through, made again when the resource changed
ID3D11ShaderResourceView* GetView(ID3D11Device* device, ID3D11Resource* resource, Composition& composition)
{
   if (composition.viewed.Get() != resource || !composition.view)
   {
      composition.view.Reset();
      composition.viewed = resource;
      if (FAILED(device->CreateShaderResourceView(resource, nullptr, &composition.view)))
         return nullptr;
   }
   return composition.view.Get();
}

// Without composition the real buffer still gets the game's image where it can be copied, black where it can't
void Fallback(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source, ID3D11Texture2D* target)
{
   D3D11_TEXTURE2D_DESC source_desc;
   D3D11_TEXTURE2D_DESC target_desc;
   source->GetDesc(&source_desc);
   target->GetDesc(&target_desc);
   if (source_desc.Width == target_desc.Width && source_desc.Height == target_desc.Height && source_desc.SampleDesc.Count == target_desc.SampleDesc.Count &&
      Utils::Format::DXGI::AreCopyCompatible(source_desc.Format, target_desc.Format))
   {
      context->CopyResource(target, source);
      return;
   }
   ComPtr<ID3D11RenderTargetView> view;
   if (SUCCEEDED(device->CreateRenderTargetView(target, nullptr, &view)))
   {
      constexpr FLOAT BLACK[4] = {0.f, 0.f, 0.f, 1.f};
      context->ClearRenderTargetView(view.Get(), BLACK);
   }
}
} // namespace

ComPtr<ID3D11Texture2D> CreateBuffer(ID3D11Device* device, const DXGI::BufferDesc& desc)
{
   D3D11_TEXTURE2D_DESC1 texture_desc = {
      .Width = desc.width,
      .Height = desc.height,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = desc.format,
      .SampleDesc = {desc.sample_count, 0},
      .Usage = D3D11_USAGE_DEFAULT,
      // Composition reads it
      .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
   };
   if ((desc.usage & DXGI_USAGE_UNORDERED_ACCESS) && desc.sample_count <= 1)
      texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
   if (desc.flags & DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE)
      texture_desc.MiscFlags |= D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
   const D3D11_TEXTURE2D_DESC plain_desc = {texture_desc.Width, texture_desc.Height, texture_desc.MipLevels, texture_desc.ArraySize, texture_desc.Format,
      texture_desc.SampleDesc, texture_desc.Usage, texture_desc.BindFlags, texture_desc.CPUAccessFlags, texture_desc.MiscFlags};
   ComPtr<ID3D11Texture2D> texture;
   if (FAILED(device->CreateTexture2D(&plain_desc, nullptr, &texture)))
   {
      Log::Error("Swapchain: the game's D3D11 back buffer couldn't be created ({}x{}, format {})", desc.width, desc.height, uint32_t(desc.format));
      return nullptr;
   }
   texture->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("Luma game back buffer") - 1, "Luma game back buffer");
   // Like any texture of the game's
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D11::UpgradeTexture(device, texture_desc, nullptr, texture.Get());
   return texture;
}

bool Compose(IDXGISwapChain* swapchain, ID3D11Device* device, ID3D11Texture2D* game_buffer, Composition& composition, const Display::Composition::Pass& pass)
{
   ComPtr<ID3D11Texture2D> target;
   if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&target))))
      return false;
   ComPtr<ID3D11DeviceContext> context;
   device->GetImmediateContext(&context);

   ID3D11Texture2D* source = game_buffer ? game_buffer : target.Get();
   // The upgraded mirror holds what the game drew while it's swapped in
   if (game_buffer && Resources::Mirrors::IsSwapping())
   {
      ComPtr<ID3D11Texture2D> mirror;
      if (ID3D11Resource* resource = Resources::Mirrors::D3D11::FindMirror(game_buffer); resource && SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&mirror))))
         source = mirror.Get();
   }
   D3D11_TEXTURE2D_DESC source_desc;
   source->GetDesc(&source_desc);
   ID3D11Texture2D* read = source;
   // In place reads a copy of what it writes; a multisampled image is resolved first
   if (!game_buffer || source_desc.SampleDesc.Count > 1)
   {
      if (!PrepareCopy(device, source, composition))
         return false;
      if (source_desc.SampleDesc.Count > 1)
         context->ResolveSubresource(composition.copy.Get(), 0, source, 0, source_desc.Format);
      else
         context->CopyResource(composition.copy.Get(), source);
      read = composition.copy.Get();
   }
   ID3D11ShaderResourceView* view = GetView(device, read, composition);
   if (view && Display::Composition::D3D11::Compose(context.Get(), view, target.Get(), pass))
      return true;
   if (game_buffer)
      Fallback(device, context.Get(), source, target.Get());
   return false;
}

void PrepareResize(ID3D11Device* device, Composition& composition)
{
   composition = {};
   // D3D11 releases what the context used later: a resize fails while it holds a view of a real buffer
   ComPtr<ID3D11DeviceContext> context;
   device->GetImmediateContext(&context);
   context->Flush();
}
} // namespace D3D11
} // namespace Swapchain
} // namespace Luma
