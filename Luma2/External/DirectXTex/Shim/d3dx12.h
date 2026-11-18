// The d3dx12.h helpers DDSTextureLoader12.cpp uses (as d3dx12_core.h has them), so the loader builds without the DirectX-Headers package. Only that file
// has this folder on its include path (Core.vcxitems).
#pragma once

#include <d3d12.h>

struct CD3DX12_HEAP_PROPERTIES : public D3D12_HEAP_PROPERTIES
{
   explicit CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE type, UINT creation_node_mask = 1, UINT visible_node_mask = 1) noexcept
   {
      Type = type;
      CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
      MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
      CreationNodeMask = creation_node_mask;
      VisibleNodeMask = visible_node_mask;
   }
};

inline UINT8 D3D12GetFormatPlaneCount(ID3D12Device* device, DXGI_FORMAT format) noexcept
{
   D3D12_FEATURE_DATA_FORMAT_INFO info = {format, 0};
   if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &info, sizeof(info))))
      return 0;
   return info.PlaneCount;
}
