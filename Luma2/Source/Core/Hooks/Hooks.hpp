#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

#include <d3d11.h>
#include <dxgi.h>

#include "Hooks/Functions.hpp"
#include "Hooks/Vtables.hpp"

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

// What Core.cpp installs, one file per API. Device and swapchain creation are always hooked (the device proxy needs them),
// `functions` adds the others.
void InstallDXGI(const D3D11::Functions& functions);
void InstallD3D11(const D3D11::Functions& functions);
void InstallD3D12(const D3D12::Functions& functions);
// Called by InstallD3D11
void InstallD3D11Device(const D3D11::Functions& functions);
void InstallD3D11Context(const D3D11::Functions& functions);

// D3D11 switches a context to other instances of its methods while the game runs (Blue Reflection's and Borderlands 2's immediate contexts
// draw through other ones than they were created with). Hooks only cover the instances they were installed on, so every context seen
// (deferred ones at creation, the immediate one at each present) is checked, and the hooks of its methods are installed if no hooked
// instance matches it.
void ObserveContext(ID3D11DeviceContext* context);
constexpr size_t CONTEXT_VARIANT_COUNT = 4;
// Declares the context hooks on the methods recorded for `Vtables::Interface::D3D11ContextVariant0 + index`
void DeclareContextVariant(size_t index, const D3D11::Functions& functions);
// Called by InstallD3D12
void InstallD3D12Device(const D3D12::Functions& functions);
void InstallD3D12CommandList(const D3D12::Functions& functions);

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
