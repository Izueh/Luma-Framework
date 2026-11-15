# Luma 2.0 Design Guidelines

The rules every 2.0 change follows. `Plan.md` is the staged execution plan built on them.

This supersedes `Design.md`, `Implementation.md` and `Core2.md` wherever they disagree. Those stay as the record of the first skeleton and of the 1.0 feature inventory.

## 1. What Luma 2.0 is

A multi-backend framework for modifying game rendering through CPU code hooks, native graphics API hooks and shader replacement or patching. It carries general rendering upgrades (HDR, super resolution, AO, anti-aliasing, RT and others) across many games.

**2.0 keeps 1.0's behaviour and flow.** It doesn't change everything 1.0 did: it organizes the project better and consolidates code that games repeated into Core. Where 1.0's way works, 2.0 keeps it, including its build model (Core compiled into each game, section 16) and its names and access patterns where reasonable (`cb_luma_global_settings`, `UpdateLumaInstanceDataCB`). A change of behaviour is the exception, with a row in section 17.

Goals, in priority order:

1. **Rebuild 1.0 incrementally, game by game.** A feature is built when a game being ported needs it. Final Fantasy VII Remake (FF7R) is first, on D3D11 and D3D12. After it, games are picked for the features FF7R doesn't use.
2. **Features and models instead of a monolith.** 1.0's `core.hpp` (13k lines) and its `DeviceData` become modules that own their state. Core is still compiled into each game, as `core.hpp` was.
3. **No ReShade baggage.** No RHI, no ReShade-shaped events, no handle types. ReShade is an optional guest (section 14).
4. **Backends stay native.** The abstraction is the Luma feature, never the graphics API.
5. **Game logic is not touched while porting.** Section 8.
6. **2.0 is built next to 1.0** in `Luma2/` until it can replace it.

## 2. Rules of construction

* **Simple first.** Write the direct implementation. Don't guard against errors nobody has seen, don't add options nobody asked for, don't generalise from one user.
* **Pick the end shape, fill it naively.** Module boundaries and public names are chosen for where they end up. The code inside starts minimal.
* **1.0 is the tested reference, not scripture.** Port behaviour as it is by default. When something is changed on purpose, add a row to section 17 with the reason. An improvement that isn't worth doing now also goes there, marked as an idea.
* **Test whole units.** A unit is one step of `Plan.md` (or one of its parts). It is verified once, when complete, in the real game through the overlay and MCP. No test per function, no test-driven scaffolding. doctest is only for pure logic where a test is cheaper than a game launch (hashing, TOML, rule matching, settings).
* **Every unit ships its inspection surface.** The trace events, dev UI panel and MCP tools for a feature are part of that feature, because they are how it gets verified.
* **Unimplemented is loud in Development, silent otherwise.** A feature a backend doesn't have calls `LUMA_UNIMPLEMENTED("Resources::Scaling")`: assert once and log in Development builds, no-op in the others. Never a silent partial implementation.
* **No per-frame or per-draw logging.** Log events and first occurrences. Anything repeating belongs to a trace capture.

## 3. Layers and modules

Dependencies point down. A module never includes one above it.

```
Games            mod code: declarations in Init(), callbacks, game hooks       (includes Luma.hpp only)
──────────────────────────────────────────────────────────────────────────────
UI               overlay shell, style, widgets, panels
Effects          SR (DLSS, FSR, XeSS), SMAA, RCAS, Bloom, GTAO                 optional, built on the features below
Features         Shaders · Resources · Swapchain · Display · Draw · Trace
Settings         the one registry, its sources and storage
──────────────────────────────────────────────────────────────────────────────
Handlers         per native function: runs the game callback and the features, in a fixed order
Hooks            install, vtables, lifetime, scope, device proxy, detours
──────────────────────────────────────────────────────────────────────────────
System           loader, dll exports, memory scan and CPU hooks, display, window
Utils / Common   log, hash, strings, json, toml, formats, math, Api
```

### Namespaces

A folder is a namespace. Something is found by asking "which feature owns it".

| Namespace | Owns |
|---|---|
| `Luma` | shared value types: `float2..4`, `uint2..4`, `Api`, `Presenter` |
| `Luma::Utils::{Log, Debug, Hash, Strings, Json, Toml, Format}` | stateless leaf helpers: no knowledge of the process, its modules or the hooks. The one exception is the log sink (`Log`, and `Debug`'s asserts that write to it), which every layer, `Utils` included, must reach |
| `Luma::System::{Loader, Modules, Paths, Memory, Display, Window, Jobs}` | process level: bootstrap, the loaded modules and their exports, Luma's folders, pattern scan and mid-function hooks, HDR state and luminance, SDR white level, borderless window, the pool background work runs on. No graphics API objects |
| `Luma::Hooks` (+ `::Input`, `::DeviceProxy`, `::Lifetime`, `::Scope`, `DepthGuard`) | interception plumbing, nested call tracking included |
| `Luma::Handlers` | `Pre<Function>` / `Post<Function>` / `On<Object>Destroyed`, one per hooked native function |
| `Luma` (`Startup`) | `Luma.hpp`, the one header a game includes; `Luma::Startup::{Initialize, IsInitialized, Shutdown}`, what `System::Loader` calls; `Luma::DllMain` |
| `CB` | the cbuffer structs shared with the shaders, compiled by FXC and by C++ (`Resources/LumaCBuffers.hpp` gives them HLSL's types, a game's file holds only its structs): Luma's `LumaGlobalSettings` and `LumaInstanceData` (`Includes/Core/CBuffers.hlsl`), the game's `LumaGameSettings` and `LumaGameData` (its `Game/GameCBuffers.hlsl`), as 1.0's `cbuffers.h` |
| `Luma::Settings` | registry, sources (`Define`, `Recipe`, `Binding`), kinds, typed ids (`Settings/Keys.hpp`, the only place Core's keys are written), TOML storage |
| `Luma::Shaders::{Database, Variants, Files, Compiler, Cache, Defines, Patching}` | everything about shader and pipeline objects, the game's, and the build of Luma's |
| `Luma::Resources::{Database, LumaCBuffers, Mirrors, Upgrade, Chains, Scaling, Samplers, Geometry, Loading}` | Luma's named resources (its shaders, cbuffers, textures) and everything about the game's textures, buffers, samplers |
| `Luma::Swapchain` | the real swapchain, the game's back buffers, fullscreen policy, output mode |
| `Luma::Display` | composition, UI separation, capability spoofing |
| `Luma::Draw` | state save and restore, the bound-state cache, fullscreen passes, copy and scale helpers |
| `Luma::Frame` | per-frame model: each device's resolutions (output, render, previous render) now; jitter, camera, pass markers later |
| `Luma::Effects::{SR, SMAA, RCAS, Bloom, GTAO}` | reusable passes |
| `Luma::Trace::{Capture, Info, DebugDraw, Mcp}` | frame capture and the MCP server |
| `Luma::UI::{Overlay, Style, Widgets, Icons, Panels}` | section 12 |
| `Luma::Game` | the mod surface (section 8), declared in `Luma.hpp`; `Luma::Game::Registry` is Core's read side of what the game registered |
| `Luma::Compat::{ReShade, RenderDoc}` | section 14 |

Per-API code is nested under its feature: `Luma::Resources::Mirrors::D3D12`, in `Resources/D3D12/D3D12Mirrors.cpp`. Feature first, API second, so the shared part of a feature is visible from its backends.

**Folders map to namespaces.** A feature folder is its namespace (`Shaders/` is `Luma::Shaders`), a module is a file pair in it and a namespace of its own (`Shaders/Variants.cpp`, `Luma::Shaders::Variants`). Per-API code sits in an `<Api>` subfolder of the feature (`D3D11`, `D3D12`, `DXGI`), whose files keep the `<Api>` prefix and add `::<Api>` to the module's namespace: `Shaders/D3D11/D3D11Variants.cpp` is `Luma::Shaders::Variants::D3D11`. `UI`'s overlay renderers follow the same rule (`UI/D3D11/D3D11Renderer.cpp`), and `UI/Panels/` holds the panels (`Luma::UI::Panels`).

### Models

The nouns shared across modules. Each has one owner and one definition.

| Model | Owner | What it is |
|---|---|---|
| Shader, Pipeline | `Shaders::Database` | a game shader or pipeline object, its hash(es), reflection, name |
| Variant | `Shaders::Variants` | a Luma version of a pipeline: replaced, patched, or with other formats |
| Mirror | `Resources::Mirrors` | a Luma resource standing in for a game resource |
| Rule, Seed | `Resources::Upgrade`, `Resources::Chains` | what gets a mirror and how |
| Entry | `Resources::Database` | a named resource any module or shader can resolve |
| Setting | `Settings` | one typed, persisted, UI-visible value |
| FrameInfo, PassMarker | `Frame` | what happened this frame |
| DisplayInfo | `System::Display` | the monitor's HDR state and luminance |
| Event | `Trace::Capture` | one recorded call of a captured frame |

## 4. Backends

* **No RHI.** Each feature has an API-agnostic part (configuration, rules, pure decisions) and one native implementation per API, written against the API's own types. Some duplication between backends is expected and fine.
* **Dispatch by overload.** A detour is typed for its API, and its handler calls the feature's implementation for that API directly. Code not tied to an API (a setting change, game-level code) uses `std::variant<std::monostate, ID3D11Device*, ID3D12Device*>` over the main device.
* **Templates only for one algorithm genuinely written once** over both APIs. Never to hide API differences.
* **Parity is guided by need.** D3D11 and D3D12 get what FF7R needs on each, then what each later game needs. A gap is a `LUMA_UNIMPLEMENTED` and a row in the parity table of `Plan.md`.
* **Swap at the earliest point where the decision is known**, on both APIs. D3D11 swaps at bind by editing the call's arguments, as 1.0 does. D3D12 does the same wherever it can:
  * views of a mirrored resource: when the game copies the descriptor into a shader-visible heap (`CopyDescriptors*`), the mirror's view (kept in Luma's own CPU heap) is copied in its place; the game's CPU heaps keep the game's views, so a toggle acts at the next copy and nothing is rewritten. Engines that copy descriptors per draw (Unreal) make this the equivalent of a D3D11 bind. A view the game writes straight into a shader-visible heap is swapped at `Create*View`;
  * samplers: the same, at the copy into a shader-visible sampler heap;
  * RTV and DSV: at `OMSetRenderTargets`, by replacing the CPU handles;
  * shader variants: at `SetPipelineState`.

  What D3D12 can only decide at draw is what depends on two binds at once, because their order isn't fixed: a PSO's render target formats against the targets bound, and rules keyed by "the target of shader X". For those the command list keeps a small record (current PSO, root signature, targets) and checks it at draw behind a dirty flag. D3D12 has no state getters, so this record is needed anyway.
* **D3D12 root signatures are extended at creation.** The game's signature gets Luma's parameters appended (its indices unchanged) and the game gets that signature, so its pipelines and Luma's variants share it and a variant needs nothing at draw. All of Luma's bindings are in their own space (`Options::bindings.d3d12_space`, default 100, SM5.1 variants): its cbuffers now, its views and samplers with the D3D12 passes. The space is Luma's whole: a game root signature or shader using any register in it is a conflict, asserted in Development, and the mod picks another space; the signature stays the game's. A signature whose size with Luma's parameters goes over the API's 64 DWORDs stays the game's too. Either way its pipelines run the game's shaders: anything Luma can't extend, compile or create falls back to the original.
* **D3D12 descriptor heaps get a Luma tail at creation**, and `GetDesc` reports the size the game asked for. Luma's descriptors live there, valid while the game's heap is bound. A heap at the device's limit uses ranges the game never wrote.
* **A Luma object is released only after the work that used it has finished.** Anything Luma creates and may replace while the game runs (shader variants, PSOs, mirrors, sampler mirrors, Luma's cbuffers and textures) is referenced by each command list that binds it. On D3D12 those references move to the queue at `ExecuteCommandLists`, tagged with Luma's fence value, and are dropped when the fence passes it. A reload or a setting change publishes the new object and drops Luma's own reference to the old one: the old one dies when the last command list that bound it has executed. D3D11 keeps bound objects alive itself, including in deferred contexts, so only D3D12 needs the tracking. A list notes an object once (a generation stamp on the object, not a search).
* **Shadow descriptor heaps are a fallback**, built only if a game needs the same resource seen as original by some draws and as mirror by others through descriptors it wrote once (persistent tables, bindless).
* **Keeping Vulkan possible** costs only these habits:
  * API-agnostic files include no `d3d*.h` or `dxgi*.h`.
  * Files for one API are named `<Api><Module>`.
  * `Api` and bytecode kind (`DXBC`, `DXIL`) are enums with room to grow, and shader hashes are plain integers.
  * DXGI lives in `Swapchain` and `System::Display` only.
  * Settings, UI, Trace and `Game::Options` never name an API type. Game callbacks do, per API. A declaration that is about an API's own values (texture upgrade rules name `DXGI_FORMAT`, as 1.0's did) is its own call typed by that format family, shared by every API of the family (DXGI: D3D11 and D3D12), and overloaded for another family when one exists; never an `Options` member, never a `std::variant`.

## 5. Hooks

What the 1.0 migration off ReShade established. All of it carries over.

* **SafetyHook inline hooks on function bodies**, found through vtables. One hook covers every interface version and both immediate and deferred contexts.
* **Vtables are copied, never pointed at.** D3D11 contexts keep their vtable in heap memory that dies with the temporary object (the 1.0 first-boot crash). Slots are computed from the SDK's C interface structs (`slots.c`).
* **Exports are read from the module's export table**, not `GetProcAddress`, and only from System32 modules. RenderDoc and same-named proxies otherwise get hooked in place of the real function.
* **A hook is installed only when something asks for it**: a Core feature that is needed, or a game callback that is registered. Hot paths (`Map`, every `*SetShaderResources`) cost nothing when unused.
* **One handler per native function**, named after it: `PreX` can edit arguments and skip the call, `PostX` sees the result, `OnXDestroyed` gets an object that can no longer be used.
* **A handler orchestrates, features say what they do.** A handler reads as the work done at that native call: each feature function it calls is one logical unit named for what it does with what it's given (`CreateVariantStore`, `TrackShader`, `SwapInVariant`, `BindRequestedResources`, `ReleaseDeviceObjects`, `WatchDeviceMessages`, `DrawOverlay`), never an `OnX` mirror of the handler. Where the order between features matters, it's visible in the handler with a comment saying why.
* **The game wraps Core.** In a `Pre` the game runs first and Core sees its edits. In a `Post` Core runs first so its tracking is current. Each side runs only if it asked for the hook.
* **Luma scope.** Luma's own API calls and calls made from game callbacks run under a thread-local guard and pass through the hooks untouched. A callback edits arguments rather than skipping a call and reissuing it.
* **Arguments reach handlers as the game passed them.** A whole-subresource copy has no box. A `ClearView` is not an RTV clear. Normalising these broke FFXV during the migration.
* **Binds are hooked for every shader stage** a feature can affect, not only PS and CS.
* **Device proxy for identity and reference count only.** All logic stays in the inline hooks. The proxy exists so Luma knows when the game is done with a device.
* **State with an object's lifetime lives in the object's private data.** A resource's mirror, a view's mirrored view, a shader's database entry, a sampler's mirror, a swapchain's state are attached with `SetPrivateDataInterface` (D3D11 and D3D12 objects both have it). The holder's final release is the cleanup and the destroy notification, so there are no side maps to erase from and no destroy hooks for these. Side tables remain only for what isn't an object (D3D12 descriptors) and for a lookup measured to be too slow through private data on a hot path. Destroy callbacks never call into the dying object.
* **DXGI hooks Core always installs:** the factory creation exports, `CreateSwapChain`, `CreateSwapChainForHwnd`, the swapchain's `GetDevice`, `GetDesc` and `GetDesc1` (the device proxy and the game's requested desc), and `IDXGISwapChain3::SetColorSpace1`, which records the color space the game sets in the swapchain's private data (Special K's GUID, with the format it was set for: a resize to another format forgets it) for what Luma draws on the swapchain. `Present`, `ResizeBuffers(1)` and `SetFullscreenState` are requested by Core's features.
* **Input blocking** covers the message loop, key state polling, raw input, DirectInput and cursor clipping (1.0 `overlay/input_hooks.cpp`).

