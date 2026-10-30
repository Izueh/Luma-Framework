#include "DXGIHooks.h"

#if ENABLE_SAFETYHOOK

#include <dxgi1_2.h>
#include <intrin.h>
#include <atomic>
#include <mutex>
#include <wrl/client.h>

namespace DXGIHooks
{
   namespace
   {
      constexpr size_t adapter_get_desc1_index = 10; // IUnknown (3) + IDXGIObject (4) + IDXGIAdapter (3)

      std::mutex init_mutex;
      safetyhook::InlineHook adapter_get_desc1_hook;
      std::atomic<HMODULE> vendor_id_override_module = nullptr;
      std::atomic<UINT> vendor_id_override = 0;

      HRESULT STDMETHODCALLTYPE HookedAdapterGetDesc1(IDXGIAdapter1* adapter, DXGI_ADAPTER_DESC1* desc)
      {
         const HRESULT hr = adapter_get_desc1_hook.stdcall<HRESULT>(adapter, desc);
         if (SUCCEEDED(hr) && desc && Hooks::IsAddressInModule(_ReturnAddress(), vendor_id_override_module))
            desc->VendorId = vendor_id_override;
         return hr;
      }
   }

   bool Init(IDXGIAdapter* adapter)
   {
      const std::lock_guard lock(init_mutex);
      if (adapter_get_desc1_hook)
         return true;
      Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_1;
      if (!adapter || FAILED(adapter->QueryInterface(IID_PPV_ARGS(&adapter_1))))
         return false;
      auto hook = safetyhook::InlineHook::create(Hooks::GetVirtualFunction(adapter_1.Get(), adapter_get_desc1_index), reinterpret_cast<void*>(&HookedAdapterGetDesc1), safetyhook::InlineHook::StartDisabled);
      if (!hook)
         return false;
      adapter_get_desc1_hook = std::move(*hook);
      return true;
   }

   bool SetAdapterVendorIdOverride(HMODULE caller_module, UINT vendor_id)
   {
      vendor_id_override = vendor_id;
      vendor_id_override_module = caller_module;
      return Hooks::SetEnabled(adapter_get_desc1_hook, caller_module != nullptr);
   }
}

#endif
