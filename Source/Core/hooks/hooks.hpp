#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

#include <dxgi.h>

#include "hooks/functions.hpp"
#include "hooks/vtables.hpp"

namespace Luma {
namespace Hooks {
// An export of a System32 dll
struct ExportTarget
{
   const wchar_t* module;
   const char* name;
};

struct VtableTarget
{
   Vtables::Interface vtable;
   uint32_t slot;
   const char* name; // For logging: "IDXGISwapChain::Present"
   // The interface version that introduced the method: skipped if the object lacks it, the slot would be past its vtable
   IID required_version = GUID_NULL;
};

struct Hook
{
   std::variant<ExportTarget, VtableTarget> target;
   void* detour;
   void** original; // Set before the hook is enabled
   // For detours that only differ by the original they call: a function shared by two is hooked once
   bool interchangeable = false;
};

struct Counts
{
   uint32_t declared = 0;
   uint32_t installed = 0;
   uint32_t failed = 0;
};

// What Core installs, one file per API. Device and swapchain creation are always hooked (the device proxy needs them),
// `functions` adds the others.
void InstallDXGI(const D3D11::Functions& functions);
void InstallD3D11(const D3D11::Functions& functions);
// Called by InstallD3D11
void InstallD3D11Device(const D3D11::Functions& functions);
void InstallD3D11Context(const D3D11::Functions& functions);

// A swapchain the factory hooks can't report: D3D11CreateDeviceAndSwapChain creates it before its device is known.
// `requested` is the desc the game asked for, before Luma's changes (null if it didn't pass one).
void OnSwapchainCreated(IDXGISwapChain* swapchain, IUnknown* creator, const DXGI_SWAP_CHAIN_DESC* requested);

// Copies the hooks, and attempts their install right away
void Declare(std::span<const Hook> hooks);
// Installs the declared hooks whose target became available
void InstallPending();
// `name` is the dll's file name
void OnModuleLoaded(std::wstring_view name);
// When Luma is unloaded, never at process exit
void UninstallAll();

Counts GetCounts();
void LogSummary();
} // namespace Hooks
} // namespace Luma
