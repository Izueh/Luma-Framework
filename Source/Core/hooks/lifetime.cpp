#include "hooks/lifetime.hpp"

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

struct Tracker final : IUnknown
{
   std::atomic<ULONG> references = 1;
   const IUnknown* object;
   ID3D11Device* device;
   DestroyCallback on_destroy;

   Tracker(const IUnknown* object_, ID3D11Device* device_, DestroyCallback on_destroy_) : object(object_), device(device_), on_destroy(on_destroy_) {}

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
         on_destroy(device, object);
         delete this;
      }
      return remaining;
   }
};

template <typename T>
bool Attach(T* object, const IUnknown* key, ID3D11Device* device, DestroyCallback on_destroy)
{
   UINT size = 0;
   if (SUCCEEDED(object->GetPrivateData(TRACKER_GUID, &size, nullptr)))
      return false;
   Tracker* tracker = new Tracker(key, device, on_destroy);
   const bool attached = SUCCEEDED(object->SetPrivateDataInterface(TRACKER_GUID, tracker));
   tracker->Release();
   return attached;
}
} // namespace

bool Track(IUnknown* object, ID3D11Device* device, DestroyCallback on_destroy)
{
   if (ComPtr<ID3D12Object> d3d12_object; SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&d3d12_object))))
      return Attach(d3d12_object.Get(), object, device, on_destroy);
   if (ComPtr<ID3D11Device> d3d11_device; SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&d3d11_device))))
      return Attach(d3d11_device.Get(), object, device, on_destroy);
   if (ComPtr<ID3D11DeviceChild> d3d11_child; SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&d3d11_child))))
      return Attach(d3d11_child.Get(), object, device, on_destroy);
   if (ComPtr<IDXGIObject> dxgi_object; SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&dxgi_object))))
      return Attach(dxgi_object.Get(), object, device, on_destroy);
   return false;
}
} // namespace Lifetime
} // namespace Hooks
} // namespace Luma
