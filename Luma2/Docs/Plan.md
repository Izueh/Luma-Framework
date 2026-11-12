# Luma 2.0 Implementation Plan

The staged plan for building Luma 2.0 under the rules of `Guidelines.md`. Each step is written to be handed to one agent as a self-contained brief.

## How to execute a step

1. Read `Guidelines.md` in full, then the step, then the files under **Read first**. The 1.0 files are the behavioural reference: understand them before writing.
2. Build only what the step lists. Something that looks missing is probably in a later step. If it really blocks, build the minimum and say so in the notes.
3. **Decide first** items have a recommended default. Take it unless what you find in the code contradicts it, and record the choice either way.
4. Don't test as you go. When the whole step (or a lettered part) is built, run its **Verify** list once, in the game.
5. Keep game logic as it is (`Guidelines.md` section 8).
6. Record:
   * a deliberate difference from 1.0 → a row in `Guidelines.md` section 17;
   * a backend gap → `LUMA_UNIMPLEMENTED` in code and the parity table below;
   * anything the next step needs to know → a short **Notes** list appended under the step. Facts and decisions only, no narrative.
7. Commit per coherent unit, in the repository's style (`Core: ...`, `FF7R: ...`). Format with `Luma2/.clang-format` before committing.

Verification tools: the overlay, `Luma.log`, and the MCP tools (`luma_status`, `luma_trace_*`, `luma_read_resource`, `luma_compare`, `luma_golden`, `luma_get_settings`...). The 1.0 FF7R build on D3D11 is the reference image and behaviour for every FF7R check: capture goldens from it once in Step 1 and compare against them from then on.

FF7R runs D3D12 by default and D3D11 with `-dx11`. The 1.0 mod is D3D11 only, so D3D12 has no reference build: its target is the D3D11 result.

## Where things stand

* **1.0** (`Source/Core`, 72 game folders) runs on native hooks since `e4bb52d0`. Its hook layer (`Source/Core/hooks`, `overlay`, `system`, `utils`) was written to match 2.0 and has since been corrected against real games. It is D3D11 only.
* **Luma2** (`Luma2/`, commit `f2ec8237`) is a first skeleton: props and solution, utils, settings registry with TOML, dxgi export forwarding, hook installer, device proxy, swapchain and present hooks on both APIs, a minimal game API, an ImGui overlay with D3D11 and D3D12 renderers, MCP `status`, debug layer validation, and a doctest project. Its own notes say most of it was checked in a test host and not yet in the game.
* So Step 0 is not a blank page. It brings that skeleton in line with the guidelines and with what 1.0's hook layer learned afterwards.

### 1.0 reference map

| Topic | Where in 1.0 |
|---|---|
| Hook layer | `Source/Core/hooks/` (`functions.hpp`, `callbacks.hpp`, `detours.hpp`, `d3d11_*_hooks.cpp`, `dxgi_hooks.cpp`, `vtables.*`, `device_proxy.*`) |
| Input blocking | `Source/Core/overlay/input_hooks.cpp` |
| Game interface | `Source/Core/includes/game.h` |
| Device, command list, swapchain state | `Source/Core/includes/instance_data.h` |
| Shader loading, compiling, cloning | `core.hpp` 850–2360 and 3364–4630, `includes/shaders.h`, `utils/shader_compiler.hpp`, `utils/pipeline.hpp` |
| Shader defines | `includes/shader_define.h` |
| Luma cbuffers | `includes/cbuffers.h`, `core.hpp` `SetLumaConstantBuffers` |
| Patching | `includes/patch.hpp`, `patch.inl`, `shader_patching.h` |
| Draw handling | `core.hpp` `OnDrawOrDispatch_Custom`, `HandlePipelineRedirections`, `utils/draw.hpp` |
| Texture upgrades and mirrors | `includes/resource_upgrades.hpp/.inl`, `core.hpp` 7214–8830, `utils/resource.hpp`, `utils/format.hpp` |
| Samplers | `core.hpp` `CreateCustomSampler`, `OnInitSampler` |
| Swapchain | `core.hpp` `OnCreateSwapchain`, `OnInitSwapchain`, `OnSetFullscreenState`, `OnPresent` |
| Display info and spoofing | `utils/display.hpp/.inl`, `hooks/DXGIHooks.*` |
| Managed textures | `includes/managed_resources.h` |
| Trace and MCP | `includes/trace.hpp/.inl`, `mcp.hpp`, `mcp_server.inl`, `Scripts/luma_mcp.py` |
| UI | `core.hpp` `OnRegisterMainOverlay` (9253 on), `includes/ui/` |
| SR | `dlss/`, `fsr/`, `xess/`, `interop/`, `sr_bridge/`, `includes/super_resolution.h` |
| FF7R mod | `Source/Games/Final Fantasy VII Remake/main.cpp`, `includes/settings.hpp`, `Shaders/Final Fantasy VII Remake/` |

---

## Step 0: Skeleton

**Goal:** the `Luma2` tree has its final shape, the hook layer matches what 1.0 proved, and FF7R loads on both APIs with the new UI shell and the settings registry reachable over MCP. No rendering features.

**Read first:** `Luma2/Source/Core` in full (about 5k lines), `Implementation.md` for why it looks the way it does, `Source/Core/hooks/`, `Source/Core/overlay/`, `plans/reshade-to-native-migration-summary.md`.

### 0.1 Tree and names
* Create the module folders of `Guidelines.md` section 3 that don't exist yet: `System/` (loader split out of `Core.cpp` if it helps), `Shaders/`, `Resources/`, `Swapchain/`, `Display/`, `Draw/`, `Frame/`, `Effects/`, `Compat/`. A folder gets a header with the module's namespace and its `IsNeeded()` only when a later step will fill it soon. Don't create empty files for the rest.
* Rename every source file and folder under `Luma2/Source` to `PascalCase` (`hooks/d3d12_hooks.cpp` → `Hooks/D3D12Hooks.cpp`), with `git mv` in a commit of its own, before any other change, and fix includes and project files. On Windows a case-only rename needs two steps (`git mv a tmp`, `git mv tmp A`).
* Rename the game header `core.hpp` to `Luma.hpp`, so "Core" only means the library.
* Replace "clone" with "mirror" for resources throughout code and comments.
* Add `Utils/Debug.hpp`: `ASSERT_ONCE`, `ASSERT_ONCE_MSG`, `LUMA_UNIMPLEMENTED(what)` as defined in the guidelines.

