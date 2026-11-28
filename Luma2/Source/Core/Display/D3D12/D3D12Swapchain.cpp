#include "Display/D3D12/D3D12Swapchain.hpp"

#include <string>

#include <dxgi1_4.h>

#include "Resources/D3D12/D3D12Mirrors.hpp"
#include "Resources/ResourceMirrors.hpp"
#if DEVELOPMENT
#include "Resources/D3D12/D3D12Descriptors.hpp"
#endif
#include "Log/Log.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Display {
namespace Swapchain {
namespace D3D12 {
ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, const DXGI::BufferDesc& desc, UINT index)
{
   const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
   D3D12_RESOURCE_DESC resource_desc = {
      .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
      .Width = desc.width,
      .Height = desc.height,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = desc.format,
      .SampleDesc = {1, 0},
      .Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
   };
   if (desc.usage & DXGI_USAGE_UNORDERED_ACCESS)
      resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
   ComPtr<ID3D12Resource> buffer;
   // A back buffer starts in the present state, which is the common one
   if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer))))
   {
      Log::Error("Swapchain: the game's D3D12 back buffer {} couldn't be created ({}x{}, format {})", index, desc.width, desc.height, uint32_t(desc.format));
      return nullptr;
   }
   buffer->SetName((L"Luma game back buffer " + std::to_wstring(index)).c_str());
#if DEVELOPMENT
   Resources::Descriptors::D3D12::TrackResource(buffer.Get());
#endif
   // Like any texture of the game's
   if (Resources::Mirrors::IsNeeded())
      Resources::Mirrors::D3D12::UpgradeResource(device, buffer.Get(), D3D12_RESOURCE_STATE_COMMON, nullptr, false);
   return buffer;
}

bool Compose(IDXGISwapChain* swapchain, Display::Composition::D3D12::Renderer& renderer, ID3D12Resource* game_buffer, const Display::Composition::Pass& pass)
{
   ComPtr<IDXGISwapChain3> swapchain3;
   ComPtr<ID3D12Resource> target;
   if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3))) || FAILED(swapchain3->GetBuffer(swapchain3->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&target))))
      return false;
   ID3D12Resource* source = game_buffer ? game_buffer : target.Get();
   // The upgraded mirror holds what the game drew while it's swapped in (its barriers follow the game buffer's)
   if (game_buffer && Resources::Mirrors::IsSwapping())
   {
      if (ID3D12Resource* mirror = Resources::Mirrors::D3D12::FindMirror(game_buffer))
         source = mirror;
   }
   return Display::Composition::D3D12::Compose(renderer, source, target.Get(), pass);
}
} // namespace D3D12
} // namespace Swapchain
} // namespace Display
} // namespace Luma
