#pragma once

#include "Hooks.h"

#if ENABLE_SAFETYHOOK

struct IDXGIAdapter;

namespace DXGIHooks
{
   // Creates the (disabled) hooks on the methods of the adapter class. Safe to call more than once.
   bool Init(IDXGIAdapter* adapter);

   // "IDXGIAdapter1::GetDesc1()" calls made by "caller_module" return "vendor_id" instead of the real one. Pass a null module to stop.
   bool SetAdapterVendorIdOverride(HMODULE caller_module, UINT vendor_id = 0);
}

#endif
