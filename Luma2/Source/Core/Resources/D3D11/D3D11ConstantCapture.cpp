#include "Resources/D3D11/D3D11ConstantCapture.hpp"

#include <cstddef>
#include <span>

#include <wrl/client.h>

#include "Hooks/ObjectLifetime.hpp"
#include "Resources/ConstantCapture.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Resources {
namespace ConstantCapture {
namespace D3D11 {
namespace {
struct ContextState
{
   ID3D11Buffer* mapped_buffer = nullptr;
   const std::byte* mapped_data = nullptr;
   uint32_t byte_width = 0;
};

bool QueryBufferDesc(ID3D11Resource* resource, ComPtr<ID3D11Buffer>& buffer, D3D11_BUFFER_DESC& desc)
{
   if (!resource || FAILED(resource->QueryInterface(IID_PPV_ARGS(&buffer))))
      return false;
   buffer->GetDesc(&desc);
   return (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0;
}
} // namespace

void TrackMap(ID3D11DeviceContext* context, ID3D11Resource* resource, D3D11_MAP map_type, const void* mapped_data)
{
   if (!HasWriteCallbacks() || IsPausedForFrame() || !context || !mapped_data)
      return;
   if (map_type != D3D11_MAP_WRITE && map_type != D3D11_MAP_WRITE_DISCARD && map_type != D3D11_MAP_READ_WRITE)
      return;
   ComPtr<ID3D11Buffer> buffer;
   D3D11_BUFFER_DESC desc = {};
   if (!QueryBufferDesc(resource, buffer, desc))
      return;
   if (ContextState* state = Hooks::Lifetime::GetOrCreateStateCached<ContextState>(context))
   {
      state->mapped_buffer = buffer.Get();
      state->mapped_data = static_cast<const std::byte*>(mapped_data);
      state->byte_width = desc.ByteWidth;
   }
}

void CaptureUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource)
{
   if (!HasWriteCallbacks() || !context || !resource)
      return;
   ContextState* state = Hooks::Lifetime::GetOrCreateStateCached<ContextState>(context);
   if (!state || !state->mapped_buffer || state->mapped_buffer != resource || !state->mapped_data)
      return;
   const std::span<const std::byte> bytes(state->mapped_data, state->byte_width);
   const uint32_t size = state->byte_width;
   state->mapped_buffer = nullptr;
   state->mapped_data = nullptr;
   state->byte_width = 0;
   if (IsPausedForFrame())
      return;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   if (device)
      DispatchWrite(device.Get(), size, bytes);
}

void DiscardMapped(ID3D11DeviceContext* context, ID3D11Resource* resource)
{
   if (!context || !resource)
      return;
   ContextState* state = Hooks::Lifetime::GetState<ContextState>(context);
   if (!state || state->mapped_buffer != resource)
      return;
   state->mapped_buffer = nullptr;
   state->mapped_data = nullptr;
   state->byte_width = 0;
}

void CaptureUpdate(ID3D11DeviceContext* context, ID3D11Resource* resource, const void* source_data)
{
   if (!HasWriteCallbacks() || IsPausedForFrame() || !context || !resource || !source_data)
      return;
   ComPtr<ID3D11Buffer> buffer;
   D3D11_BUFFER_DESC desc = {};
   if (!QueryBufferDesc(resource, buffer, desc))
      return;
   ComPtr<ID3D11Device> device;
   context->GetDevice(&device);
   if (device)
      DispatchWrite(device.Get(), desc.ByteWidth, std::span(static_cast<const std::byte*>(source_data), desc.ByteWidth));
}
} // namespace D3D11
} // namespace ConstantCapture
} // namespace Resources
} // namespace Luma
