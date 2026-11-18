#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include <d3d11.h>
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

// A method found through its object (DirectInput's devices), a function of the game
struct AddressTarget
{
   void* address;
   const char* name;
};

struct Hook
{
   std::variant<ExportTarget, VtableTarget, AddressTarget> target;
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
   uint32_t neutralised = 0; // Left in another hooker's chain at unhook, passing straight to the original
};

// What Core installs, one file per API. Device and swapchain creation are always hooked (the device proxy needs them),
// `functions` adds the others.
void InstallDXGI(const D3D11::Functions& functions);
void InstallD3D11(const D3D11::Functions& functions);
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

// A swapchain the factory hooks can't report: D3D11CreateDeviceAndSwapChain creates it before its device is known.
// `requested` is the desc the game asked for, before Luma's changes (null if it didn't pass one).
void OnSwapchainCreated(IDXGISwapChain* swapchain, IUnknown* creator, const DXGI_SWAP_CHAIN_DESC* requested);

// Copies the hooks, and attempts their install right away
void Declare(std::span<const Hook> hooks);
// Never loads a dll: safe under the loader lock
void InstallPending();
// `name` is the dll's file name
void OnModuleLoaded(std::wstring_view name);

// Where a jump at `code` leads, through the relays hookers put before their detour (E9, EB, FF 25, mov rax/jmp rax). Null if it isn't one.
const void* FollowJump(const void* code);

enum class Chain : uint8_t
{
   Intact, // Luma's patch is first: hookers installed after Luma are none, or chained through the vtable
   Chained, // Another jump replaced Luma's: a hooker installed after Luma runs before it
   Restored, // The original bytes are back: someone that hooked before Luma unhooked over it
};

struct ChainState
{
   Chain chain = Chain::Intact;
   std::string detoured_by; // The module the target already jumped to when Luma installed, empty if it didn't
   std::string chained_by; // The module of the jump above Luma's
};

struct Verification
{
   uint32_t intact = 0;
   uint32_t chained = 0;
   uint32_t restored = 0;
   uint32_t detoured_before = 0;
};

// Logs each hook whose chain changed since the last. Any thread.
Verification Verify();
Verification GetLastVerification();
// At the first present, then once a minute in Development. The presenting thread.
void VerifyOnPresent();
std::optional<ChainState> GetChainState(const void* detour);

struct UninstallReport
{
   uint32_t removed = 0; // The original bytes back, the trampoline freed
   uint32_t neutralised = 0; // Under a hooker that came after: its chain now goes from Luma's jump to the original, without Luma's code
   uint32_t kept = 0; // Couldn't be neutralised (a far jump another hooker copied): its detour stays in the chain, bypassed
   uint32_t leaked_trampolines = 0; // A thread was in them when they were to be freed
   bool code_in_use = false; // A thread was running, or returning into, Luma's code

   bool NeedsModule() const { return kept != 0 || code_in_use; }
};

// When Luma is unloaded (never at process exit), after Scope::SetBypass(true). `can_wait` is false under the loader lock. Neutralised
// and kept hooks and their trampolines stay until the process exits.
UninstallReport UninstallAll(bool can_wait);

Counts GetCounts();
void LogSummary();
} // namespace Hooks
} // namespace Luma
