#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "Common/Device.hpp"
#include "Hooks/HookFunctions.hpp"

namespace Luma {
namespace Resources {
namespace ConstantCapture {
struct Options
{
   bool enable = false;
};

using BufferWriteCallback = void (*)(Device device, std::span<const std::byte> bytes);

void SetOptions(const Options& options);
const Options& GetOptions();
// Registers a D3D11 CPU write observer for constant buffers of `byte_size` (0 matches any size). Init only.
void RegisterWriteCallback(uint32_t byte_size, BufferWriteCallback callback);
bool HasWriteCallbacks();
void DispatchWrite(Device device, uint32_t byte_size, std::span<const std::byte> bytes);

bool IsNeeded();
Hooks::D3D11::Functions GetD3D11Functions();
Hooks::D3D12::Functions GetD3D12Functions();
} // namespace ConstantCapture
} // namespace Resources
} // namespace Luma
