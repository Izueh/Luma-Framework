#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <Unknwn.h>

namespace Luma {
namespace Hooks {
namespace Vtables {
enum class Interface : uint8_t
{
   DXGIFactory,
   DXGISwapChain,
   D3D11Device,
   D3D11ImmediateContext,
   D3D11DeferredContext,
   D3D11DeviceChild,
   D3D12Device,
   D3D12CommandQueue,
   D3D12CommandAllocator,
   D3D12GraphicsCommandList,
   D3D12DescriptorHeap,
   D3D12Fence,
   Count,
};

struct InterfaceVersion
{
   IID iid;
   std::string_view name;
};
#define LUMA_INTERFACE_VERSION(Interface) ::Luma::Hooks::Vtables::InterfaceVersion(__uuidof(Interface), #Interface)

// Records the vtable the first time, with the `versions` (oldest first, the first one names it in the log) the object supports. False if it differs from the
// recorded one, which hooks may not cover.
bool Set(Interface interface_type, IUnknown* object, std::span<const InterfaceVersion> versions);

// Null until set
void* const* Get(Interface interface_type);
bool Supports(Interface interface_type, const IID& version);
} // namespace Vtables
} // namespace Hooks
} // namespace Luma
