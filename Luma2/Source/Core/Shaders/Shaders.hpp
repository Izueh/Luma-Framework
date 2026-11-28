#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

#include "Common/Api.hpp"
#include "Common/Device.hpp"
#include "Hooks/HookFunctions.hpp"

// Game shaders replaced by files, Luma's own shaders, their defines. One feature with one need decision: Core asks
// GetD3D11Functions once.
namespace Luma {
namespace Shaders {
using Bytecode = std::vector<uint8_t>;

struct Store;

// A resource a compiled shader reads from Luma: a Resources::Database entry, at a register of its stage. Known from its
// reflection (a cbuffer named like an entry); patches add theirs from their recipe.
struct Request
{
   uint32_t entry = 0;
   uint32_t slot = 0;

   friend bool operator==(const Request&, const Request&) = default;
};

// What a build makes of a replacement or a Luma shader
struct CompiledShader
{
   Bytecode bytecode;
   std::vector<Request> requests;
};

// Before the game's Init: registers Luma's defines and the dump setting, which the game's settings can change the defaults of
void Register();
// After the settings load, before the hooks install: scans the shader files (registering Luma's shaders in Resources::Database),
// decides whether the feature is needed and queues the first build
void Start();
// Decided by Start: always in Development and Test builds (files can be added while the game runs, dumping), otherwise when
// there are replacement files, patches or draw callbacks (which find the game's shaders by the binds this tracks)
bool IsSupported();
bool IsNeeded();
// The native functions to hook: creations (the database), binds (the variants), draws (the resources variants request) and
// what resets a context's state. None if it isn't needed.
Hooks::D3D11::Functions GetD3D11Functions();
// Pipeline and root signature creation (the database, the extended signatures, the variants), the binds and draws (the variants
// and their requests). None if it isn't needed.
Hooks::D3D12::Functions GetD3D12Functions();

// Any thread, at each device's creation, before its first shader: its store, built for its API (D3D12: SM5.1, Luma's cbuffers in
// their own register space) from now on
void CreateStore(Device device, Api api);
// Any thread, when the game releases the device: detaches the store so in-flight jobs stop even if another ref keeps the device alive
void DestroyStore(Device device);
// Null for a device created inside Luma's scope
std::shared_ptr<Store> GetStore(Device device);
// Any thread: every live device's
std::vector<std::shared_ptr<Store>> GetStores();
// Any thread, for the dev tools: the store of the device that presented last, else of a live one. Null before any device.
std::shared_ptr<Store> GetShownStore();
// On D3D12 upgrades <5.1 to 5.1 and returns empty for 6.x (DXIL unsupported)
std::string GetTarget(std::string_view target, Api api);
// Hashes with a replacement file, from the scans so far
bool HasReplacementFile(uint32_t hash);

// Presenting thread: every device's store compiles what changed (from the cache when it can) and publishes at a later present
void Reload();
// Presenting thread: replaced shaders back to the game's, their objects kept for Load()
void Unload();
// Presenting thread: replaced shaders on again, and a reload
void Load();

// Presenting thread, at the present boundary of `device`'s swapchain
void CountFrame(Device device);
// Presents so far
uint64_t GetFrame();

struct BuildInfo
{
   uint64_t id = 0; // Of the last finished build, 0 before the first one
   uint32_t compiled = 0;
   uint32_t cached = 0;
   uint32_t failed = 0;
   // Each failed file's errors (and warnings in Development and Test builds)
   std::string errors;
};

// Any thread
BuildInfo GetLastBuild(const Store& store);
// The shown store's, empty before any device
BuildInfo GetLastBuild();
} // namespace Shaders
} // namespace Luma
