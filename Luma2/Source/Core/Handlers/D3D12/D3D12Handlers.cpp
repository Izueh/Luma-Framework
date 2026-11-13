#include "Handlers/D3D12/D3D12Handlers.hpp"

#include "Frame/Frame.hpp"
#include "Game/Registry.hpp"
#include "Handlers/DXGI/DXGIHandlers.hpp"
#include "Hooks/D3D12/D3D12Lifetime.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "UI/Overlay.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Log.hpp"
#include "Validation/D3D12/D3D12Validation.hpp"

namespace Luma {
namespace Handlers {
void PreCreateDevice()
{
   Validation::D3D12::EnableDebugLayer();
}

void PostCreateDevice(ID3D12Device* device)
{
   Validation::D3D12::WatchDeviceMessages(device);
   Utils::Log::Info("D3D12: device {} created ({} live)", static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   Game::Registry::CreateDeviceData(device);
   Frame::CreateDeviceState(device);
   LUMA_UNIMPLEMENTED("Shaders and Resources::Database objects on D3D12 (Step 2)");
   Game::Registry::Call(Game::Registry::GetD3D12Events().on_init_device, device);
}

void OnDeviceDestroyed(ID3D12Device* device)
{
   Utils::Log::Info("D3D12: device {} released by the game ({} live)", static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   Game::Registry::Call(Game::Registry::GetD3D12Events().on_destroy_device, device);
   UI::DetachFromDevice(device);
   ForgetSwapchains(device);
   Validation::D3D12::StopWatchingDevice(device);
}

void PostExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
   Hooks::Lifetime::D3D12::MoveReferencesToQueue(queue, count, lists);
}
} // namespace Handlers
} // namespace Luma
