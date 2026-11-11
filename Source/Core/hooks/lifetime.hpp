#pragma once

#include <Unknwn.h>

struct ID3D11Device;

namespace Luma {
namespace Hooks {
namespace Lifetime {
// Receives the device that created the object and the object, which can't be used anymore. The object still holds its device reference.
using DestroyCallback = void (*)(ID3D11Device* device, const IUnknown* object);

// False if the object already has a tracker or has no private data
bool Track(IUnknown* object, ID3D11Device* device, DestroyCallback on_destroy);
} // namespace Lifetime
} // namespace Hooks
} // namespace Luma
