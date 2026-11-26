# Luma 2.0 graphics backend design review

Source baseline reviewed November 25, 2026 on `Luma_v_2_0` at `1771c3521dcddbf38cefe3cb1a94513b9acb8778`. The review covers Luma 2.0 Core, the FF7R integration, the shared MSBuild project, and relevant tests. The proposed code is illustrative. The rules it settled are in [Guidelines.md](Guidelines.md#4-backends) section 4 and the section 17 rows "API declaration", "Shader state scope", "SR backend storage" and "Overlay attachment"; this document keeps the reasoning and the order of work.

## Executive summary

Luma compiles native D3D11 and D3D12 implementations into each game DLL. A native draw hook already knows its API and calls the corresponding handler directly. Luma shares algorithms through ordinary functions, overloads, templates, and a few feature-specific traits. At boundaries where the API is learned from the game, it uses variants of native pointers and runtime visitation. Super resolution (SR) independently selects an upscaler at runtime from a tuple of concrete provider states. Both kinds of runtime choice lead to concretely typed code. The current architecture has no Luma graphics RHI or virtual renderer interface. [Backend guidelines](Guidelines.md#4-backends) · [FF7R project](../Source/Games/Final%20Fantasy%20VII%20Remake/Final%20Fantasy%20VII%20Remake.vcxproj#L24) · [Shared Core project](../Source/Core/Core.vcxitems)

Keep one DLL per game with the APIs it uses declared in `Game::Options::supported_apis` and statically typed native handlers; devices and swapchains of other APIs are left to the game. `Game::Info` names no API: the running one is the presenter's. Replace the process-wide shader API with **per-device shader stores** attached in `PostCreateDevice` and destroyed with the device, backed by the shared on-disk cache, with worker jobs given plain build inputs and a `std::weak_ptr` to the store. Move the remaining per-device state that lives in process globals (`Resources::Database` compiled shaders, D3D11 sampler registry and SR LOD bias) to the device. Share handler workflows, super-resolution orchestration, and overlay UI where the steps match. Keep DXGI presentation and format rules shared by D3D11 and D3D12, clean up DXGI/COM leakage at shared boundaries now. Live API handoff, per-swapchain `DisplayMode` and Vulkan presentation wait for a game that needs them. [API identity and presenter](../Source/Core/Common/Api.hpp#L11-L38) · [Shader build selection](../Source/Core/Shaders/Shaders.cpp#L364-L431) · [Frame state storage](../Source/Core/Frame/FrameState.cpp#L57-L76)

## 1. How backend selection works today

| Point | Current mechanism | What selects the path |
| --- | --- | --- |
| DLL build | Each game imports `Core.vcxitems`. Core and the game's concrete data types compile together. The project uses `stdcpplatest`. | MSBuild selects the game, configuration, and optional SDKs. Both Direct3D implementations are in Core. |
| Hook install | `Game::Info::api` could narrow `Startup::InstallHooks` (now `Game::Options::supported_apis`). With no declaration, startup requests both native paths. D3D11 and D3D12 have their own function enums and bitsets. | Game declaration, loaded modules, and feature hook requests. |
| Native calls | Native ABI detour → typed handler → native feature functions. Representative entry points are `Handlers::D3D11::PreDraw` and `Handlers::D3D12::PreDraw`. | The intercepted API entry point establishes the type. There is no graphics-API switch on this path. |
| DXGI presentation | `Presenter` carries `ID3D11Device*` or `ID3D12CommandQueue*`. DXGI identifies the creator with `QueryInterface`, then visitors route present, resize, composition, or overlay work. | The creator of the swapchain. D3D12 keeps the queue because presentation work needs it. |
| Game-level helpers | `Device` carries `std::monostate`, `ID3D11Device*`, or `ID3D12Device*`. `Game::Frame` forwards common queries through `GetUnknown`. `GetGameDeviceData` visits the native type to unwrap the proxy and retrieve the attached data. | The native device supplied by the caller. FF7R adapts both typed present callbacks to one `OnPresent(Device)`. |
| Shader compilation | `Shaders::Start` can seed the API from `Game::Info`. Otherwise the first handled device creation wins. `Reload` captures that API for the background build. | One process-wide choice of shader build API. Native hooks can still exist for both APIs. |

[Build props](../Props/Luma2.Common.props#L29-L49) · [Hook requests and installation](../Source/Core/Startup.cpp#L82-L137) · [Native function sets](../Source/Core/Hooks/HookFunctions.hpp) · [D3D11 draw detour](../Source/Core/Hooks/D3D11/D3D11ContextHooks.cpp#L334-L350) · [D3D12 draw detour](../Source/Core/Hooks/D3D12/D3D12CommandListHooks.cpp#L149-L183) · [DXGI creator discovery](../Source/Core/Handlers/DXGI/DXGIHandlers.cpp#L92-L100) · [Game device visitors](../Source/Core/Game/GameRegistry.cpp#L202-L240) · [Shader startup](../Source/Core/Shaders/Shaders.cpp#L288-L305)

These are the two graphics pointer variants in Core. `std::visit` branches on the active alternative at runtime and instantiates the visitor for each alternative at compile time. `Overloaded` combines lambda call operators by variadic inheritance. In the overlay, a generic visitor takes the stored presenter to `Draw<T>`, which then uses the concrete renderer specialization. `Settings::Value` and the hook manager's target variant model settings and hook locations. Shader *variants* are replacement objects selected by source and priority. [Device](../Source/Core/Common/Device.hpp#L5-L11) · [Presenter and `Overloaded`](../Source/Core/Common/Api.hpp#L22-L38) · [Overlay dispatch](../Source/Core/UI/Overlay.cpp#L232-L265) · [Hook target model](../Source/Core/Hooks/HookManager.hpp#L24-L48) · [Shader variant storage](../Source/Core/Shaders/ShaderVariants.cpp#L17-L66)

```cpp
using Device = std::variant<std::monostate, ID3D11Device*, ID3D12Device*>;
using Presenter = std::variant<ID3D11Device*, ID3D12CommandQueue*>;

// Overlay.cpp, inside the DXGI overlay path
const bool drawn = std::visit(
   [&](auto* presenter) { return Draw(swapchain, presenter, desc); },
   Presenter(attached->presenter));
```

Native COM calls, stored game callbacks, hook trampolines, the COM state-holder deleter, and the optional ReShade ABI all involve runtime indirection. They serve the native or extension boundary. Shared graphics algorithms use concrete types. A single DLL can keep supporting both renderers by resolving the native object at that boundary, then using typed code from there. [Game event definitions](../Source/Core/Game/GameEvents.hpp) · [Hook forwarding](../Source/Core/Hooks/Detours.hpp#L12-L35) · [COM state holder](../Source/Core/Hooks/ObjectLifetime.cpp#L12-L67) · [ReShade ABI](../Source/Core/ReShade/ReShadeABI.hpp#L39-L66)

## 2. Compile-time techniques in use

### Specialized traits and typed hooks within an API

`Hooks::D3D11::StageTraits<Stage>` explicitly specializes the six stages. Each specialization carries the shader type, native methods, C vtable slots, and names. `StageSetShader<Stage, V>` specializes by stage and native vtable instance, and `AddStageHooks` uses the same traits to register it. `Detours::original<Detour>` is an inline variable template with the detour's exact function pointer type. This removes repetitive method bodies while retaining the native ABI. The selected native vtable instance and trampoline are determined at runtime. [Stage traits](../Source/Core/Hooks/D3D11/D3D11Stages.hpp#L11-L177) · [Stage detour](../Source/Core/Hooks/D3D11/D3D11ContextHooks.cpp#L93-L125) · [Stage hook registration](../Source/Core/Hooks/D3D11/D3D11ContextHooks.cpp#L478-L496) · [Typed original](../Source/Core/Hooks/Detours.hpp#L15-L17)

```cpp
// Excerpt from the pixel-stage specialization.
template <>
struct StageTraits<ShaderStage::Pixel>
{
   using Shader = ID3D11PixelShader;
   static constexpr auto GetShader = &ID3D11DeviceContext::PSGetShader;
   static constexpr auto SetShader = &ID3D11DeviceContext::PSSetShader;
};

template <auto Detour>
inline decltype(Detour) original = nullptr;
```

D3D12 similarly uses `<bool Compute>` and `<bool Compute, int Kind>` for families of root-argument hooks. `CreatePipeline` uses descriptor-type branches for graphics, compute, and stream PSOs. The root-signature parser uses `if constexpr (requires { range.Flags; })` for descriptor range versions. Both Direct3D hook installers expand a `std::index_sequence` into a table of distinct `GetDevice<N>` detours for use during hook registration. D3D11 also guards its descriptor-prefix conversions with SDK layout `static_assert`s. [D3D12 root argument hooks](../Source/Core/Hooks/D3D12/D3D12CommandListHooks.cpp#L82-L113) · [D3D12 PSO hooks](../Source/Core/Hooks/D3D12/D3D12DeviceHooks.cpp#L42-L73) · [Root signature parsing](../Source/Core/Shaders/D3D12/D3D12RootSignatures.cpp#L115-L127) · [D3D11 detour table](../Source/Core/Hooks/D3D11/D3D11Hooks.cpp#L275-L280) · [D3D12 detour table](../Source/Core/Hooks/D3D12/D3D12Hooks.cpp#L207-L229) · [D3D11 descriptor helpers](../Source/Core/Hooks/Detours.hpp#L54-L83)

### Feature-local renderer traits across APIs

`UI::Overlay.cpp` has a private `Renderer<T>` mapping with specializations for `ID3D11Device` and `ID3D12CommandQueue`. Each exposes three `static constexpr` function pointers for `NeedsRecreation`, `NewFrame` and `Render`. `Draw<T>` uses the mapping for one overlay workflow: recreate if needed, start the backend's frame, build common ImGui contents, and submit the draw data. The mapping is tied to DXGI's `Presenter` types and keeps the renderers independently responsible for native resources. This is the closest existing example of how to share a feature workflow using a narrow trait. [Renderer traits and shared workflow](../Source/Core/UI/Overlay.cpp#L167-L211) · [D3D11 renderer](../Source/Core/UI/D3D11/D3D11Renderer.cpp) · [D3D12 renderer](../Source/Core/UI/D3D12/D3D12Renderer.cpp)

```cpp
template <>
struct Renderer<ID3D12CommandQueue>
{
   static constexpr auto NeedsRecreation = &D3D12::NeedsRecreation;
   static constexpr auto NewFrame = &D3D12::NewFrame;
   static constexpr auto Render = &D3D12::Render;
};
```

### Shared workflows using template parameters and lambdas

`HandlePostCreateDevice<Api>` and `HandleOnDeviceDestroyed<Api>` share lifecycle order with typed validation and feature callbacks. `HandleDrawOrDispatch<IsDispatch>` shares the Development capture, tracking, draw-outcome and readback sequence. Native handlers supply lambdas and retain their own render preparation, binding and cleanup. `DispatchDrawEvents<Api>` shares the shader-hash lookup order, variant-event precedence, refresh after a variant callback, and fallback to the generic draw event. D3D11 and D3D12 still pass their own `Call` types. [Handler templates](../Source/Core/Handlers/Handlers.hpp#L24-L79) · [D3D11 draw workflow](../Source/Core/Handlers/D3D11/D3D11Handlers.cpp#L570-L639) · [D3D12 draw workflow](../Source/Core/Handlers/D3D12/D3D12Handlers.cpp#L69-L124) · [Game draw dispatch](../Source/Core/Game/GameRegistry.hpp#L51-L104)

Other shared templates rely on concrete types with compatible operations. `Lifetime::SetState<T, Object>` attaches typed state via COM private data and `GetOrCreateStateCached` caches the hot lookup. The `GetStateOf(IUnknown*)` path discovers the private-data interface by `QueryInterface`. `Validation::InfoQueueReader<InfoQueue>` and the related templates reuse Direct3D debug-queue work. `Resources::Database::UploadIfDirty` shares stamp or byte-change decisions and invokes a typed emitter. `Resources::Mirrors::PlanCopy` shares copy decisions across two resource types and uses DXGI format helpers. On the game side, `GameDeviceDataType::Of<T>()` generates constructor and destructor callbacks where the game's type is complete. This is per-game state construction, with `requires std::same_as<T, GameDeviceData>` checking the declared type. [Typed COM state and cache](../Source/Core/Hooks/ObjectLifetime.hpp#L15-L111) · [COM interface discovery](../Source/Core/Hooks/ObjectLifetime.cpp#L84-L135) · [Validation](../Source/Core/Validation/Validation.hpp#L61-L136) · [Upload policy](../Source/Core/Resources/ResourceDatabase.hpp#L123-L150) · [Mirror copy planning](../Source/Core/Resources/ResourceMirrors.hpp#L138-L167) · [Game data factory](../Source/Core/Game/Game.hpp#L84-L98)

### Concepts and concrete provider state in super resolution

The named `SrBackend` concept checks free functions found through argument-dependent lookup for each provider's concrete `State`. The provider headers use `static_assert(SrBackend<...>)`. Here a backend means an **upscaler implementation for a graphics API**. The native context and resource types are parameters. Initialization stays outside the concept because the SDK arguments and order differ. [Full concept](../Source/Core/Effects/SR/SuperResolution.hpp#L138-L158) · [D3D11 provider assertion](../Source/Core/Effects/SR/D3D11/D3D11Fsr4.hpp#L45) · [D3D12 provider assertion](../Source/Core/Effects/SR/D3D12/D3D12FfxFsr.hpp#L48)

```cpp
template <typename StateT, typename ContextT, typename ResourceT>
concept SrBackend = requires(StateT& state, ContextT* ctx,
   const SettingsData& settings, const DrawInputs<ResourceT>& inputs,
   const DrawParams& params) {
   { state.capabilities } -> std::convertible_to<const Capabilities&>;
   { UpdateSettings(state, ctx, settings) } -> std::same_as<bool>;
   { Evaluate(state, ctx, inputs, params) } -> std::same_as<bool>;
   { GetJitterPhases(state) } -> std::same_as<int>;
   { ReleaseResources(state) } -> std::same_as<void>;
};
```

Each API stores the provider states together in a `std::tuple`. `VisitBackend` takes the runtime upscaler `Type` and calls a generic lambda with the concrete state in the selected slot. D3D12 holds two `FfxFsr::State` values for FSR 3 and 4, so positional `std::get` distinguishes the two. D3D11 uses native FSR 3 and can share a D3D12 interop context between FSR 4 and XeSS. Selecting an upscaler does not choose the graphics API used by the game's native hooks. [D3D11 state and initialization](../Source/Core/Effects/SR/D3D11/D3D11SuperResolution.cpp#L34-L110) · [D3D12 state and initialization](../Source/Core/Effects/SR/D3D12/D3D12SuperResolution.cpp#L35-L103) · [D3D12 evaluate call](../Source/Core/Effects/SR/D3D12/D3D12SuperResolution.cpp#L264-L269)

### Native implementations around a common model

Shader replacements are the clearest example. The shared variant database publishes compiled choices by shader hash and source. D3D11 creates individual native shader objects and swaps the selected shader at `*SetShader`. D3D12 creates whole PSOs, swaps at `SetPipelineState`, can revisit the choice when bound target formats are known, and references the selected PSO for GPU lifetime. The same split applies to binding requests. Dirty checks can be shared, while D3D11 maps a buffer and binds it at a stage slot and D3D12 writes into an upload allocation and binds through root parameters. [Shared variant database](../Source/Core/Shaders/ShaderVariants.cpp#L153-L203) · [D3D11 shader swapping](../Source/Core/Shaders/D3D11/D3D11Variants.cpp#L127-L201) · [D3D12 PSO swapping and format reconciliation](../Source/Core/Shaders/D3D12/D3D12Variants.cpp#L641-L752) · [D3D11 binding](../Source/Core/Resources/D3D11/D3D11ResourceDatabase.cpp#L475-L514) · [D3D12 upload and root CBV](../Source/Core/Resources/D3D12/D3D12ResourceDatabase.cpp#L213-L244)

## 3. What can be shared

| Area | Share or extract | Keep in native code |
| --- | --- | --- |
| Super resolution | Provider slot lookup, capability snapshot, present-time selection/reset, `SRType` publication, tagged input gathering, input/dimension checks, update/evaluate bookkeeping, success statistics. `ResolveActiveType`, `BuildSettingsData` and `BuildDrawParams` already share policy. | D3D11 state save/restore and interop. D3D12 command-list resource references, transitions, copy-back, and state replay. SDK initialization and teardown order. |
| Shaders and patching | File discovery, defines, digest/cache plumbing, publication jobs, source priority, diagnostics, and binding requests as feature data. Carry a target and binding profile explicitly into builds and patch jobs. | Container parsing and reflection by bytecode family, signature or pipeline-layout compatibility, D3D11 shader creation and binding, and D3D12 PSO creation, cache and retirement. |
| Resources and scaling | Resolution and pass policy, dimensions and coordinate transforms, invalidation decisions, upload dirtiness, and DXGI upgrade rules across the two Direct3D APIs. | View and descriptor edits, resource references, staging or upload memory allocation, barriers, and recycling eligibility. D3D12 pool lookup additionally checks resource state. |
| Present, HDR and overlay | Frame housekeeping, display policy, composition inputs and plan, ImGui contents, input, and settings. `Display::Composition::Plan` is already a good example. | Swapchain images and presentation synchronization, HDR signaling, render-target/descriptor setup, and native drawing. D3D11 and D3D12 already share DXGI. |
| Tracing and validation | Capture filters, outcomes, labels, presentation-independent snapshots, and readback output formatting. Keep DXGI format lookup as a shared format-family utility. | Snapshot collection, descriptor decoding, queue and command-list association, resource references and readback synchronization. |
| Game ports | Keep shared game decisions next to the port. FF7R already shares `PrepareTaaSr` and `OnPresent(Device)`. Pass sizes and viewport data into common game logic. | Bound resource discovery, cbuffer reads and motion-vector rendering. FF7R's invalid viewport fallback differs between APIs today and needs an explicit decision before extraction. |

[SR policy](../Source/Core/Effects/SR/SuperResolution.cpp#L74-L142) · [Shared scaling helpers](../Source/Core/Resources/ResourceScaling.hpp#L92-L124) · [D3D12 pool matching](../Source/Core/Resources/D3D12/D3D12Mirrors.cpp#L535-L552) · [Composition plan](../Source/Core/Display/DisplayComposition.cpp#L298) · [D3D11 composition](../Source/Core/Display/D3D11/D3D11Composition.cpp) · [D3D12 composition](../Source/Core/Display/D3D12/D3D12Composition.cpp) · [Capture model](../Source/Core/Trace/FrameCapture.hpp) · [FF7R D3D11 TAA](../Source/Games/Final%20Fantasy%20VII%20Remake/Main.cpp#L251-L282) · [FF7R D3D12 TAA](../Source/Games/Final%20Fantasy%20VII%20Remake/Main.cpp#L373-L411)

### Super-resolution orchestration

Both `OnPresent` implementations repeat the same sequence after native initialization. Both evaluate paths gather the same seven resource keys, require the same four inputs, build the settings and draw parameters, apply the pending reset, check minimum dimensions, and repeat the same provider update/evaluate sequence. Extract pure selection and validation functions, a small shared state value for the duplicated bookkeeping, and a typed helper for the resource keys. Replace the positional `std::tuple` + `std::get<0..3>` storage with a named `Backends` struct (e.g., `dlss`, `fsr3`, `fsr4`, `xess`) visited via an explicit `switch` on `Type`—avoiding error-prone positional indexing, especially on D3D12 where FSR 3 and FSR 4 share `FfxFsr::State`. Give `None` and unsupported values an explicit no-provider path. Keep D3D12's two FSR instances separate and preserve FSR 4 initialization before FSR 3 on D3D12. [D3D11 present and input preparation](../Source/Core/Effects/SR/D3D11/D3D11SuperResolution.cpp#L189-L276) · [D3D12 present and input preparation](../Source/Core/Effects/SR/D3D12/D3D12SuperResolution.cpp#L167-L269) · [D3D12 provider initialization order](../Source/Core/Effects/SR/D3D12/D3D12SuperResolution.cpp#L90-L103)

```cpp
// Proposed helper inside the SR feature. The resolver is a concrete callable.
template <typename Resource, typename Resolve>
DrawInputs<Resource> GatherInputs(Resolve&& resolve)
{
   namespace Keys = Resources::Database::Sr;
   DrawInputs<Resource> inputs;
   inputs.source_color = resolve(Keys::COLOR);
   inputs.depth_buffer = resolve(Keys::DEPTH);
   inputs.motion_vectors = resolve(Keys::MOTION_VECTORS);
   inputs.output_color = resolve(Keys::OUTPUT);
   inputs.exposure = resolve(Keys::EXPOSURE);
   inputs.bias_mask = resolve(Keys::REACTIVE);
   inputs.transparency_alpha = resolve(Keys::TRANSPARENCY);
   return inputs;
}

// D3D12 call site. The D3D11 resolver uses its device and GetResource.
auto inputs = GatherInputs<ID3D12Resource>([&](uint64_t key) {
   return Resources::Database::D3D12::ReferenceResource(list, key);
});
```

The helper preserves the lookup order. Preserve present-time publication before clearing `drew_this_frame`, and fix `force_reset` consumption during extraction: `state->force_reset` is cleared before the `min_res` and `EnsureUavOutput` checks, and those two early returns drop a needed temporal reset. A failed `Evaluate` already sets it again. Clear it only once `Evaluate` returns `true`, on both APIs ([D3D11](../Source/Core/Effects/SR/D3D11/D3D11SuperResolution.cpp#L249-L262), [D3D12](../Source/Core/Effects/SR/D3D12/D3D12SuperResolution.cpp#L228-L240)). Keep the D3D11 save/restore and D3D12 barriers and restore visibly in their native implementations. [D3D11 state restoration](../Source/Core/Effects/SR/D3D11/D3D11SuperResolution.cpp#L259-L288) · [D3D12 transitions and restoration](../Source/Core/Effects/SR/D3D12/D3D12SuperResolution.cpp#L238-L297)

### Shared handler and present bookkeeping

Use `Handlers::Common` for workflows whose order and effects match. Start with the existing lifecycle and Development draw-capture templates. Let the native device type determine the event table where it already carries the API. Use ordinary functions for value-only policy, and small templates with concrete callables for the parts that invoke a native operation. The native handlers continue to own render preparation, bindings, and cleanup. [Existing handler templates](../Source/Core/Handlers/Handlers.hpp#L24-L79) · [Shared game draw-event order](../Source/Core/Game/GameRegistry.hpp#L78-L104)

`PrePresentWork` already shares settings, job completion and frame-level shader work. Move its long D3D11 visitor arm behind a typed `PresentD3D11` function, mirroring the existing `PresentD3D12`. Then look for a small shared post-update sequence where the ordering is demonstrably identical. Preserve the placement of game callbacks, variant publication, sampler refresh and shader object updates. A named function per API makes the DXGI visitor easier to read without committing all renderers to one present algorithm. [DXGI work and current visitor](../Source/Core/Handlers/DXGI/DXGIHandlers.cpp#L418-L488) · [D3D12 present implementation](../Source/Core/Handlers/D3D12/D3D12Handlers.cpp#L663-L684)

## 4. Agreed design direction

### One game DLL and explicit API support

Replace `Game::Info::api` with `Game::Options::supported_apis`, an `ApiSet` that defaults to both Direct3D APIs. It decides which hooks install, which [Guidelines.md](Guidelines.md#8-game-api) section 8 makes an option, not identity. `Info` names no API: what is running is the presenter's, and the overlay and MCP report it from there.

A game can register D3D11 and D3D12 event tables in the same DLL. `supported_apis` declares which native hook paths `Startup::InstallHooks` installs. A swapchain created by a device of an API outside it is left alone, and the first one is logged. Luma's own interop devices (D3D11 SR's `D3D12Interop`) already create under `Hooks::Scope::Guard`, so `PostCreateDevice` never sees them; keep that. [Current game info](../Source/Core/Game/Game.hpp#L42-L56) · [Game registration](../Source/Games/Final%20Fantasy%20VII%20Remake/Main.cpp#L727-L742) · [Startup hook aggregation](../Source/Core/Startup.cpp#L82-L137) · [D3D11 SR interop](../Source/Core/Effects/SR/D3D11/D3D12Interop.cpp#L114-L141)

Make registry access explicit. Today `GetName(Api)` treats all other values as D3D12, and `GetEvents<Api>`, `FindDrawCallback<Api>`, and `HasDrawCallbacks<Api>` use `else` for D3D12. Use an exhaustive `switch` for names and an `if constexpr` chain naming each `Api` (the last arm asserting it) for game registry access, or the trait below where a native type is what the caller has. In DXGI pre-creation, change `GetCreatorApi(IUnknown*)` to return `std::optional<Api>`, returning `std::nullopt` for both `nullptr` and unrecognized creators and skipping `on_create_swapchain` cleanly instead of defaulting to D3D11 or D3D12. [API name](../Source/Core/Common/Api.hpp#L11-L20) · [Draw registry fallbacks](../Source/Core/Game/GameRegistry.hpp#L51-L75) · [DXGI pre/post creator handling](../Source/Core/Handlers/DXGI/DXGIHandlers.cpp#L157-L242)

```cpp
// Proposed. A feature-local mapping with no default implementation.
template <typename NativeDevice>
struct EventAccess;

template <>
struct EventAccess<ID3D11Device>
{
   static const D3D11::Events& Get() { return GetD3D11Events(); }
};

template <>
struct EventAccess<ID3D12Device>
{
   static const D3D12::Events& Get() { return GetD3D12Events(); }
};
```

Keep the mapping in the private game registry and keep each API's event structure. D3D11 currently exposes native game callbacks as well as Core events. D3D12 startup requests the game draw events through its own hook set. [Game registry interface](../Source/Core/Game/GameRegistry.hpp#L15-L33) · [Startup hook aggregation](../Source/Core/Startup.cpp#L82-L137)

### Device-attached lifecycle, secondary devices, and presenter handoff

Follow a strict **device-attached lifecycle**: initialize per-device state in `PostCreateDevice` for every device of a declared API so it lives and dies with that device. Work starts at creation, not at a first present: tracking, patch scheduling and shader builds begin there, so a draw made before the first present (a LUT baked once at boot) can already run its replacement. Because Luma's [Mirrors rule](Guidelines.md#6-mirrors) never mutates game textures or buffers in place at creation, running normal tracking and mirroring on any such device is safe. Third-party SDK initialization (DLSS/FSR/XeSS `EnsureInitialized`) and the overlay already start at first use; that stays as it is, not as a requirement.

Separate API support, device ownership, and presenter selection. Device creation alone never selects the active presenter. A game-owned secondary device can perform useful mod work without taking over the overlay or display composition. Currently `UI::AttachOverlay` runs in `PostCreateSwapChain` and keeps the first swapchain for which attachment succeeds, which lets a dummy or launcher swapchain take the overlay. Attach **at the first `Present` of an eligible swapchain** (window-backed, not tiny), moving to another only once the attached one stops presenting. `DetachFromSwapchain` and `DrawOverlay` already check the attached swapchain's identity. [DXGI swapchain registration](../Source/Core/Handlers/DXGI/DXGIHandlers.cpp#L234-L265) · [Overlay attachment and draw](../Source/Core/UI/Overlay.cpp#L232-L260)

**Deferred until a game switches renderer live** (no game in `Plan.md` Steps 8–9 does; FF7R picks its API at launch). When one does, `supported_apis` makes both hook paths available. The handoff is driven by the game's renderer-change event where available, or by selection of an eligible game swapchain at presentation. Keep one active presenter for overlay, display mode, and composition while old and new devices coexist. Retire the old renderer's objects using its API's native lifetime rules. Game and user settings persist across the handoff. [Current DXGI present workflow](../Source/Core/Handlers/DXGI/DXGIHandlers.cpp#L418-L488) · [D3D12 retirement](../Source/Core/Hooks/D3D12/D3D12Lifetime.hpp)

### Per-device shader stores backed by the shared disk cache

The first API set in `Shaders::SetApi` currently wins process-wide, even if a different API subsequently creates a device. The build adds `_D3D11` and `_D3D12` defines, remaps older D3D12 targets to SM5.1, and declines SM6 targets. `ShaderVariants` maps `hash → Variant` with no device/API dimension, `Resources::Database::shaders` stores one global vector of compiled Luma pass shaders, `ShaderPatching` keeps one global pending queue and `job->d3d12` flag, `ShaderInvariants` consults `GetApi()`, and `Trace::RuntimeStatus` gates D3D12 stats on `Shaders::GetApi() == Api::D3D12`. [Build selection and defines](../Source/Core/Shaders/Shaders.cpp#L364-L431) · [DXIL reflection](../Source/Core/Shaders/ShaderCompiler.cpp#L255-L282) · [Patch job setup](../Source/Core/Shaders/ShaderPatching.cpp#L841-L863) · [Shader invariants](../Source/Core/Shaders/ShaderInvariants.cpp#L121-L144) · [Runtime status](../Source/Core/Trace/RuntimeStatus.cpp#L244-L330)

Remove `Shaders::SetApi` and move compiled/patched shader state into a **per-device `Shaders::DeviceState` / `DeviceShaderStore`** created in `PostCreateDevice` and destroyed automatically on `OnDeviceDestroyed`:

- **Self-contained and idempotent per device:** Each device owns its replacement variants, compiled Luma resource shaders (`Resources::Database` shader states), compiled defines snapshot, build/reload flags, frame counter, and pending patch queue. Each device also creates and owns its native shader objects and PSOs. When a device dies (or a game switches `D3D11 → D3D12 → D3D11`), its entire shader state dies with it—zero stale cross-device state and no profile-eviction lifecycle to manage.
- **Deduplication via the shared on-disk cache:** `Shaders::Cache` already keys compiled bytecode by `(preprocessed_text, target, entry)`, which naturally encodes API defines and register spaces. Add the API/target to the on-disk `ShaderPatching` index key as well. A recreated or second device rebuilds from scratch cleanly and hits the disk cache in milliseconds instead of invoking FXC/DXC.
- **Safe background jobs with `std::weak_ptr` (never a strong COM ref):** Do not hold a strong `ComPtr<ID3D11Device>` / `ComPtr<ID3D12Device>` (or proxy ref) from worker threads, as that delays `OnDeviceDestroyed` and GPU teardown. Instead, attach a `std::shared_ptr<DeviceShaderStore>` (pure C++ state, no COM refs) to the device's lifetime data and pass a small plain-data `BuildConfig` (`Api`, bytecode family `DXBC`/`DXIL`, target/binding rules, defines) plus a `std::weak_ptr<DeviceShaderStore>` to the worker job. The worker checks `weak.expired()` between shaders, so a released probe device stops its build and frees FXC (one caller at a time) for the real device's. The job's `done` runs on the presenting thread, as every job's does, and publishes into the store only if `weak.lock()` succeeds. It publishes to the device the job was started for, not to whichever device presents: a device address can be reused after release, and a device that never presents still needs its shaders.
- **Preserve failure isolation and keep shared originals global:** Preserve each device's last good compiled artifact on a failed reload, keep compile/patch/prewarm off the draw path (falling back to the original shader until ready), and keep immutable original shader metadata (`Shaders::Database`: hash → original bytecode, reflection, debug names, dump-once tracking) process-wide. Dev UI and MCP inspection read from the active presenter's device (falling back to the sole live device before first present). [Compile cache call site](../Source/Core/Shaders/Shaders.cpp#L120-L150) · [Variant storage](../Source/Core/Shaders/ShaderVariants.cpp#L17-L25) · [Original tracking and patch scheduling](../Source/Core/Shaders/ShaderDatabase.cpp#L358-L389) · [Compiled Luma shader storage](../Source/Core/Resources/ResourceDatabase.cpp#L124-L149) · [D3D11 shader object creation](../Source/Core/Shaders/D3D11/D3D11Variants.cpp#L127-L166) · [D3D12 PSO bind and lifetime](../Source/Core/Shaders/D3D12/D3D12Variants.cpp#L641-L668)

### Native presentation and shared overlay UI

Keep the DXGI swapchain handlers shared by D3D11 and D3D12. Vulkan should eventually have native swapchain and presentation handlers (`vkQueuePresentKHR`). To pick the right end shape without writing speculative Vulkan `std::variant` attachment plumbing before a target game exists, split `Overlay.cpp` cleanly now: change `BuildFrame` to take a plain `{uint32_t width, uint32_t height}` extent instead of `DXGI_SWAP_CHAIN_DESC`, and separate the shared window/ImGui-context lifecycle (`AttachWindow(HWND)`, `BuildFrame(Extent)`, `DetachWindow()`) from the DXGI-typed `AttachOverlay(IDXGISwapChain*, ...)` / `DrawOverlay(IDXGISwapChain*)`, which move with the renderer traits to `UI/DXGI/DXGIOverlay.cpp` (`Luma::UI::Overlay::DXGI`). When Vulkan lands, its present handler calls the same shared window/frame helpers and plugs in its own `VK::Render(...)` without rewriting `Overlay.cpp`. [Current overlay attachments](../Source/Core/UI/Overlay.cpp#L41-L48) · [DXGI renderer traits and workflow](../Source/Core/UI/Overlay.cpp#L150-L211) · [Vulkan presentation specification](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html)

Keep the DXGI presenter variant scoped to DXGI: `Presenter` moves out of `Api.hpp` to `Swapchain/DXGI/DXGIPresenter.hpp` as `Swapchain::DXGI::Presenter`. Native presentation entry points call their typed overloads directly.

### Separate typed texture upgrade rules

**Deferred until the first Vulkan game, as `Plan.md` Step 4 already decided:** today's `Game::Resources::SetUpgrades(const Upgrade::Rules&)` stays as it is, and Vulkan adds an overload taking `VkFormat` rules. The names below are illustrative, not a rename. Keep `DXGI_FORMAT` for D3D11 and D3D12 and give Vulkan `VkFormat` rules. Use separate typed registrations that can coexist in one game declaration. Each backend reads its own color and depth lists directly. An omitted or empty native list matches zero formats for a format upgrade. Independently declared resize rules can still match. Format filters on a resize rule also belong to the appropriate format family. [Current format lists and resize declarations](../Source/Core/Resources/ResourceUpgrades.hpp#L72-L94) · [Current matching](../Source/Core/Resources/ResourceUpgrades.cpp#L202-L235)

```cpp
// Proposed registration API. Both declarations can be present in one mod.
SetUpgrades(DxgiUpgradeRules{
   .formats = {DXGI_FORMAT_R8G8B8A8_UNORM},
});
SetUpgrades(VulkanUpgradeRules{
   .formats = {VK_FORMAT_R8G8B8A8_UNORM},
});
```

Share texture dimensions, mip and layer counts, sample count, size predicates, and feature intent. Each format family owns format matching, replacement and view format selection, and copy compatibility. Typed formats can share a pixel representation across APIs. DXGI typeless resources and Vulkan mutable-format images have different creation and view rules. Vulkan also queries device support for the intended usage. Use a typed variant in a shared trace value that holds one native format, or store a format name and its family for display. Use native format types for texture creation and upgrade matching. [DXGI format helpers](../Source/Core/Utils/DXGI/DXGIFormat.hpp#L23-L55) · [DXGI format reference](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format) · [Vulkan image creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageCreateInfo.html) · [Vulkan format support](https://docs.vulkan.org/guide/latest/formats.html)

### Separate shared data from native ownership

The guidelines call for API-agnostic files, but several shared-looking surfaces describe the two Direct3D APIs or store per-device state in process globals. Fixing these boundary shapes now keeps adding Vulkan additive rather than a refactor:

- **Keep `Api.hpp` pure and split DXGI hooks out of `D3D11::Function`.** Move `Presenter` out of [Api.hpp](../Source/Core/Common/Api.hpp) into a DXGI header so `Api.hpp` has no Direct3D forward declarations. Split `Present`, `ResizeBuffers`, and `SetFullscreenState` out of `Hooks::D3D11::Function` ([HookFunctions.hpp](../Source/Core/Hooks/HookFunctions.hpp#L11-L17)) into `Hooks::DXGI::Functions` so D3D12-only games do not need to build a D3D11 function bitset to call `InstallDXGI`.
- **Decouple shared state structs from COM `IUnknown*`.** Extract algorithms over ordinary `Frame::DeviceState` or snapshot values. Keep COM private-data attachment and proxy unwrapping in thin DirectX adapters (`GetState(ID3D11Device*)` / `GetState(ID3D12Device*)`), so Vulkan can back `GetState(VkDevice)` with a side map tied to `vkDestroyDevice` without changing shared math/policy code. [Frame state API](../Source/Core/Frame/FrameState.hpp#L48-L78) · [Scaling state API](../Source/Core/Resources/ResourceScaling.hpp#L89-L110)
- **Align remaining process-wide globals with the device/swapchain-attached model:**
  - `Resources::Samplers::D3D11` keeps a process-wide `registry` and `refreshed_generation`, and `SamplerMirrors` stores SR's derived `sr_offset` as a single global atomic—move both to device state so one device's SR scale cannot leak into another's samplers. [D3D11 samplers](../Source/Core/Resources/D3D11/D3D11Samplers.cpp#L28-L32) · [Sampler LOD bias](../Source/Core/Resources/SamplerMirrors.cpp#L16)
  - `Display::DisplayMode` is a single-swapchain global singleton writing directly into `cb_luma_global_settings`, and `LumaCBuffers::GetSettingsStamp()` sums global frame/generation counters. It only conflicts when two devices both run SR or composition. Deferred until a game does: then keep user/game settings global, store `DisplayMode` per swapchain and derived runtime fields (`SRType`, display mode/nits) per device/presenter, and merge them on cbuffer upload. [DisplayMode](../Source/Core/Display/DisplayMode.cpp#L18-L29) · [LumaCBuffers stamp](../Source/Core/Resources/LumaCBuffers.cpp#L172-L177)
  - `Trace::DebugDraw.{hpp,cpp}` mixes D3D11 and D3D12 in one ~1,250-line file with global snapshots (breaking `<Api><Module>`), and `Trace::RuntimeStatus` branches on `Shaders::GetApi() == Api::D3D12`. [DebugDraw](../Source/Core/Trace/DebugDraw.hpp#L96-L119) · [RuntimeStatus](../Source/Core/Trace/RuntimeStatus.cpp#L244-L330)
  - `Detours.hpp` couples reusable hook templates to D3D11 descriptor helpers—split the D3D11 helpers into a D3D11 header. [Detour dependencies](../Source/Core/Hooks/Detours.hpp)
  - `ReShade` (`CREATION_EXPORTS` in [ReShade.cpp](../Source/Core/ReShade/ReShade.cpp#L80-L87), D3D11-only filter in [AddonEvents.cpp](../Source/Core/ReShade/AddonEvents.cpp#L87)) and `<LumaDllName>dxgi</LumaDllName>` ([Luma2.Common.props](../Props/Luma2.Common.props#L12)) assume DirectX and will need a non-DXGI loader/proxy path for Vulkan-only games.
- **What explicitly stays process-wide:** immutable declarations and user settings (`Game::Info`, `Game::Options`, `Settings::Registry`, `Resources::Database::entries`, `Upgrade`/`Chains` rules, scanned shader file list), on-disk caches (`Shaders::Cache` and API/target-keyed `ShaderPatching` index), immutable original shader metadata (`Shaders::Database`), and singleton inspection/UI surfaces (`Overlay`, `Trace::Capture`, `RuntimeStatus`, MCP) targeting the active presenter.
- **Scope shader containers and trace formats explicitly.** `ShaderContainer` currently models DXBC/DXIL and the common stage enum has six entries; keep it scoped to DirectX so a future `SpirvContainer` sits beside it. Tag trace format fields with a format family (or formatted string) rather than raw `DXGI_FORMAT` integers. [Existing container model](../Source/Core/Shaders/ShaderContainer.hpp#L12-L34) · [Existing stage set](../Source/Core/Common/ShaderStage.hpp#L5-L17) · [Trace fields](../Source/Core/Trace/FrameCapture.hpp#L84-L146) · [Vulkan SPIR-V guide](https://docs.vulkan.org/guide/latest/spirv_extensions.html)

## 5. Choosing the abstraction

**No RHI and no runtime backend polymorphism.** Use direct native overloads when the type is known and feature-local templates or traits for shared work. For shared entry points that receive one of several possible native values, resolve a closed variant with explicit visitor arms. `std::visit` makes a runtime choice of data and enters statically typed code. Use `std::monostate` for a real absent state and model unsupported or unknown inputs explicitly when they require different handling. Keep backend routing out of catch-all `if/else` branches and generic visitor fallbacks. Use a set or independent typed registrations for declarations that support multiple APIs at once.

| Technique | Best use here | Cost or limit |
| --- | --- | --- |
| A concrete free function over values | Extent math, settings, frame and display policy, cache keys, trace filtering. | Requires native code to supply the relevant facts. Usually the smallest change and creates one implementation. |
| Native overloads | The same intent with distinct native call shapes, such as drawing an overlay. | Every API provides its own native entry point. Call resolution stays local and explicit. |
| A feature-local template taking a callable | One order of work with a typed operation, such as uploads or SR input gathering. | Instantiates per concrete use. Keep bodies small and keep larger common work in ordinary functions to limit compile-time and code-size cost. |
| A feature-local trait or specialization | A real type mapping or declaration table, such as `StageTraits` or typed game registry access. | Best when the mapping has multiple consumers. Add concept or `static_assert` checks where the contract pays for itself. |
| A closed variant at a shared boundary | A game DLL serves both native APIs and DXGI supplies an unknown-at-compile-time presenter. | Every visitor compiles for every alternative. Scope the set to that boundary. Prefer a native overload once the caller already knows its API. |

Keep `CreateTexture`, `BindPipeline`, and `Transition` in their native APIs. A common renderer interface would introduce an RHI even if implemented through global traits or CRTP. Concepts can describe an existing feature operation or constrain a specific helper. Keep D3D11 deferred context ownership and D3D12 fence-based retirement visible in each implementation. [D3D12 retirement](../Source/Core/Hooks/D3D12/D3D12Lifetime.hpp) · [D3D11 resource lifetime test](../Source/Tests/DevicesTests.cpp#L494) · [Guidelines on templates and lifetime](Guidelines.md#4-backends)

## 6. Open questions

1. **Swapchain eligibility and live handoff commit rules.** With Mirrors keeping creation-time device work non-destructive and unsupported APIs ignored, device creation eligibility is straightforward. What remains to define is swapchain nomination: how to distinguish the game's primary swapchain from auxiliary/launcher swapchains on first `Present`, and when to commit a live handoff if a candidate's overlay or composition setup fails (default recommendation: keep the old presenter active until the new one attaches cleanly, and guard `Detach` by swapchain identity).
2. **Active-presenter view for global inspection and cbuffer publication.** User settings persist, compiled/patched shaders and sampler/SR state live per device, and `DisplayMode` lives per swapchain. Confirm which derived fields in `cb_luma_global_settings` (`SRType`, `DisplayMode`, nits, `DisplayComposition`) are merged per device at upload time vs. published strictly as the active presenter's view for UI/Trace/MCP. [SR runtime snapshot](../Source/Core/Effects/SR/SuperResolution.cpp#L285-L300) · [Global cbuffer publication](../Source/Core/Resources/LumaCBuffers.cpp#L154-L170) · [DisplayMode](../Source/Core/Display/DisplayMode.cpp#L18-L29)
3. **First Vulkan acceptance target and loader bootstrap.** Choose the game, proxy/loader bootstrap path (since Vulkan-only games may not load `dxgi.dll`), and window path; prove native presentation and shared overlay UI, then pick one native texture upgrade to exercise typed `VkFormat` rules. That first upgrade determines which format, usage, and view cases need support.

## 7. Recommended implementation order and verification

Each unit ships its inspection surface ([Guidelines.md](Guidelines.md#2-rules-of-construction) section 2): `luma_status` reports `supported_apis` and `devices[]` (each live device's API, whether it's the one shown, and its features' per-device state, its shader build first), and `luma_shader_state` names the device and API it read.

1. **Make API boundaries exhaustive and clean up DXGI seams.** *Built.* `Game::Options::supported_apis` replaces `Game::Info::api` (FF7R and the template updated), implicit D3D12 registry defaults are exhaustive, `Presenter` moved to `Swapchain::DXGI`, `Hooks::DXGI::Functions` split out of `D3D11::Function`, and `GetCreatorApi` returns `std::optional<Api>` with swapchains of undeclared or unknown creators left alone. Verify D3D11 devices, D3D12 queues, null/unsupported creators, and that FF7R still builds as one DLL with both event tables.
2. **Move shaders, variants, patching, and compiled Luma shaders to per-device stores.** *Built.* Attach `DeviceShaderStore` in `PostCreateDevice` and start its build there, pass plain `BuildConfig` + `std::weak_ptr<DeviceShaderStore>` to worker jobs (stopping when it expires, publishing in `done` only if it locks), the on-disk patch index stays keyed by what changes a recipe's output (Luma's space is in a recipe's key on D3D12), remove `Shaders::SetApi`, and update `Trace::RuntimeStatus` away from `Shaders::GetApi()`. Verify cache reuse across devices, separate D3D11/D3D12 artifacts for identical original hashes, failed-reload preservation, and clean teardown on `OnDeviceDestroyed` (the second-device cases in the Tests host).
3. **Extract shared super-resolution orchestration.** *Built.* Extract `GatherInputs`, pure selection/validation policy, and a named `Backends` struct replacing positional `std::tuple` indexing. Fix `force_reset` so it is cleared only after `Evaluate` succeeds. Keep both native draw sequences. Verify unavailable providers, Auto and manual switching, first draw after a provider change, required/optional inputs, small resolutions, copy-back, and publication at present on FF7R D3D11 and D3D12.
4. **Presenter ownership and device-attached globals.** *Built*, except `Handlers::Common`: the lifecycle and capture templates are already shared in `Handlers.hpp`, so nothing moved. Move `UI::AttachOverlay` to the first eligible `Present`, move the DXGI overlay entry points to `UI/DXGI/DXGIOverlay.cpp` (`BuildFrame(Extent)` + shared window lifecycle), move matching lifecycle/capture templates into `Handlers::Common`, extract `PresentD3D11`, and move the D3D11 sampler registry and `sr_offset` to device state. Verify a launcher or probe swapchain created first doesn't keep the overlay.
5. **Live API handoff (deferred until a game switches renderer live).** Per-swapchain `DisplayMode`, per-presenter derived cbuffer fields, and a **D3D11 → D3D12 → D3D11** switch while GPU work is in flight.
6. **Prove the Vulkan path end to end (deferred until a target game is chosen).** Add native `vkQueuePresentKHR` presentation hooked into the shared ImGui frame helpers first, then implement one typed `VulkanUpgradeRules` texture upgrade alongside `DxgiUpgradeRules`. Verify omitted format lists, independent resize rules, view compatibility, swapchain recreation, and native presentation synchronization. Extend shader (SPIR-V), mirror, ReShade, and trace features as the port needs them. [Vulkan layer guide](https://docs.vulkan.org/guide/latest/layers.html)

[Current game tests](../Source/Tests/GameTests.cpp#L105) · [Current SR tests](../Source/Tests/SuperResolutionTests.cpp) · [Current D3D12 swapchain tests](../Source/Tests/DevicesTests.cpp#L685) · [Current shader target tests](../Source/Tests/ShadersTests.cpp#L514) · [Current resource and descriptor tests](../Source/Tests/ResourcesTests.cpp#L424)