### 0.2 Hook layer reconciliation
* Diff `Luma2/Source/Core/hooks` against `Source/Core/hooks`. Where both have a file, take 1.0's behaviour: it has the fixes found in games (copied vtables with bounds-checked lookup, export table reads, hook-on-demand, `GetDesc` handling, the D3D11 creation scope).
* Bring over from 1.0: the full D3D11 device and context detours, `functions.hpp` (the per-function request bitset), `detours.hpp`, the x86 device proxy thunks, `overlay/input_hooks.*`.
* Shape of a hook: detour → `Handlers::Pre<Function>` / `Post<Function>` → game callback and feature calls, in the order of `Guidelines.md` section 5. With no features yet, the D3D11 handlers only forward to game callbacks.
* D3D12 keeps what the skeleton has (device, swapchain, present). Its function set grows in Step 2.

### 0.3 Game API
* `Luma.hpp`: `Init()`, `SetInfo`, per-API callback registration covering what 1.0's `game.h` exposes, `Game::DeviceData<T>` registration, `Game::OnDraw(hashes, fn)` declared but answering `LUMA_UNIMPLEMENTED` until Step 1.
* Review the 1.0 callback set once, here, and settle the improved one before any game is ported: merge the per-stage and per-draw-function copies where a stage or kind argument does the same job, drop what no game overrides. Record each change in section 17. Check the result against FF7R's and two other games' overrides (Borderlands 2, Persona 5 Royal) so porting stays a signature change.
* UI callbacks: settings section, info, about, dev panel.
* Object state helper: one small template over `SetPrivateDataInterface` / `GetPrivateData` (`Hooks::Lifetime`) that attaches a typed struct to a D3D11 or D3D12 object and destroys it with the object. Every later step uses it instead of pointer-keyed maps.

### 0.3b File layout
* Runtime paths resolve from the `Luma` folder next to the executable (`Guidelines.md` section 8, Game files): `Luma.toml` and `Luma.log` move there now. Development builds can point `Shaders`, `Recipes`, `Textures` and `Global` at the repository through a path in `Luma.toml`, so sources are edited in place.
* Create `Luma2/Resources/` with the layout, empty until Step 1.
* The post-build deployment copies the dll and creates `Luma/`.
* Registering a callback sets its bit in the requested functions. `Core.cpp` installs exactly the requested set.

### 0.4 Settings
* Extend the registry to the `Setting` of `Guidelines.md` section 9: `std::string` and `Key` values, `Source` with `Value` and `Binding` implemented, `Define` and `Recipe` declared for Steps 1 and 3.
* MCP: `luma_get_settings`, `luma_set_setting` over the registry, wire-compatible with `Scripts/luma_mcp.py`.

### 0.5 UI foundation
* `UI/Style` (tokens, applied to `ImGuiStyle`), `UI/Icons` (icon font merged into the atlas), `UI/Widgets` (the table in `Guidelines.md` section 13; build `SettingRow`, `Toggle`, `Slider`, `Combo`, `KeyBind`, `Section`, `Button`, `Toast`, `ProgressBar`, `StatusBar` now, `ListDetail`, `PropertyGrid`, `HashChip` in Step 1 with their first user).
* The shell: Release layout and Development tab bar, the settings view drawn entirely from the registry, the about footer from `Info`, the status bar.
* Rebuild the existing Overlay Settings (overlay key, font size) on the widgets, as the first proof.
* Input blocking through the ported `input_hooks`.

### 0.6 Style enforcement
* Apply the `.clang-format` additions suggested in `Guidelines.md` section 15, format `Luma2/Source` once, add the CI job, the pre-commit path and `.editorconfig`.

### 0.7 Tests
* Keep the doctest project and the pure-logic tests. Fix the device tests if 0.2 breaks them, don't extend them. From here on tests are added only as section 2 of the guidelines allows.

**Not in this step:** any shader, resource or swapchain feature. Win32. ReShade.

**Verify (once):**
* All four configurations build.
* FF7R on D3D11 and on D3D12: the game runs, the overlay opens with the new style and icons, input is blocked while it's open, resize and alt-tab survive, the game exits cleanly.
* A setting changed in the overlay persists in `Luma.toml`. The same setting read and written through `luma_get_settings` / `luma_set_setting`.
* `luma_status` reports the installed hook count. A build of FF7R registering no callbacks installs no context hooks.
* The CI format check passes.

**Decide first:**
* Icon font: Lucide, vendored (decided).
* Whether `Handlers` stays a separate folder or each feature registers into a per-function table. Recommended: keep `Handlers/`, one file per API. It makes the call order readable in one place.

