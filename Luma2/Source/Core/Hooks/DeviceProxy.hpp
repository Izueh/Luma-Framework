#pragma once

#include <cstdint>

#include <Unknwn.h>

namespace Luma {
namespace Hooks {
namespace DeviceProxy {
// With the device's base interface, inside a Luma scope
using ReleasedCallback = void (*)(IUnknown* device);

// Replaces `*object`, interface `riid` of `device` (its base interface) just returned by the API, with the proxy's, moving
// its reference to the proxy. A device seen for the first time gets a proxy (`created`). False, leaving `*object` alone, if
// proxies don't cover `riid`.
bool Wrap(void** object, REFIID riid, IUnknown* device, ReleasedCallback on_released, bool& created);

// The same for an `*object` that may be an interface of a proxied device (a child's GetDevice)
void Replace(void** object);

// The real interface behind a proxy, or `object`
IUnknown* Unwrap(IUnknown* object);
bool IsProxy(const void* object);

uint32_t GetCount();
} // namespace DeviceProxy
} // namespace Hooks
} // namespace Luma
