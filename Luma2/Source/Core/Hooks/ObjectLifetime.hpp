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

template <typename T, typename Object>
void RemoveState(Object* object)
{
   if (object)
      object->SetPrivateDataInterface(GetStateGuid<T>(), nullptr);
}

// The same for an object known only as IUnknown (a device of either API): found through the interface that has private data
void* GetStateOf(IUnknown* object, const GUID& guid);
bool AttachStateOf(IUnknown* object, const GUID& guid, IUnknown* holder);

template <typename T>
T* GetStateOf(IUnknown* object)
{
   return static_cast<T*>(GetStateOf(object, GetStateGuid<T>()));
}

template <typename T, typename... Arguments>
T* SetStateOf(IUnknown* object, Arguments&&... arguments)
{
   T* value = new T(std::forward<Arguments>(arguments)...);
   IUnknown* holder = CreateStateHolder(value, [](void* destroyed) { delete static_cast<T*>(destroyed); });
   const bool attached = AttachStateOf(object, GetStateGuid<T>(), holder);
   holder->Release();
   return attached ? value : nullptr;
}

template <typename T>
void RemoveStateOf(IUnknown* object)
{
   if (object)
      AttachStateOf(object, GetStateGuid<T>(), nullptr);
}

// How many states have been destroyed so far: a state pointer remembered with this count is valid while it hasn't changed
uint64_t GetDestroyedCount();

// GetState for the same few objects asked about over and over on a hot path (a context at every bind or draw, ~20 ns through
// private data): the last object of the calling thread is remembered. The state is default constructed on first use. Null if
// the object has no private data.
template <typename T, typename Object>
T* GetOrCreateStateCached(Object* object)
{
   thread_local const Object* last_object = nullptr;
   thread_local T* last_state = nullptr;
   thread_local uint64_t last_destroyed_count = 0;
   const uint64_t destroyed_count = GetDestroyedCount();
   if (object == last_object && destroyed_count == last_destroyed_count)
      return last_state;
   T* state = GetState<T>(object);
   if (!state)
      state = SetState<T>(object);
   last_object = object;
   last_state = state;
   last_destroyed_count = destroyed_count;
   return state;
}
} // namespace Lifetime
} // namespace Hooks
} // namespace Luma