**Notes (Step 0 done, commits `261f9f0f`..`9e25d0b0`):**
* Decided: `Handlers/` kept, one file per API (`D3D11Handlers`, `D3D12Handlers`, `DXGIHandlers`), one function per hooked native function. Features add their calls there: game first in a `Pre`, Core first in a `Post`.
* Decided, then superseded by the corrections below: the icon font is Lucide, not Fork Awesome. The text font is Windows' Segoe UI (ImGui's default font without it).
* Game API (`Luma.hpp`): `Game::SetCallbacks(Game::D3D11::Callbacks{...})` and the D3D12 struct; designated initializers must follow field order. `Game::GetRequestedFunctions()` maps each non-null field to the `Hooks::D3D11::Function` bits; `Core.cpp` installs Core's own (Present, ResizeBuffers, SetFullscreenState) OR the game's, nothing else. No game callback means no context or device-object hooks.
* `Game::OnDraw(hashes, callback)` is declared with 1.0's `DrawResult` meaning and answers `LUMA_UNIMPLEMENTED`: Step 1 defines its real signature (1.0 passes command list and device data). The native draw/dispatch handlers already exist (`Handlers::PreDraw(context, DrawCall)`, `PreDispatch`) and are where Step 1 dispatches by hash. 1.0's higher-level game virtuals (`OnDrawOrDispatch`, `OnMapBufferRegion`, `OnBindViewports`, `OnInitSwapchain`, `PatchShaderBytecode*`...) belong to the features of later steps, not to the native callback set.
* Object state: `Hooks::Lifetime::SetState<T>(object, value)` / `GetState<T>(object)` work on any D3D11, D3D12 or DXGI object (anything with `SetPrivateDataInterface`); the key GUID is derived per type, no GUID to declare. First users: the swapchain's requested desc (GetDesc answers) and per-device game data (`Game::SetDeviceData<T>()`, `Game::GetDeviceData<T>(device)`). `Lifetime::Track` is still the destroy notification for `On*Destroyed` handlers.
* D3D12 hooks are still the skeleton's (device creation, children's GetDevice, swapchain and present through DXGI). The D3D12 vtables now also record method counts (`Hooks/Slots.h`), so Step 2 adds slots and hooks the same way as D3D11.
* Runtime folder: `System::Paths` (`<exe>/Luma`, created at startup; `Luma.toml`, `Luma.log`, `Cache`, `Dump`). Shaders and textures resolve through roots, see the corrections below. Old `Luma.toml`/`Luma.log` next to the FF7R executable are ignored now.
* Settings: `Value` adds `std::string` and `Settings::Key`; `Source` is `Sources::Value | Binding | Define | Recipe`. `Binding` writes its target on registration and every change. `Define` and `Recipe` changes hit `LUMA_UNIMPLEMENTED` (Steps 1 and 3 implement them where `ApplySource` in `Settings/Registry.cpp` dispatches). A setting with a string default can't be in a `constexpr` array.
* MCP `get_settings`/`set_setting` (keys as the registry's, values parsed by type) run as jobs at the next present (`Trace::Mcp::RunPendingJobs` in `Handlers::PrePresent`), and time out when the game isn't presenting; Step 1's MCP tools that need the presenting thread use the same queue. `Scripts/luma_mcp.py` was not changed: its tool descriptions still name 1.0's settings.
* UI: `UI/Style` tokens (applied every frame from the font size setting and the window DPI), `UI/Icons`, `UI/Widgets` (SettingRow, Toggle, Slider, Combo, KeyBind, TextInput, Section, Button with Danger confirmation, IconButton, ProgressBar, Toast, StatusBar), `UI/Shell` (was `MainUI`). Not built: `Segmented`, `ListDetail`, `PropertyGrid`, `HashChip` (Step 1), the soft shadow under popups (this ImGui has no window shadows), a Log tab, docking/detached windows (1.0's dockable Overlay Settings window is now the "Luma" category of the settings view).
* Input blocking is 1.0's `input_hooks` (`Hooks/InputHooks.cpp`, namespace `Hooks::Input`), installed with the other hooks; window procedure and message hooks share `UI::Input::HandleMessage`.
* Formatting: clang-format 21 isn't installed here; the tree was formatted with 22.1.3 (Visual Studio's) and 20.1.8 reports no differences on it, so 21 is expected to agree. The CI job installs 21 from PyPI. The old cpp-linter job now ignores `Luma2/**`.
* Not verified in the game yet: FF7R on D3D11 and D3D12 (overlay, input blocking, resize, alt-tab, exit), settings persistence, `luma_status`, `luma_get_settings`/`luma_set_setting`. All four configurations build and the 40 doctest cases pass.

### Step 0 corrections

Decided after the first in-game look at Step 0. Apply before Step 1, as one unit.

1. **No ReShade files.** Remove the ReShade submodule from every Luma2 include path. Vendor Lucide (TTF and license) under `Luma2/External/Fonts` and switch `UI/Icons` to it. Add the brand font for Ko-fi, Buy Me a Coffee, Discord and GitHub (`Guidelines.md` section 13) and use it on the link buttons, falling back to generic icons if it can't be built cleanly.
2. **Settings threading** (`Guidelines.md` section 9): changes still applied at present by one writer, scalar values readable lock-free from any thread, strings under a lock.
3. **Release after use** (`Guidelines.md` section 4): the D3D12 helper that lets a command list reference a Luma object and releases it after the list has executed (`ExecuteCommandLists` hook, a fence per queue). Steps 1 to 4 use it for variants and mirrors.
4. **Resource roots**: `System::Paths` resolves `Shaders` and `Textures` through an ordered list of roots (game, then Global). Remove the `Luma.ResourcesPath` setting. Development builds take the repository's `Resources` folder from a props-defined macro. The deploy step copies Global then the game into `Luma/`. `Luma2/Resources` takes the overlay layout of `Guidelines.md` section 8.
5. **Global switch**: the `Luma.Enabled` setting, a toggle in the overlay header and a hotkey setting. Nothing reads it yet: Step 1 onward checks it at each draw-path entry.
6. **UI consistency pass**:
   * sliders apply on release, with the live opt-in flag on `Setting`;
   * an `Overlay` category for the overlay key and font size;
   * Credits and every other header drawn with the same `Section` widget as settings categories, lists and links through widgets;
   * review the whole shell against the style tokens for spacing, header and row heights, and fix what differs.

7. **Icons and look, second pass** (replaces the font part of item 1):
   * Font Awesome Free Solid and Brands replace Lucide and the Simple Icons font. Vendor the two TTFs whole with their license, embed them as Windows resources, delete `Luma2/Scripts/BuildFonts.py`, `LumaFonts.inl`, `External/Fonts/Lucide` and `External/Fonts/SimpleIcons`.
   * Apply the depth and color rules of `Guidelines.md` section 13: gradient and top highlight on headers, accent bar and tinted icon per section, per-category icon colors, brand marks in brand colors, accent on changed settings, shadows on popups and toasts.

**Verify (once):** all four configurations build, the format check passes, no path under `Source/External/reshade` appears in any Luma2 project or props file. In FF7R: the icons render (Lucide and the four marks), dragging the font size slider changes nothing until release, Overlay is its own category, Credits looks like the other sections, the global toggle and its hotkey change the setting, and there is no Resources path setting.

**Notes (corrections done):**
* Fonts: superseded by item 7 (see the notes after it).
* No Luma2 project or props file names `Source/External/reshade`. `Hooks/InputHooks.cpp` keeps ReShade's BSD notice, as adapted code.
* Settings threading: values are stored as atomics (every type but strings fits in 32 bits) and strings under a lock; `GetValue` returns a copy. `Set`, `Reset`, `Register` and the listeners are for the presenting thread; other threads call `Settings::Queue`, applied by `Settings::ApplyQueued()` at the top of `Handlers::PrePresent`. Registering is still meant for startup (the key map isn't locked).
* `Setting::live`: sliders, drags and text inputs apply on release (or when the typed value is confirmed) unless it's set; toggles, combos and key binds apply at once. Font size isn't live.
* Settings keys: `Luma.Overlay.Key` and `Luma.Overlay.FontSize` in the `Overlay` category, `Luma.Enabled` and `Luma.EnabledKey` (default F9) in `Luma`. In `Luma.toml`, `Luma.` and then the category are left out of a key in its table (`[Overlay] Key`), so earlier files' overlay key and font size are reset once.
* `Luma::IsEnabled()` (in `Luma.hpp`, lock-free) is what Step 1's draw-path entries check. The hotkey is handled with the overlay key in `UI::Input::HandleMessage` and queued, since the window's thread may not be the presenting one.
* Resource roots: `System::Paths::GetShaderRoots()`/`GetTextureRoots()` (game, then Global), `GetRecipes()`, `Find(roots, relative)`. Development builds get `LUMA_RESOURCES_DIR` (`Luma2/Resources/`) from `Luma2.Common.props` and use `<it>/<game title>` and `<it>/Global` when both exist; otherwise the roots are `Luma/Shaders` and `Luma/Textures`. The deploy step copies `Resources/Global/**` then `Resources/<project name>/**` into `<game>/Luma/` (`.gitkeep` files skipped). Step 1's include handler resolves `#include` lines through `GetShaderRoots()`.
* Release after use: `Hooks::Lifetime::D3D12::Object` (a `ComPtr` plus a stamp) and `Reference(list, object)`. `ID3D12CommandQueue::ExecuteCommandLists` is always hooked on D3D12 and calls `Handlers::PostExecuteCommandLists`, which moves the lists' references to the queue behind a fence of Luma's and drops those the GPU has passed (checked at every submission). Nothing references objects yet; the D3D12 overlay test runs through the hook. `Hooks::Lifetime::SetState<T>(object, arguments...)` now constructs in place.
  * `Lifetime::D3D12::Replace(object, replacement)` publishes a new object under the wrapper's lock, and `Reference(list, object)` returns the pointer the list holds: bind that one, never `object.object`. A list notes what it holds in a small address table, so a repeated bind costs no lock. Still open for Step 2, with the `Reset` hook: a closed list executed more than once gives up its references at the first execution.
* UI: every header is `Widgets::Section` (one padding, color and height), including Credits and Info's Status. New widgets: `Property`, `Caption`, `ListItem`, `Link` (opens through ImGui's platform shell hook), `WrapSameLine`. Links carry the service's mark when the URL is Ko-fi, Buy Me a Coffee, Discord or GitHub. The header has the global switch at the right, with the hotkey in its tooltip.
* Not verified in the game: everything in the Verify list after the format check.

**Notes (item 7 done):**
* Fonts: Font Awesome Free 6.7.2, `fa-solid-900.ttf` and `fa-brands-400.ttf` whole with `LICENSE.txt` (fonts SIL OFL 1.1, icons CC BY 4.0), and IconFontCppHeaders' `IconsFontAwesome6.h`/`IconsFontAwesome6Brands.h` (zlib), in `Luma2/External/Fonts/FontAwesome` (on the include path). `UI/Icons.hpp` names icons with the headers' macros: adding one is a line there, nothing to regenerate. The CC BY attribution is in the Credits section, added by the shell after the game's credits.
* Embedding: `Source/Core/UI/Fonts.rc` (RCDATA `FontAwesomeSolid`, `FontAwesomeBrands`, found through the ResourceCompile include path in `Luma2.Common.props`). A resource compiled into the static Core library would never be linked into a dll (nothing references it), so `Luma2.Game.props` compiles `Fonts.rc` into each game project and `Tests.vcxproj` into Tests.exe. Checked: both resources are in FF7R's `dxgi.dll` and in Tests.exe at the TTFs' full size. `UI::Fonts::Load` reads them with `FindResourceW` from `Utils::Modules::GetSelf()` (null, the executable, before `SetSelf`), and logs an error and shows no icons if one is missing.
* Brand marks: the Free Brands font has Discord (U+F392) and GitHub (U+F09B). It has **no Ko-fi and no Buy Me a Coffee** mark (checked in the TTF's cmap, not only the header): those links use Solid's `mug-hot` and `mug-saucer`, in the services' colors.
* Look, all in `UI/Style` tokens and the widgets: `Style::GetCategory(name)` picks a category's icon and tint by words in its name (Overlay, Luma, Display/HDR/Color, Upscaling/Resolution, Post/Effect, Advanced, Development/Debug, Credits, Status/Info, else sliders); `Style::DrawRaised` (gradient and top highlight) and `Style::DrawShadow` (fading rings outside the window). `Widgets::Section` draws the raised background, the left accent bar and the tinted icon under the header (draw list channels). `Widgets::Tab` underlines the selected tab in the accent and tints its icon. Tooltips, the Danger confirmation popup and toasts have shadows; ImGui's own popups (combo lists) don't. A changed setting's label is brighter and its reset icon is accent-colored; unchanged labels are a step dimmer than body text. Link buttons, the key bind button and status bar icons are tinted. The title bar gets a translucent gradient and highlight drawn over it.
* Icon names that are Windows or build macros are renamed: `Icons::CONSOLE` (terminal), `Colors::Tints::CONSOLE` (not `DEVELOPMENT`, the build define), `ERROR_`, `DELETE_`.
* Seen in FF7R (D3D11, from a screenshot of the Settings tab): the Font Awesome icons, the four link marks in their colors (two of them the mug stand-ins), the section accent bars and tinted icons, and the tab underline all render. A step at the top corners of the section headers' gradient was fixed after it. MCP `status`, `get_settings` and `set_setting` answered in the game; a font size set through MCP was saved to `Luma/Luma.toml`.
* Not checked yet: shadows (no tooltip or toast was open), slider on release, the F9 hotkey, and anything on D3D12. `Luma.toml` files from earlier builds keep their old `[Luma]` keys (`ResourcesPath`, `OverlayKey`, `FontSize`): unknown keys are preserved, harmless, and can be deleted by hand.

---

## Step 1: Shaders on D3D11

**Goal:** FF7R on D3D11 loads its replacement and custom shaders through 2.0, with the dev tools to see it. Shaders are 1.0's biggest and most mature feature: port its behaviour closely.

**Read first:** the shader, defines, cbuffer and draw rows of the reference map. `Design.md` "Shaders".

**Build:**
* `Shaders::Database`: every shader object the game creates, its hash, stage, bytecode (kept when a feature needs it), reflection (cbuffer, sampler, SRV, UAV, RTV slots), name. Destroyed with the object.
* Resources move: bring `Shaders/Global` and `Shaders/Includes` into `Luma2/Resources/Global` regrouped by purpose (`Guidelines.md` section 8), and `git mv` FF7R's shaders, recipes and textures into `Luma2/Resources/Final Fantasy VII Remake`, without their `.cso` files. Update every `#include` to the new paths. No shader code changes in that commit. Before the move, package a 1.0 FF7R build and capture the goldens from it.
* `Shaders::Files`: scan the `Shaders` roots (the game's, then Global's; one merged `Luma/Shaders` when packaged), resolve includes from the `Shaders` root, match files to hashes by the 1.0 naming, including multi-hash names and `0x########` redirections (`redirected_shader_hashes`).
* `Shaders::Compiler` (FXC, loaded at runtime) and `Shaders::Cache`: compiled objects in `Luma/Cache/Shaders`, keyed by source hash and defines (1.0's preprocessed-hash check), plus cached reflection. Compile on a job queue with a progress count. A second boot with nothing changed compiles nothing.
* `Draw` state: the state stack with a group mask and 1.0's three modes as presets, and the bound-state cache recording only the groups a game declared (all of them in Development).
* Cbuffer display: typed fields from reflection with the fallbacks of `Guidelines.md` section 11, in the trace detail pane and for `luma_read_cbuffer` later.
* `Shaders::Defines`: shader defines as settings with `Source::Define`, the global and per-game `Settings.hlsl` write-out, recompile on change or on request, Auto Compile.
* `Shaders::Custom`: Luma's named shaders with per-entry defines.
* `Shaders::Constants`: the `LumaSettings` and `LumaData` cbuffers, their slots, binding them where a replaced shader runs. Dev sliders as Development settings.
* `Shaders::Variants` for D3D11: a replacement shader object per replaced stage, swapped at bind time (`Pre*SetShader`), with the load and unload toggles.
* `Draw`: fullscreen triangle, copy and scale helpers (`utils/draw.hpp`).
* `Game::OnDraw(hashes, fn)` and the generic draw and dispatch callbacks with 1.0's `DrawOrDispatchOverrideType` meaning.
* `Trace::Capture` v1 for D3D11: one frame's calls with shader hashes, bound views and their descs, blend, depth, viewport, sampler and cbuffer info, and the variant used. This is the data behind 1.0's "Captured Commands" tab.
* Dev UI: Trace tab (`ListDetail` of events, `PropertyGrid` for State Analysis, per-shader actions), Shaders tab (loaded files, custom shaders, compile errors), shader defines inside the settings view. Status bar compile progress.
* MCP: `luma_trace_capture/list/get`, `luma_shader_list`, `luma_shader_state`, `luma_dump_shader`, `luma_reload_shaders`, `luma_log`.
* Shader dumping (`Dump` folder) in Development and Test.
* `Luma.Enabled` checked where variants are bound and where draw callbacks and custom passes run. Tracking is unaffected by it.
* FF7R: `Init()` declares its shader hash lists, custom shaders, define defaults, cbuffer slots and settings table. Draw callbacks are registered and ported only where they don't depend on later steps. The rest stay out until their step.

**Not in this step:** texture upgrades (the replaced shaders will write into the game's original formats, so the image clips: expected), patching, D3D12, debug-drawing a texture from a capture (Step 5).

**Verify (once):**
* Capture goldens from the 1.0 FF7R D3D11 build first: a fixed save, fixed settings, `luma_read_resource` of the tonemap input, the tonemap output and the final image.
* 2.0 FF7R D3D11: `luma_shader_list` shows the same replaced hashes as 1.0. Toggling "Unload Shaders" shows the vanilla image.
* A trace capture lists the frame's draws with the correct hashes, and the tonemap draw shows the Luma variant.
* Editing an `.hlsl` and reloading recompiles it, with the progress bar and a toast. A compile error is shown, the old variant stays.
* Changing a shader define through the overlay and through `luma_set_setting` recompiles.
* `Luma.Enabled` off (overlay and hotkey) shows the vanilla image at once, and back on restores the mod without recompiling or reloading anything.
* Editing and reloading a shader repeatedly while the game runs causes no crash or validation error (the old variant is released only after use).
* Publishing-Release build: no dev tabs, same replaced shaders, and the trace, dumping, MCP and bytecode-keeping code absent from the binary (check that their hooks aren't installed in `Luma.log`).
* Second boot: `Luma.log` reports every shader loaded from cache and none compiled.
* A cbuffer of a replaced shader shows named fields in the trace pane. One of an unreplaced, stripped shader shows raw rows.

**Decide first:**
* How much shader bytecode to keep in memory outside Development. Recommended: as 1.0, keep it only when dumping or patching needs it.
* Deferred contexts. FF7R on D3D11 uses the immediate context for what the mod touches. Recommended: build per-context state the way 1.0's `CommandListData` does, since the hooks already fire for both.

---

## Step 2: D3D12 foundation and shaders on D3D12

**Goal:** the same FF7R shaders replaced on D3D12, and the same trace.

**Read first:** `Design.md` "DX12 binding model" and "Shaders". Step 1's result.

**Research before building (half a day, write the answers into Notes):**
* Dump FF7R's shaders on D3D12 and compare hashes with D3D11. If the DXBC is identical, the existing replacement files apply as they are. If not, find what differs (root signature blob, debug info) and whether hashing a normalised container makes them match.
* FF7R's shader model on D3D12 (expected SM5.0 DXBC), how its root signatures are built, whether it uses descriptor tables per draw, bindless, bundles or `ExecuteIndirect`.
* Whether the Luma cbuffer slots (b12, b13) are free in its root signatures.
* **How it writes descriptors**: copied into the shader-visible heap per draw (expected for Unreal's D3D12 RHI), or written once into persistent tables. This decides whether swapping at descriptor write (`Guidelines.md` section 4) is enough for FF7R or shadow heaps are needed in Step 4.
* The order it binds root signature, PSO and render targets in, and whether that order is stable.

**Build:**
* D3D12 hooks: PSO and root signature creation, command list binds (pipeline state, root signature, descriptor heaps, render targets, viewports), draws and dispatches, `Reset`, resource and view creation and descriptor copies as far as trace needs.
* Swap at bind: the shader variant at `SetPipelineState`. The per-command-list record (PSO, root signature, targets) and the dirty-flag check at draw only for a variant that needs an extended root signature.
* `Shaders::Database` for PSOs (per-stage hashes, a deep copy of the desc), `Shaders::Variants` for PSOs created on the job queue, fallback to the original while a variant isn't ready.
* Luma's `ID3D12PipelineLibrary` in `Luma/Cache/Pipelines`, so variant PSOs aren't compiled again on later boots. Discarded and rebuilt when the driver rejects it.
* Luma constants on D3D12: root signature extension for the Luma slots when the game's root signature doesn't cover them, extended signatures cached per game signature.
* Descriptor tracking (slot → resource and view desc), needed by trace now and by mirrors in Step 4.
* Deferred release of Luma's D3D12 objects (fence per queue).
* `Trace::Capture` for D3D12, same event model as D3D11 so the UI and MCP don't branch.
* `Game::OnDraw` for D3D12. FF7R's `Init()` registers per API.

**Not in this step:** descriptor heap expansion and shadow heaps (fallback only, see Step 4), DXIL and DXC (first SM6 game), bundles and `ExecuteIndirect` (`LUMA_UNIMPLEMENTED`).

**Verify (once):** the Step 1 list, on D3D12. Plus the D3D12 debug layer and GPU-based validation clean with shaders replaced, and no measurable frame time change with the mod's shaders unloaded.

**Decide first:**
* SM5.0 shaders can't use register spaces. Recommended: Luma bindings use fixed slots in space 0 on both APIs for SM5.x games, and the Luma register space of `Design.md` is used only by SM5.1 and SM6 shaders.
* PSO variant creation: synchronous when the PSO is in the pipeline library, on the queue otherwise.
* Root signature extension, if FF7R's signatures don't cover the Luma slots. An extended signature can't be used with the game's own PSOs, so either every PSO of that signature gets a twin or the signature is swapped only for draws using a variant (needing the root arguments replayed). Recommended: the second, limited to variants that declare Luma bindings. Decide with the research in hand.

---

## Step 3: Shader patching

**Goal:** DXP recipes and game bytecode patches produce variants on both APIs. FF7R's dithering fix works.

**Read first:** `patch.hpp`, `patch.inl`, `shader_patching.h`, FF7R `PatchShaderBytecodeSync` and its recipe registration, FF7R's `Recipes` folder, the DXP library's README.

**Build:**
* `Shaders::Patching`: recipe loading from `Luma/Recipes`, recipe registration by games (identity, display name, default, visibility), the sync and async bytecode callbacks of `game.h`, patched variants through the Step 1 and 2 machinery.
* Patch cache in `Luma/Cache/Patches`: patched bytecode and an index of which hashes each recipe matched and didn't, keyed by recipe content, environment values and patcher version. A later boot patches nothing it has an answer for.
* Recipe switches and environment values as settings with `Source::Recipe`. A change re-patches.
* Recipe resource bindings resolved by name. Until Step 4 only Luma's own resources resolve.
* `System::Memory`: the pattern scan FF7R's bytecode patch uses (`System::ScanMemoryForPattern`).
* Dev UI: Patches tab (recipes, matched shaders, state, errors). Patch progress in the status bar.
* MCP: `luma_patch_state`, `luma_set_recipe`, `luma_reload_patches`. `luma_trace_list variant=original` finds draws that fell back.
* Patched shader dumping where 1.0 has it.

**Verify (once):** FF7R's "Dithering Fix" recipe on and off changes the matched shaders on both APIs (`luma_patch_state`), toggling it from the overlay and from MCP re-patches without a restart, and a broken recipe reports an error and leaves the originals running.

**Decide first:** DXP on DXIL isn't needed for FF7R. Recommended: wire DXBC only and leave DXIL as `LUMA_UNIMPLEMENTED`.

---

## Step 4: Resources: database, mirrors, upgrades, samplers

**Goal:** FF7R's texture format upgrades, LUT upgrade and sampler upgrade work through mirrors on both APIs.

**Read first:** the texture, sampler and managed texture rows of the reference map, FF7R's upgrade configuration at the end of its `main.cpp`, `Design.md` "Resources" and "DX12 binding model".

**Build, part A (shared and D3D11):**
* `Resources::Upgrade`: rules as data. Port 1.0's configuration surface one to one (formats, 2D size filters, aspect ratios and pixel threshold, custom sizes, LUT size and dimensions, depth formats), and `GetBestResourceUpgradeFormat` and its view counterpart.
* `Resources::Mirrors` on D3D11: mirror textures and mirrored views (SRV, RTV, UAV, DSV), created when a rule matches at creation. Swapped at bind for every stage. Copies, resolves and clears redirected or converted by the table in `Design.md` "Copies and clears". `Map`, `UpdateSubresource` and initial data converted into the mirror (`ConvertResourceData`).
* `Resources::Samplers`: sampler mirrors with LOD bias and anisotropy, refreshed on change.
* `Resources::Database`: named entries, invalidation events, DDS loading from `Textures` folders (FF7R's noise texture).
* Trace: mirror info on every event that touches one. Dev UI Resources tab (mirrors, database entries). MCP `luma_read_resource`, `luma_read_cbuffer`, `luma_compare`, `luma_golden`.

**Build, part B (D3D12):**
* Mirror resources with a matching barrier for every barrier on the original.
* Swap at descriptor write: a view created or copied for a mirrored resource is written as the mirror's view. A resource that gains a mirror later has its existing descriptors rewritten through the resource → slots index from Step 2.
* RTV and DSV handles replaced at `OMSetRenderTargets`.
* PSO variants keyed by render target formats, since D3D12 bakes them into the PSO. Chosen at draw from the command list record, because the PSO and the targets can be bound in either order.
* Shadow descriptor heaps only if Step 2's research showed FF7R writes descriptors once and needs a per-draw choice. Otherwise they stay unbuilt.
* Mirror memory: a pool of committed resources keyed by desc.

**Not in this step:** the swapchain (Step 5), chains by hash and scaling (Step 6), reserved resources, cross-queue resources, bundles (declared exclusions, visible in trace).

**Verify (once):**
* FF7R D3D11: `luma_read_resource` of the tonemap input shows the upgraded format with values above 1.0 where 1.0's build has them. `luma_compare` against the Step 1 goldens for the tonemap input and output.
* Upgrades off (the runtime switch) returns the Step 1 image.
* The LUT is upgraded and sampled correctly (no banding or shift against the golden).
* Sampler LOD bias changes when its setting changes.
* D3D12: the same checks against the D3D11 result. Debug layer clean. A resolution change in the game recreates mirrors without leaks (counts in `luma_status`).

**Decide first:**
* D3D11 mirrors are swapped at bind, as 1.0's indirect path. Confirm no FF7R path reads a bound view back and compares pointers.
* Memory: mirrors double the upgraded targets. Recommended: accept it, measure in FF7R at 4K and note the number.

---

## Step 5: Swapchain, display and HDR

**Goal:** FF7R presents through a Luma-owned swapchain in HDR10, scRGB or SDR, switchable at runtime, with 1.0's display composition and UI handling.

**Read first:** the swapchain and display rows of the reference map, `Shaders/Global/Luma_DisplayComposition.hlsl`, `Shaders/Includes/UI.hlsl` and `Settings.hlsl`, FF7R's `OnDraw_OutputPass`, `Guidelines.md` section 12.

**Build:**
* `Swapchain`: creation desc rewritten to flip model with tearing. The game's `GetBuffer`, `GetDesc*`, `GetCurrentBackBufferIndex`, `ResizeBuffers*`, `ResizeTarget` and `SetFullscreenState` answered from the game's requested state. Game back buffers are Luma textures that take mirrors like any other.
* Output mode (`SDR`, `HDR10`, `scRGB`) as a setting applied at the next present by resizing the real buffers and setting the color space. HDR10 metadata from the display's peak.
* Fullscreen policy: 1.0's `prevent_fullscreen_state` and borderless handling.
* `System::Display`: HDR supported and enabled, peak and paper white defaults, SDR white level, enabling Windows HDR, re-query when the window changes display. NVAPI display modes behind `UseLumaNVAPI`.
* `Display` spoofing: `IDXGIOutput6::GetDesc1`, `CheckColorSpaceSupport`, adapter vendor, with the scoped "real query" escape for Luma's own calls.
* `Frame` resolutions: render, output, swapchain and display as four fields. Output and swapchain are equal in this step, but no code assumes it: composition has the scale stage, and the overlay and input use the swapchain size.
* `Display` composition on both APIs: the 1.0 composition shader and its define-driven behaviour unchanged, plus the encode for the output mode. UI separation for the `UI_DRAW_TYPE` modes FF7R uses.
* A switch per composition stage (`Guidelines.md` section 12), as settings. Cheap runtime flags in the composition cbuffer where the shader allows it, defines otherwise.
* Debug draw of any view of a captured event into the composition (1.0's "Debug Draw Shader Instance" and resource highlight).
* Release UI: the Display category (output mode, peak white, paper white, UI paper white, gamma) built from settings, matching 1.0's options.
* FF7R: the output pass callbacks.

**Verify (once):**
* On an HDR display: FF7R in HDR10 on both APIs, `luma_status` showing the real format and color space, peak white and paper white settings changing the image. Switching to scRGB and to SDR at runtime without a restart or a crash.
* HDR10 against scRGB: `luma_read_resource` of the composed image decoded to nits agrees within tolerance between the two modes.
* Against the 1.0 D3D11 golden of the final image in the same mode.
* With Windows HDR off: SDR path, and the HDR options disabled with a reason.
* Resize, borderless and alt-enter. Monitor change if two displays are available.
* The UI renders at the correct brightness in every mode.
* Each composition stage switched off alone changes only what that stage does (`luma_ab` on the stage's setting, reading the composed image).
* With the swapchain size forced to differ from the output size through a Development setting, the image is scaled and the overlay and mouse still line up.

**Decide first:**
* What the composition shader takes as input per game (linear, gamma, already PQ). FF7R sets `EARLY_DISPLAY_ENCODING` and friends: keep the defines as the contract and add the output encode as a final stage. Don't rework the shader's logic.
* D3D12 back buffer count and index: the game's buffers are separate Luma resources with their own index sequence. Confirm FF7R doesn't depend on the buffers being the swapchain's (shared handles, `IDXGISurface`).
* `scRGB` stays the Development default only if something needs it. Recommended: HDR10 everywhere, scRGB as an option.

---

## Step 6: Chains and scaling

**Goal:** one system for upgrade-by-hash chains and resolution scaling, replacing per-game upscale tracking. FF7R's early upscale runs on it on both APIs.

**Read first:** `resource_upgrades.hpp/.inl` (seeds, `ChainTextureFormatUpgradesType`, the scale chain), FF7R `GetEarlyUpscaleTargetResolution`, `OnBindViewports`, `OnBindScissorRects`, FFXV `includes/upscale_tracking.hpp`, Granblue `includes/upscale.*`, Quantum Break `Upscaling.*`.

**Build:**
* `Resources::Chains`: seeds by shader hash and RTV or UAV slot, `Direct` and `DirectAndIndirect` propagation, mirrors created when the chain reaches a resource.
* `Resources::Scaling`: scaled mirrors, viewport and scissor rescale, copy box and clear rect rescale, mismatched-scale copies as a scaled blit, stencil stand-ins, per-frame activation ("from this pass on, the scene is output resolution").
* Invalidation on render resolution and output resolution change.
* Trace shows a mirror's scale and the chain that produced it.
* FF7R: its early upscale and viewport code replaced by declarations to `Scaling`.

**Verify (once):** FF7R with SR placeholder off and a fixed render scale: post-TAA passes run at output resolution on both APIs (trace viewports and target sizes), bloom, DOF and motion blur match 1.0 goldens at the same render scale, dynamic resolution changes don't leak mirrors.

**Decide first:** the design takes the union of what FF7R, FFXV and Core's scale chain do. If FFXV needs something FF7R doesn't (stencil stand-ins, pooling by frame), build the interface for it and the implementation only when FFXV is ported, unless it's trivial.

---

## Step 7: Super resolution and the complete FF7R

**Goal:** FF7R at feature parity with 1.0 on D3D11, and equivalent on D3D12.

**Read first:** the SR row of the reference map, all of FF7R `main.cpp` not yet ported (TAA callback, motion vector decode, cbuffer capture through `Map`/`Unmap`, LOD bias, SSAO temporal noise, DOF and motion blur).

**Build:**
* `Effects::SR`: the 1.0 interface (`SR::SettingsData`, draw data, user type selection), DLSS on D3D11 and D3D12, FSR on D3D11 and D3D12, XeSS where 1.0 has it. On D3D12 the upscalers run natively, so the D3D11→D3D12 interop is used by D3D11 only.
* `Effects::RCAS`.
* `Frame`: render, output and upscaled resolution, jitter, camera cut, pass markers. Decide here whether the camera model of section 17 is worth it now.
* `Resources` constant capture: the framework call for "contents of this cbuffer", implemented on D3D11 through the `Map`/`Unmap`/`UpdateSubresource` hooks and on D3D12 for FF7R's per-view globals (research: how UE4's D3D12 RHI uploads uniform buffers).
* The rest of FF7R's callbacks, ported as they are.
* MCP: `luma_sr_state`, `luma_sr_capture`, `luma_dev_values`, `luma_set_dev_value`, `luma_ab`.
* Release UI: the Upscaling category.

**Verify (once):**
* A feature checklist written from the 1.0 FF7R `main.cpp` and settings, each item checked on both APIs.
* DLSS and FSR on both APIs: `luma_sr_capture` inputs (color, depth, motion vectors, jitter) match between 1.0 and 2.0 on D3D11, and between D3D11 and D3D12.
* Final image goldens against 1.0 with `luma_compare metrics=["flip"]`, in HDR and SDR.
* A play session of at least one chapter transition, a menu, a cutscene and a video on each API, with no validation errors and no growth in `luma_status` counts.

---

## Step 8: Release quality and compatibility

**Goal:** FF7R on 2.0 can be given to users. Guests work.

**Build:**
* Release UI pass: every FF7R setting in the registry with categories, tooltips and defaults as in 1.0. Warnings as toasts. About and credits.
* Packaging: `Scripts/package.ps1` and the CI workflow build and package the Luma2 FF7R.
* `Compat::ReShade` as `Guidelines.md` section 14: chain-load, foreign work recognised by calling module, and the choice of which buffer ReShade's effects run on. No ReShade headers.
* RenderDoc: a capture of FF7R on each API with the mod active. Fix only what's a real Luma bug.
* OptiScale: FF7R built with another `LumaDllName` next to OptiScale's `dxgi.dll`. Document the working arrangement, or that there isn't one.
* Detached UI: enable ImGui multi-viewport in Development and try it in FF7R on both APIs.

**Verify (once):** each bullet above, once. ReShade with one effect on and Luma's HDR output correct underneath it.

---

## Step 9: Game by game

Each game is one unit: port the mod, build the features it needs that don't exist, verify against its 1.0 build. Before starting a game, list its feature inventory from its `main.cpp` (callbacks overridden, `ENABLE_*` defines, Core settings it changes, files next to `main.cpp`) and split it into "exists", "to build" and "game code to move as is".

Order, chosen so each game adds features the previous ones didn't need:

| # | Game | Why now: features it adds |
|---|---|---|
| 1 | Unreal Engine (generic) | runtime shader classification (`shader_detect.hpp`), 2D and 3D LUT handling, a mod that covers many titles. Then one Unreal D3D12 title to exercise SM6, DXIL and DXC |
| 2 | Final Fantasy XV | the full `Scaling` tracker (stencil stand-ins, pooling), VBAO, immediate-context `UpdateSubresource` handling, DirectInput blocking |
| 3 | Borderlands 2 and The Pre-Sequel, then GOTY Enhanced | **Win32 build**, the SR bridge, `Resources::Geometry` (vertex and index buffer bind and write tracking, current and previous copies swapped at present, vertex shader patching for motion vectors), blend state callbacks, `Effects::SMAA`, `Effects::Bloom` |
| 4 | Persona 5 Royal, then Metaphor ReFantazio | deferred contexts and command lists as the main path, context ordering, blend edits |
| 5 | Prey | CPU hooks and a native plugin (`System::Memory`, mid-function hooks), the oldest and most customised mod |
| 6 | Mass Effect Andromeda | NVAPI HDR, append buffer redirection, XeSS, permutation-heavy shaders |
| 7 | Granblue Fantasy Relink | UI scaling, game function hooks, a second engine-level upscale integration |
| 8 | `_Template`, `_Generic Mod`, Graphics Analyzer | the starting points for new mods, on the 2.0 API |
| 9 | Everything else | in batches by engine or by shared code. Most need no new feature by now |

After each game: move code that the new game shares with an earlier one into the framework, and port the earlier game onto it in the same unit.

## Step 10: The swap

* All games ported or explicitly retired.
* `Luma2/Source` replaces `Source/Core` and `Source/Games`, `Luma2/External` merges into `Source/External`, the ReShade submodule goes unless `Compat::ReShade` needs its headers, `Luma.sln` is replaced, CI builds only 2.0.
* `Design.md`, `Implementation.md` and `Core2.md` are folded into `Guidelines.md` or deleted.

---

## Parity table

Update when a backend gains or skips something. `—` means `LUMA_UNIMPLEMENTED`.

| Feature | D3D11 | D3D12 | Planned |
|---|---|---|---|
| Hooks, overlay, settings, MCP status | Step 0 (full 1.0 detours) | Step 0 (device, swapchain, present) | Step 0 |
| Shader replacement, custom shaders, defines | | | 1 / 2 |
| Trace capture | | | 1 / 2 |
| Shader patching (DXBC) | | | 3 |
| Shader patching (DXIL) | n/a | — | first SM6 game |
| Mirrors, format upgrades, samplers | | | 4 |
| Swapchain output modes, composition, spoofing | | | 5 |
| Chains and scaling | | | 6 |
| DLSS, FSR | | | 7 |
| XeSS | | | 7, where 1.0 has it |
| Constant capture | | FF7R's per-view globals only | 7 |
| Deferred contexts as main path | | n/a | 9.4 |
| Bundles, `ExecuteIndirect`, reserved resources | n/a | — | when a game needs them |
| Geometry tracking | | — | 9.3 |
| Win32 | | — | 9.3 |

## What can run in parallel

Once Step 0 is in:

* Step 3 (patching) only needs Step 1 for D3D11 and can run next to Step 2.
* `System::Display` and the spoofing hooks of Step 5 depend on nothing after Step 0.
* The UI widgets `ListDetail`, `PropertyGrid`, `HashChip` and the Release settings layout can be built against fake data.
* The SR wrappers of Step 7 (DLSS, FSR, XeSS as libraries with a native interface per API) depend on nothing. Only their integration into FF7R needs Step 6.

Two agents must not edit the same handler file at once: `Handlers/D3D11Handlers.cpp` and `Handlers/D3D12Handlers.cpp` are where every feature meets.

## Additional considerations

Proposals found during the review. None is committed work until it's moved into a step.

* **Shared frame model.** Nearly every game redeclares `has_drawn_taa`, `has_drawn_upscaling`, `camera_cut`, `is_in_menu`, render and output resolutions. A `Frame` module with named pass markers reset at present would remove that. Raised in Step 7.
* **Camera model.** Jitter, FOV, near and far, projection matrices extracted per game for SR, AO and motion vectors. A common struct the effects read would let an effect be added to a game by filling one struct.
* **Motion vector helpers.** 35 game folders touch motion vectors. After `Geometry` exists, a shared "decode or generate motion vectors" effect is the natural next fold-in.
* **Pass replacement helper.** "Replace this draw with this fullscreen custom shader, same targets" is written by hand in many draw callbacks.
* **Shader hash stability across APIs.** If Step 2's research finds a normalisation that makes D3D11 and D3D12 hashes match, use it for every game: one set of replacement files per game.
* **Crash context.** An unhandled exception filter that flushes `Luma.log` with the last hook entered and the installed hooks would shorten most bug reports.
* **Performance counters.** Time spent in Luma per frame and per hook class, reported through `luma_status`, to catch regressions as features land.
* **Mod and Core version check.** With Core as a library, a game DLL should log the Core commit it was built with.
* **Output resolution decoupling.** With a Luma-owned swapchain the real swapchain can be a different size from what the game renders to. That gives borderless upscaling for games with fixed resolutions. Not planned, but Step 5 shouldn't prevent it.
* **Frame generation hooks.** FF7R has a disabled `ENABLE_FRAMEGEN` path with a hudless target. The composition stage in Step 5 is where a hudless output would be exposed.
* **1.0 settings migration.** Users of 1.0 mods have values in the old config. Decide at Step 8 whether to import them once or start from defaults.
* **Global shaders during the transition.** `Resources/Global` is a regrouped copy of 1.0's `Shaders/Global` and `Shaders/Includes`. A fix made on one side has to be made on the other until the swap. File names and contents stay the same so the two can still be compared file by file.
