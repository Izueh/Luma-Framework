# Luma 2.0 Design Guidelines

The rules every 2.0 change follows. `Plan.md` is the staged execution plan built on them.

This supersedes `Design.md`, `Implementation.md` and `Core2.md` wherever they disagree. Those stay as the record of the first skeleton and of the 1.0 feature inventory.

## 1. What Luma 2.0 is

A multi-backend framework for modifying game rendering through CPU code hooks, native graphics API hooks and shader replacement or patching. It carries general rendering upgrades (HDR, super resolution, AO, anti-aliasing, RT and others) across many games.

Goals, in priority order:

1. **Rebuild 1.0 incrementally, game by game.** A feature is built when a game being ported needs it. Final Fantasy VII Remake (FF7R) is first, on D3D11 and D3D12. After it, games are picked for the features FF7R doesn't use.
2. **Features and models instead of a monolith.** 1.0's `core.hpp` (13k lines) and its `DeviceData` become modules that own their state.
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
| `Luma::Utils::{Log, Hash, Strings, Json, Toml, Modules, Format}` | leaf utilities, no Luma state |
| `Luma::System::{Loader, Memory, Display, Window}` | process level: bootstrap, pattern scan and mid-function hooks, HDR state and luminance, SDR white level, borderless window. No graphics API objects |
| `Luma::Hooks` (+ `::Input`, `::DeviceProxy`, `::Lifetime`, `::Scope`) | interception plumbing |
| `Luma::Handlers` | `Pre<Function>` / `Post<Function>` / `On<Object>Destroyed`, one per hooked native function |
| `Luma::Settings` | registry, sources (`Define`, `Recipe`, `Binding`), TOML storage |
| `Luma::Shaders::{Database, Variants, Files, Compiler, Cache, Defines, Custom, Constants, Patching}` | everything about shader and pipeline objects |
| `Luma::Resources::{Database, Mirrors, Upgrade, Chains, Scaling, Samplers, Geometry, Loading}` | everything about textures, buffers, samplers |
| `Luma::Swapchain` | the real swapchain, the game's back buffers, fullscreen policy, output mode |
| `Luma::Display` | composition, UI separation, capability spoofing |
| `Luma::Draw` | state save and restore, the bound-state cache, fullscreen passes, copy and scale helpers |
| `Luma::Frame` | per-frame model: resolutions, jitter, camera, pass markers |
| `Luma::Effects::{SR, SMAA, RCAS, Bloom, GTAO}` | reusable passes |
| `Luma::Trace::{Capture, Info, DebugDraw, Mcp}` | frame capture and the MCP server |
| `Luma::UI::{Overlay, Style, Widgets, Icons, Panels}` | section 12 |
| `Luma::Game` | the mod surface (section 8) |
| `Luma::Compat::{ReShade, RenderDoc}` | section 14 |

Per-API code is nested under its feature: `Luma::Resources::Mirrors::D3D12`, in `Resources/D3D12Mirrors.cpp`. Feature first, API second, so the shared part of a feature is visible from its backends.

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
  * views of a mirrored resource: when the game writes the descriptor (`Create*View`, `CopyDescriptors*`), the mirror's view is written in its place. Engines that copy descriptors per draw (Unreal) make this the equivalent of a D3D11 bind;
  * RTV and DSV: at `OMSetRenderTargets`, by replacing the CPU handles;
  * shader variants: at `SetPipelineState`.

  What D3D12 can only decide at draw is what depends on two binds at once, because their order isn't fixed: a PSO's render target formats against the targets bound, a variant's root signature against the one bound, and rules keyed by "the target of shader X". For those the command list keeps a small record (current PSO, root signature, targets) and checks it at draw behind a dirty flag. D3D12 has no state getters, so this record is needed anyway.
