#include "Validation/D3D11/D3D11Validation.hpp"

#include <d3d11sdklayers.h>

#include "Log/Log.hpp"
#include "Validation/Validation.hpp"

namespace Luma {
namespace Validation {
namespace D3D11 {
namespace {
InfoQueueStore<ID3D11Device, ID3D11InfoQueue> store;
} // namespace

UINT GetCreateFlags(UINT flags)
{
   return WantsDebugLayer() ? flags | D3D11_CREATE_DEVICE_DEBUG : flags;
}

void WatchDeviceMessages(ID3D11Device* device)
{
   if (WatchInfoQueue<Api::D3D11>(store, device))
      SetActiveLayers(true, IsGpuValidationOn());
   else if (WantsDebugLayer())
      Log::Warning("D3D11: device {} has no debug layer (Windows' \"Graphics Tools\" optional feature isn't installed?)", static_cast<void*>(device));
}

void ReadMessages()
{
   ReadInfoQueues<Api::D3D11, D3D11_MESSAGE>(store);
}

void StopWatchingDevice(ID3D11Device* device)
{
   StopWatchingInfoQueue<Api::D3D11, D3D11_MESSAGE>(store, device);
}
} // namespace D3D11
} // namespace Validation
} // namespace Luma
