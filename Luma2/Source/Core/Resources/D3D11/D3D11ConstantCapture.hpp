#pragma once

#include <d3d11.h>

namespace Luma {
namespace Resources {
namespace ConstantCapture {
namespace D3D11 {
void OnMap(ID3D11DeviceContext* context, ID3D11Resource* resource, D3D11_MAP map_type, const void* mapped_data);
void OnUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource);
void OnUpdateSubresource(ID3D11DeviceContext* context, ID3D11Resource* resource, const void* source_data);
} // namespace D3D11
} // namespace ConstantCapture
} // namespace Resources
} // namespace Luma
