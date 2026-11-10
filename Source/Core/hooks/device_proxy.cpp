#include "hooks/device_proxy.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include "hooks/scope.hpp"
#include "utils/log.hpp"

namespace Luma {
namespace Hooks {
namespace DeviceProxy {
namespace {
// More than any device interface has
constexpr size_t SLOT_COUNT = 256;
#ifdef _WIN64
constexpr size_t THUNK_SIZE = 16;
#else
constexpr size_t THUNK_SIZE = 32;
#endif

struct Device;

// What the game holds. Thunks read `real` right after the vtable.
struct Facet
{
   void* const* vtable;
   IUnknown* real;
   Device* owner;
};
static_assert(offsetof(Facet, real) == sizeof(void*));

struct Device
{
   Facet device; // The base interface
   std::atomic<ULONG> references = 0;
   ReleasedCallback on_released = nullptr;
   // Other interfaces (IDXGIDevice), sharing its count
   std::vector<std::unique_ptr<Facet>> facets;
};

// Lock order: `lifetime_mutex` (recursive: a device's release callback may create devices), then `mutex`
struct Registry
{
   std::recursive_mutex lifetime_mutex;
   std::mutex mutex;
   std::unordered_map<const void*, Facet*> facets;
   uint32_t devices = 0;
};

// Leaked: the game may release devices after static destructors ran
Registry& GetRegistry()
{
   static Registry* registry = new Registry();
   return *registry;
}

// Anything else is returned unwrapped: info queues, and the private interfaces DXGI, D3D11On12 or drivers query
const std::array FACETED_INTERFACES = {
   __uuidof(ID3D11Device),
   __uuidof(ID3D11Device1),
   __uuidof(ID3D11Device2),
   __uuidof(ID3D11Device3),
   __uuidof(ID3D11Device4),
   __uuidof(ID3D11Device5),
   __uuidof(ID3D12Device),
   __uuidof(ID3D12Device1),
   __uuidof(ID3D12Device2),
   __uuidof(ID3D12Device3),
   __uuidof(ID3D12Device4),
   __uuidof(ID3D12Device5),
   __uuidof(ID3D12Device6),
   __uuidof(ID3D12Device7),
   __uuidof(ID3D12Device8),
   __uuidof(ID3D12Device9),
   __uuidof(ID3D12Device10),
   __uuidof(ID3D12Device11),
   __uuidof(ID3D12Device12),
   __uuidof(ID3D12Device13),
   __uuidof(ID3D12Device14),
   __uuidof(IDXGIDevice),
   __uuidof(IDXGIDevice1),
   __uuidof(IDXGIDevice2),
   __uuidof(IDXGIDevice3),
   __uuidof(IDXGIDevice4),
};

bool IsFaceted(REFIID riid)
{
   for (const IID& iid : FACETED_INTERFACES)
   {
      if (iid == riid)
         return true;
   }
   return false;
}

HRESULT STDMETHODCALLTYPE QueryInterface(Facet* self, REFIID riid, void** result);
ULONG STDMETHODCALLTYPE AddRef(Facet* self);
ULONG STDMETHODCALLTYPE Release(Facet* self);

// Slots 0-2 are the proxy's IUnknown, every other one a thunk.
// x64:
//   mov rcx, [rcx + 8]           ; this = facet->real
//   mov rax, [rcx]               ; its vtable
//   jmp qword ptr [rax + slot*8] ; its function, with the caller's arguments and return address
// x86 (stdcall, `this` on the stack):
//   mov eax, [esp + 4]           ; the facet
//   mov eax, [eax + 4]           ; facet->real
//   mov [esp + 4], eax           ; this = facet->real
//   mov eax, [eax]               ; its vtable
//   jmp dword ptr [eax + slot*4] ; its function
void WriteThunk(uint8_t* thunk, uint32_t offset)
{
   std::memset(thunk, 0xCC, THUNK_SIZE);
#ifdef _WIN64
   const uint8_t code[] = {0x48, 0x8B, 0x49, 0x08, 0x48, 0x8B, 0x01, 0xFF, 0xA0, uint8_t(offset), uint8_t(offset >> 8), uint8_t(offset >> 16), uint8_t(offset >> 24)};
#else
   const uint8_t code[] = {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x40, 0x04, 0x89, 0x44, 0x24, 0x04, 0x8B, 0x00, 0xFF, 0xA0, uint8_t(offset), uint8_t(offset >> 8),
      uint8_t(offset >> 16), uint8_t(offset >> 24)};
#endif
   static_assert(sizeof(code) <= THUNK_SIZE);
   std::memcpy(thunk, code, sizeof(code));
}

void* const* GetVtable()
{
   static void* const* vtable = [] {
      auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, SLOT_COUNT * THUNK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
      static std::array<void*, SLOT_COUNT> table = {};
      table[0] = reinterpret_cast<void*>(&QueryInterface);
      table[1] = reinterpret_cast<void*>(&AddRef);
      table[2] = reinterpret_cast<void*>(&Release);
      for (size_t slot = 3; slot < SLOT_COUNT; slot++)
      {
         uint8_t* thunk = page + slot * THUNK_SIZE;
         WriteThunk(thunk, uint32_t(slot * sizeof(void*)));
         table[slot] = thunk;
      }
      DWORD old_protection;
      VirtualProtect(page, SLOT_COUNT * THUNK_SIZE, PAGE_EXECUTE_READ, &old_protection);
      FlushInstructionCache(GetCurrentProcess(), page, SLOT_COUNT * THUNK_SIZE);
      return table.data();
   }();
   return vtable;
}

// With the registry locked. Null for interfaces proxies don't cover.
Facet* GetFacetLocked(Registry& registry, Device& device, IUnknown* real, REFIID riid)
{
   // Another layer's private interface may be the base object itself (ReShade's proxy): it must get the real one
   if (!IsFaceted(riid))
      return nullptr;
   if (real == device.device.real)
      return &device.device;
   if (const auto it = registry.facets.find(real); it != registry.facets.end())
      return it->second;
   auto facet = std::make_unique<Facet>(Facet{GetVtable(), real, &device});
   Facet* result = facet.get();
   device.facets.push_back(std::move(facet));
   registry.facets.emplace(real, result);
   return result;
}

HRESULT STDMETHODCALLTYPE QueryInterface(Facet* self, REFIID riid, void** result)
{
   if (!result)
      return E_POINTER;
   Device& device = *self->owner;
   // COM identity
   if (riid == __uuidof(IUnknown))
   {
      device.references++;
      *result = &device.device;
      return S_OK;
   }
   IUnknown* real = nullptr;
   const HRESULT hr = device.device.real->QueryInterface(riid, reinterpret_cast<void**>(&real));
   if (FAILED(hr) || !real)
   {
      *result = nullptr;
      return hr;
   }
   Facet* facet;
   {
      Registry& registry = GetRegistry();
      std::scoped_lock lock(registry.mutex);
      facet = GetFacetLocked(registry, device, real, riid);
   }
   if (!facet)
   {
      *result = real;
      return hr;
   }
   device.references++;
   real->Release();
   *result = facet;
   return hr;
}

ULONG STDMETHODCALLTYPE AddRef(Facet* self)
{
   return ++self->owner->references;
}

ULONG STDMETHODCALLTYPE Release(Facet* self)
{
   Device* device = self->owner;
   const ULONG remaining = --device->references;
   if (remaining != 0)
      return remaining;

   Registry& registry = GetRegistry();
   std::scoped_lock lifetime_lock(registry.lifetime_mutex);
   {
      std::scoped_lock lock(registry.mutex);
      // Replace() handed out a new reference meanwhile
      if (device->references != 0)
         return 0;
      registry.facets.erase(device->device.real);
      for (const auto& facet : device->facets)
         registry.facets.erase(facet->real);
      registry.devices--;
   }
   IUnknown* real = device->device.real;
   {
      Scope::Guard scope;
      device->on_released(real);
   }
   delete device;
   real->Release();
   return 0;
}
} // namespace

bool Wrap(void** object, REFIID riid, IUnknown* device, ReleasedCallback on_released, bool& created)
{
   created = false;
   if (!object || !*object || !device)
      return false;
   auto* returned = static_cast<IUnknown*>(*object);
   if (returned != device && !IsFaceted(riid))
      return false;

   Registry& registry = GetRegistry();
   std::scoped_lock lifetime_lock(registry.lifetime_mutex);
   Facet* facet;
   {
      std::scoped_lock lock(registry.mutex);
      Device* proxy;
      if (const auto it = registry.facets.find(device); it != registry.facets.end())
         proxy = it->second->owner;
      else
      {
         proxy = new Device();
         proxy->device = {GetVtable(), device, proxy};
         proxy->on_released = on_released;
         registry.facets.emplace(device, &proxy->device);
         registry.devices++;
         device->AddRef(); // The proxy's own reference
         created = true;
      }
      facet = GetFacetLocked(registry, *proxy, returned, riid);
      proxy->references++;
   }
   *object = facet;
   returned->Release(); // The game's reference is the proxy's now
   return true;
}

void Replace(void** object)
{
   if (!object || !*object)
      return;
   auto* real = static_cast<IUnknown*>(*object);
   Facet* facet;
   {
      Registry& registry = GetRegistry();
      std::scoped_lock lock(registry.mutex);
      const auto it = registry.facets.find(real);
      if (it == registry.facets.end())
         return;
      facet = it->second;
      facet->owner->references++;
   }
   *object = facet;
   real->Release();
}

IUnknown* Unwrap(IUnknown* object)
{
   return IsProxy(object) ? reinterpret_cast<Facet*>(object)->real : object;
}

bool IsProxy(const void* object)
{
   return object && *static_cast<void* const* const*>(object) == GetVtable();
}

uint32_t GetCount()
{
   Registry& registry = GetRegistry();
   std::scoped_lock lock(registry.mutex);
   return registry.devices;
}
} // namespace DeviceProxy
} // namespace Hooks
} // namespace Luma
