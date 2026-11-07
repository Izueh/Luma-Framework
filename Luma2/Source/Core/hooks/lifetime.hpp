#pragma once

#include <cstdint>

#include <Unknwn.h>

namespace Luma {
namespace Hooks {
namespace Lifetime {
// Receives the address of the object, which can't be used anymore. Its own references (to its device) are still held.
using DestroyCallback = void (*)(const void* object);

// False if the object already has a tracker or has no private data
bool Track(IUnknown* object, DestroyCallback on_destroy);
} // namespace Lifetime
} // namespace Hooks
} // namespace Luma
