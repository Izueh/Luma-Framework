#pragma once

// Function hooks (through SafetyHook, see "UseLumaSafetyHook" in "Luma.Features.props"), split by the interface they hook (e.g. "DXGIHooks.h").
// Hooks are created disabled and only enabled while some feature needs them (they are removed when Luma unloads).
#ifndef ENABLE_SAFETYHOOK
#define ENABLE_SAFETYHOOK 0
#endif

#if ENABLE_SAFETYHOOK

#include <Windows.h>
#include <safetyhook.hpp>

namespace Hooks
{
   // Use "_ReturnAddress()" to know whether a hooked function was called by a specific module
   inline bool IsAddressInModule(const void* address, HMODULE module)
   {
      HMODULE address_module = nullptr;
      return module && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &address_module) && address_module == module;
   }

   // The implementation of a virtual method (shared by all the objects of the same class)
   inline void* GetVirtualFunction(void* object, size_t index)
   {
      return (*reinterpret_cast<void***>(object))[index];
   }

   inline bool SetEnabled(safetyhook::InlineHook& hook, bool enabled)
   {
      return hook && (enabled ? hook.enable() : hook.disable()).has_value();
   }
}

#endif
