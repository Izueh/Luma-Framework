#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <Unknwn.h>

#include "hooks/slots.h"

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
   uint32_t method_count;
};
// Needs the interface in "LUMA_VTABLE_INTERFACES" (slots.h)
#define LUMA_INTERFACE_VERSION(Interface) ::Luma::Hooks::Vtables::InterfaceVersion(__uuidof(Interface), #Interface, LumaMethodCount_##Interface)

// Records the object's methods the first time, with the `versions` (oldest first, the first one names it in the log) the object supports.
// The methods are copied: a vtable can be heap memory owned by its object (D3D11 contexts), freed with it.
// False if its methods differ from the recorded ones, which hooks may not cover.
bool Set(Interface interface_type, IUnknown* object, std::span<const InterfaceVersion> versions);

// The recorded methods (as many as the highest supported version has), null until set
void* const* Get(Interface interface_type);
// Null if not set or past the methods of the highest supported version
void* GetMethod(Interface interface_type, uint32_t slot);
bool Supports(Interface interface_type, const IID& version);
} // namespace Vtables
} // namespace Hooks
} // namespace Luma
