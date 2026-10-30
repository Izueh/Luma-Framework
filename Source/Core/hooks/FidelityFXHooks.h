#pragma once

#include "Hooks.h"

#if ENABLE_SAFETYHOOK

struct IDXGIAdapter;

namespace FidelityFXHooks
{
   // Creates the (disabled) hooks needed by the spoofing below ("adapter" is the real one)
   bool Init(IDXGIAdapter* adapter);

   // Until "EndAMDSpoofing()", the calls made by "ffx_module" (e.g. the FSR 4 upscaler dll) see an AMD RDNA 3 GPU without the AMD driver dll,
   // which makes it offer the FSR 4 INT8 model on any GPU, independently of what AMD drivers are installed.
   // This is what OptiScaler does with its "Fsr4GpuSupport" setting. Calls can't be nested, and must be on the same thread.
   bool BeginAMDSpoofing(HMODULE ffx_module);
   void EndAMDSpoofing();
}

#endif
