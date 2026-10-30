#include "FidelityFXHooks.h"

#if ENABLE_SAFETYHOOK

#include "DXGIHooks.h"
#include "SystemHooks.h"

#include <mutex>

namespace FidelityFXHooks
{
   namespace
   {
      constexpr UINT vendor_id_amd = 0x1002;
      // The AMD driver private adapter data, the FFX dlls read the ASIC family and revision from it (see "amdgpu_asic_addr.h" in Mesa)
      constexpr UINT amd_private_data_size = 608;
      constexpr size_t amd_asic_family_index = 12;
      constexpr size_t amd_asic_revision_index = 13;
      constexpr UINT amd_asic_family_rdna3 = 145; // gfx11
      constexpr UINT amd_asic_revision_rdna3 = 1;
      // The AMD driver dll the FFX dlls look for, to get the driver FSR (which might not support the spoofed GPU)
      constexpr const wchar_t* amd_driver_module_name = L"amdxc64.dll";

      std::mutex spoofing_mutex;

      LONG OverrideAMDPrivateData(void* data, UINT data_size, LONG status)
      {
         if (data_size != amd_private_data_size)
            return status;
         auto* amd_gpu_info = static_cast<UINT*>(data);
         amd_gpu_info[amd_asic_family_index] = amd_asic_family_rdna3;
         amd_gpu_info[amd_asic_revision_index] = amd_asic_revision_rdna3;
         return 1; // Positive NT status (success), like OptiScaler
      }
   }

   bool Init(IDXGIAdapter* adapter)
   {
      return DXGIHooks::Init(adapter) && SystemHooks::Init();
   }

   bool BeginAMDSpoofing(HMODULE ffx_module)
   {
      spoofing_mutex.lock();
      if (DXGIHooks::SetAdapterVendorIdOverride(ffx_module, vendor_id_amd)
         && SystemHooks::SetAdapterPrivateDataOverride(ffx_module, &OverrideAMDPrivateData)
         && SystemHooks::SetHiddenModule(ffx_module, amd_driver_module_name))
      {
         return true;
      }
      EndAMDSpoofing();
      return false;
   }

   void EndAMDSpoofing()
   {
      SystemHooks::SetHiddenModule(nullptr);
      SystemHooks::SetAdapterPrivateDataOverride(nullptr);
      DXGIHooks::SetAdapterVendorIdOverride(nullptr);
      spoofing_mutex.unlock();
   }
}

#endif
