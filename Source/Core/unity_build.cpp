// Unity build cpp that aggregates all other cpps so we don't need to manually include them in every project.
// This is not about building speed.

#define STB_IMAGE_IMPLEMENTATION
#include "../External/stb/stb_image.h"

#include "utils/system.cpp"

#include "interop/D3D12Interop.cpp"
#include "spoofing/DXGIHooks.cpp"
#include "spoofing/SystemHooks.cpp"
#include "spoofing/FidelityFXHooks.cpp"

#include "dlss/DLSS.cpp"
#include "fsr/FSR.cpp"
#include "fsr/FSR4.cpp"
#include "xess/XeSS.cpp"
#include "sr_bridge/SRBridge.cpp"
