#include "Validation/D3D12/D3D12Validation.hpp"

#include <atomic>

#include <d3d12sdklayers.h>
#include <wrl/client.h>

#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "System/Modules.hpp"
#include "Log/Log.hpp"
#include "Validation/Validation.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Validation {
namespace D3D12 {
namespace {
std::atomic<bool> device_created = false;
InfoQueueStore<ID3D12Device, ID3D12InfoQueue> store;
} // namespace

void EnableDebugLayer()
{
   if (device_created.exchange(true))
      return;
   const bool gpu_validation = WantsGpuValidation();
   // GPU-based validation is part of the debug layer
   const bool debug_layer = WantsDebugLayer() || gpu_validation;
   SetActiveLayers(false, false);
   if (!debug_layer)
      return;
   // Inside the game's D3D12CreateDevice, so d3d12.dll is loaded
   const auto get_debug_interface =
      reinterpret_cast<PFN_D3D12_GET_DEBUG_INTERFACE>(System::Modules::FindExport(System::Modules::FindSystemModule(L"d3d12.dll"), "D3D12GetDebugInterface"));
   ComPtr<ID3D12Debug> debug;
   if (!get_debug_interface || FAILED(get_debug_interface(IID_PPV_ARGS(&debug))))
   {
      Log::Warning("D3D12: the debug layer isn't available (Windows' \"Graphics Tools\" optional feature isn't installed?)");
      return;
   }
   debug->EnableDebugLayer();
   bool gpu_validated = false;
   if (gpu_validation)
   {
      if (ComPtr<ID3D12Debug1> debug1; SUCCEEDED(debug.As(&debug1)))
      {
         debug1->SetEnableGPUBasedValidation(TRUE);
         gpu_validated = true;
      }
      else
         Log::Warning("D3D12: GPU-based validation isn't available");
   }
   SetActiveLayers(true, gpu_validated);
   Log::Info("D3D12: debug layer turned on{}", gpu_validated ? ", with GPU-based validation" : "");
}

void WatchDeviceMessages(ID3D12Device* device)
{
   device_created = true;
   WatchInfoQueue<Api::D3D12>(store, device);
}

void ReadMessages()
{
   ReadInfoQueues<Api::D3D12, D3D12_MESSAGE>(store);
}

void StopWatchingDevice(ID3D12Device* device)
{
   StopWatchingInfoQueue<Api::D3D12, D3D12_MESSAGE>(store, device);
}
} // namespace D3D12
} // namespace Validation
} // namespace Luma
