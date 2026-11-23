#pragma once

#include "Hooks.h"

#if ENABLE_SAFETYHOOK

namespace SystemHooks
{
   // Creates the (disabled) hooks. Safe to call more than once.
   bool Init();

   // "D3DKMTQueryAdapterInfo()" calls made by "caller_module" that query the driver private data are passed to "data_override" after the original call,
   // which returns the status to return. Pass a null module to stop.
   using AdapterPrivateDataOverride = LONG (*)(void* data, UINT data_size, LONG status);
   bool SetAdapterPrivateDataOverride(HMODULE caller_module, AdapterPrivateDataOverride data_override = nullptr);

   // "GetModuleHandleA/W()" calls made by "caller_module" don't find "module_name" (e.g. "example.dll"). Pass a null module to stop.
   bool SetHiddenModule(HMODULE caller_module, const wchar_t* module_name = nullptr);
}

#endif