## 6. Mirrors

**A game object is never modified. Anything Luma changes is a mirror.**

* Applies to textures, swapchain back buffers, samplers and views. For shaders and pipelines the equivalent is a variant.
* The game's view of its own objects stays exactly as created: descs, sizes, formats, heap offsets, GPU addresses.
* 1.0's "direct" upgrade (editing the desc at creation) is not ported. Every upgrade is what 1.0 calls indirect.
* One mirror core serves format upgrades, size scaling and the swapchain.
* A mirror is released with its original. Mirrors never keep a device alive.
* Content is kept in sync by redirecting writes, copies and clears to the mirror, and converting when data lands in the original (uploads, CPU writes).
* Costs to accept: the original's memory stays allocated, and D3D11 needs mirrored views plus a swap at every bind.

## 7. Feature activation

* No feature registry and no preprocessor feature flags. The only build defines are `DEVELOPMENT` and `TEST`.
* **Expensive development features are compiled out of Publishing** with `#if DEVELOPMENT`: trace capture, buffer and cbuffer tracking done for inspection, kept shader bytecode and disassembly, dumping, the MCP server, the dev panels, and the hooks only they need. Use the define only where it removes real cost (a hook, a per-draw branch, a field in a hot struct, a dependency). Cheap code that merely isn't shown is hidden by `Visibility`, not by a define.
* A feature knows from its own data whether it's needed (`Upgrade::IsNeeded()` is "are there rules"). Inputs are the game's declarations in `Init()` and files on disk (shader files, recipes, textures).
* **One need decision per feature, and only where there is something to decide.** `Startup.cpp` asks each feature once for the native functions it needs (`Shaders::GetD3D11Functions`, `Draw::GetD3D11Functions`...) and the handlers ask the same decision (`Shaders::IsNeeded`); a feature's files (database, variants...) don't decide on their own. A part is split off only where it's needed independently (Development-only tracking is the feature's own `DEVELOPMENT` branch). `IsNeeded()` exists only for what installs hooks or keeps tracked state: stateless helpers (`Draw`'s passes) and what every device gets (`Resources::Database`) have none.
* `Startup.cpp` is the one place that turns "needed" into installed hooks, once, after `Init()` and the settings load.
* Development builds treat every tracking feature as needed, so files can be added while the game runs.
* A needed feature can still be switched off at runtime by an ordinary setting.
* **One global switch, `Luma.Enabled`**, turns off everything Luma does to the image, immediately: variants aren't bound, mirrors aren't swapped in, custom passes and game draw callbacks don't run, composition passes the game's image through. Tracking keeps running (databases, descriptor and bind records, patch and compile queues, mirror bookkeeping), so switching back on needs no catch-up. It has a place in the overlay header and a hotkey, and every feature's draw-path entry checks it.
* Optional SDKs (NGX, FidelityFX, XeSS, DXP, NVAPI) are opt-in per game project through props, as in 1.0 (`UseLumaNGX`...). A feature whose SDK isn't linked reports itself unavailable.

## 8. Game API

Games include one header, `Luma.hpp`, and implement `Luma::Game::Init()`, which only declares things. Core is compiled with the game (section 16), so it sees the game's cbuffer structs and its `GameDeviceData` as types. `Luma.hpp` holds only `DllMain` and `IsEnabled` and includes the game API, one public header per kind under `Game/`: `Game.hpp` (the declarations made in `Init` and the runtime queries, `RunOriginal`, `UseOriginal` and `BindLumaShader` for the draw events included), `Events.hpp` (Luma's events, the draw events and the `Call` they get included), `Callbacks.hpp` (the native callbacks). `Game/Registry.hpp` is Core's private read side, and `Game/D3D11/D3D11DrawEvents.cpp` (`Game::DrawEvents::D3D11`) is the handlers' way of running the draw events.

