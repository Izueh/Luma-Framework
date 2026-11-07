#include "validation/d3d12_validation.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>

#include <d3d12sdklayers.h>
#include <wrl/client.h>

#include "hooks/scope.hpp"
#include "utils/log.hpp"
#include "utils/modules.hpp"
#include "validation/validation.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Validation {
namespace D3D12 {
namespace {
struct Reader
{
   // Holds the real device until the game releases it
   ComPtr<ID3D12InfoQueue> info_queue;
   uint64_t next = 0;
};

// Never held with another lock
std::mutex mutex;
std::unordered_map<const void*, Reader> readers;
std::atomic<uint32_t> reader_count = 0;
std::atomic<bool> device_created = false;
} // namespace

void BeforeCreateDevice()
{
   if (device_created.exchange(true) || !DEBUG_LAYER)
      return;
   // Inside the game's D3D12CreateDevice, so d3d12.dll is loaded
   const auto get_debug_interface =
      reinterpret_cast<PFN_D3D12_GET_DEBUG_INTERFACE>(Utils::Modules::FindExport(Utils::Modules::FindSystemModule(L"d3d12.dll"), "D3D12GetDebugInterface"));
   ComPtr<ID3D12Debug> debug;
   if (!get_debug_interface || FAILED(get_debug_interface(IID_PPV_ARGS(&debug))))
   {
      Utils::Log::Warning("D3D12: the debug layer isn't available (Windows' \"Graphics Tools\" optional feature isn't installed?)");
      return;
   }
   debug->EnableDebugLayer();
   if (GPU_VALIDATION)
   {
      if (ComPtr<ID3D12Debug1> debug1; SUCCEEDED(debug.As(&debug1)))
         debug1->SetEnableGPUBasedValidation(TRUE);
      else
         Utils::Log::Warning("D3D12: GPU-based validation isn't available");
   }
   Utils::Log::Info("D3D12: debug layer turned on{}", GPU_VALIDATION ? ", with GPU-based validation" : "");
}

void OnDeviceCreated(ID3D12Device* device)
{
   device_created = true;
   ComPtr<ID3D12InfoQueue> info_queue;
   {
      Hooks::Scope::Guard scope;
      if (FAILED(device->QueryInterface(IID_PPV_ARGS(&info_queue))))
         return;
   }
   info_queue->SetMessageCountLimit(UINT64(-1));
   Utils::Log::Info("D3D12: device {} has the debug layer, its messages are logged", static_cast<void*>(device));
   std::scoped_lock lock(mutex);
   readers[device] = {std::move(info_queue), 0};
   reader_count = uint32_t(readers.size());
}

void ReadMessages()
{
   if (reader_count.load(std::memory_order_relaxed) == 0)
      return;
   std::scoped_lock lock(mutex);
   for (auto& [device, reader] : readers)
      Validation::ReadMessages<D3D12_MESSAGE>(Api::D3D12, reader.info_queue.Get(), reader.next);
}

void OnDeviceReleased(ID3D12Device* device)
{
   Reader reader;
   {
      std::scoped_lock lock(mutex);
      const auto it = readers.find(device);
      if (it == readers.end())
         return;
      reader = std::move(it->second);
      readers.erase(it);
      reader_count = uint32_t(readers.size());
   }
   Validation::ReadMessages<D3D12_MESSAGE>(Api::D3D12, reader.info_queue.Get(), reader.next);
}
} // namespace D3D12
} // namespace Validation
} // namespace Luma
