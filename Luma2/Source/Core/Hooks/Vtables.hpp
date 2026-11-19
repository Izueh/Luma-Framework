#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <Unknwn.h>

#include "Hooks/Slots.h"

namespace Luma {
namespace Hooks {
namespace Vtables {
enum class Interface : uint8_t
{
   DXGIFactory,
   DXGISwapChain,
   DXGIOutput,
   D3D11Device,
   D3D11ImmediateContext,
   D3D11DeferredContext,
   // Contexts whose methods are neither: D3D11 switches a context to other instances of its methods while the game runs
   D3D11ContextVariant0,
   D3D11ContextVariant1,
   D3D11ContextVariant2,
   D3D11ContextVariant3,
   D3D11DeviceChild,
   D3D12Device,
   D3D12CommandQueue,
   D3D12CommandAllocator,
   D3D12GraphicsCommandList,
   D3D12DescriptorHeap,
   D3D12Fence,
   D3D12PipelineLibrary,
   Count,
};

struct InterfaceVersion
{
   IID iid;
   std::string_view name;
   uint32_t method_count;
};

// `method_count` is the interface's LumaMethodCount_ (it needs to be in LUMA_VTABLE_INTERFACES, slots.h)
template <typename T>
InterfaceVersion GetVersion(std::string_view name, uint32_t method_count)
{
   return {__uuidof(T), name, method_count};
}

// Records the object's methods the first time, with the `versions` (oldest first, the first one names it in the log) the object supports.
// The methods are copied: a vtable can be heap memory owned by its object (D3D11 contexts), freed with it.
// False if its methods differ from the recorded ones (the hooks may not cover them). A third-party patch isn't a difference.
bool Set(Interface interface_type, IUnknown* object, std::span<const InterfaceVersion> versions);

// False if nothing is recorded
bool Matches(Interface interface_type, IUnknown* object);

enum class Method : uint8_t
{
   Same,
   Variant, // Another implementation in the API's own dlls (another runtime, the debug layer): hooks on the recorded one don't see it
   ThirdParty, // Patched into the vtable from another dll (an overlay), which calls the recorded one
};

// Takes the loader lock
Method ClassifyMethod(const void* recorded, const void* observed);

// The recorded methods (as many as the highest supported version has), null until set
void* const* Get(Interface interface_type);
// Null if not set or past the methods of the highest supported version
void* GetMethod(Interface interface_type, uint32_t slot);
bool Supports(Interface interface_type, const IID& version);
} // namespace Vtables
} // namespace Hooks
} // namespace Luma
