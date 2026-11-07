#include "handlers/d3d11_handlers.hpp"

#include "game/game.hpp"
#include "handlers/dxgi_handlers.hpp"
#include "hooks/device_proxy.hpp"
#include "ui/overlay.hpp"
#include "utils/log.hpp"
#include "validation/d3d11_validation.hpp"

namespace Luma {
namespace Handlers {
void OnDeviceCreated(ID3D11Device* device)
{
   Validation::D3D11::OnDeviceCreated(device);
   Utils::Log::Info("D3D11: device {} created (feature level 0x{:X}, {} live)", static_cast<void*>(device), uint32_t(device->GetFeatureLevel()), Hooks::DeviceProxy::GetCount());
   Game::CallDeviceCreated(device);
}

void OnDeviceReleased(ID3D11Device* device)
{
   Utils::Log::Info("D3D11: device {} released by the game ({} live)", static_cast<void*>(device), Hooks::DeviceProxy::GetCount());
   Game::CallDeviceDestroyed(device);
   UI::OnDeviceReleased(device);
   ForgetSwapchains(device);
   Validation::D3D11::OnDeviceReleased(device);
}
} // namespace Handlers
} // namespace Luma
