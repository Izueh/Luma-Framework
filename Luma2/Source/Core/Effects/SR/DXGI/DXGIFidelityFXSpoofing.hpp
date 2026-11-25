#pragma once

#include <Windows.h>
#include <dxgi.h>

namespace Luma {
namespace Effects {
namespace SR {
namespace DXGI {
// Scoped AMD RDNA3 spoofing for FidelityFX API DLLs so FSR 4 INT8 is offered on non-AMD GPUs.
namespace FidelityFXSpoofing {
bool Init(IDXGIAdapter* adapter);
bool Begin(HMODULE ffx_module);
void End();
} // namespace FidelityFXSpoofing
} // namespace DXGI
} // namespace SR
} // namespace Effects
} // namespace Luma
