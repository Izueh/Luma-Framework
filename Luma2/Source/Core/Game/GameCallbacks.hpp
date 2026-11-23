#pragma once

#include <Windows.h>
#include <d3d11_4.h>

namespace Luma {
namespace Game {
namespace D3D11 {
// Only for the functions no Core feature hooks (Startup::GetOwnedFunctions): the others have events. Setting one installs its hook,
// so they are set in Init, in field order. They get the real API objects, not Luma's proxies. A Pre returning true skips the call.
struct Callbacks
{
   // CreateBlendState is reported as a DESC1 with logic ops disabled
   void (*pre_create_blend_state)(ID3D11Device* device, D3D11_BLEND_DESC1& desc) = nullptr;

   void (*post_ia_set_vertex_buffers)(ID3D11DeviceContext* context, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* strides, const UINT* offsets) = nullptr;
   void (*post_ia_set_index_buffer)(ID3D11DeviceContext* context, ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset) = nullptr;

   bool (*pre_copy_structure_count)(ID3D11DeviceContext* context, ID3D11Buffer* destination, UINT destination_offset, ID3D11UnorderedAccessView* source) = nullptr;

   // Deferred contexts
   void (*post_create_deferred_context)(ID3D11Device* device, ID3D11DeviceContext* context) = nullptr;
   void (*on_deferred_context_destroyed)(ID3D11Device* device, const ID3D11DeviceContext* context) = nullptr;
};
} // namespace D3D11

// Replaces the callbacks set before
void SetCallbacks(const D3D11::Callbacks& callbacks);
} // namespace Game
} // namespace Luma
