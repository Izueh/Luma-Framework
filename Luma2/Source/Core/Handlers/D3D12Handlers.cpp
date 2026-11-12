#include "Handlers/D3D12Handlers.hpp"

#include "Game/Game.hpp"
#include "Handlers/DXGIHandlers.hpp"
#include "Hooks/D3D12Lifetime.hpp"
#include "Hooks/DeviceProxy.hpp"
#include "UI/Overlay.hpp"
#include "Utils/Log.hpp"
#include "Validation/D3D12Validation.hpp"

namespace Luma {
namespace Handlers {
void PreCreateDevice()
{
   Validation::D3D12::BeforeCreateDevice();
}

void PostCreateDevice(ID3D12Device* device)
{
   Validation::D3D12::OnDeviceCreated(device);
   Utils::Log::Info("D3D12: device {} created ({} live)", static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   Game::CreateDeviceData(device);
   Game::Call(Game::GetD3D12().post_create_device, device);
}

void OnDeviceDestroyed(ID3D12Device* device)
{
   Utils::Log::Info("D3D12: device {} released by the game ({} live)", static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   Game::Call(Game::GetD3D12().on_device_destroyed, device);
   UI::OnDeviceReleased(device);
   ForgetSwapchains(device);
   Validation::D3D12::OnDeviceReleased(device);
}

void PostExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
   Hooks::Lifetime::D3D12::OnExecute(queue, count, lists);
}
} // namespace Handlers
} // namespace Luma
