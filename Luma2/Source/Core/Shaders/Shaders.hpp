#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Hooks/Functions.hpp"

// Game shaders replaced by files, Luma's own shaders, their defines. One feature with one need decision: Core asks
// GetD3D11Functions once.
namespace Luma {
namespace Shaders {
using Bytecode = std::vector<uint8_t>;

// A resource a compiled shader reads from Luma: a Resources::Database entry, at a register of its stage. Known from its
// reflection (a cbuffer named like an entry); patches (Step 3) add theirs from their recipe.
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
// there are replacement files or draw callbacks (which find the game's shaders by the binds this tracks)
bool IsNeeded();
// The native functions to hook: creations (the database), binds (the variants), draws (the resources variants request) and
// what resets a context's state. None if it isn't needed.
Hooks::D3D11::Functions GetD3D11Functions();

// Presenting thread: compiles what changed (from the cache when it can) and publishes the result at a later present
void Reload();
// Presenting thread: replaced shaders back to the game's, their objects kept for Load()
void Unload();
// Presenting thread: replaced shaders on again, and a reload
void Load();

// Presenting thread, at the present boundary
void CountFrame();
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
BuildInfo GetLastBuild();
} // namespace Shaders
} // namespace Luma
