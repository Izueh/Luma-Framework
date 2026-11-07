#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

#include <dxgi.h>

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

// What core.cpp installs, one file per API
void InstallDXGI();
void InstallD3D11();
void InstallD3D12();

// A swapchain the factory hooks can't report: D3D11CreateDeviceAndSwapChain creates it before its device is known
void OnSwapchainCreated(IDXGISwapChain* swapchain, IUnknown* creator);

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
