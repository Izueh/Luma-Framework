#pragma once

#include <cstdint>
#include <cstring>
#include <utility>

#include <Unknwn.h>

namespace Luma {
namespace Hooks {
namespace Lifetime {
// Receives what was passed to Track (the device that created the object, or the swapchain's creator) and the object,
// which can't be used anymore. The object still holds its own references.
using DestroyCallback = void (*)(IUnknown* device, const IUnknown* object);

// False if the object already has a tracker or has no private data
bool Track(IUnknown* object, IUnknown* device, DestroyCallback on_destroy);

// A struct attached to the private data of a D3D11, D3D12 or DXGI object, destroyed with the object: the release of the
// object is the cleanup, there's no map to erase from. One per type and object.
using StateDestroy = void (*)(void* state);
IUnknown* CreateStateHolder(void* state, StateDestroy destroy);
void* GetStateFromHolder(IUnknown* holder);

// Unique within the process: the private data key of each state type is derived from the address of a variable of its own
template <typename T>
inline const uint8_t STATE_KEY = 0;

template <typename T>
GUID GetStateGuid()
{
   GUID guid = {0x4C554D41, 0x5354, 0x4154}; // "LUMA" "ST" "AT"
   const void* key = &STATE_KEY<T>;
   static_assert(sizeof(key) <= sizeof(guid.Data4));
   std::memcpy(guid.Data4, &key, sizeof(key));
   return guid;
}

// Null if the object has none. Valid while the object is alive.
template <typename T, typename Object>
T* GetState(Object* object)
{
   IUnknown* holder = nullptr;
   UINT size = sizeof(holder);
   if (FAILED(object->GetPrivateData(GetStateGuid<T>(), &size, &holder)) || !holder || size != sizeof(holder))
      return nullptr;
   T* state = static_cast<T*>(GetStateFromHolder(holder));
   holder->Release(); // GetPrivateData added a reference, the object keeps its own
   return state;
}

// Constructs the state from `arguments`, replacing (and destroying) the existing one of this type. Null if the object has no
// private data.
template <typename T, typename Object, typename... Arguments>
T* SetState(Object* object, Arguments&&... arguments)
{
   T* value = new T(std::forward<Arguments>(arguments)...);
   IUnknown* holder = CreateStateHolder(value, [](void* destroyed) { delete static_cast<T*>(destroyed); });
   const bool attached = SUCCEEDED(object->SetPrivateDataInterface(GetStateGuid<T>(), holder));
   holder->Release();
   return attached ? value : nullptr;
}
} // namespace Lifetime
} // namespace Hooks
} // namespace Luma
