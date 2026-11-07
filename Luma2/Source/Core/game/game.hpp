#pragma once

#include "core.hpp"

namespace Luma {
namespace Game {
const Info& GetInfo();
void LogDeclarations();
// Forgets the info and callbacks (tests)
void Reset();

// Run the game's callback with the real objects, if it set one, inside a Luma scope
void CallDeviceCreated(ID3D11Device* device);
void CallDeviceCreated(ID3D12Device* device);
void CallDeviceDestroyed(ID3D11Device* device);
void CallDeviceDestroyed(ID3D12Device* device);
void CallSwapchainCreated(IDXGISwapChain* swapchain, const Presenter& presenter);
void CallSwapchainResized(IDXGISwapChain* swapchain, const Presenter& presenter);
void CallPresent(IDXGISwapChain* swapchain, const Presenter& presenter);
// So nothing is looked up every frame for a game without present callbacks
bool HasPresentCallbacks();
} // namespace Game
} // namespace Luma
