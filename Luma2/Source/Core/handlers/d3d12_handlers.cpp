#include "handlers/d3d12_handlers.hpp"

#include "game/game.hpp"
#include "handlers/dxgi_handlers.hpp"
#include "hooks/device_proxy.hpp"
#include "ui/overlay.hpp"
#include "utils/log.hpp"
#include "validation/d3d12_validation.hpp"

namespace Luma {
namespace Handlers {
void OnDeviceCreated(ID3D12Device* device)
{
   Validation::D3D12::OnDeviceCreated(device);
   Utils::Log::Info("D3D12: device {} created ({} live)", static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   Game::CallDeviceCreated(device);
}

void OnDeviceReleased(ID3D12Device* device)
{
   Utils::Log::Info("D3D12: device {} released by the game ({} live)", static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   Game::CallDeviceDestroyed(device);
   UI::OnDeviceReleased(device);
   ForgetSwapchains(device);
   Validation::D3D12::OnDeviceReleased(device);
}
} // namespace Handlers
} // namespace Luma
