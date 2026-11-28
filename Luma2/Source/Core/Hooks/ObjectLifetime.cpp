#include "Hooks/ObjectLifetime.hpp"

#include <atomic>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Hooks {
namespace Lifetime {
namespace {
// {19BE01C2-FB6F-421A-BF5B-3815AFB2465D}
constexpr GUID TRACKER_GUID = {0x19be01c2, 0xfb6f, 0x421a, {0xbf, 0x5b, 0x38, 0x15, 0xaf, 0xb2, 0x46, 0x5d}};

std::atomic<uint64_t> destroyed_count = 0;

// Released by the object when it's destroyed, or when its private data is replaced
struct Holder final : IUnknown
{
   std::atomic<ULONG> references = 1;
   void* state;
   StateDestroy destroy;

   Holder(void* state_, StateDestroy destroy_) : state(state_), destroy(destroy_) {}

   HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** result) override
   {
      if (!result)
         return E_POINTER;
      if (riid == __uuidof(IUnknown))
      {
         AddRef();
         *result = static_cast<IUnknown*>(this);
         return S_OK;
      }
      *result = nullptr;
      return E_NOINTERFACE;
   }

   ULONG STDMETHODCALLTYPE AddRef() override
   {
      return ++references;
   }

   ULONG STDMETHODCALLTYPE Release() override
   {
      const ULONG remaining = --references;
      if (remaining == 0)
      {
         destroy(state);
         delete this;
         destroyed_count.fetch_add(1, std::memory_order_release);
      }
      return remaining;
   }
};

struct Tracker
{
   IUnknown* device;
   const IUnknown* object;
   DestroyCallback on_destroy;

   ~Tracker() { on_destroy(device, object); }
};

template <typename T>
bool Attach(T* object, const IUnknown* key, IUnknown* device, DestroyCallback on_destroy)
{
   UINT size = 0;
   if (SUCCEEDED(object->GetPrivateData(TRACKER_GUID, &size, nullptr)))
      return false;
   IUnknown* holder = CreateStateHolder(new Tracker{device, key, on_destroy}, [](void* tracker) { delete static_cast<Tracker*>(tracker); });
   const bool attached = SUCCEEDED(object->SetPrivateDataInterface(TRACKER_GUID, holder));
   holder->Release();
   return attached;
}

// Calls `function` with the object as the first of its interfaces that has private data
template <typename Function>
auto WithPrivateData(IUnknown* object, Function function) -> decltype(function(static_cast<ID3D12Object*>(nullptr)))
{
   if (ComPtr<ID3D12Object> d3d12_object; SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&d3d12_object))))
      return function(d3d12_object.Get());
   if (ComPtr<ID3D11Device> d3d11_device; SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&d3d11_device))))
      return function(d3d11_device.Get());
   if (ComPtr<ID3D11DeviceChild> d3d11_child; SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&d3d11_child))))
      return function(d3d11_child.Get());
   if (ComPtr<IDXGIObject> dxgi_object; SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&dxgi_object))))
      return function(dxgi_object.Get());
   return {};
}
} // namespace

IUnknown* CreateStateHolder(void* state, StateDestroy destroy)
{
   return new Holder(state, destroy);
}

void* GetStateFromHolder(IUnknown* holder)
{
   return static_cast<Holder*>(holder)->state;
}

uint64_t GetDestroyedCount()
{
   return destroyed_count.load(std::memory_order_acquire);
}

bool Track(IUnknown* object, IUnknown* device, DestroyCallback on_destroy)
{
   return WithPrivateData(object, [&](auto* typed) { return Attach(typed, object, device, on_destroy); });
}
} // namespace Lifetime
} // namespace Hooks
} // namespace Luma