* **`Game` is only the registration surface.** A declaration's type and storage belong to the module that owns the concept (shader names to `Shaders::Database`, redirects to `Shaders::Files`, defines to `Shaders::Defines`, the Luma cbuffer options to `Resources::LumaCBuffers`, bound-state groups to `Draw`); the `Game::` function forwards to it. `Game/Registry.cpp` keeps only what is the game's own: its info, its events, its callbacks (native and draw), its panels and its device data type.
* **Calls are grouped by what they are for.** `Game::Shaders::{SetDebugName, RedirectHashes, SetDefine, AddDefine}` for shaders, `Settings::Register` for plain settings, `Game::D3D11::OnShaderDraw` for draw callbacks by shader hash (the draw events themselves are `Events` fields, below), `Game::Frame::{Get, SetRenderResolution}` for the device's resolutions. Lists stay calls.
* **`Game::Options` is init-time configuration.** `Game::SetOptions(const Options&)`, once, in `Init` (a second call asserts and is ignored). It covers what is fixed for the process because it installs hooks, shapes cbuffer layouts or is compiled into every shader: `.cbuffers` (`Resources::LumaCBuffers::Options`: slots, the game's LumaData writer `update_luma_instance_data`, 1.0's `UpdateLumaInstanceDataCB`: `void(CB::LumaInstanceData&, const GameDeviceData&, const Frame::Resolutions&)`, which writes `data.GameData` at every draw with a Luma shader, `restore_after_draw`), `.draw` (`Draw::Options`: the bound-state groups), `.device_data` (`GameDeviceDataType::Of<GameDeviceData>()`). Each member's type is its module's. Defaults and anything a user or developer changes at runtime are settings, never options. `Info` stays `SetInfo` (identity, not configuration). Later steps add their members (sampler upgrade, swapchain policy) where they carry no API type; what does (upgrade rules) is its own call, section 4.
* **Free functions and registration.** No `Game` base class. A 1.0 `class X : public Game` becomes functions in an anonymous namespace plus registrations in `Init()`.
* **A function Core hooks has a Luma event, never a native callback.** `Startup::GetOwnedFunctions` lists every native function a Core feature may hook (present, resize, shader creation and binds, constant buffer binds, the bound-state cache's binds, the state resets, draws and dispatches). For those the game gets events (`Game::D3D11::Events`, `Game::D3D12::Events`, `on_...` fields set with `Game::SetEvents`): Core calls each at a fixed point of its own handling, where 1.0's Core called its `Game` virtual (`on_init_device` is 1.0's `OnInitDevice`, `on_init_swapchain` its `OnInitSwapchain` at creation and after each resize, `on_present` its `OnPresent`), so Core keeps tracking what the game does and the order of work is 1.0's. An event reports: it doesn't edit the native arguments or skip the call, except where its comment says so (`on_create_swapchain` edits a `Game::SwapchainDesc` before Core's own changes, `on_execute_command_list` can skip the execution, as 1.0's `PreExecuteCommandList`). **The draw callbacks are Core's events for draws and dispatches**, and 1.0's two draw virtuals are two fields of `Events`: `on_variant_draw` (`OnDrawOrDispatchCustomPipeline`) and `on_draw_or_dispatch` (`OnDrawOrDispatch`). They take a `Call` and return a `DrawResult` (skip or replace the game's call). `OnShaderDraw(hashes, fn)` (1.0's `draw_callbacks_by_shader_hashes`) stays a call, as lists do, and its callback runs in place of `on_draw_or_dispatch`. Any of the three requests the draw and dispatch hooks.
* **Native callbacks are for the other functions only** (`Game::D3D11::Callbacks`, set with `Game::SetCallbacks`). They keep 1.0's general shape: `Pre`/`Post`/`On...Destroyed`, native arguments, so a game's body moves over with little more than a signature change. Installing the hooks logs an error for a callback on a function Core owns, and a test checks that none is. A Core feature that starts hooking a function adds it to `GetOwnedFunctions` and turns the game's callback for it into an event, placed where 1.0 called the game. The exact set is free to improve where it clearly helps (one callback or event with a stage argument in place of six per-stage copies). Each such change is a row in section 17. Registering an event or a callback is what requests its hook.
* **UI callbacks.** A game can draw its own content into the overlay: a settings section, an info panel, about text, a dev panel (1.0's `DrawImGuiSettings`, `PrintImGuiInfo`, `PrintImGuiAbout`, `DrawImGuiDevSettings`). They use `UI::Widgets` so custom content looks like the rest. Plain settings still go through the registry.
* **Draw callbacks by shader hash** stay the main entry point (`Game::D3D11::OnShaderDraw(hashes, &fn)`).
* **The game's settings object** is Core's, as in 1.0: `Game::cb_luma_global_settings` (the LumaSettings cbuffer, a `CB::LumaGlobalSettings`), whose `GameSettings` member is the game's `CB::LumaGameSettings`. The game's settings bind to its fields (`Binding{&cb_luma_global_settings.GameSettings.custom_bloom}`) and its code writes them, on the presenting thread (section 10).
* **Per-device game data** is the game's `Luma::Game::GameDeviceData` struct (1.0's `GameDeviceData`, which games derived theirs from), declared by Core and defined by a game that has one (`Options::device_data`, read with `Game::GetGameDeviceData(device)`), holding only the game's own fields. Luma creates and destroys it with the device. What 1.0 kept in Core's `DeviceData` (resolutions) is `Luma::Frame`'s, which games read and update through `Game::Frame`.
* **Porting a game is a move, not a rewrite.** Allowed changes are exactly those the new API forces:
  * class members become functions, fields of `DeviceData` become calls into the module that now owns them;
  * settings declared through the registry instead of the game's own ImGui code;
  * code that a framework feature now provides is deleted in favour of it (settings framework, vertex buffer tracking, upscale tracking, managed textures).
* **No fixes, no cleanups, no behaviour changes** to game logic during a port. A bug noticed in a game goes in the step's notes.
* **No compatibility layer for the 1.0 API.** Games are ported to the new one.

### Game files

Everything a mod reads or writes at runtime is inside one `Luma` folder next to the game's executable. There is one folder per kind of resource, with Luma's shared files and the game's own in the same tree.

```
<game>/
  dxgi.dll                    the mod
  Luma/
    Luma.toml   Luma.log
    Shaders/
      Includes/               the shared library: Core/  Color/  Tonemap/  Effects/  Compat/
      Luma/                   Luma's own passes: Display/  Copy/  Sanitize/  Effects/
      Game/                   the game's includes (its Settings.hlsl, shared headers)
      *.hlsl                  the game's replacement and custom shaders, in subfolders if it wants
    Recipes/                  shader patches
    Textures/                 .dds registered in the Resource Database by file name
    Cache/                    rebuilt freely, safe to delete
      Shaders/                compiled and patched .cso, by what they were made from; the patch index
      Pipelines/              D3D12 pipeline library
    Dump/                     Development and Test: dumped game shaders and patched shaders
```

**The tree is an overlay of two sources.** The repository keeps the shared files once and each game's files apart, both in the layout above:

```
Luma2/Resources/
  Global/        Shaders/{Includes, Luma}   Textures/
  <Game>/        Shaders/{Game, *.hlsl}     Recipes/   Textures/
```

* Luma looks a file up in an ordered list of roots: the game's, then Global's. Packaging and the deploy step copy Global and then the game on top into `Luma/`, so a packaged mod has one root and the same lookups succeed. Development builds have the repository's `Resources` folder compiled in by the props (as 1.0's `SOLUTION_DIR`) and use it as the roots when it exists, so sources are edited in place. It is not a setting.
* **The game wins a name clash.** That is how a game overrides a shared include or pass (1.0's per-game `Settings.hlsl`) without editing it.
* **Includes are written from the `Shaders` root**: `#include "Includes/Color/Color.hlsl"`, `#include "Game/Settings.hlsl"`, the same line from any file at any depth. The compiler's include handler resolves them through the roots. No `../../` paths.
* What a file is comes from where it is and its name, as in 1.0: a hash in the name is a replacement, `Luma/` holds custom passes, nothing under `Includes/` or `Game/` is compiled on its own.

* Source folders hold source only. Compiled objects never sit next to `.hlsl` files.
* A release package ships a prebuilt `Cache/Shaders`, so users don't compile on first boot.
* **Nothing is done twice across boots.** The cache holds compiled shaders, patched bytecode, the patch index (which hashes a recipe matched *and which it didn't*, so later boots skip the pattern scan), shader reflection, and on D3D12 Luma's `ID3D12PipelineLibrary` for variant PSOs. Each is keyed by what it was built from (source hash, defines, patcher and Luma version). The pipeline library is discarded when the driver or adapter rejects it.
* A game's files move from the root `Shaders/<Game>` into `Luma2/Resources/<Game>` in the commit that ports it, with their `#include` paths updated to the new layout. A packaged 1.0 build of each game is kept before its move, as the reference.
* **`Global` is restructured, not copied as is.** 1.0's flat `Shaders/Includes` and `Shaders/Global` are regrouped by purpose when brought over in Step 1, for example:

  ```
  Global/Shaders/
    Includes/
      Core/         Common  Math  Settings  UI
      Color/        Color  Oklab  JzAzBz  DarktableUCS  MacLeodBoynton  ColorGradingLUT
      Tonemap/      Tonemap  ACES  DICE  Reinhard  CInfinityRolloff
      Effects/      Bloom  FXAA  SMAA  RCAS  XeGTAO  LensDistortion
      Compat/       DgVoodoo  HardwareBlendsEmulation
    Luma/           Luma's own passes: Display  Copy  Sanitize  Effects
  ```

  Only files move and include paths change. Shader code is not edited in the same commit. Until the swap 1.0 keeps its own copy, so a fix to shared shader code is made on both sides.

## 9. Settings

One registry for everything a user or developer can change: shader defines, shader patch (recipe) switches and environment values, overlay settings, Luma's own settings and each game's.

```cpp
struct Setting
{
   std::string_view key;         // Unique. Luma's own start with "Luma.", defines' with "Defines."
   std::string_view label;
   std::string_view tooltip;
   std::string_view category;    // UI section and TOML table
   Visibility visibility;        // Release | Test | Development
   Value default_value;          // bool, int32_t, float, EnumIndex, std::string, Key
   std::optional<Range> range;
   std::string_view format;
   std::span<const std::string_view> enum_labels;
   Source source;                // What a change does, see below
   bool (*is_enabled)() = nullptr;
   bool (*is_visible)() = nullptr;
   Kind kind;                    // Game | Luma | Define | Recipe | Development, set by the call that registered it
};
```

* **Sources** say what else a change does. The registry handles each generically:
  * `Value`: nothing. Read with `Settings::Get<T>(key)` or a change listener.
  * `Binding{float* target, float (*parse)(float)}`: writes a variable, usually a field of the Luma settings cbuffer (`cb_luma_global_settings.GameSettings`). This is the 1.0 `settings.hpp` pattern, kept so FF7R's and Metro's tables move over as they are.
  * `Define{name}`: a shader define. A change marks dependent shaders for recompile (immediate with Auto Compile, otherwise on request).
* **Registration has two steps, Core's then the game's,** with a call per responsibility, all on the same `Setting` underneath. Core registers its settings, its shader defines included, before the game's `Init`. The game then sets Luma's defines with `Game::Shaders::SetDefine(Defines::UI_DRAW_TYPE, 1)` (their default; still user-editable where visible), adds its own defines with `Game::Shaders::AddDefine({.name, .label, .tooltip, .default_value, .range, .visibility})` (key `Defines.<NAME>`, category and `Define` source generated; int or bool), registers plain settings with `Settings::Register`. Recipes describe themselves (their `meta:`) and get their settings from Core. Core's own defaults use `Settings::SetDefault(Id<T>, T)`.
* **Typed ids.** `Settings::Id<T>{key}` gives `Get`, `Set`, `Queue` and `SetDefault` their type: a wrong one is a compile error. Every id Core owns is in `Settings/Keys.hpp` (`Settings::Core::*`, `Shaders::Defines::*` as `Defines::Id<T>`, an id with the define's name), and the registration tables take their `.key` from it. String-keyed functions remain for TOML, MCP and the UI.
* **Kinds.** A setting carries the `Kind` its registration call gave it: `Game`, `Luma`, `Define`, `Recipe`, `Development`. The settings view shows Luma's "Luma" and "Overlay" categories first, then the game's categories (and its settings panel), then one Shader Defines section (Auto Compile, then Luma's and the game's defines together), then Recipes, then Luma's other categories; `Development` settings are its last section, "Development Settings", in Development builds (with the game's development panel). Auto Compile keeps its key and place in the file (`[Luma.Shaders]`), it's only drawn with the defines. `Luma.toml` has `[<Category>]` for the game, `[Luma.<Category>]` (`[Luma]` for its Luma category), `[Defines]` keyed by define name (define names are global to the compiler: a name registered twice is refused), `[Recipes]`, `[Development]`.
  * `Recipe{identity, env}`: enables a patch or sets one of its environment values. A change re-patches.
* **One UI.** A setting is drawn by its value type through `UI::Widgets`, grouped by category. Games don't write ImGui code for settings. A game-drawn panel remains possible for the rare custom view.
* **One store.** `Luma/Luma.toml`, a table per kind and category (above).
* **Threading.** There is no single render thread: on D3D12, and on D3D11 with deferred contexts, other threads record the next frame while one presents. So:
  * *One writer, at present.* Every change (overlay, MCP, a game's own call) is applied on the presenting thread at the present boundary. That stays the rule: it orders writes and is the closest thing to a frame boundary there is.
  * *Readers never lock.* Scalar values are atomics, readable from any hook on any thread. Strings are read under a lock and never on a per-draw path.
  * *A change is never assumed to land on a frame boundary.* For one frame, command lists recorded before and after the change can both execute. A value that only tunes a pass tolerates that.
  * *A change that creates or destroys objects* (upgrades on or off, output mode, SR, sampler bias) never frees what a recorded command list may still use: the old objects are released by the rule in section 4 (after the lists that bound them finish), and hooks keep working with either state until then.
  * *A change that rebuilds* (defines, recipes) runs as a job (`System::Jobs`) and publishes the new variant atomically.
  * *Background work is one pool.* Compiling, dumping, creating pipelines and loading files run as `System::Jobs`, on a private Win32 thread pool whose size is the `Luma.Advanced.WorkerThreads` setting (applied at present, like every change). A feature starts no thread of its own, except for a loop that blocks on I/O (the MCP server's pipe). A job's `work` runs on a worker: it reads settings (atomics) and calls no presenting-thread code; it opens a `Hooks::Scope::Guard` before any API call, since `System` can't include `Hooks`. Its `done` runs on the presenting thread at the next present and is where the result is published. Jobs never check `Luma.Enabled`: tracking and queues keep running. FXC is used by one thread at a time (1.0: "apparently FXC isn't thread safe so ideally we should only ever access it from one thread at any given time"): every `Shaders::Compiler` call, and any use of a reflector it returns (`Compiler::Lock`), holds one lock for its duration. The game's shader creation, which reflects, therefore waits for the compile in flight, never for a whole build.
* **Operable through MCP.** `luma_get_settings` and `luma_set_setting` read and write the same registry, so everything the UI can change, an agent can.
* Dev sliders (1.0's `LumaDevSettings`) are settings with `Visibility::Development`. `luma_dev_values`, `luma_set_dev_value` and `luma_ab` operate on those.

## 10. Shaders

* **Hash** is CRC32 of the bytecode, as in 1.0, so every existing `Name_0xHASH.ps_5_0.hlsl` and `.cso` keeps working.
* **Three ways to change a shader**, all producing a variant: a replacement file matched by hash, a patch (a DXP recipe or a game's bytecode callback), and Luma's own custom shaders that run between game passes.
* **Variants are one model.** A game shader hash has a list of variants, each with a source (`File`, `Patch` from Step 3) and a priority; the bound one is the highest-priority variant that compiled and the device took. Files and patches behave the same from there: callbacks, requests and fulfil, trace. `Events::on_variant_draw` (1.0's `OnDrawOrDispatchCustomPipeline`) runs first for any draw or dispatch with a variant bound, before the per-hash or generic callback and before its requests are fulfilled; `Game::D3D11::UseOriginal(call, stage)` (1.0's `UsePipelineVariant`) puts the game's shader back. Trace records the variant used and those available, and its per-shader picker (Auto, Original, each variant) forces the choice for a hash until cleared, in the Trace tab and MCP `shader_state variant=`.
* **Custom shaders are found, not declared.** A Luma shader file (under `Luma/`, or a game's `Luma_*.hlsl`) declares its programs on lines `// Luma: <target> [name] [entry=<entry>] [<DEFINE>=<value>...]`: the target gives the stage, an unnamed program has the file's name, several programs of one file name theirs (`Luma_DrawPurple_PS COLOR=...`). Core scans the roots at init and compiles every declared program; a file without a declaration line (an include, a sample) isn't compiled. Programs are looked up by `"name"_h`. Nothing is registered by code, a game ships files.
* **Variants request their resources, a fulfil step binds them.** A program (a replacement, one of Luma's shaders, a patch in Step 3) carries the `Resources::Database` entries it reads and at which registers: from its reflection (a cbuffer named like an entry) or its recipe. Where it runs, the fulfil step (`Resources::Database::D3D11::BindRequested`) uploads each entry where its contents changed for that context and binds it there, nothing else. **The game's draw events run first** (`on_variant_draw`, then the per-hash callback or `on_draw_or_dispatch`); a draw's requests are fulfilled only when they all return None and the trace doesn't skip it (1.0 bound Luma's cbuffers after its callbacks). A callback that draws with one of Luma's shaders binds it with `BindLumaShader`, which binds that shader's requests and stands for 1.0's `updated_cbuffers`: Core then fulfils nothing for that call. `LumaSettings` and `LumaData` are such entries, requested by **every compiled Luma shader** (a replacement, one of Luma's own) at the game's slots whatever its reflection says, so they are bound to every stage running one (1.0); reflection adds requests for other entries. Their slots are `Options::cbuffers` (1.0's b13 and b12 by default), passed to the compiler as defines. Their structs are one definition for both sides: `Includes/Core/CBuffers.hlsl` (`CB::LumaGlobalSettings`, `CB::LumaInstanceData`, each ending with the game's part from `Game/GameCBuffers.hlsl`), which `Settings.hlsl` and `Common.hlsl` declare the cbuffers with and Core compiles as C++; what precedes a game's part is a multiple of 16 bytes on both sides (`static_assert`s in `Resources/LumaCBuffers.hpp`, and a test comparing every member's offset with FXC's reflection). Their uploads follow 1.0. LumaSettings is Core's `cb_luma_global_settings`, written on the presenting thread only (Init, the present, swapchain creation and resize callbacks, the settings registry, the overlay); the end of every present and `Game::MarkLumaSettingsDirty` publish it (a copy under a small mutex, where 1.0 held `s_mutex_reshade`), and an upload copies the published one, so a context never reads it while it's written and never uploads contents older than the stamp it records. A game that writes it elsewhere in a frame (a swapchain callback) calls `MarkLumaSettingsDirty` after, on that thread. LumaSettings is uploaded by a context when it's dirty: its stamp (1.0's `cb_luma_global_settings_dirty`, per context) is the frame plus `Frame::GetGeneration()` plus a settings generation, so it moves at every present (and again after the game's present callback and the overlay, which may change it), when a device's resolutions change, and when the game calls `Game::MarkLumaSettingsDirty` after changing it within a frame. Its sizes and frame index are written at the upload from the context's device. LumaData is written at every bind (Luma's part, the pass's `Resources::Database::CustomData`, the render scales, then the game's writer on the whole struct, 1.0's `UpdateLumaInstanceDataCB`) and uploaded when the bytes differ from that context's last upload (1.0's compare). The writer gets the device's `GameDeviceData` and resolutions, which the context found once with its device: nothing is looked up per draw. `restore_after_draw` (1.0's `ENABLE_AUTO_CBUFFER_RESTORATION`) records the game's buffers at the two slots and binds them back after a draw whose requests were fulfilled, and after a callback's call that bound Luma's shaders with `BindLumaShader`. Step 3's recipe bindings and Step 4's textures go through the same request and fulfil.
* **Compilation is a job (`System::Jobs`) with progress.** The overlay shows a progress bar while it runs. A variant that isn't ready falls back to the original shader and says so in trace.
* **Cache** compiled and patched bytecode on disk keyed by source hash, defines and patcher version.
* **Shader roles are named once.** A game's hash lists also name the shaders for trace (`Game::Shaders::SetDebugName`, replacing `forced_shader_names`).
* **Draw state.** Two tools from 1.0 are kept, both parametrised so nothing is copied that isn't used:
  * the state stack (`DrawStateStack`): saves and restores the context around Luma's own passes. It takes a mask of state groups (shaders, per-stage SRVs, cbuffers and samplers with slot ranges, targets, blend, depth, rasterizer, viewports and scissors, input assembler) in place of 1.0's three fixed modes, which remain as presets. The default preset is what a fullscreen pixel pass touches;
  * **Stages are sets, not masks.** A set of stages (the ones a call uses, where a variant runs, where Luma bound its buffers, which stages a state group covers) is a `Shaders::StageSet` (`Shaders/Stages.hpp`): a struct over `std::bitset`, with `GRAPHICS_STAGES` and `COMPUTE_STAGES`. The `ShaderStage` it holds is `Common/ShaderStage.hpp`, below the features because Hooks names stages too. No raw integer masks or `1u << stage`.
  * the bound-state cache (`ENABLE_DRAW_DISPATCH_DATA_CACHE`): what the game has bound, recorded at bind so draw callbacks read it without querying the API. A game declares which groups it reads (`Options::draw`, three bools: `viewports` and scissors, `blend`, `depth`), and only those are recorded. Development records all of them for trace. Views and buffers aren't cached (the API unbinds them on its own); the shaders bound are Variants' per-context record.
* D3D12 specifics (PSO variants keyed by formats, root signature extension) follow section 4. The details in `Design.md` "DX12 binding model" are the fallback design, not the starting point.

## 11. Resources

Framework systems replacing code that 1.0 games each wrote for themselves.

* **Resource Database.** Named entries (`"name"_h`) any module, patch or shader binding can resolve. Covers Luma's textures, DDS files loaded from the `Textures` folders, and game resources a mod tags. Entries declare what invalidates them (swapchain resize, render resolution change) and are recreated by the framework. Replaces `LumaTexture` / `ManagedResources` and the per-game `Clean*Resources` functions. Its core is in from Step 1: entries are registered at init, before any device, by Core from what's on disk (Luma's shaders from the scan, its cbuffers) and fixed from then on (a file added while the game runs needs a restart); each device gets an object per entry, released when the game releases the device. A game registers entries only for what it captures from the game at runtime. **Core's entries are read-only:** each entry has an owner, and a game registering a name Core owns is refused (asserted and logged). Shader files keep their override rule instead (the game's file wins over Global's, section 8), logged in Development so it's never accidental.
* **Upgrade.** Declarative rules: formats, size filters, aspect ratios, LUT size and dimensions, per-shader-hash targets. Rules produce mirrors.
* **Chains.** A seed (the tonemap target, the SR output) and everything derived from it by copy (`Direct`) or by being drawn from it (`DirectAndIndirect`) get mirrors with the same treatment.
* **Scaling.** One tracker for "this was render resolution and is now output resolution": scaled mirrors, viewport and scissor rescale, copy box and clear rect rescale, stencil stand-ins. It replaces FF7R's early-upscale code, FFXV's `upscale_tracking.hpp`, Granblue's and Quantum Break's upscale code and Core's scale chain. It is the same system as Upgrade with a size change.
* **Samplers.** Sampler mirrors for LOD bias and anisotropy, refreshed when the SR scale changes.
* **Geometry.** Vertex and index buffer tracking, so a vertex shader can be patched to output motion vectors and be fed the previous frame's data. Replaces the Borderlands `patched_draws.h` family and `ENABLE_DRAW_STATE_VERTEX_BUFFER_CACHE`.
  * Binds are tracked, and **writes are tracked as they happen** (initial data, `Map`/`Unmap`, `UpdateSubresource`, copies into the buffer), so the cache builds up with the frame instead of being read back at draw.
  * Each tracked buffer keeps a current and a previous copy, swapped at present.
  * Only buffers a game's filter selects are tracked (usage, size, stride), since every tracked write is a copy.
  * Dynamic buffers reused for different meshes within a frame (`NO_OVERWRITE` rings) are keyed by the written range, not by the buffer.
  * D3D12 upload heaps stay mapped and their writes can't be seen. There the copy is taken at draw. `LUMA_UNIMPLEMENTED` until a D3D12 game needs it.
* **Constant capture.** "Give me the contents of the cbuffer bound to slot N of this shader" as a framework call, with the per-API mechanism behind it (D3D11 `Map`/`UpdateSubresource` interception, D3D12 upload heap ranges).
  * **Typed through reflection.** A captured cbuffer is mapped to named, typed fields from the shader's reflection data. Games usually strip it, so the fallbacks are, in order: the reflection of Luma's replacement shader for that hash (its HLSL declares the cbuffer), a layout the game mod registers, raw `float4` rows. Trace, the dev UI and `luma_read_cbuffer` all show the typed view.

## 12. Swapchain and display

The largest rework from 1.0.

* **The game never sees the real swapchain buffers.** Luma owns the real swapchain. The game's `GetBuffer` returns Luma textures in the size and format the game asked for, and `GetDesc*` reports what the game asked for.
* **Output mode is a runtime setting**, because the game holds no reference to the real buffers:
  * `SDR`: 8 or 10 bit, sRGB or gamma 2.2;
  * `HDR10`: `R10G10B10A2_UNORM` with `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`, PQ-encoded BT.2020 written by the composition pass, optional HDR10 metadata. The default for HDR;
  * `scRGB`: `R16G16B16A16_FLOAT` with `G10_NONE_P709`. Kept for development and for displays or capture paths that prefer it.
* **Always flip model**, tearing allowed, exclusive fullscreen replaced by borderless where the game permits.
* **Four resolutions, never conflated** (`Frame`):
  * *render*: what the game renders the scene at;
  * *output*: the size of the back buffer the game asked for, and the target of upscaling;
  * *swapchain*: the size of the real buffers Luma presents;
  * *display*: the monitor's mode.

  Output and swapchain are equal at first, but they are separate fields from Step 5 on, and composition scales between them when they differ. That is what later allows presenting a fixed-resolution game at the display's resolution.
* **Display composition is the only writer of the real back buffer.** Luma's overlay draws after it. Composition is a chain of stages, **each one a setting that can be switched off on its own** and is an identity when off:
  1. decode of the game's image to linear;
  2. UI composite (the separated UI, its paper white, the background tonemap under it);
  3. gamma correction;
  4. gamut mapping;
  5. scale from output to swapchain resolution;
  6. encode for the output mode (cannot be removed, but can pass through);
  7. development views: debug draw of a captured view, 2x zoom, SDR/HDR split.

  The stage choices that 1.0 makes with shader defines (`GAMMA_CORRECTION_TYPE`, `GAMUT_MAPPING_TYPE`, `UI_DRAW_TYPE`...) stay defines, exposed as settings. The on/off switch per stage is added on top.
* **Display information is backend independent** (`System::Display`): HDR supported and enabled, peak and paper white, SDR white level, turning Windows HDR on, reacting to the window moving to another display.
* **Spoofing** (`Display`): what the game is told about the display (HDR capability, color spaces, adapter vendor), so games without an HDR toggle take the path the mod wants.

## 13. UI language

One look and one behaviour for every panel. Reference: the 1.0 screenshots show what to fix (booleans as 0/1 sliders, labels at inconsistent positions, dangerous buttons that look like safe ones, three list panels each laid out differently).

* **`UI/Style`** holds every color, size and spacing as named tokens. No literal colors or pixel sizes in panels.
  * Dark neutral surfaces in three depths (window, panel, control), separated by 1px borders. Simple, not flat: depth and color are part of the language, not decoration added per panel.
  * **Depth.** Section headers and the title bar have a subtle vertical gradient and a 1px highlight on their top edge. Controls are a step lighter than the panel they sit on and lift another step on hover. Popups, tooltips and toasts have a drop shadow. The active tab and the focused control carry an accent underline or outline.
  * **Color.** One accent (Luma blue) for selection, focus, slider fills, toggles that are on, and primary buttons. Semantic colors: green success, amber warning, red danger or error, violet for Luma-injected things in trace. Each section header has a left accent bar and a tinted icon.
  * **Icons are tinted, never plain text color by default.** An icon font is one color per glyph, so color comes from the tint: each category has its own icon color from a small fixed palette (Display, Upscaling, Post Processing, Overlay, Advanced, Development...), state icons use the semantic colors, brand marks use the brand's color, and a disabled icon is grey. The palette is tokens in `UI/Style` like everything else.
  * A changed setting shows it: its reset icon is accent-colored and its label is a step brighter.
  * 4px spacing unit, 4px rounding on controls, 6px on panels. Sizes scale with the font size setting.
* **`UI/Icons`**: Font Awesome Free, the Solid and Brands fonts, vendored whole under `Luma2/External/Fonts` with their license (the fonts are SIL OFL 1.1, the icons CC BY 4.0) and embedded in the DLL as Windows resources. No subsetting and no generation step: adding an icon is naming its codepoint (IconFontCppHeaders' `IconsFontAwesome6.h` and `IconsFontAwesome6Brands.h`). Icons replace the `"U"`, `"R"`, `"X"` placeholders. An icon-only button always has a tooltip.
* **`UI/Widgets`** is the only place that calls raw ImGui controls for settings:

  | Widget | Use |
  |---|---|
  | `SettingRow` | label column on the left at a fixed width, control, reset icon when changed, a state tag ("pending" on a define the shaders weren't built with yet) |
  | `Toggle` | every boolean |
  | `Slider`, `Combo`, `Segmented`, `KeyBind`, `TextInput` | by value type; a choice among a few options (`MAX_SEGMENTS`, five) is a `Segmented` control, among more a `Combo` dropdown |
  | `FilterChip` | a compact on/off button for list filters |
  | `Tag`, `Banner` | a small inline state label ("Variant", "Original", "pending"); a tinted message across the width with an optional primary action (a pending compile with its Compile button) |
  | `DataTable` | a table with a header row and aligned columns for state with several columns (views, samplers, cbuffer contents), scrolling on its own above a height |
  | `Section` | collapsible category header |
  | `Button` | `Primary`, `Secondary`, `Danger` (danger asks to confirm) |
  | `ListDetail` | left list with filter, right detail pane. Used by Trace, Shaders and Resources alike |
  | `PropertyGrid` | key and value table for state dumps, with copy on click |
  | `HashChip` | a shader or resource hash: monospace, colored by kind, click to copy, hover for the name |
  | `ProgressBar`, `Spinner` | shader compilation, patching, capture in progress |
  | `Toast` | transient messages, replacing `MessageBox` and on-screen text |
  | `StatusBar` | bottom strip: API, output mode, resolutions, compile progress, warning count |

* **One look per role.** Every category header is the same `Section` widget, whether it holds settings, credits or a game's custom content. Lists, links and captions come from widgets too, not raw ImGui calls with their own spacing.
* **Sliders apply on release.** While dragging, the control shows the value. The setting changes, its source runs and it is saved when the mouse is released or the typed value is confirmed. A setting can opt into live updates when its effect is cheap and worth previewing (paper white, peak white).
* **Luma's own settings are grouped by what they are**: an `Overlay` category (overlay key, font size), separate from rendering categories.
* **Link buttons carry the service's mark** (Ko-fi, Buy Me a Coffee, Discord, GitHub) from the Brands font, in the service's own color. A mark the font lacks falls back to a generic Solid icon.
* **Visual feedback for work in progress.** Shader compilation and patching show a determinate bar in the status bar and a toast when done or failed.
* **Few options are segmented, many are a dropdown.** Five or fewer choices (an enum setting, the trace's variant picker, a stage's cbuffer slots) are a `Segmented` control showing them all; more are a `Combo`.
* **A state is said once, where it applies.** A change that needs work to apply (defines while Auto Compile is off) shows one banner with the action, and a tag on each pending row with what it was built with in its tooltip. Nothing is tagged with what a change *would* do.
* **Layout.**
  * The window's header (game, version, the API, the Luma switch) and, in Development, the tab bar are pinned at the top; the game's links and the status bar are pinned at the bottom. Only the content between them scrolls.
  * The window is opaque by default; its background opacity is an Overlay setting.
  * Release: one window. Settings by category, then an About section like 1.0's About tab (the game's description, its about panel, the credits, what Luma uses, version and build).
  * Development: adds tabs for Shaders, Trace and Info (Resources, Patches and Log as they're built). Dev-only settings appear in the same settings view, in its Development Settings section, not in a separate tab.
* **HDR swapchains.** The overlay's colors are sRGB-encoded for an SDR buffer. On an HDR swapchain ImGui's pixel shader linearizes them (gamma 2.2), scales them to the overlay's paper white (the Overlay "HDR brightness" setting, 203 nits by default, BT.2408's reference white) and encodes them for the swapchain's color space: scRGB (an FP16 buffer with `G10_NONE_P709` or no color space set) or PQ (`G2084_NONE_P2020`, full or studio range, in any format: FP16 can carry PQ). The shader is compiled once; the encoding and paper white are its constants, so the live slider never recompiles. The color space is what the game last set with `SetColorSpace1` (recorded in the swapchain's private data, as 1.0, ReShade and Special K do); other buffers are drawn as they are. Blending happens in the buffer's encoding, as under ReShade, so linear in scRGB. This is what 1.0 got from ReShade's overlay.
* **Trace list.** A draw or dispatch is one entry with all its stages, labelled by its primary shader (pixel, else vertex, else compute). Its filters (`Trace::Capture::Filter`, shared with MCP's `trace_list`) match any stage: a stage, a hash, a text matching part of a hash or debug name; draws without a pixel shader are hidden unless "Vertex-only draws" is on (MCP's `include_vs`; 1.0 hid vertex shader entries), Luma's passes can be hidden, and "Unique" keeps the first call of each shader. The detail is grouped in sections with the same tables: shaders (a variant tag and the variant picker), the call, output targets, shader resources, unordered access, constant buffers (a slot selector per stage, the selected slot's typed fields or float4 rows, Track), samplers, blend, depth stencil, rasterizer, viewports.
* **Track.** 1.0's constant buffer tracking: a captured call's slot of a shader is read again every frame (that call's instance of the shader in the frame), its live values shown in place of the captured ones, the words that changed in the last 60 frames highlighted. A copy made on a deferred context is read only once the command list it ended in was executed on the immediate context; until then, or when the call didn't happen in a frame, the last values are shown as stale.
* **Detached UI (optional, Development first).** ImGui's docking branch multi-viewport lets a panel be dragged out of the game window into its own OS window. Its swapchains are created inside a Luma scope. If it proves fragile with a game's fullscreen handling it stays off for that game.
* UI code uses ImGui's public API. `imgui_internal.h` is avoided so the panels could be hosted by another ImGui (section 14).

## 14. Compatibility

Within reason. A tool that needs Luma's design bent around it is dropped.

* **No ReShade dependency.** Luma 2.0 includes no ReShade header, source, font or other file, and the submodule is not on any Luma2 include path. Compatibility is achieved from the outside only.
* **ReShade.** Installed next to Luma as `ReShade64.dll` or `ReShade32.dll` and chain-loaded: Luma's exports forward to it when present, so it sits between the game and DXGI as usual while Luma's inline hooks sit below both.
  * ReShade's own rendering is recognised by the calling module (the return address is inside ReShade's dll) and marked foreign: not resolved, not redirected, not traced. No addon is registered.
  * ReShade asks for the back buffer itself, so Luma chooses what it gets by caller: the real back buffer (effects on the composed image, as under 1.0) or the game's buffer (effects before composition). Which one, and where composition then runs, is decided in Step 8 by trying it.
  * Luma always draws its own overlay. Hosting it inside ReShade's is dropped: it needs ReShade's ImGui headers.
* **RenderDoc.** Works through the export table rule in section 5. Nothing else is added for it.
* **OptiScale and other dll proxies.** `LumaDllName` lets a game build under another proxy name when `dxgi.dll` is taken, and Luma's exports can chain to a configured dll. No code to cooperate with their hooks.
* **Overlays** (Steam, RTSS, Discord): inline hooks chain with theirs. Work they submit between the game's last draw and `Present` is not attributed to the game.

## 15. Code style

* **C++23**, MSVC. Structs and free functions in namespaces. `Namespace::Function(object)` over `object.Function()`. No inheritance or virtuals unless a COM interface requires them.
* **Names.** `PascalCase` functions, types and namespaces. `snake_case` variables. `ALL_CAPS` constants and macros. Fixed-width integer types.
* **Files.** Every file and folder is `PascalCase`, source and resources alike (`Resources/D3D12/D3D12Mirrors.cpp`, `Luma/Shaders`). A file for one API is `<Api><Module>`, in its feature's `<Api>` folder (`Hooks/D3D11/D3D11Hooks.cpp`, `UI/D3D12/D3D12Renderer.cpp`): no two files share a name anywhere in the project (MSVC object files, search), the folder only groups them. Game folders carry the game's title. Shader files keep their `Name_0xHASH.ps_5_0.hlsl` form.
* **RAII.** `ComPtr` for every COM reference Luma owns. State tied to a D3D object lives in its private data.
* **Headers hold the public interface only.** Private code is in the `.cpp` in an anonymous namespace. No `Detail` or `Internal` namespaces, no state in headers.
* **One `.cpp` per module** until it's hard to work in, then split by area.
* **Lock order is written where each mutex is declared.**
* **Comments are brief and say why.**
  * No file preambles, no banner comments, no description of what a file is for.
  * No comment per field or per function restating its name.
  * One line where a reader would otherwise be surprised: a constraint, an ordering, a measured fact, a 1.0 behaviour kept on purpose.
  * History belongs in commit messages and in section 17.
* **Templates, not code-generating macros.** What differs between copies of code (a shader stage, an interface version, a vtable) goes in a traits type or a template argument (`Hooks::D3D11::StageTraits`, one detour template per method kind). A macro remains only for what C++ can't express: the C slot table shared with `slots.c` (`Slots.h`), linker directives (`DXGIExports.cpp`), and the build switches and asserts (`DEVELOPMENT`, `ASSERT_ONCE`, `LUMA_UNIMPLEMENTED`).
* **Formatting is clang-format's job.** `Luma2/.clang-format` is the style: Microsoft base, 3 spaces, braces on their own line except namespaces, no column limit, pointers with the type, includes not sorted.

Suggestions for enforcement, to be adopted in Step 0:

* Pin clang-format 21 (the version CI already uses) and say so in the file's first line.
* Add to `.clang-format`: `AllowShortIfStatementsOnASingleLine: WithoutElse` to match 1.0's compact guard clauses, `SeparateDefinitionBlocks: Always`, `QualifierAlignment: Left`, `InsertBraces: false`, `KeepEmptyLines: {AtStartOfBlock: false}`, `MaxEmptyLinesToKeep: 1`.
* Extend `.github/workflows/lint.yml` with a job that checks `Luma2/Source/**` in full (not changed files only) and fails the build, since the tree starts clean.
* Point the pre-commit hook at `Luma2/Source` with `--style=file` and drop its clang-tidy pass, which has no compile database.
* Add an `.editorconfig` with the indent and charset so editors agree before clang-format runs.
* Later, once a compile database exists (`msbuild -t:ClCompile` through a generator, or Clang Power Tools), add `.clang-tidy` with `readability-identifier-naming` to enforce the naming rules, and nothing else at first.
* Game folders are formatted only when ported. A port commit doesn't mix reformatting with the move: format in a commit of its own.

## 16. Build

* **Core is compiled into each game, as in 1.0.** `Source/Core/Core.vcxitems` is a shared items project: each game project (and `Tests`) imports it and compiles Core's sources with its own include paths and defines, as 1.0's games included `core.hpp`. Core keeps its folders and modules (no single header); `Luma.hpp` is the one header a game includes. Core's sources are found by folder (a new file needs no project edit), their objects under `Core\<folder>` of the project's intermediate folder. A game's `Resources/<Game>/Shaders` is on its include path before Global's (`Luma2.Game.props`), so Core includes the game's `Game/GameCBuffers.hlsl`; Tests has a stand-in in `Source/Tests/Game`.
* A new game project is a copy of FF7R's `.vcxproj`: it imports `Luma2.Configurations.props`, `Luma2.Common.props`, `Luma2.Game.props` and `Core.vcxitems` (the `Shared` import group), and lists its own `Main.cpp`. The dll exports file is picked by `LumaDllName` and the fonts resource comes with Core. Release builds link with LTCG.
* Configurations: Development-Debug, Development-Release, Test-Release, Publishing-Release.
* x64 first. Win32 is added with the first 32-bit game (`Plan.md` Step 9).
* Shared settings live in `Props/`. A game project is a short file importing them.
* New third-party code goes under `Luma2/External`. Existing submodules are used from `Source/External`.

## 17. Deviations from 1.0

Add a row whenever 2.0 behaves differently from 1.0 on purpose, or when an improvement is noticed and deferred.

| What | 1.0 | 2.0 | Why | Status |
|---|---|---|---|---|
| Texture upgrades | direct (desc edited) or indirect (mirrored) | mirrors only | the game's view of its objects stays true, one code path, works on D3D12 | decided |
| Swapchain | upgraded in place at creation, format fixed until recreated | Luma-owned real swapchain, game gets its own buffers | output mode switchable at runtime, proper HDR10, no `GetDesc` spoofing needed | decided |
| HDR output default | scRGB in Development, HDR10 in FF7R Publishing | HDR10 by default, scRGB selectable | cheaper, better overlay and frame generation compatibility | decided |
| Feature selection | `ENABLE_*` defines per game, unity build | runtime, from declarations and files | no preprocessor feature flags (section 7), no silent Publishing-only breakage | decided |
| Build | `core.hpp` included by each game's `main.cpp`, one translation unit | Core's modules compiled into each game project (`Core.vcxitems`), `Luma.hpp` the one header | 1.0's model kept (Core sees the game's types), organized in modules | decided |
| Luma cbuffer structs | `cbuffers.h` mirrors `Settings.hlsl` and `Common.hlsl` by hand, the game's structs from `GameCBuffers.hlsl` | `Includes/Core/CBuffers.hlsl` compiled by FXC and C++, the cbuffers declared with its structs; layout checked by `static_assert`s and against FXC's reflection in a test | one definition, no hand mirror | decided |
| LumaSettings contents | `cb_luma_global_settings` copied under `s_mutex_reshade` (shared), the UI changing it under it (unique) | written on the presenting thread only, published (copied under a small mutex) at the end of every present and by `MarkLumaSettingsDirty`; uploads copy the published one | contexts upload on other threads; an upload's contents are never older than its stamp | decided |
| Game interface | `class Game` virtuals, overrides detected at compile time | registered free functions, registration requests the hook | no base class, same effect | decided |
| Game API names | `GameInfo`, `OnInit`, `forced_shader_names`, `redirected_shader_hashes`, `DrawOrDispatchOverrideType`, `original_draw_dispatch_func`, `is_custom_pass`, `OnDrawOrDispatchCustomPipeline` | `Game::Info`, `Game::Init`, `Game::Shaders::SetDebugName`, `RedirectHashes`, `Game::DrawResult`, `RunOriginal`, `Call::variant_stages`, `Events::on_variant_draw`, `on_draw_or_dispatch` | one name per concept in the game API; the 1.0 names are kept here, not in code comments | decided |
| Settings | per-game ImGui code, `settings.hpp` in two games, shader defines and recipes each with their own UI and storage | one registry, one UI, one store, MCP access | section 9 | decided |
| Config file | get/set API shaped like ReShade's | typed registry over TOML | no ReShade-shaped API | decided |
| Per-frame flags | `has_drawn_*`, `camera_cut`... redeclared in each game's device data | `Frame` pass markers and common frame fields | same flags in dozens of games | idea, decide in Step 7 |
| Camera data for SR and AO | each game extracts jitter, FOV, near plane into its own fields | a `Frame` camera model the effects read | 35 games do motion vectors or SR | idea, decide in Step 7 |
| D3D12 swaps | (no D3D12) | views and samplers at the copy into a shader-visible heap (at `Create*View` only for views written straight into one), RTV/DSV handles at bind, draw-time check only for cross-bind decisions (PSO render target formats) | same model as D3D11's bind swap; `Luma.Enabled` and upgrade toggles act at the next copy without rewriting the game's heaps; no shadow heaps unless a game forces them | decided, Step 4 |
| In-place texture upgrade | FF7R and most 1.0 games edit the desc at creation (`enable_indirect_texture_format_upgrades` off) | not built: mirrors only | §6; games shipping DLSS or Streamline, where mirrors could be missed, rarely need upgrades | idea, as a per-game option when a game needs it |
| Object state | maps keyed by pointer, erased in destroy handlers | private data on the object | cleanup is the object's own release | decided |
| File layout | `Shaders/<Game>` with `.cso` next to `.hlsl`, config and log next to the dll | one `Luma/` folder per game with `Shaders`, `Recipes`, `Textures`, `Cache`, `Dump`, config and log | section 8 | decided |
| Boot work | shaders cached, patches re-scanned each boot | patch index with negative results, reflection and D3D12 pipeline library cached | no repeated work on later boots | decided |
| Resolutions | render and output | render, output, swapchain, display | output can differ from what is presented | decided |
| Composition | one shader configured by defines | same shader, each stage also switchable on its own | isolate a stage when debugging or when a game does that stage itself | decided |
| Draw state stack | three fixed modes | mask of state groups, the modes as presets | skip copies a pass doesn't need | decided |
| Vertex buffer cache | per game, read at draw | framework, built from tracked writes, current and previous swapped at present | less work per draw, shared | decided |
| Cbuffer display | raw values | typed fields from reflection, with fallbacks | readable trace and MCP output | decided |
| File names | `snake_case` in Core, mixed elsewhere | `PascalCase` for every file and folder | one convention | decided |
| Shared shader layout | flat `Includes` and `Global` | grouped by purpose, include paths updated | easier to find things | decided |
| ReShade | host, then optional addon | chain-loaded guest, detected by calling module, no ReShade file used | no dependency on it | decided |
| Icon font | placeholders (ReShade used Fork Awesome) | Font Awesome Free Solid and Brands, vendored whole and embedded as resources | has the brand marks, no generation script, no ReShade file | decided |
| Deployed layout | `Shaders/Global` and `Shaders/<Game>` | one `Luma/Shaders` tree, an overlay of Global and the game with root-relative includes | one place to look, games can override shared files | decided |
| Releasing Luma objects | released on reload, D3D11 keeps them alive | per command list references, released after the lists that bound them execute (D3D12) | reload and setting changes while other threads record | decided |
| Mod toggle | "Mod Enabled" checkbox | `Luma.Enabled`: all image changes off at once, tracking keeps running, hotkey | instant A/B without losing state | decided |
| Sliders | change while dragging | change on release, live by opt-in | a drag fired a change per pixel | decided |
| User-facing errors | `MessageBox`, on-screen text | toasts | section 13 | decided |
| Old shader file cleanup | `old_shader_file_names` per game | keep as is | no reason found to change | keep |
| Native game callbacks | one virtual per native function and stage (`PreVSSetConstantBuffers`, `PreDraw`...), Core's functions included | one struct of function pointers (`Game::D3D11::Callbacks`) for the functions Core doesn't hook only; callbacks no 1.0 game overrides are not offered (texture 1D/3D, view `Pre`s, input layouts, samplers destruction, per-stage SRV/sampler binds, clears other than RTV and `ClearView`, `PreCreateDevice`, `PreFlush`) | a game's work never runs in the middle of Core's on the same function | decided |
| Game's part of Core's functions | 1.0's Core virtuals (`OnInitDevice`, `OnInitSwapchain`, `OnPresent`, `OnDestroyDeviceData`), plus `Pre`/`Post` virtuals on the same functions | events only, one struct per API (`Game::D3D11::Events`, `Game::D3D12::Events`), each called where 1.0's Core called its virtual: `on_init_device`, `on_destroy_device`, `on_create_swapchain` (one normalized `SwapchainDesc` for the three creation and resize functions, both APIs), `on_init_swapchain` (creation and each resize), `on_present`, `on_create_shader`, `on_bind_*`, `on_clear_state`, and from Step 4 the resource events (`on_create_buffer`/`on_init_buffer`, `on_create_texture_2d`/`on_init_texture_2d`, `on_resource_destroyed`, `on_init_*_view`, `on_view_destroyed`, `on_create_sampler_state`, `on_bind_render_targets*`, `on_map`, `on_unmap`, `on_unmapped`, `on_update_subresource`, `on_copy_resource`, `on_copy_subresource_region`, `on_resolve_subresource`, `on_clear_render_target_view`, `on_clear_view`: the native callbacks they were, same signatures, where an `on_create_*` or a bind may still edit and a copy or clear may skip, as 1.0's virtuals could), `on_finish_command_list`, `on_execute_command_list`; no `Pre`/`Post` present, swapchain, shader, bind or command list callbacks, no `SetFullscreenState` or `*SetShader` ones | Core keeps tracking what the game does, in 1.0's order; FF7R, Borderlands 2, Persona 5 Royal and Far Cry 5 port to them | decided |
| Shader creation callbacks | `PreCreate*Shader` could edit the bytecode, which Core then hashed | `on_create_shader(device, stage, hash, bytecode, shader)` after Core tracked it, read-only; the hash is the game's bytecode's; editing goes through variants (patches, Step 3) | a shader's hash stays what replacements, draw callbacks and debug names know it by | decided |
| Game's requested swapchain desc | map keyed by swapchain pointer | the swapchain's private data | section 5 | decided |
| Asserts | `MessageBox` asking to break | logged, `__debugbreak()` only with a debugger attached, Development only | a message box can hide behind a fullscreen game | decided |
| MCP settings | fixed names (`scene_peak_white`, `dev:<index>`...), not saved | registry keys, saved like an overlay change | one registry, section 9 | decided |
| Compiled shader cache | `.cso` next to each `.hlsl`, its preprocessed text's `std::hash` in the config (`Shader#<hash>`) | `Luma/Cache/Shaders/<key>.cso`, the key an FNV-1a of the preprocessed text without its `#line` lines, with target, entry and a cache version | source folders hold source only; no machine path in a key (FXC makes the source name absolute), so a prebuilt cache ships; no config entry per shader | decided |
| Includes | relative to the including file | from the Shaders root, through the roots; each file compiled through a one-line `#include` source so messages name files from the root | section 8 | decided |
| Shader compile error | the shader's code is cleared, its pipelines fall back to the original | the last bytecode that compiled stays published | Step 1's Verify: the old variant stays while the error is fixed | decided |
| Boot compilation | blocks the game's shader creation (lock) until every custom shader compiled | never blocks for the build: the game's shaders run until their variant is published at a present (shader creation can wait for the one FXC call in flight, since FXC is used by one thread at a time) | section 10 | decided |
| Variant objects | one clone per game shader object, destroyed and recreated on every load | one per device and hash, shared by its objects; recreated only when its bytecode changes (unload/load, `Luma.Enabled`, reloads keep it); released when the game releases the device | no repeated work, nothing of Luma's keeps a device alive | decided |
| Luma cbuffers at a draw | bound to every stage of a draw with a clone, uploaded through a device-wide dirty flag merged across deferred lists | bound to the stages running a variant, uploaded per context (LumaSettings when its stamp moved: every present, a resolution change, `MarkLumaSettingsDirty`; LumaData when its bytes differ from that context's last upload; both again after a command list boundary) | per-context state on deferred contexts without the merge | decided |
| LumaData slot | none unless the game sets one | b12 by default, game-overridable | section 10 | decided |
| Background work | compiled under a lock at shader creation (see Boot compilation), no pool | `System::Jobs`: a private Win32 thread pool sized by `Luma.Advanced.WorkerThreads`, jobs with a priority (dumps low), per-job progress, results published at present; FXC stays one caller at a time | dumps no longer wait behind a build, and Step 2's pipeline jobs and Step 4's file loading need a bounded pool in the game's process | decided |
| Cancelling a stale build, one "run at present" queue | (no jobs) | a reload waits for the running build (`reload_pending`); `Jobs::RunFinished`, `Mcp::RunPendingJobs` and `Settings::ApplyQueued` are three queues | nothing needs either yet | idea |
| Parallel shader compilation | one thread at a time | the same | FXC is documented and reported as not thread safe, and 1.0's comment says so. A stress test (24 shaders, up to 8 threads, /O0 and /O3, the system `D3DCompiler_47` 10.0.26100, compile beside reflect and disassemble) found no failure and no output difference, and 5.4x on 8 threads, but it isn't proof and Wine's DLL wasn't tested | decided: stays serial |
| Raw `.cso` replacements | loaded from the game folder when named `0x<hash>...` or with `!` | not loaded | no game being ported ships one; source folders hold source only | idea, when a game needs it |
| Shader defines | `DEVELOPMENT` and `TEST` shown read-only; Development and Test builds can add defines by name; `#define` lines of Settings.hlsl scanned for Prey | settings with a `Define` source; `DEVELOPMENT`, `TEST`, the game's name and the cbuffer slots fixed by the build; no defines added by name; no scan (Prey ports it) | section 9 | decided |
| Dev sliders | names, defaults and ranges re-read from Settings.hlsl at every compile | read once at boot, from `CBuffers.hlsl` (the struct's file) | registry settings are declared at startup | decided |
| Shader dumps and reflection | dumped next to the game's shaders, target from reflection or disassembly, reflection in `.meta` files next to the dumps | `Luma/Dump`, target from the DXBC program chunk, reflection in `Luma/Cache/Shaders/Reflection` | section 8 | decided |
| Shader names | `forced_shader_names`, Development only | `Game::Shaders::SetDebugName`, which the hash lists of draw callbacks use too | section 10 | decided |
| MCP shader tools | 1.0's fields (`file_clones`, `cloned`, `replace_draw_type`) | what 2.0 has: `replaced`, `pipelines` (live objects), no draw replacement until part B; `status` gains `log_path` and `shaders` | replies carry what Luma knows | decided |
| FXC optimization | `D3DCOMPILE_OPTIMIZATION_LEVEL0` (/O0), commented as /O3 | the same, for identical output to the goldens | a 1.0 bug: switch to /O3 (and bump the cache version) once the goldens are captured | idea |
| Shader define defaults | the game's `OnInit` edits `shader_defines_data` | `Game::Shaders::SetDefine(Defines::X, value)` on Core's typed define ids, `AddDefine` for the game's own; all are settings of kind `Define` in one registry | one registry underneath, a call per responsibility, a wrong type doesn't compile | decided |
| Custom shader declarations | `native_shaders_definitions` in C++ (name, file, stage, entry, defines), games adding theirs | `// Luma: <target> [name] [entry=] [DEFINE=value]` lines in the shader files, found by the scan at init; lookup names are the file (or declared) names, `"Luma_Copy_VS"_h` for 1.0's `"Copy VS"` | Core loads what is on disk, no registration step; a program and what it compiles with are in one place | decided |
| Luma cbuffers on a variant's draw | `LumaSettings` and `LumaData` to every stage running a variant | the same: every compiled Luma shader requests both at the game's slots whatever its reflection says, through request and fulfil (Step 1 bound only what reflection declared, corrected) | 1.0's behaviour; shaders that read them through includes without declaring them in reflection still get them | decided |
| Luma's own shaders and cbuffers | `native_shaders_definitions` and `DeviceData` fields, created per device by Core | `Resources::Database` entries, looked up by `"name"_h` | one database for everything Luma owns on a device | decided |
| Draw callback signature | `(device, context, cmd_list_data, device_data, stages, original_shader_hashes, is_custom_pass, updated_cbuffers, original_draw_dispatch_func)` | `DrawResult (*)(const Game::D3D11::Call&)`: the context, the native arguments, the hash per stage, the variant stages; `RunOriginal(call)`; Luma's cbuffers bound after the callbacks by request when they return None, not when one bound them with `BindLumaShader` (1.0's `updated_cbuffers`) | Core's state isn't the game's to read; what 1.0 callbacks used is in `Call` | decided |
| Draw callbacks by hash | a list per stage, all listed stages must match (`ContainsAll`) | any listed hash bound on any stage (pixel first, then vertex, geometry, hull, domain) | games list one stage's hashes (FF7R: pixel); the stage of a hash is known from its shader | decided |
| Bound-state cache | the current draw's arguments (`ENABLE_DRAW_DISPATCH_DATA_CACHE`) | the arguments are in `Call`; viewports and scissors, blend and depth are recorded at bind for the groups a game declares (all in Development); views, buffers and samplers are queried | the API unbinds views on its own, a record would hold dangling pointers or extend lifetimes | decided |
| Game's LumaData part | `UpdateLumaInstanceDataCB(data, cmd_list_data, device_data)` at every draw binding LumaData, bytes compared | the same flow: `Options::cbuffers.update_luma_instance_data(CB::LumaInstanceData&, const GameDeviceData&, const Frame::Resolutions&)`, called at every draw with a Luma shader with the device's `GameDeviceData` and resolutions (cached in the context's state), the bytes compared with that context's last upload (a dirty flag version was tried and reverted) | no command list data to pass; Core's resolutions passed apart, as they're `Luma::Frame`'s | decided |
| Trace entries | one per pipeline stage (VS entries hidden by default), copies, clears, binds, CPU writes, merged deferred lists | one per draw or dispatch with every stage's shader, labelled by its primary shader (PS, else VS, else CS), filters matching any stage; draws without a pixel shader hidden by default (1.0's VS entries); Luma's passes shown by default (1.0 hid custom draws); copies, resolves, clears and CPU writes as entries since Step 4 (`copy`, `clear`, `cpu_write`, buffer writes hidden by default as 1.0); binds and the deferred merge not yet, so no "Ignore Bindings" filter | the draw is the unit of the state shown; the rest comes with the features that need it (Step 4) | v1, extend |
| Overlay on HDR | drawn by ReShade's overlay, which encodes for the back buffer's color space at its "HDR overlay brightness" (203 nits) | Luma's own ImGui renderers with an encoding pixel shader, the color space from the game's `SetColorSpace1`, the brightness an Overlay setting | Luma draws its own overlay (section 14) | decided |
| Constant buffer tracking | one slot index for all stages, the tracked buffer read at every matching draw, floats and ints listed | a slot per stage chosen in the capture, read once a frame for the selected call's instance, typed fields when a layout is known, changes highlighted | readable, and what changed is what's looked for | decided |
| Cbuffer restoration | `ENABLE_AUTO_CBUFFER_RESTORATION` define, records all 14 slots of every stage | `Options::cbuffers.restore_after_draw`, records only Luma's two slots per stage; rebinds after a call whose requests were fulfilled, and after a callback's call (run, skipped or replaced) that bound Luma's shaders with `BindLumaShader` | a define per game becomes an init option (no preprocessor feature flags); only those two slots can be changed by Luma | decided |
| Define keys | `shader_defines_data` in the config by name, Luma's and the game's alike | `Defines.<NAME>` keys in `[Defines]`, Luma's and the game's alike; a name registered twice is refused | define names are global to the compiler; Step 1's `Luma.Defines.*` keys reset once | decided |
| Ignore Warnings | FF7R's `IgnoreWarnings` setting (ReShade-era warning popups) | dropped | nothing reads it, owner-approved | decided |
| Device resolutions | `DeviceData` fields written by Core and games | `Luma::Frame` per device (the three under a small lock, read as one), set by Core at swapchain creation and resize, `Game::Frame::SetRenderResolution` for SR/TAA; a change moves LumaSettings' stamp | Core's state with an owner; games read and update it through calls | decided |
| Variants | file clones and patch clones as separate paths, `UsePipelineVariant` | one list per hash with source and priority, `on_variant_draw`, `UseOriginal`, a trace picker | one model for files and patches from Step 3 on | decided |
| D3D12 root signatures | (no D3D12) | every game signature extended at creation with Luma's parameters, the game given the extended one | one signature for the game's PSOs and the variants: swap at bind, no replay; the one game object changed in place (section 6), since the game can't read a signature back | decided, Step 2 |
| D3D12 register space | (no D3D12) | all of Luma's bindings in their own space (default 100, `Options::bindings`) with SM5.1 variants; a game using it is a Development assert and the mod picks another | never clashes with a game register, and leaves room for Luma's views and samplers | decided, Step 2 |
| D3D12 descriptor heaps | (no D3D12) | a Luma tail on shader-visible heaps, `GetDesc` spoofed; holes in heaps at the limit | Luma's descriptors valid in the game's bound heap without shadow heaps | decided, built with its first user (Step 3 or 4) |
| `UseOriginal` on D3D12 | (no D3D12) | reverts the whole PSO, every stage | a PSO per mixed choice isn't worth building until a game needs it | decided, Step 2 |
| Recipe registration | `Patch::RegisterRecipe` in the game's `OnInit` (identity, name, description, default, visibility, env options) | the recipe's own `meta:` (DXP 0.10), read by Core; no game call | a recipe is a file: it carries its description, like Luma's shaders their `// Luma:` lines | decided, Step 3 |
| Patch providers | `LUMA_PATCH_BYTECODE_SYNC/ASYNC`, `LUMA_PATCH_RECIPE_SYNC/ASYNC` defines, in-place or clone modes, clones made lazily at bind | recipes when the game opts into DXP, the game's sync and async bytecode callbacks as `Options::patching`; every patch a variant (`Source::Patch`), made when the game creates the shader, published at a present | no preprocessor feature flags; one variant model; no per-bind patching work | decided, Step 3 |
| Re-patching | recipes reloaded, pipelines re-patched at their next bind | every created shader patched again at once on a job, from the cache when it has an answer; the game's bytecode kept while patching is needed | the originals are at hand, and a toggle shows at once | decided, Step 3 |
| Patch cache | none: re-scanned each boot | patched bytecode in `Cache/Shaders` with its own keys, and an index of matches and non-matches | nothing done twice across boots (section 8) | decided, Step 3 |
| Recipe resources | `managed_resources` entries, `luma_settings`/`luma_data` handles, `fast_noise` mapped to "FAST Noise" by name | `Resources::Database` entries by name (textures from the Textures folders, `LumaSettings`, `LumaData`) as requests; bound before the draw events as 1.0 | one database for everything Luma owns | decided, Step 3 |
| Textures from disk | `LoadTexture2DArray` of a PNG sequence behind `ENABLE_FAST_NOISE_TEXTURES` | `.dds` files in the Textures folders, all loaded when the game sets `Options::textures.load` (inclusion and exclusion lists later); FAST noise converted to one DDS array | a per-game option instead of a define; any texture by name | decided, Step 3 |
| Patches with bindings on D3D12 | (no D3D12) | not published: DXP adds SM5.0 bindings in space 0, which the game's signature doesn't describe; the game's pipeline runs | needs DXP's upgrade to SM5.1 (Plan.md Step 3) | idea, DXP |