* **A Luma object is released only after the work that used it has finished.** Anything Luma creates and may replace while the game runs (shader variants, PSOs, mirrors, sampler mirrors, Luma's cbuffers and textures) is referenced by each command list that binds it. On D3D12 those references move to the queue at `ExecuteCommandLists`, tagged with Luma's fence value, and are dropped when the fence passes it. A reload or a setting change publishes the new object and drops Luma's own reference to the old one: the old one dies when the last command list that bound it has executed. D3D11 keeps bound objects alive itself, including in deferred contexts, so only D3D12 needs the tracking. A list notes an object once (a generation stamp on the object, not a search).
* **Shadow descriptor heaps are a fallback**, built only if a game needs the same resource seen as original by some draws and as mirror by others through descriptors it wrote once (persistent tables, bindless).
* **Keeping Vulkan possible** costs only these habits:
  * API-agnostic files include no `d3d*.h` or `dxgi*.h`.
  * Files for one API are named `<Api><Module>`.
  * `Api` and bytecode kind (`DXBC`, `DXIL`) are enums with room to grow, and shader hashes are plain integers.
  * DXGI lives in `Swapchain` and `System::Display` only.
  * Settings, UI, Trace and the Game declarations never name an API type. Game callbacks do, per API.

## 5. Hooks

What the 1.0 migration off ReShade established. All of it carries over.

* **SafetyHook inline hooks on function bodies**, found through vtables. One hook covers every interface version and both immediate and deferred contexts.
* **Vtables are copied, never pointed at.** D3D11 contexts keep their vtable in heap memory that dies with the temporary object (the 1.0 first-boot crash). Slots are computed from the SDK's C interface structs (`slots.c`).
* **Exports are read from the module's export table**, not `GetProcAddress`, and only from System32 modules. RenderDoc and same-named proxies otherwise get hooked in place of the real function.
* **A hook is installed only when something asks for it**: a Core feature that is needed, or a game callback that is registered. Hot paths (`Map`, every `*SetShaderResources`) cost nothing when unused.
* **One handler per native function**, named after it: `PreX` can edit arguments and skip the call, `PostX` sees the result, `OnXDestroyed` gets an object that can no longer be used.
* **The game wraps Core.** In a `Pre` the game runs first and Core sees its edits. In a `Post` Core runs first so its tracking is current. Each side runs only if it asked for the hook.
* **Luma scope.** Luma's own API calls and calls made from game callbacks run under a thread-local guard and pass through the hooks untouched. A callback edits arguments rather than skipping a call and reissuing it.
* **Arguments reach handlers as the game passed them.** A whole-subresource copy has no box. A `ClearView` is not an RTV clear. Normalising these broke FFXV during the migration.
* **Binds are hooked for every shader stage** a feature can affect, not only PS and CS.
* **Device proxy for identity and reference count only.** All logic stays in the inline hooks. The proxy exists so Luma knows when the game is done with a device.
* **State with an object's lifetime lives in the object's private data.** A resource's mirror, a view's mirrored view, a shader's database entry, a sampler's mirror, a swapchain's state are attached with `SetPrivateDataInterface` (D3D11 and D3D12 objects both have it). The holder's final release is the cleanup and the destroy notification, so there are no side maps to erase from and no destroy hooks for these. Side tables remain only for what isn't an object (D3D12 descriptors) and for a lookup measured to be too slow through private data on a hot path. Destroy callbacks never call into the dying object.
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
* `Core.cpp` is the one place that turns "needed" into installed hooks, once, after `Init()` and the settings load.
* Development builds treat every tracking feature as needed, so files can be added while the game runs.
* A needed feature can still be switched off at runtime by an ordinary setting.
* **One global switch, `Luma.Enabled`**, turns off everything Luma does to the image, immediately: variants aren't bound, mirrors aren't swapped in, custom passes and game draw callbacks don't run, composition passes the game's image through. Tracking keeps running (databases, descriptor and bind records, patch and compile queues, mirror bookkeeping), so switching back on needs no catch-up. It has a place in the overlay header and a hotkey, and every feature's draw-path entry checks it.
* Optional SDKs (NGX, FidelityFX, XeSS, DXP, NVAPI) are opt-in per game project through props, as in 1.0 (`UseLumaNGX`...). A feature whose SDK isn't linked reports itself unavailable.

## 8. Game API

Games include one header and implement `Luma::Game::Init()`, which only declares things.

* **Free functions and registration.** No `Game` base class. A 1.0 `class X : public Game` becomes functions in an anonymous namespace plus registrations in `Init()`.
* **Callbacks keep 1.0's general shape**: `Pre`/`Post`/`On...Destroyed`, native arguments, one set per API, so a game's body moves over with little more than a signature change. The exact set is free to improve where it clearly helps, for example one callback with a stage argument in place of six per-stage copies, or one draw callback for the seven draw functions. Each such change is a row in section 17. Registering a callback is what requests its hook.
* **UI callbacks.** A game can draw its own content into the overlay: a settings section, an info panel, about text, a dev panel (1.0's `DrawImGuiSettings`, `PrintImGuiInfo`, `PrintImGuiAbout`, `DrawImGuiDevSettings`). They use `UI::Widgets` so custom content looks like the rest. Plain settings still go through the registry.
* **Draw callbacks by shader hash** stay the main entry point (`Game::OnDraw(hashes, &fn)`).
* **Per-device game data** is a struct the game registers. Luma creates and destroys it with the device.
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
      Shaders/                compiled .cso, by hash and defines
      Patches/                patched bytecode and the patch index
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
   std::string_view key;         // Unique. Luma's own start with "Luma."
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
};
```

* **Sources** say what else a change does. The registry handles each generically:
  * `Value`: nothing. Read with `Settings::Get<T>(key)` or a change listener.
  * `Binding{float* target, float (*parse)(float)}`: writes a variable, usually a field of the Luma settings cbuffer. This is the 1.0 `settings.hpp` pattern, kept so FF7R's and Metro's tables move over as they are.
  * `Define{name}`: a shader define. A change marks dependent shaders for recompile (immediate with Auto Compile, otherwise on request).
  * `Recipe{identity, env}`: enables a patch or sets one of its environment values. A change re-patches.
* **One UI.** A setting is drawn by its value type through `UI::Widgets`, grouped by category. Games don't write ImGui code for settings. A game-drawn panel remains possible for the rare custom view.
* **One store.** `Luma/Luma.toml`, a table per category.
* **Threading.** There is no single render thread: on D3D12, and on D3D11 with deferred contexts, other threads record the next frame while one presents. So:
  * *One writer, at present.* Every change (overlay, MCP, a game's own call) is applied on the presenting thread at the present boundary. That stays the rule: it orders writes and is the closest thing to a frame boundary there is.
  * *Readers never lock.* Scalar values are atomics, readable from any hook on any thread. Strings are read under a lock and never on a per-draw path.
  * *A change is never assumed to land on a frame boundary.* For one frame, command lists recorded before and after the change can both execute. A value that only tunes a pass tolerates that.
  * *A change that creates or destroys objects* (upgrades on or off, output mode, SR, sampler bias) never frees what a recorded command list may still use: the old objects are released by the rule in section 4 (after the lists that bound them finish), and hooks keep working with either state until then.
  * *A change that rebuilds* (defines, recipes) runs on the job queue and publishes the new variant atomically.
* **Operable through MCP.** `luma_get_settings` and `luma_set_setting` read and write the same registry, so everything the UI can change, an agent can.
* Dev sliders (1.0's `LumaDevSettings`) are settings with `Visibility::Development`. `luma_dev_values`, `luma_set_dev_value` and `luma_ab` operate on those.

## 10. Shaders

* **Hash** is CRC32 of the bytecode, as in 1.0, so every existing `Name_0xHASH.ps_5_0.hlsl` and `.cso` keeps working.
* **Three ways to change a shader**, all producing a variant: a replacement file matched by hash, a patch (a DXP recipe or a game's bytecode callback), and Luma's own custom shaders that run between game passes.
* **Custom shaders** are declared by name, file and defines (1.0 `native_shaders_definitions`) and looked up by name hash.
* **Luma constants** (`LumaSettings`, `LumaData`) bind to slots a game can override, defaulting to 1.0's b13 and b12.
* **Compilation is a job queue with progress.** The overlay shows a progress bar while it runs. A variant that isn't ready falls back to the original shader and says so in trace.
* **Cache** compiled and patched bytecode on disk keyed by source hash, defines and patcher version.
* **Shader roles are named once.** A game's hash lists also name the shaders for trace (replacing `forced_shader_names`).
* **Draw state.** Two tools from 1.0 are kept, both parametrised so nothing is copied that isn't used:
  * the state stack (`DrawStateStack`): saves and restores the context around Luma's own passes. It takes a mask of state groups (shaders, per-stage SRVs, cbuffers and samplers with slot ranges, targets, blend, depth, rasterizer, viewports and scissors, input assembler) in place of 1.0's three fixed modes, which remain as presets. The default preset is what a fullscreen pixel pass touches;
  * the bound-state cache (`ENABLE_DRAW_DISPATCH_DATA_CACHE`): what the game has bound, recorded at bind so draw callbacks read it without querying the API. A game declares which groups it reads, and only those are recorded. Development records all of them for trace.
* D3D12 specifics (PSO variants keyed by formats, root signature extension) follow section 4. The details in `Design.md` "DX12 binding model" are the fallback design, not the starting point.

## 11. Resources

Framework systems replacing code that 1.0 games each wrote for themselves.

* **Resource Database.** Named entries (`"name"_h`) any module, patch or shader binding can resolve. Covers Luma's textures, DDS files loaded from the `Textures` folders, and game resources a mod tags. Entries declare what invalidates them (swapchain resize, render resolution change) and are recreated by the framework. Replaces `LumaTexture` / `ManagedResources` and the per-game `Clean*Resources` functions.
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
  | `SettingRow` | label column on the left at a fixed width, control, reset icon when changed, badge ("restart", "recompile") |
  | `Toggle` | every boolean |
  | `Slider`, `Combo`, `Segmented`, `KeyBind`, `TextInput` | by value type |
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
* **Layout.**
  * Release: one window. Header (game, version, output mode chip), settings by category, about and credits footer.
  * Development: adds tabs for Trace, Shaders, Resources, Patches, Log and Info. Dev-only settings appear in the same settings view under their own categories, not in a separate ad-hoc list.
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
* **Files.** Every file and folder is `PascalCase`, source and resources alike (`Resources/D3D12Mirrors.cpp`, `Luma/Shaders`). A file for one API is `<Api><Module>` (`D3D11Hooks.cpp`, `D3D12Renderer.cpp`), so no two files share a name. Game folders carry the game's title. Shader files keep their `Name_0xHASH.ps_5_0.hlsl` form.
* **RAII.** `ComPtr` for every COM reference Luma owns. State tied to a D3D object lives in its private data.
* **Headers hold the public interface only.** Private code is in the `.cpp` in an anonymous namespace. No `Detail` or `Internal` namespaces, no state in headers.
* **One `.cpp` per module** until it's hard to work in, then split by area.
* **Lock order is written where each mutex is declared.**
* **Comments are brief and say why.**
  * No file preambles, no banner comments, no description of what a file is for.
  * No comment per field or per function restating its name.
  * One line where a reader would otherwise be surprised: a constraint, an ordering, a measured fact, a 1.0 behaviour kept on purpose.
  * History belongs in commit messages and in section 17.
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

* `Core` is a static library built once per configuration, linked with LTCG in Release. Games reference it.
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
| Feature selection | `ENABLE_*` defines per game, unity build | runtime, from declarations and files | Core built once, no silent Publishing-only breakage | decided |
| Game interface | `class Game` virtuals, overrides detected at compile time | registered free functions, registration requests the hook | no base class, same effect | decided |
| Settings | per-game ImGui code, `settings.hpp` in two games, shader defines and recipes each with their own UI and storage | one registry, one UI, one store, MCP access | section 9 | decided |
| Config file | get/set API shaped like ReShade's | typed registry over TOML | no ReShade-shaped API | decided |
| Per-frame flags | `has_drawn_*`, `camera_cut`... redeclared in each game's device data | `Frame` pass markers and common frame fields | same flags in dozens of games | idea, decide in Step 7 |
| Camera data for SR and AO | each game extracts jitter, FOV, near plane into its own fields | a `Frame` camera model the effects read | 35 games do motion vectors or SR | idea, decide in Step 7 |
| D3D12 swaps | (no D3D12) | at descriptor write and at bind, draw-time check only for cross-bind decisions | same model as D3D11 where D3D12 allows it, no shadow heaps unless a game forces them | decided, confirm in Step 2 |
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
| Native game callbacks | one virtual per native function and stage (`PreVSSetConstantBuffers`, `PreDraw`...) | one struct of function pointers per API (`Game::D3D11::Callbacks`); per-stage copies merged with a `ShaderStage` argument (`pre_set_constant_buffers`, `pre_create_shader`/`post_create_shader`); callbacks no 1.0 game overrides are not offered (texture 1D/3D, view `Pre`s, input layouts, samplers and shader destruction, per-stage shader/SRV/sampler binds, clears other than RTV and `ClearView`, `PreCreateDevice`, `PreSetFullscreenState`, `PreFlush`, every native draw and dispatch) | porting stays a signature change for FF7R, Borderlands 2 and Persona 5 Royal; registering a merged callback hooks every stage's function | decided |
| Present callbacks | `PrePresent(swapchain)`, plus Core's `OnPresent(device, device_data)` | `pre_present`/`post_present(swapchain, device or queue)` per API | one callback covers both 1.0 shapes | decided |
| Game's requested swapchain desc | map keyed by swapchain pointer | the swapchain's private data | section 5 | decided |
| Asserts | `MessageBox` asking to break | logged, `__debugbreak()` only with a debugger attached, Development only | a message box can hide behind a fullscreen game | decided |
| MCP settings | fixed names (`scene_peak_white`, `dev:<index>`...), not saved | registry keys, saved like an overlay change | one registry, section 9 | decided |
