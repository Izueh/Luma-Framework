#include "validation/d3d11_validation.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>

#include <d3d11sdklayers.h>
#include <wrl/client.h>

#include "hooks/scope.hpp"
#include "utils/log.hpp"
#include "validation/validation.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace Validation {
namespace D3D11 {
namespace {
struct Reader
{
   // Holds the real device until the game releases it
   ComPtr<ID3D11InfoQueue> info_queue;
   uint64_t next = 0;
};

// Never held with another lock
std::mutex mutex;
std::unordered_map<const void*, Reader> readers;
std::atomic<uint32_t> reader_count = 0;
} // namespace

UINT GetCreateFlags(UINT flags)
{
   return DEBUG_LAYER ? flags | D3D11_CREATE_DEVICE_DEBUG : flags;
}

void OnDeviceCreated(ID3D11Device* device)
{
   ComPtr<ID3D11InfoQueue> info_queue;
   {
      Hooks::Scope::Guard scope;
      if (FAILED(device->QueryInterface(IID_PPV_ARGS(&info_queue))))
      {
         if (DEBUG_LAYER)
            Utils::Log::Warning("D3D11: device {} has no debug layer (Windows' \"Graphics Tools\" optional feature isn't installed?)", static_cast<void*>(device));
         return;
      }
   }
   info_queue->SetMessageCountLimit(UINT64(-1));
   Utils::Log::Info("D3D11: device {} has the debug layer, its messages are logged", static_cast<void*>(device));
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
      Validation::ReadMessages<D3D11_MESSAGE>(Api::D3D11, reader.info_queue.Get(), reader.next);
}

void OnDeviceReleased(ID3D11Device* device)
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
   Validation::ReadMessages<D3D11_MESSAGE>(Api::D3D11, reader.info_queue.Get(), reader.next);
}
} // namespace D3D11
} // namespace Validation
} // namespace Luma
