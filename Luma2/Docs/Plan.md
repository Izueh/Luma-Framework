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
* Rename the game header `core.hpp` to `Luma.hpp`, so "Core" only means the framework's code.
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
* Embedding: `Source/Core/UI/Fonts.rc` (RCDATA `FontAwesomeSolid`, `FontAwesomeBrands`, found through the ResourceCompile include path in `Luma2.Common.props`). A resource compiled into the static Core library would never be linked into a dll (nothing references it), so `Luma2.Game.props` compiled `Fonts.rc` into each game project and `Tests.vcxproj` into Tests.exe (since Step 1 corrections item 10 Core is compiled into each project and `Fonts.rc` comes with `Core.vcxitems`). Checked: both resources are in FF7R's `dxgi.dll` and in Tests.exe at the TTFs' full size. `UI::Fonts::Load` reads them with `FindResourceW` from `Utils::Modules::GetSelf()` (null, the executable, before `SetSelf`), and logs an error and shows no icons if one is missing.
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
* `Game::D3D11::OnShaderDraw(hashes, fn)` and the generic draw and dispatch events (`Events::on_draw_or_dispatch`) with 1.0's `DrawOrDispatchOverrideType` meaning.
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

**Notes (part A: the shader replacement path, commits `5788b1c6`, `a0980082`, `c90fac78`, `dd9d049a`; its module layout, `Custom`, `Constants` and the per-module need decisions are superseded by part B's corrections below):**
* Goldens: the 1.0 FF7R tree to package and capture them from is `5788b1c6` (the commit before FF7R's files moved). Global's shaders are a regrouped copy (`Includes/{Core,Color,Tonemap,Effects,Compat}`, `Luma/{Display,Copy,Sanitize,Effects,Debug,Samples}`); 1.0's `Shaders/Global/Textures/FAST` wasn't copied (textures, Step 4). FF7R had no textures to move; its `includes/` is `Shaders/Game/`. `Game/GameCBuffers.hlsl`'s C++ include is `Common/Math.hpp`, and FF7R's project has its `Shaders` folder on the include path.
* Decided: per-context state as 1.0's `CommandListData`, in each context's private data (immediate and deferred), one struct per module (`Variants`: the stages running a variant; `Constants`: what was uploaded). Found per bind or draw with `Hooks::Lifetime::GetOrCreateStateCached<T>` (the thread's last context, until any state is destroyed). Reset by `ClearState`, `FinishCommandList`/`ExecuteCommandList` without restore; constants are re-uploaded after either of those.
* Decided: game bytecode is kept in Development and Test builds only (dumping, `luma_dump_shader`), never in Publishing. Reflection is Development only.
* Modules (`Luma::Shaders`): `Jobs` (one worker thread, `Queue(name, work, done)`, `done` at the next present, `GetStatus`, `GetLastReport`), `Compiler` (FXC only: `Preprocess`, `Compile`, `Reflect`, `Disassemble`; files and includes named from the Shaders root through `System::Paths::GetShaderRoots()`), `Cache` (`GetKey`, `ReadShader`/`WriteShader`, the reflection of game shaders only `D3DReflect` couldn't read: reflection runs first, the disassembly fallback's result is cached), `Files` (`Parse`, `Scan`), `Defines` (`Collect`, `NeedsCompile`), `Constants`, `Custom`, `Database`, `Variants`; `Shaders.hpp` has `Register`, `Start`, `Reload`, `Load`, `Unload`, `OnPresent`, `GetFrame`, `GetLastBuild`. D3D11 parts: `D3D11Database`, `D3D11Variants`, `D3D11Constants`, `D3D11Custom`, and `D3D11Shaders` (shared: `CreateShader`, `Published`/`Publish`/`Retired`).
* What part B and Step 2 build on: `Database::Shader` (hash, stage, target, name, live object count, Development reflection slots) by hash, `Database::D3D11::Find(object)`; `Variants::D3D11::GetVariantStages(context)` and its per-context record (part B adds the bound hashes per stage there for `Game::OnDraw`); `Constants::D3D11::PreDraw/PreDispatch` (part B adds the per-pass LumaData and the game's data callback); `Custom` bytecode by `"name"_h` and per-device objects in `D3D11Custom` (part B adds the getter its draw helpers need); `Jobs` for PSO variants and patching; `Variants::Publish`/`GetBytecode`/`GetGeneration` for D3D12 PSO variants (a PSO is recreated only when the bytecode it was built from changes, as here).
* Build: `Shaders::Start` (after the settings load, before the hooks) scans the roots, decides `Variants::IsNeeded()` (always in Development) and queues the first build. A build preprocesses each unit (file x hash, custom shader) on the worker, keys it, reads `Luma/Cache/Shaders/<key>.cso` or compiles and writes it, and publishes at a present. A failed unit keeps its last compiled bytecode. Equal bytes are the same bytecode: no object is recreated, on reload, unload/load or `Luma.Enabled` off/on.
* Cache keys: FNV-1a 64 of the preprocessed text without its `#line` lines (FXC makes the source name absolute with the current directory), with the target, entry and a version. Each file is compiled through a one-line `#include "<path>"` source, so messages and `#line` name files from the Shaders root.
* Variant objects are per (device, hash), owned by the device's state, shared by objects of the same hash, released in `OnDeviceDestroyed` with the constant buffers and custom shader objects. A replaced object is kept two presents (binds in flight on other threads), then released. A device refusing a variant is logged once and the game's shader runs.
* `Luma::IsEnabled()` is a cached atomic set from the setting after the load, kept by a change listener (Core.cpp).
* Constants: LumaSettings is uploaded once a frame per context (frame stamp), LumaData once per context until a command list boundary; both are bound, at each draw or dispatch running a variant, to the stages running one (slots from `Game::SetConstants`, default 13 and 12; the UI cbuffer's slot is only a define, as 1.0 computes it). LumaSettings' swapchain size follows the swapchain (Constants::SetSwapchainSize at creation and resize, an atomic; render size equals it until Step 7), its display mode and white levels hold 1.0's boot values (HDR, 1000 and 203 nits) until Step 5; LumaData's render scales are 1 and the game's part is zero until part B ports FF7R's per-draw data. Dev sliders: `Luma.Development.Setting01` to `10`, named and ranged from `Includes/Core/Settings.hlsl` at boot. (Superseded by "Step 1 corrections": options, typed cbuffers and 1.0's upload flow.)
* Defines: Luma's as `Luma.Defines.*` in `[Defines]`, the game's own as plain settings with a `Define` source (FF7R: `Defines.SSAO_ALGORITHM`), `Luma.Defines.AutoCompile` (off in Development, as 1.0). `ApplySource` does nothing for `Define`: `Shaders` listens to setting changes. The settings view shows a Compile Shaders button in a category holding defines when one changed since the last build started (a flag set by a `Define` setting's change, cleared by `SetCompiled`).
* Inspection: Development tab "Shaders" (`UI/Panels/Shaders.cpp`, `UI::Panels::DrawShaders`: Reload, Unload/Load, files list with their hashes and errors, what runs replaced, compile errors), widgets `ListDetail`, `HashChip`, `Code`, a monospace font (Consolas). Status bar: the running job with a progress bar, and failed shaders. A toast per finished build. MCP: `shader_list`, `shader_state`, `dump_shader`, `reload_shaders` (waits for the build), and `status` has `log_path` (what the bridge's `luma_log` reads) and a `shaders` block. Dumps: `Luma/Dump/0x<hash>.<target>.cso`, once per hash, on the worker (`Luma.Development.DumpShaders`).
* Measured offline (Development-Release, WARP, through the real hooks): `PSSetShader` 34 ns hooked (10 ns with Luma off, 4 ns unhooked); a draw with a variant bound 77 ns (two `*SetConstantBuffers` per stage). Per bind: one `GetPrivateData` on the shader (~20 ns), the cached context lookup, two atomic loads (`Luma.Enabled`, the variant). Per draw: the cached context lookups, a frame comparison, the binds. No lock on either path.
* Offline checks (a throwaway test, not committed): the FF7R tree compiled through `Compiler` and `Cache` from an empty cache (30 units: 29 replacements, 1 custom shader, 0 failures), a second build compiled 0 and read 30 from the cache, changing `SSAO_ALGORITHM` recompiled 1 unit; the tree copied to two other folders, with the current directory changed, gave the same 29 keys, all found in the cache the builds wrote. On a WARP device through the hooks: the variant swapped in at bind, the cbuffers bound at a draw, equal bytes republished kept the same object, unload/load and `Luma.Enabled` off/on gave back the same object, a changed bytecode's old object was released after two presents, and releasing the game's device, context and shader destroyed the real device.
* FF7R: `Init()` names its hash lists (and 1.0's other forced names), redirects `Output_HDR`/`Output_SDR`, declares "Decode MVs", the define defaults, the cbuffer slots and its settings table (keys as 1.0's `settings.hpp`). Left out until their step: the draw callbacks (part B), the `is_visible`/`is_enabled` predicates on display mode and SR (Steps 5, 7), the per-frame `custom_random` and `can_sharpen` writes, the `ENABLE_FRAMEGEN` custom shaders (never enabled in 1.0), `old_shader_file_names` (packaging, Step 8).
* Noticed in 1.0, kept: FXC flags are `D3DCOMPILE_OPTIMIZATION_LEVEL0` (/O0) though its comment says /O3; reflection counts typed UAVs only (`D3D_SIT_UAV_RWTYPED`); the disassembly fallback's early stop never triggers (`matching_line` is never reset).
* D3D12: `LUMA_UNIMPLEMENTED("Shaders on D3D12 (Step 2)")` at device creation when variants or custom shaders are needed.
* Not verified: see part B's list.

**Notes (part B: draws, trace and the design corrections, commits `692a306d` to the FF7R and notes commits):**
* Design corrections from the owner's review, each a rule in `Guidelines.md`: per-API code in `<Feature>/<Api>/` (§3, §15); `Utils` stateless (DepthGuard to `Hooks`, Modules to `System`, §3); stage traits and templates instead of code-generating macros (`Hooks::D3D11::StageTraits`, §15; installed hooks unchanged, 182 declared and 174 installed with every function requested on WARP, before and after); `Game` only forwards to the owning module (§8); defines are settings, Core registers before the game's `Init` and the game uses `Settings::SetDefault` (§9); Luma's shaders found by `// Luma: <target> [name] [entry=] [DEFINE=value]` lines in their files (§10; 16 files got lines, 21 programs, all compile); `Resources::Database` holds Luma's shaders and cbuffers per device, and programs request entries that a fulfil step binds (§10, §11); one need decision per feature, no `IsNeeded` on stateless helpers (§7); handlers orchestrate features named for what they do (§5).
* `Draw`: `StateMask` (stage masks and slot ranges per group) with 1.0's three modes as presets and `FULLSCREEN_PASS` as the default; `Draw::D3D11::Save/Restore`. Bound-state cache (`Draw::Recorded`: viewports and scissors, blend, depth; all in Development, where trace reads it): views and buffers are not in it, the API unbinds them on its own (a resource bound as output leaves the input slots), which a record without references would miss. The shader group is Variants' per-context record (`BoundShaders`: game shader, variant program and callback flag per stage), no second record. Passes: `DrawFullscreen` (1.0's DrawCustomPixelShader: 4 vertex strip, saves and restores `FULLSCREEN_PASS`, binds what its shaders request), `Copy`, `Scale`; Luma off draws nothing; no device state.
* Draw callbacks: `Game::OnDraw(hashes, fn)` (one callback per hash, any stage) and `Game::OnDrawOrDispatch(fn)`, `DrawResult` None/Skip/Replaced as 1.0, `Game::D3D11::Call` (context, the native arguments, the hash per stage, the variant stages), `RunOriginal`, `BindLumaShader` (a callback's own pass on one of Luma's shaders, with its requests). `Handlers::PreDraw/PreDispatch`: trace records the state, the game's callback, the trace's skip action (Development), then the variants' requested resources. A draw without a callback costs a cached context lookup and a bit test.
* LumaData: per pass values (`Resources::Database::CustomData`) are an argument of the fulfil step, the game's part is pushed with `Game::SetLumaGameData` (1.0 asked `UpdateLumaInstanceDataCB` at each upload); a context uploads an entry again when its stamp (frame, game data generation) or the pass values changed. (Superseded by "Step 1 corrections": options, typed cbuffers and 1.0's upload flow.)
* Trace: `Trace::Capture` (Development only, compiled out with its hook requests): one frame, present to present, events per draw or dispatch (shaders with hash, replaced and variant, outcome, arguments, views with resource descs, blend per target, depth stencil, rasterizer, viewports and scissors, samplers, cbuffers of every stage with a shader, contents read back at the present for the immediate context, 64 MB per capture). Typed cbuffers (`Trace::Cbuffers::Describe(hash, slot, bytes)`, shaped for `luma_read_cbuffer`): the shader's reflection, else its replacement's, else float4 rows. UI: Trace tab (`UI/Panels/Trace.cpp`, `ListDetail` with a clipper, new `PropertyGrid`; per shader: copy hash, Dump, the variant picker (Auto, Original, each variant; Step 1 corrections), Skip Draws). MCP: `trace_capture` (trigger, frames, summary, top), `trace_list` (1.0's filters and fields 2.0 has), `trace_get` (1.0's state fields, cbuffers with `layout` and typed `fields`).
* Not in trace v1: copies, clears, binds and CPU writes as events, deferred command lists merged at execute (deferred calls are listed in recording order, marked), `upgrade_flags`, `replaced_slots`, `note`. `include_bindings` and `include_buffer_writes` are taken and include nothing.
* FF7R: `Init()` declares names, redirects, defaults over Luma's defines, its constants, settings and three callbacks: the LumaData game part (1.0's `UpdateLumaInstanceDataCB`) and `can_sharpen` set at swapchain creation and resize, `custom_random` at each present. Draw callbacks left for their step: TAA (`has_drawn_taa`, then SR: Step 7), Output HDR and SDR (the display mode switch: Step 5; the two pass UI split is `ENABLE_FRAMEGEN`, off in 1.0), DoF and Motion Blur (SR's sampler: Step 7), SSAO Temporal (`ENABLE_FAST_NOISE_TEXTURES`, off in 1.0). Their flags alone would be read by nothing before those steps.
* Measured offline (Development-Release, WARP, through the real hooks, a game with draw callbacks): a draw passed through in Luma's scope 23 ns; without a callback 30 ns, with a callback returning None 47 ns, with a variant bound requesting LumaData 60 ns (87 ns when both Luma cbuffers went to every variant stage); while a capture runs about 2.8 us per draw without cbuffer copies and 12 us with them.
* Offline checks (throwaway, not committed): callbacks None/Skip/Replaced give green/untouched/blue, none while Luma is off; `Copy` and `Scale` draw and leave targets, shader, sampler, viewport and topology as they were; a stripped replaced shader's cbuffer shows the replacement's fields, an unreplaced stripped one raw rows; `trace_capture` with a trigger, `frames=3`, `trace_list replaced_only=1`, `trace_get` answer as above.
* Not verified (Step 1, no game run): every item of the Verify list above; the goldens; FF7R's image with request and fulfil (only the Luma cbuffers a replaced shader reads are bound now; `LumaUIData` is no entry, as 1.0 never bound it for FF7R); the Trace tab and MCP tools on FF7R's frame; capture cost on a real frame; Luma's 21 shaders compiled in a Publishing layout (`Luma/Shaders`) rather than the repository roots.

### Step 1 corrections

Decided with the owner after reviewing Step 1, applied as one unit before Step 2 builds on the API.

1. **Settings: typed ids, one registry, calls per responsibility.** `Settings::Id<T>` (typed `Get`, `Set`, `Queue`, `SetDefault`), `Settings/Keys.hpp` with every id Core owns, `Shaders::Defines::Id<T>` for Luma's defines. `Game::Shaders::SetDefine`, `Game::Shaders::AddDefine`, `Settings::Register` for plain settings. A `Kind` per setting (Game, Luma, Define, Recipe, Development) that the settings view and `Luma.toml` group by.
2. **`Game::Options`**: `SetOptions` once in `Init` for what is fixed for the process (`.cbuffers`, `.draw`, `.device_data`). `SetConstants`, `Constants`, `SetLumaGameData`, `RecordBoundState`, `SetDeviceData` go.
3. **`Resources::LumaCBuffers`** (was `LumaConstants`): both cbuffers requested by every compiled Luma shader at the game's slots; the Development debug slot check; `restore_after_draw` (1.0's `ENABLE_AUTO_CBUFFER_RESTORATION`); Core-owned database entries read-only; shader file overrides logged.
4. **Luma's cbuffers follow 1.0's flow.** LumaSettings is uploaded by a context when dirty: its stamp moves at every present (and after the game's present callback and the overlay), at a resolution change and with `Game::MarkLumaSettingsDirty`, which the game calls after changing its settings object within a frame. LumaData is written at every bind (Luma's part, pass values, render scales, the game's writer pulled with its `GameDeviceData` and the device's resolutions) and uploaded when its bytes differ from that context's last upload. (A version with a LumaData dirty flag, `Game::MarkLumaDataDirty`, was tried and reverted.)
5. **`Luma::Frame`**: per-device output, render and previous render resolutions; `Game::Frame::Get`, `SetRenderResolution`. FF7R's `DeviceData` is `GameDeviceData` with only its own fields.
6. **Variants: one model** (source and priority per hash), `Events::on_variant_draw`, `Game::D3D11::UseOriginal`, a variant picker in trace and MCP.
7. **Names**: `Game::Shaders::SetDebugName`, `RedirectHashes`; `Shaders::CompiledShader`, `MakeCompiledShader`, `Files::Declaration`; `Game::GetGameDeviceData<T>`; `Core.*` is `Startup.*`, `Game/Game.*` is `Game/Registry.*`, `Luma::DllMain`.
8. **Overlay**: one templated `Draw` over a `Renderer<T>` traits struct.
9. **FF7R**: no `IgnoreWarnings`; the type shim for the cbuffer structs in `LumaCBuffers.hpp`, not in the game's `GameCBuffers.hlsl`; `Init` on the new API.
10. **Core compiled per game, typed cbuffers** (owner's decision, after the notes below: "2.0 is not to completely change everything 1.0 did but to better organize the project and consolidate repeated code into Core"). Core is compiled into each game like 1.0's `core.hpp`, keeping its modules and `Luma.hpp` as the one header. With the game's `Game/GameCBuffers.hlsl` visible, LumaSettings and LumaData are single types shared with HLSL (1.0's `CB::LumaGlobalSettings` with `GameSettings`, `CB::LumaInstanceData` with `GameData`); Core owns `cb_luma_global_settings`, the game's LumaData writer is typed (1.0's `UpdateLumaInstanceDataCB`). The type erasure that existed for a prebuilt Core goes. LumaSettings' contents are published at the points its stamp moves and uploads copy the published ones.
11. **Overlay after the owner's FF7R test** (D3D11, 3840x2160, FP16 swapchain in HDR): pinned header and footer, opaque by default with an opacity setting, an About section, the overlay encoded for HDR swapchains, settings order (Luma and Overlay first), Auto Compile with the defines and one pending-compile banner, Development settings in the settings view, and a trace with filters matching any stage of a call, segmented variant picker, grouped detail tables, per-stage cbuffer slots and Track.

**Notes (corrections done, commits `004dd792` to the docs commit):**
* Settings: `Setting::kind` is set by `Settings::Register(settings, kind)` (default `Kind::Game`); Core registers its tables with `Kind::Luma`, `Define` or `Development`. Typed `SetDefault`/`Set`/`Queue` take `template <T, U> requires std::same_as<T, U>`: `SetDefault(Id<int32_t>, true)` and `Set(Id<float>, 1.0)` don't compile (checked by `static_assert` in `SettingsTests.cpp`).
* Keys: `Luma.Enabled`, `Luma.EnabledKey`, `Luma.Overlay.Key`, `Luma.Overlay.FontSize`, `Luma.Overlay.Opacity` and `Luma.Overlay.HdrPaperWhite` (added with item 11), `Luma.Shaders.AutoCompile` (Auto Compile is a Luma setting in a "Shaders" category, `[Luma.Shaders]`; it was `Luma.Defines.AutoCompile`), `Luma.Development.DumpShaders` and `Luma.Development.Setting01` to `10` (kind Development). Defines are `Defines.<NAME>` for Luma's and the game's. Step 1's `Luma.Defines.*` values and the earlier `[Overlay]`/`[Luma]` layout reset once; unknown keys are kept as before.
* `Shaders::Defines::Id<T>` derives from `Settings::Id<T>` and adds `name`; it lives in `Settings/Keys.hpp` with Core's other ids (the plan's single file for keys), so that file holds two namespaces. Luma's define ids for `TEST_2X_ZOOM` exist in every build, the setting only in Development (a `SetDefine` on it elsewhere returns false and logs a warning once per name, through `Shaders::Defines::SetDefault`).
* `AddDefine` takes `Shaders::Defines::Definition` (`name`, `label` (the name when empty), `tooltip`, `default_value`, `range`, `visibility`), whose strings it copies; category "Shader Defines". A define name registered twice is refused by `Settings::Register` whatever the key, so a game's plain setting with a `Define` source can't shadow one either.
* UI: settings view is the game's categories, Luma's, one "Shader Defines" section (with the Compile Shaders button), then "Recipes"; the Development tab now always exists in Development builds and shows the Development settings before the game's development panel. MCP `get_settings` entries have a `kind`.
* `Game::Options` member types: `Resources::LumaCBuffers::Options`, `Draw::Options`, `Game::GameDeviceDataType` (`GameDeviceDataType::Of<T>()`; the plan's sketch wrote `GameDeviceData::Type<GameDeviceData>()`, a Core type can't be named after the game's struct). `LumaCBuffers::GameSettings` and `GameData` capture address and size with template constructors; `GameData` stores the writer as an erased function pointer and calls it on a value-initialized `T` that is then copied into the buffer (the game always writes into a zeroed struct). (Superseded by item 10: the structs are typed, nothing is captured.) A second `SetOptions` asserts, logs and is ignored; `Game::Registry::Reset` clears that and the LumaCBuffers and Draw options for tests.
* LumaCBuffers: entries are registered after `Init` (sizes come from the options); `GetRequests()` is what `MakeCompiledShader` starts every request list with, reflection adding other entries once each. `Database::Entry::write` takes a `Database::WriteTarget` (the device, its `Frame::DeviceState`, its `GameDeviceData`), which a context finds once with its store (`ID3D11DeviceContext::GetDevice`, `Frame::Find`, `Game::GetDeviceDataPointer`): a write looks nothing up, only `Frame::Read` takes the resolutions' small lock. An entry with `get_stamp` (LumaSettings: `LumaCBuffers::GetSettingsStamp()`, the frame plus `Frame::GetGeneration()` plus the generation `MarkSettingsDirty`/`Game::MarkLumaSettingsDirty` bump, which Core also bumps at the end of every present) is uploaded when `Database::NeedsUpload(upload, stamp)`; one without (LumaData) is written into the context's scratch at every bind and uploaded when `Database::ReplaceIfChanged(upload, written)` (the bytes differ from the last upload, 1.0's memcmp). Both are shared with D3D12. (Two earlier versions, a whole-buffer compare with lookups per draw and a LumaData dirty flag, were revised into this.) (Superseded by "Step 1 corrections": options, typed cbuffers and 1.0's upload flow.)
* The Development debug slot check is `DEVELOPMENT && _DEBUG` (Development-Debug only), skipped with `restore_after_draw`, and applies to every database cbuffer the fulfil step binds.
* Restoration: `Resources/D3D11/D3D11LumaCBuffers.*` records, in the context's private data, the game's buffer (held) and D3D11.1 range at each of the two slots per stage when `*SetConstantBuffers(1)` is called outside Luma's scope, after the game's own callback; `ClearState` and a command list boundary without restore forget them. After a draw or dispatch whose fulfil step bound something, or whose callback bound Luma's shaders with `BindLumaShader` (the callbacks report those stages through a thread-local of the running call), the handler issues the call itself (if a callback didn't run or skip it) and binds the game's buffers back on those stages. Decided: also after a call a callback skipped or replaced (1.0 restored only when it issued the call), so Luma's buffers never stay in a game slot.
* Handler order (as Step 1 and 1.0): the game's draw callbacks first (`on_variant_draw`, then by hash or generic), then the variants' requests, fulfilled only after a None result the trace didn't skip. A callback that draws with Luma's shaders binds them with `BindLumaShader`, which binds their requests and, like 1.0's `updated_cbuffers`, stops Core fulfilling any for that call. (The first version of these corrections fulfilled before the callbacks; reverted.) `FULLSCREEN_PASS` saves the vertex stage's cbuffers too, now that Luma's vertex shaders get Luma's cbuffers.
* Frame: `Frame::Resolutions` (output, render, previous render as `Math::uint2`, under one small mutex per device so `Get` returns the three as set together), a state in each device's private data (`Hooks::Lifetime::SetStateOf`/`GetStateOf`, new: private data on an object known as `IUnknown`), created with the device at the screen's size (1.0's guess). `SetOutputResolution` (Core, at swapchain creation and resize, before the game's callbacks) sets render and previous render to the output too; `SetRenderResolution` moves render to previous. Readers unwrap the game's proxy. `render_scale_active` stays 0 until Step 7.
* Resource database owners: `Entry::owner` (`Core` by default). A game's entry with a Core name is refused; one registered before Core's is replaced by Core's at the same index; both assert once and log. There is no game call to register entries yet.
* Shader file overrides: `Files::Scan` lists the paths a later root lost to an earlier one; `Shaders::Start` logs them once in Development.
* Variants: `Variants::Variant` (hash, source, priority, file, compiled, messages, failed); `Publish(source, variants)` replaces that source's, publishers serialized by a writer mutex (Step 3 publishes patches off the presenting thread); `FILE_PRIORITY` = 100 (Step 3 picks the patches' relative to it). Each D3D11 device keeps a `Published` per source and hash and an atomic priority order (4 bits per source) read at bind. `BoundShaders` adds the game's original object (held while a variant runs, for `UseOriginal`) and the source. The forced choice is `Database::Shader::forced_variant` (`Variants::Choice`: Auto, Original, File, Patch), Development only, read at the next bind. `UseOriginal` holds until the game binds another shader on that stage (1.0's behaviour); `Call::variant_stages` is refreshed for the callback by hash after `on_variant_draw`.
* Trace: `Capture::Shader` has `variants` (the compiled ones, by priority), `variant` (the one that ran) and `forced`; MCP `trace_list`/`trace_get` keep `replaced` and `variant` (now the source names), `trace_get` adds `variants` and `forced`. MCP `shader_state hash=... variant=auto|original|file|patch` sets the choice and replies with `variants` and `forced`; `Scripts/luma_mcp.py` gained the `variant` argument.
* Overlay renderers: D3D11 `NeedsRecreation` is always false; both `NewFrame` take the swapchain desc.
* FF7R: `WriteLumaGameData(CB::LumaGameData&, const GameDeviceData&, const Frame::Resolutions&)` (1.0's `UpdateLumaInstanceDataCB`, which got the device data); its swapchain callbacks set `upscaled_render_resolution` from the device's output resolution (1.0's `OnInitSwapchain`), which LumaData's compare picks up. `can_sharpen` (a LumaSettings field, which 1.0 set in `UpdateLumaInstanceDataCB`) is computed in the same swapchain callbacks, where its inputs change, followed by `Game::MarkLumaSettingsDirty` (the resize moved the stamp before the callback). `SSAO_ALGORITHM` is an int 0 to 1 defaulting to 1.
* Doctests added: typed ids (and the compile-time type checks), kinds, `AddDefine` generation and duplicate refusal, a TOML table per kind, Core-owned database entries, the cbuffer option captures, variants by source and priority; the upload decisions (`NeedsUpload` by stamp, `ReplaceIfChanged` by bytes), the game writer's device data and resolutions and its type tag, LumaSettings' stamp moving with `MarkLumaSettingsDirty` and with a device's resolutions (`Frame::Find`/`Read` on a WARP device), a second `SetOptions` ignored and `Reset` clearing the options, `AddDefine` copying its strings, `SetDefault` refusing a define the build doesn't have. `ParseDevSettings`' test is unchanged.
* Not verified: no offline WARP run through the real hooks was done for this unit (Verification step 4: both Luma cbuffers on every variant stage, LumaData re-upload only when its bytes change, `restore_after_draw` after fulfil and after `BindLumaShader`, the callbacks-then-fulfil order, `on_variant_draw` and `UseOriginal`, the trace picker, a game registering `LumaSettings` at runtime); nothing was run in FF7R (step 5: `luma_status`, `Defines.*` in `luma_get_settings`, recompile on a define change, LumaSettings/LumaData with the swapchain size on the tonemap draw, `variant=original`, the overlay on D3D11 and D3D12, the new `Luma.toml` tables). Step 1's own unverified list stands.

**Notes (item 10 done):**
* Build: `Source/Core/Core.vcxitems` (shared items, in the solution in place of `Core.vcxproj`) is imported by each game `.vcxproj` and `Tests.vcxproj` (`<ImportGroup Label="Shared">`). Core's `.cpp`/`.c` are found by folder (`**`), their objects in `<IntDir>Core\<folder>`; third party code (SafetyHook, Zydis, ImGui) and `UI/Fonts.rc` come with it. The dll exports file is `System/<LumaDllName>/<LumaDllName>Exports.cpp`, the others excluded, so `ForceSymbolReferences` and `LumaDllExports_dxgi` are gone. Include paths: Core's folder, then the project's own (`Luma2.Game.props` adds `Resources/<Game>/Shaders`, Tests its own folder with a stand-in `Game/GameCBuffers.hlsl`), then `Resources/Global/Shaders`. No precompiled header: a solution rebuild (Development-Release) took about as long as with the static library (85 s against 81 s), the FF7R project alone about 65 s (Core with it).
* Cbuffer structs: `Includes/Core/CBuffers.hlsl` (HLSL and C++) declares `CB::LumaGlobalSettings` and `CB::LumaInstanceData` (with `float3 Padding1` before `GameData`, where HLSL put it anyway) and the empty game structs when a game has none; `Settings.hlsl` and `Common.hlsl` declare `cbuffer LumaSettings { CB::LumaGlobalSettings LumaSettings; }` and `LumaData` with them, so shaders read the same names. `Resources/LumaCBuffers.hpp` includes `Game/GameCBuffers.hlsl` (`__has_include`) and `CBuffers.hlsl` and `static_assert`s the 16-byte rule; `SETTINGS_SIZE`/`DATA_SIZE` are the padded sizes. The dev sliders bind to `DevSetting01..10` through member pointers and are named from `CBuffers.hlsl`.
* Game API: `Game::cb_luma_global_settings` (Core's, `Resources::LumaCBuffers`); `Options::cbuffers.update_luma_instance_data` (`void(CB::LumaInstanceData&, const Game::GameDeviceData&, const Frame::Resolutions&)`, called with Luma's part filled and the game's zeroed, only for devices with device data); `Luma::Game::GameDeviceData` is declared by Core and defined by the game, `GameDeviceDataType::Of<GameDeviceData>()` keeps create and destroy (compiled where the definition is), `Game::GetGameDeviceData(device)` is typed. Removed: `LumaCBuffers::GameSettings`, `GameData`, `DEVICE_DATA_TYPE`, the writer's type check in `SetOptions`, `GetDeviceDataPointer`.
* LumaSettings synchronization: `cb_luma_global_settings` is written on the presenting thread only (Init, the present and swapchain callbacks, the registry's bindings, the overlay). `MarkSettingsDirty` (end of every present, `Game::MarkLumaSettingsDirty`, after the settings load at startup) copies it into a published copy under a mutex and then bumps the generation; `WriteSettings` copies the published one under that mutex. The stamp is read before the copy, so an upload's contents are never older than the stamp it records (at worst a context uploads once more). A resolution change moves the stamp without a publication: sizes are written per upload from the device. A game writing it outside the present callbacks calls `MarkLumaSettingsDirty` after, on that thread.
* FF7R: `struct Luma::Game::GameDeviceData`, `cb_luma_global_settings.GameSettings.*` for its settings bindings, `custom_random` and `can_sharpen` (as 1.0), `UpdateLumaInstanceDataCB(CB::LumaInstanceData&, ...)` writing `data.GameData.*`.
* Tests: the stand-in's `CB::LumaGameSettings`/`LumaGameData`; a test compiles `CBuffers.hlsl` with FXC and compares every member's offset and both cbuffer sizes with C++ (it fails on a member HLSL moves to the next register, checked with a `float3` straddling one); `WriteSettings` uploads the published contents, not later writes; `WriteData` with the typed writer and without device data. 64 cases.
* Shaders: FF7R's replacements and Luma's own programs compile with FXC against the new includes (checked offline, outside the game). Not run in the game.

**Notes (item 11 done):**
* Layout: the window doesn't scroll; the header and the Development tab bar are above a "Content" child, the links and the status bar under it (`GetFooterHeight` lays the links out as `WrapSameLine` does). `Luma.Overlay.Opacity` (percent, 20 to 100, default 100, live) scales the window background's alpha, `Style::Colors::WINDOW` is opaque. `Game::Info::description` is new (FF7R has 1.0's About text); the About section has it, the game's about panel, the credits, "Luma uses", the version and the build.
* HDR overlay: 1.0's own overlay (`Source/Core/overlay`) did nothing for HDR; under ReShade, ReShade's overlay encoded ImGui's vertex colors (`pow(c, 2.2)`, `* brightness / 80` for scRGB; BT.2020 and PQ at `brightness / 10000` for HDR10; brightness 203 nits). `UI/Encoding` does the same on the product of texture and vertex color (the same for the white font atlas, and right for images), in a pixel shader bound by a draw callback put first in each draw list: D3D11 binds its pixel shader (the backend restores the game's), D3D12 binds its own root signature (ImGui's two parameters in their places, plus root constants at b1) and pipeline, and sets the projection again, the backend binding the texture at each command after. The shader is compiled once; the encoding kind and paper white (`Luma.Overlay.HdrPaperWhite`, 203 nits, live) are its constants (`Encoding::Constants` at b1: a cbuffer on D3D11, which Luma restores with its D3D11.1 range since the backend doesn't, root constants on D3D12), so dragging the slider compiles nothing. The color space comes from a new `IDXGISwapChain3::SetColorSpace1` hook (`Handlers::PostSetColorSpace`, kept in the swapchain's private data under Special K's GUID with the format it was set for, forgotten by a resize to another format, logged when it changes): `G2084` (full or studio range) is PQ in any format, FP16 with `G10` or none is scRGB, the rest unchanged. `Luma.Overlay.Opacity` and `Luma.Overlay.HdrPaperWhite` are the two new keys. HLG isn't handled (no game uses it). Not verified in the game: the D3D12 path, and the look on FF7R's swapchain.
* Settings: `UI::CollectSettingsSections()` (tested) gives the order; `Shaders::Defines` keeps the defines the last build started with (`SetCompiled(defines)`), `IsPending(setting)` compares a define's value with them (a change back is not pending), `NeedsCompile()` is any pending. The old "recompile" badge was drawn on every define row whatever its state: it said what a change does, not that one was waiting. Now a define the shaders weren't built with has a "pending" tag (its tooltip gives the built value), and the Shader Defines section has one warning banner with the Compile Shaders button. Enum settings with five or fewer options are segmented.
* Trace: `Capture::GetPrimaryShader` (PS, else VS, else CS) and `Capture::Filter`/`Matches` are shared by the list and MCP `trace_list` (`stage` and `hash` match any stage, `include_vs` also lists the draws whose primary shader is the vertex one, as the UI's "Vertex-only draws", stage names any case and trimmed, new `filter` text, entries gain `stages`). Why some VS appeared alone: an event was always one draw with all its stages, but the list label and filter only had the "main" shader (PS or CS, else the first), so a depth only draw showed as its VS and a VS bundled with a PS was neither shown nor matched by the text filter. The UI filters: text (part of a hash or name, any stage), Vertex-only draws, Luma passes, Replaced, Unique (1.0's "List Unique Shaders Only"), Threads (1.0's "Show Command List / Thread Info"). 1.0's Ignore Bindings and Ignore Buffer Writes have nothing to filter: 2.0 records no such entries yet.
* Track (`Trace/Track`, D3D11 in `D3D11Capture`): the target is a hash, stage, slot and the call's instance: the n-th call of the shader to reach the draw or dispatch hook in the frame, across contexts in hook order, which is the captured list's order (Luma's passes not counted; recording order on deferred contexts). The draw handler copies the slot into a staging buffer at that call (before the draw callbacks, as the capture), on any context. A deferred copy is tied to the command list `FinishCommandList` ends it in and becomes readable when `ExecuteCommandList` runs that list on the immediate context (a list executed into another deferred context moves to that one); up to 8 copies wait. The present maps the newest readable one and `Track::SetReading` keeps per 4 bytes the frame it last changed; a frame without one marks the last reading stale, shown as such. Shown in place of the captured contents with the changes of the last 60 frames highlighted, and as a banner with Stop at the top of the Trace tab. Not verified in the game.
* Tests: settings sections order, Auto Compile alone, pending defines, the overlay's encoding by format and color space (and both shaders compile), variant choices, the primary shader, the filters, Track's change marks.

### Jobs: one worker pool

Before Step 2, which queues pipeline creation on it. `Guidelines.md` section 9 ("Background work is one pool") has the rules.

* `Shaders::Jobs` (one detached thread, one job at a time) becomes `System::Jobs` on a private Win32 thread pool (`CreateThreadpool`, `SetThreadpoolThreadMaximum` from the new `Luma.Advanced.WorkerThreads` setting, default `clamp(cores / 4, 1, 4)`). `Queue(name, work, done, priority)` with `Priority::{Normal, Low}` (dumps are Low), `SetProgress` from inside `work` for that job, `RunFinished()` at present, `GetStatus()` (the oldest running job's name and progress, how many run and wait, the thread count) and `GetLastReport()`.
* Workers run at below-normal priority, set at the start of each callback. If the pool can't be created the job runs on a plain `std::thread`, and the first use logs which of the two it got (a Wine/Proton check).
* FXC is used by one thread at a time, as 1.0 says it should ("only ever access it from one thread at any given time"): every `Shaders::Compiler` call (`Preprocess`, `Compile`, `Reflect`, `Disassemble`) takes the recursive `Compiler::Lock` for its duration, and the code that uses a reflector holds a `Lock` across that use (`ReflectSlots`, `ReflectConstantBufferBindings`, `ReflectConstantBuffers`). The shader build is still one job; nothing compiles in parallel. The game's shader creation (`D3D11Database`) reflects, so it waits for the compile in flight (one file), not for the build: 1.0 held its creation lock for the whole boot build, which the "Boot compilation" row of section 17 still doesn't.
* Tests: the pool's logic only (priority order, the thread cap, `done` only inside `RunFinished`, per-job progress).

**Verify (once):** a cold-cache boot and a reload on FF7R with 1 and with 4 workers (overlay status bar, `luma_status`), a dump running during a reload, the same on Proton.

**Notes (done, not verified in the game):**
* `System/Jobs.{hpp,cpp}` over `TrySubmitThreadpoolCallback` and a private pool with one callback environment per priority; a job's `Run` is the callback. `Shaders/Jobs.*` and `Shaders::PublishFinishedBuilds` are gone: `Handlers::PrePresent` calls `System::Jobs::RunFinished()`.
* `Luma.Advanced.WorkerThreads` (`Settings::Core::WORKER_THREADS`, 1 to 16) is registered in `Startup.cpp`, its default set at startup from the CPU's thread count, applied at load and by a change listener.
* `luma_status` gains `jobs: {threads, running, queued}`; `shaders.job` is the oldest running job's name. The status bar shows that job's progress, `luma_reload_shaders` waits for no job running or queued.
* Dumps are `Priority::Low`. Not done, as decided: cancelling a stale build, parallel compile.
* 5 doctest cases (`JobsTests.cpp`), 76 pass. Development-Release and Publishing-Release of FF7R build. Not checked: the pool on Wine/Proton (the first job logs "Jobs: Win32 thread pool, up to N threads", or the fallback warning), FF7R in the game.
* Decided: the build stays serial. A standalone stress test (not committed) found FXC safe in practice (no failure or difference over concurrent compiles, preprocesses, and compiles beside reflect and disassemble, at /O0 and /O3), but FXC is widely documented as not thread safe, so nothing compiles in parallel.
* Tests: `Compiler::Lock` keeps another thread's FXC call waiting, and the thread holding it can still call in (77 doctest cases pass).

---

## Step 2: D3D12 foundation and shaders on D3D12

**Goal:** the same FF7R shaders replaced on D3D12, and the same trace.

**Read first:** `Guidelines.md` sections 4, 5 and 10 (`Design.md` no longer exists: section 4 is the D3D12 binding model). Step 1's result: `Shaders/Database`, `Shaders/Variants`, `Shaders/D3D11/*`, `Resources/ResourceDatabase` and `Resources/D3D11/*`, `Handlers/D3D11/D3D11Handlers.cpp`, `Game/D3D11/D3D11DrawEvents.cpp`, `Trace/D3D11/D3D11Capture.cpp`, `Hooks/D3D12/*`. 1.0 has no D3D12: the D3D11 result is the reference for behaviour and image.

**Shape.** Three parts, each committed and verified on its own. Part A is the research build: it answers the questions the rest depends on, in the game, before anything is designed around a guess.

### 2A: Pipelines tracked, research

**Build:**
* `Hooks::D3D12::Function` and `Functions` (the bitset of `Hooks/Functions.hpp`), `GetD3D12Functions()` per feature, `Startup::GetOwnedFunctions` for D3D12, installed exactly as D3D11's. No D3D12 native game callbacks in this step: everything Core hooks gets events only (`Guidelines.md` section 8).
* Slots and detours (`Hooks/Slots.h`, `Hooks/D3D12/D3D12DeviceHooks.cpp`, `D3D12CommandListHooks.cpp`):
  * device: `CreateGraphicsPipelineState`, `CreateComputePipelineState`, `CreatePipelineState` (Device2, stream desc), `CreateRootSignature`, `CreatePipelineLibrary` and the library's `LoadGraphicsPipeline`/`LoadComputePipeline`/`LoadPipeline` (a game's library hands out PSOs too), `CreateDescriptorHeap`, `Create{ShaderResource,RenderTarget,DepthStencil,UnorderedAccess,ConstantBuffer}View`, `CreateSampler`, `CopyDescriptors`, `CopyDescriptorsSimple`, `CreateCommittedResource*`, `CreatePlacedResource*`;
  * command list: `Reset`, `Close`, `ClearState`, `SetPipelineState`, `Set{Graphics,Compute}RootSignature`, `SetDescriptorHeaps`, `Set{Graphics,Compute}Root{DescriptorTable,32BitConstant,32BitConstants,ConstantBufferView,ShaderResourceView,UnorderedAccessView}`, `OMSetRenderTargets`, `RSSetViewports`, `RSSetScissorRects`, `IASetPrimitiveTopology`, `OMSetBlendFactor`, `OMSetStencilRef`, `DrawInstanced`, `DrawIndexedInstanced`, `Dispatch`, `ExecuteIndirect`, `ExecuteBundle`.
  * One detour template per kind of method (graphics/compute pairs share one through a traits type, as `StageTraits`).
* `Shaders::Database::D3D12` (`Shaders/D3D12/D3D12Database.cpp`): `TrackPipeline` hashes each stage's bytecode (CRC32, the same `Database::Shader` by hash as D3D11, so names, dumps, reflection and the Shaders tab apply unchanged; `objects` counts pipelines) and attaches a `Pipeline` to the PSO's private data: the per-stage `Shader*`, the root signature, a deep copy of the desc (graphics, compute or stream; the stream copied as bytes with its shader subobjects found by type), and in Development its blend, depth, rasterizer and target formats for trace. `TrackRootSignature` deserializes the blob (`D3D12CreateVersionedRootSignatureDeserializer`) into a `RootLayout` (parameters, their visibility, ranges, registers and spaces) in the root signature's private data. A pipeline whose root signature comes from the bytecode (no `pRootSignature`) reads it from there.
* Dumps on D3D12 from the desc's bytecode, through `Database::QueueDumps` as D3D11.
* Research counters, Development only, in `luma_status` (`d3d12` block) and logged once where it says: PSOs by kind and source (device, stream, game library), shaders by model, root signatures (each layout logged once), descriptor writes and copies per frame by heap kind (shader visible or not), `ExecuteIndirect` and `ExecuteBundle` calls, and the order of `SetGraphicsRootSignature`, `SetPipelineState` and `OMSetRenderTargets` before each draw (counts per order).

**Research (answers into Notes):**
* Hashes: FF7R's D3D12 shaders against its D3D11 dumps. Unreal compiles one `PCD3D_SM5` format for both RHIs, so identical DXBC is expected. If not, what differs and whether a normalised hash makes them match.
* Shader models (expected SM5.0 DXBC). **Whether D3D12 accepts a PSO mixing SM5.0 and SM5.1 DXBC**: checked first, offline, with a throwaway program on the real GPU and on WARP (a 5.0 vertex shader with a 5.1 pixel shader reading a cbuffer in space 100, graphics and compute, debug layer on). It picks the register model of 2B.
* Root signatures: how many, how they're shaped (Unreal's quantized bound shader state: root CBVs per stage, tables for SRVs, UAVs and samplers), versions (1.0, 1.1), whether any declares b12 or b13 in space 0, whether any comes embedded in bytecode, and their sizes in DWORDs (64 at most: Luma's extension adds 4, or 1 as a table).
* Descriptor heaps: count, types, sizes against the device's limit (the tail of 2B must fit), whether the game reads `GetDesc` back. Descriptors copied into the shader-visible heap per draw (expected), or written once into persistent tables (decides whether Step 4 needs shadow heaps).
* Bind order (root signature, PSO, targets) and whether it's stable; `ExecuteIndirect`, bundles, whether the game uses a pipeline library of its own.

**Verify (once):** FF7R on D3D12 runs with the hooks installed, dumps its shaders, `luma_status` reports the counters; the research questions have answers in Notes.

**Notes (2A research, not committed yet):**
* Built: `Hooks::D3D12::Function` requests (`Shaders::GetD3D12Functions`, `Startup` installs Core's set), `Hooks/D3D12/D3D12DeviceHooks.cpp` and `D3D12CommandListHooks.cpp`, `Shaders::Database::D3D12` (`TrackPipeline`, `TrackRootSignature`, `Describe` for graphics, compute and stream descs), the shared `Database::Track` (hash and reflection moved out of `D3D11Database`) and `HasChunk`, `Trace::Stats::D3D12` (Development, `Luma/Dump/D3D12Stats.json` every 5 s). Not built from the 2A list: the deep copy of the desc (with 2B's variants), the counters in `luma_status` (the report file answered the questions).
* SM5.0 and SM5.1 in one PSO: accepted on an RTX 4080, an AMD iGPU and WARP with the debug layer and GPU-based validation (5.0 VS + 5.1 PS reading `b13, space100`, the reverse, a 5.1 CS), values read back correct. 2B uses the Luma space model.
* One FF7R D3D12 session (4977 frames, the scene of the last D3D11 session):
  * **Hashes are identical to D3D11's.** 109 shaders on D3D12, 105 of them already dumped under the same hash by the D3D11 session; the 10 replaced hashes seen on D3D12 are the 10 the D3D11 session saw. The replacement files apply as they are. The other 4: two SM5.0 shaders not seen on D3D11 in that session, and two DXIL shaders on an empty version 1.2 root signature (8 pipelines), not the game's (UE4 here is SM5.0 only); most likely an overlay's, left to be identified.
  * Shaders: all the game's are SM5.0 DXBC (ps 57, vs 31, cs 18, gs 1). No root signature embedded in bytecode.
  * Pipelines: 88, of which 79 through `ID3D12Device2::CreatePipelineState` (stream descs, no unknown subobject, no mesh shaders), 9 graphics or compute descs; 22 with a replaced shader. No game pipeline library: the one-time recompile question doesn't arise for FF7R.
  * Root signatures: 30, Unreal's quantized layout (per stage tables `t0-31`, `s0-15`, `u0-7` and one root CBV per cbuffer used, `b0` up), all version 1.1 but one 1.0 and the 1.2 one above. Every register in space 0, **none uses b12 or b13**, largest 27 DWORDs (Luma's 4 fit everywhere). The 7 signatures of the 22 replaced pipelines have 2 to 15 pipelines each.
  * Descriptors: views are created in CPU heaps (about 8 a frame, 1370 at most) and copied into the shader-visible heap per draw (182 `CopyDescriptors*` calls, 394 descriptors a frame); views are practically never written into a shader-visible heap. Swapping at descriptor write (copies included) is enough for Step 4: no shadow heaps.
  * Heaps: one shader-visible CBV/SRV/UAV heap of 500000 (and one of 2048), seven shader-visible sampler heaps of 2048. Resource binding tier 3. A tail fits.
  * Bind order before draws varies: nothing changed (188k), `T` (142k), `PR` (160k), `PRT` (141k), `PT` (128k), `P` (38k); dispatches `PR` or `P`. Unreal sets the PSO before the root signature. Anything depending on two binds is decided at draw, as section 4 says.
  * `ExecuteIndirect`: about 5 a frame. No bundles. 9 lists executed a frame (45 at most), recorded on 1 thread on average (5 at most).
* Logs: 5 devices created and released at startup (adapter checks) before the real one. The `LUMA_UNIMPLEMENTED` asserts at device creation and at `ExecuteIndirect` fired once each, as intended.

### 2B: Root signatures, heaps, variant pipelines and Luma's cbuffers

**Build:**
* **Root signatures are extended at creation** (`Shaders/D3D12/D3D12RootSignatures.cpp`, owner's decision). `CreateRootSignature` deserializes the game's blob, appends Luma's parameters, serializes it at the same version and returns that signature in place of the game's. The game's parameter indices don't move and it has no way to read a signature back, so its PSOs, its arguments and Luma's variants all use the same signature: a variant is swapped in at `SetPipelineState` with nothing to do at draw, and no root arguments are replayed or restored.
  * Luma's parameters: the two root CBVs (LumaSettings, LumaData), visibility all, at the registers of the register model below. Root CBVs take a GPU address, so they need no descriptor heap. A descriptor table of Luma's SRVs and UAVs comes with its first user (Step 3 recipe bindings, Step 4 textures), together with the heap tail.
  * Over 64 DWORDs with the CBVs: the signature stays the game's, its pipelines get no variants, `ASSERT_ONCE` and a log line with its size. (With the heap tail, the two CBVs can later become one table, 1 DWORD.)
  * A signature embedded in bytecode (no `pRootSignature`) is extended at the PSO's creation, the desc given the extended one. Local root signatures (DXR) are left alone.
  * Only while `Shaders::IsNeeded()`, which is always true in Development, so a replacement file added while testing finds its signatures extended. A deviation from the "game objects are never modified" rule, as a section 17 row: the signature is the one object the game can't observe.
  * A game's pipeline library stored from a boot without Luma (or with other signatures) fails its loads once, and the game creates those PSOs again: accepted. If that turns out costly, the alternative is to return the game's own signature and bind an extended clone in its place at `Set*RootSignature`, with every PSO created against the clone (the PSO desc then still differs from what the library stored, so only a game that keys its library by something other than the desc gains).
* **Register model**, picked by 2A's check:
  * *Mixing accepted*: on D3D12, replacement files and Luma's own shaders compile as SM5.1 (`ps_5_0` → `ps_5_1`, same file) with Luma's cbuffers in their own register space, `Options::bindings.d3d12_space` (default 100, set in `Init`; moved out of the cbuffer options after 2C: the space is for all of Luma's bindings), passed to the compiler as `LUMA_REGISTER_SPACE` and used by the cbuffer declarations in `Settings.hlsl` and `Common.hlsl` through a register macro. Game registers stay in space 0. Never clashes with a game register, whatever the game's shaders declare. D3D11 keeps SM5.0 and space 0, so the two APIs compile separate bytecode (separate cache keys, the target is in the key).
  * *Mixing refused*: as D3D11, b12 and b13 (the game's slots) in space 0, SM5.0. A game signature already declaring one of them is a conflict, handled as 1.0 handles a game shader reading Luma's slot: `ASSERT_ONCE` in Development and a log line, then Luma takes the slot. A root CBV there is reused (Luma's address set at a variant's draw and the game's recorded value put back after, the only replay there is); a table range covering it can't be overridden, so that signature's variants run without Luma's cbuffers until the game's options move the slot.
  * Both are built, the second only where the first can't be used (2A's answer, then per signature).
* **Descriptor heap tail: designed, deferred** (owner's decision, with its first user in Step 3 or 4, since Luma's cbuffers are root CBVs). It is a prerequisite of the D3D12 passes (Step 5): only one CBV/SRV/UAV heap is bound at a time, the game's, so a table of Luma's views in its space must point into it. `CreateDescriptorHeap` of a shader-visible CBV/SRV/UAV heap adds a tail of Luma descriptors, and the heap's `GetDesc` reports the size the game asked for. Luma allocates its descriptors from the tail, so a table of Luma's is valid with the game's heap bound. A heap already at the device's limit gets no tail: Luma finds unused ranges (holes) by tracking the game's writes. 2A still measures the heaps against the limit.
* `Shaders::Variants::D3D12` (`Shaders/D3D12/D3D12Variants.cpp`). A variant PSO is the game's desc with each replaced stage's bytecode swapped, per stage the variant D3D11 would bind (the published one of highest priority, or the trace's forced choice), and the same (extended) root signature. Its key is the set of compiled shaders it was built from: a PSO is recreated only when that set changes (Step 1's rule for objects).
  * Created on `System::Jobs` (Normal priority, `Pipelines` as the job name, progress counted) when: the game creates a PSO with a replaced stage; a publication changes a hash's variants (each `Database::Shader` keeps the pipelines that use it, a weak list removed from by the pipeline's state destructor); the trace forces a choice. Synchronous, in the game's creation call, when Luma's pipeline library has it.
  * Published at the next present into the `Pipeline`'s `Lifetime::D3D12::Object` (`Replace`). Until then the game's PSO runs, and trace says "pending".
  * `SwapInVariant` at `SetPipelineState`: while `Luma.Enabled` and loaded, a ready variant goes in the argument's place through `Lifetime::D3D12::Reference(list, object)`. A device refusing one is logged once, and the game's PSO runs.
* Pipeline library (`Shaders/D3D12/D3D12PipelineLibrary.cpp`): one `ID3D12PipelineLibrary` per device, `Luma/Cache/Pipelines/<vendor>-<device>.bin`. Entries named by a hash of the variant desc with bytecode replaced by its hash. Loaded at device creation (the file mapped for the library's lifetime, as it requires); `D3D12_ERROR_DRIVER_VERSION_MISMATCH`, `ADAPTER_NOT_FOUND` or any failure deletes the file and starts empty. `Serialize` runs as a Low job a few seconds after the last new entry, with stores held off while it runs (the one call the library doesn't allow concurrently).
* Per-list record (`Draw::D3D12`, `Draw/D3D12/D3D12Recorded.cpp`, in the list's private data, found with `GetOrCreateStateCached`): pipeline (game's and bound), graphics and compute root signatures, descriptor heaps, the variant stages, whether Luma's CBVs are set for the current signature. Development adds what trace reads: root arguments (tables, root descriptors, constants), render target and depth handles, viewports, scissors, topology, blend factor, stencil ref. Cleared at `Reset` and `ClearState`; a root signature change clears its arguments and Luma's CBVs. A list is recorded by one thread at a time, so no lock. Publishing hooks no root argument function (the space 0 fallback's reused root CBV excepted).
* Luma's cbuffers (`Resources/D3D12/D3D12LumaCBuffers.cpp`, `D3D12ResourceDatabase.cpp`): an upload ring per device (64 KB chunks of an upload heap buffer, mapped once; a chunk is a small COM object referenced by the lists that wrote into it, back to the pool when its last reference drops after the fence). Per list, the same decisions as a D3D11 context: `NeedsUpload` by stamp for LumaSettings, `ReplaceIfChanged` by bytes for LumaData, written once per list and reused while unchanged (a new list starts empty, as a D3D11 command list boundary). Set as root CBVs at a draw with a variant when the signature changed or the contents did; Luma's parameters are its own, so nothing is restored. `restore_after_draw` has nothing to do on D3D12 (Luma's registers are never the game's), except in the space 0 fallback.
* Release after use, completed: a list keeps its references until `Reset`; each `ExecuteCommandLists` tags them with a new fence value, so a closed list executed again keeps its objects alive (the open item of Step 0's notes).
* `Resources::Database` on D3D12: its cbuffer entries. Luma's shader entries get no objects on D3D12 until something draws with them (see Decide first: passes).
* `Shaders::IsNeeded()` decides D3D12's functions as D3D11's: creations (pipelines, root signatures, heaps) always while needed, `SetPipelineState`, `Set*RootSignature`, `Reset`, draws and dispatches for the variants, root arguments and view creation only for the trace (Development).

**Verify (once):** FF7R D3D12: every root signature extended (`luma_status`: count, largest size, any left unextended), `luma_shader_list` shows the D3D11 replaced hashes, "Unload Shaders" shows the vanilla image, `luma_read_resource` of the tonemap output against the D3D11 result, a reload recompiles and the old variant PSO is released after its lists ran (debug layer clean while reloading repeatedly), `Luma.Enabled` off and on swaps at once with nothing recreated, a second boot loads every variant PSO from the pipeline library (logged: hits, misses, created), a define change through the overlay and `luma_set_setting` recompiles.

**Notes (2B built, not verified in the game, not committed):**
* Owner's additions while building: D3D12 is covered beyond what FF7R uses; shaders are built with `_D3D11` and `_D3D12` (0 or 1) so a game's shaders can branch per API.
* Every creation path: graphics, compute and stream pipelines and the game's pipeline library loads go through one `CreatePipeline` detour. A root signature that comes with a pipeline (an RTS0 chunk with no `pRootSignature`, or a `SERIALIZED_ROOT_SIGNATURE` stream subobject) is replaced by its extended one, created once per device and blob (`RootSignatures::D3D12::RewritePipelineDesc`). Local root signatures aren't extended. State objects and `CreateRootSignatureFromSubobjectInLibrary` are hooked and `LUMA_UNIMPLEMENTED` (DXIL).
* Every bind path: `Reset` and `ClearState` (their pipeline argument), `SetPipelineState` swap in the variant; `SetPipelineState1` unbinds it. Draws are `DrawInstanced`, `DrawIndexedInstanced`, `DispatchMesh`, `Dispatch` and `ExecuteIndirect`; before `ExecuteBundle` Luma's cbuffers are bound on the list's graphics signature, which the bundle inherits (a bundle's own draws bind nothing).
* When variant pipelines are made: the desc is deep-copied when the game creates a pipeline (`Database::D3D12::CopyDesc`: Development and Test always, Publishing when a stage has a replacement file). A variant is made then if a stage already has a compiled variant, and at every D3D12 present after `Variants::GetGeneration()` moved (a build published, or the trace forced a choice, which now bumps the generation through `Variants::MarkChoiceChanged`). From Luma's pipeline library at once when it has it (`Luma/Cache/Pipelines/Library.bin`, saved by a Low job 3 s after the last store), otherwise as a "Creating pipelines" job; the game's pipeline runs until it's published.
* The build waits for the first device's API when the game doesn't declare one (`Shaders::SetApi` at device creation, the pending build starts at the next present). D3D12 builds compile `_5_0` targets as `_5_1` with `LUMA_*_CB_INDEX` as `bN, space<d3d12_space>`.
* Luma's cbuffers: a 256 KB upload chunk at a time per device, root CBVs at Luma's parameters, set at a draw when the list's signature changed or the address did. Lists keep their references until `Reset` (`Lifetime::D3D12::ForgetReferences`), each execution tags them again.
* Luma's pipeline library names a variant by the desc's fields (`ContentHasher`), not its bytes: games leave struct and stream padding uninitialized, so a byte hash changed every boot and never hit.
* Linkage: D3D12 refuses a pipeline whose stages don't link (FF7R's motion blur pixel shader replacements dropped an unused `TEXCOORD0`, moving `SV_Position` to register 0); D3D11 reads the wrong data instead, and the compiler can't see it. `Shaders::Container` reads the container's chunks without reflection (target, root signature, input/output/patch constant signatures, DXBC and DXIL); `Signatures::Check` compares a replacement's with its original's when both exist (on publish, or when the game creates the original): an error in the log and an assert. Development builds only: Publishing assumes the shipped replacements were fixed while the mod was made, and doesn't parse the originals' signatures.
* Not built: the space 0 fallback (2A showed it isn't needed), the descriptor heap tail (deferred), D3D12 trace and draw events (2C).

### 2C: Draw events, descriptors, trace, FF7R

**Build:**
* `Game::D3D12::Call` (the list, `DrawCall` or `DispatchCall` with the native arguments, the hash per stage, `variant_stages`), `Events::on_variant_draw` and `on_draw_or_dispatch`, `Game::D3D12::OnShaderDraw(hashes, fn)`, `RunOriginal`, `UseOriginal` (binds the game's whole PSO: D3D12 can't put one stage back, see Decide first), in `Game/D3D12/D3D12DrawEvents.cpp`. The handler order of Step 1: trace records, the game's events, the trace's skip, then the variants' requests.
* `Resources::Descriptors::D3D12` (`Resources/D3D12/D3D12Descriptors.cpp`): a side table per descriptor heap (descriptors aren't objects, `Guidelines.md` section 5): per descriptor the resource (not held) and the view desc, written by `Create*View` and `CopyDescriptors*`, read by handle (CPU or GPU, heaps found in a sorted snapshot of their ranges). A buffer's GPU address range → resource, for root CBVs and SRVs. Needed by trace now (Development), by mirrors in Step 4 (which adds its need and the resource → descriptors index).
* `Trace::Capture` on D3D12 (`Trace/D3D12/D3D12Capture.cpp`), the D3D11 event model unchanged so the UI and MCP don't branch:
  * shaders and the variant from the `Pipeline`; blend, depth, rasterizer from its desc (D3D12 bakes them in, so trace shows them exactly); views from the bound tables through the root layout and the descriptor tables, limited to the slots the shader's reflection uses; root CBVs by address; viewports, scissors, topology from the list record;
  * events are kept per list and appended to the frame at `ExecuteCommandLists`, in submission order, with the queue and list (see Decide first);
  * cbuffer contents read at the draw when the buffer is in an upload heap (the CPU copy the game wrote for it); default heap buffers have none in v1. Track works the same way for upload heap buffers;
  * the Skip Draws action and the variant picker apply. On D3D12 the picker's choice takes effect when its PSO is ready.
* MCP: every Step 1 tool answers on D3D12; `status` gains the `d3d12` block (pipelines, variant PSOs ready and pending, library hits and misses, extended and unextended signatures, ring memory, the register model in use).
* Validation: `Luma.Development.GpuValidation` (D3D12 GPU-based validation, applied at the next device creation).
* FF7R: `SetEvents(D3D12::Events{...})` with its swapchain and present callbacks, which reach the same game code (a thin wrapper per API). No game logic changes.

**Not in this step:** shadow heaps (Step 4 if the research needs them), holes in heaps at the device's limit, DXIL and DXC (first SM6 game, which uses Luma's space natively), D3D12 Luma passes (Step 5). Bundles were built in 2B after all (below).

**Notes (2C built, first run in the game, not committed):**
* First D3D12 run (debug layer, title screen): 22/22 variant pipelines ready, none refused (the motion blur fix holds), library 20 hits and the 2 changed shaders stored, 30 extended root signatures, 0 debug layer errors (8502 warnings, all the game's own: initial states, barriers, fence waits, unbound targets). Trace: 187 entries in submission order with queue and list, file variants on the replaced shaders, full state and cbuffer contents read back. Not yet checked: draw events from a game mod, skip and picker, the image against D3D11, unload, GPU validation.
* The linkage check compares what a replacement input reads (the signature's used mask), not what it declares: Bloom's `float4 TEXCOORD0` reading `.xy` where the original declared `.xy` was a false error.
* Draw events (`Game/D3D12/D3D12DrawEvents.cpp`): `Game::D3D12::DrawCall` (`DrawInstanced`, `DrawIndexedInstanced`, `DispatchMesh`, `ExecuteIndirect` with its arguments), `DispatchCall`, `Call` (the list, the game's pipeline, the hash per stage, `variant_stages`), `on_variant_draw`, `on_draw_or_dispatch`, `OnShaderDraw`, `RunOriginal` (binds the variant's requests, then issues the call in a Luma scope) and `UseOriginal` (binds the game's pipeline until the game binds another). `ExecuteIndirect` is a draw or a dispatch by the bound pipeline's kind. The draw hooks now return whether the game's call is skipped. Variants' list record keeps the game's pipeline and its record in every build (`GetBoundPipeline`), which the events and trace read. `BindLumaShader` on D3D12 waits for the passes (Step 5).
* Order in the handler, as D3D11's: trace records, `TrackCall`, the game's events, the trace's skip, the variant's requests. `Registry::GetRequestedD3D12Functions` hooks what the game's draw events need.
* Descriptors (`Resources/D3D12/D3D12Descriptors.cpp`): a side table per heap (chunks of 1024 allocated as written), written by `Create*View` (resource and desc) and `CopyDescriptors*` (the hook splits ranges into matching runs), read by CPU or GPU handle. Resources are known from their creation (every `Create*Resource` version, and the swapchain's buffers at init): desc, heap type, a buffer's GPU range, and an upload heap buffer mapped for its lifetime (its CPU copy is the cbuffer contents trace reads). A descriptor remembers its resource's id, so a resource destroyed since (or another at the same address) shows as "Stale" and is never touched. Development only in this step.
* Trace (`Trace/D3D12/D3D12Capture.cpp`): each list records its root signatures and arguments (tables, root descriptors, constants), render targets, viewports, scissors, topology, blend factor, stencil ref (new hooks: `SetDescriptorHeaps`, `SetRootArguments`, `SetFixedState`). A call reads its stages' cbuffers through the root layout (root CBVs by address, table CBVs, root constants as a cbuffer named "root constants"), its main stage's SRVs, UAVs, samplers (static ones too, kept in the root layout now), gated by reflection, space 0 only; blend, depth and rasterizer from the pipeline's desc (`Database::D3D12::GetGraphicsState`). Events stay with the list and go into the frame at `ExecuteCommandLists` with the queue and list, so the frame is in submission order; a bundle's go into the list that runs it; a list recorded before the capture started has none. Cbuffer contents are copied at the call (upload heap only), only the bound range (`ConstantBuffer::contents_ranged`). The skip action and the picker work as D3D11's; Track reads the slot at the call. The state names are shared with D3D11 (`Trace/StateNames.hpp`).
* MCP: the Step 1 tools work on D3D12 through the same events; `trace_list` and `trace_get` add `queue` and `list`; `status` has a `d3d12` block (live pipelines, variant pipelines, Luma's library, extended and unextended signatures, ring bytes, the register space, and in Development the heaps and resources trace knows).
* Validation: `Luma.Development.GpuValidation` turns on the debug layer with GPU-based validation from the game's next start (the debug layer can't be turned on once a device exists, and FF7R makes devices before the one it renders with). Status reports what this run has.
* FF7R: its swapchain and present callbacks run on D3D12 through thin wrappers (the queue's device), the same game code.
* ~~Space 0 only in trace's register reading~~: replaced after 2C by the shader's own declarations (below).

**After 2C (decided with the owner, built, not committed):**
* The register space is `Game::Options::bindings.d3d12_space` (`Resources::Bindings`), no longer a cbuffer option: it holds all of Luma's bindings, its views and samplers later too.
* The space is Luma's whole: a root signature with any range, root descriptor, constants or static sampler in it isn't extended (its pipelines run the game's shaders), with an assert in Development naming the option to change. In space 0 (the D3D11 model) only the cbuffer slots conflict, as before. "Too big" is the API's limit: 64 DWORDs with Luma's two root CBVs (4).
* `Container::GetBindings` reads which registers a shader declares, in which space, without reflection: SM4 and SM5 declarations in SHEX/SHDR (SM5.1's ranges and space, unbounded arrays), SM6's PSV0 resource list, and SFI0's heap indexing flags (SM6.6 `ResourceDescriptorHeap[]`, `SamplerDescriptorHeap[]`). Development builds keep them per shader: a game shader declaring anything in Luma's space is logged and asserted; trace reads registers of every space by them (reflection, space 0, when there are none), skips Luma's own parameters by index, and shows a space other than 0 (`t3 space1`, `space` in MCP); `status` lists the game's spaces and counts heap indexed and unbounded array shaders.
* Bindless: a heap indexed shader declares no register for what it indexes, so trace can't list those views; status counts them. Listing them needs the GPU (Step 4 research), not planned.

**Verify (once):** the Step 1 list on D3D12, compared with the D3D11 result rather than 1.0. The debug layer and GPU-based validation clean with shaders replaced. Frame time with the mod's shaders unloaded within noise of a build without the bind hooks (Development's capture hooks excluded: measured on Publishing-Release). A trace of the same scene lists the same draws and hashes as D3D11's.

**Decided with the owner:**
* Root signatures are extended unconditionally at creation (not per draw with replay, not twin PSOs): one signature for the game's pipelines and Luma's, variants swapped at bind.
* Luma's D3D12 resources live in their own register space (`Options::bindings.d3d12_space`, default 100) with SM5.1 variants, if 2A shows SM5.0 and SM5.1 mix in one PSO; otherwise D3D11's slots in space 0. Both models are built.
* Descriptor heaps get a Luma tail at creation, their `GetDesc` answering the game's size; heaps at the limit use holes found by write tracking. Built with the first user that needs descriptors (Step 3 or 4), not in Step 2.
* Conflicts with a game register are handled as 1.0: `ASSERT_ONCE` in Development, logged, Luma's binding wins where it can.
* A game pipeline library failing its loads once after the signatures change is accepted.
* Default space 100. Extension follows `Shaders::IsNeeded()`, always on in Development.
* Research first: 2A's findings are reported to the owner before 2B starts.
* `ExecuteIndirect` is handled as a draw or dispatch (2A found about 5 a frame in FF7R): the variant is already swapped at `SetPipelineState`, Luma's cbuffers are bound before the call like a draw's, and trace lists it as an indirect event. A command signature that changes root arguments is `LUMA_UNIMPLEMENTED`. Bundles stay `LUMA_UNIMPLEMENTED` (none in FF7R).
* Trace lists D3D12 events in submission order, merged at `ExecuteCommandLists`.
* D3D12 Luma passes wait for Step 5.

**Decided here (owner may revise):**
* `UseOriginal` on D3D12 binds the game's whole PSO, so every stage of that call reverts. A PSO per mixed choice isn't built until a game needs one (section 17 row).
* Variant PSOs are created eagerly when the game creates a pipeline with a replaced stage, not at first bind, so a variant is usually ready before its first draw. The trace picker's choice on D3D12 takes effect when its PSO is ready.
* Descriptor tracking is Development only in this step (trace is its only user); Step 4 makes it needed in Publishing.
* The default space is 100 rather than 50, further from the low spaces bindless engines use for their unbounded ranges.

---

## Step 3: Shader patching

**Goal:** DXP recipes and game bytecode patches produce variants on both APIs. FF7R's dithering fix works.

**Read first:** `patch.hpp`, `patch.inl`, `shader_patching.h`, FF7R `PatchShaderBytecodeSync` and its recipe registration, FF7R's `Recipes` folder, the DXP library's README.

**Build:**
* `Shaders::Patching`: recipe loading from `Luma/Recipes`, recipe registration by games (`Game::Shaders::AddRecipe`: identity, display name, default, visibility), the sync and async bytecode callbacks of `game.h`. Patched shaders register as `Variants::Source::Patch` variants on the Step 1 model (a priority relative to `FILE_PRIORITY`), so requests, fulfil, `on_variant_draw`, trace and the picker apply to them unchanged. 1.0's `OnDrawOrDispatchCustomPipeline` binds Luma's cbuffers before the game's callback when a patch's recipe needs them: decide here whether a patch's requests are bound before `on_variant_draw` (the Step 1 order fulfils after the callbacks).
* Patch cache in `Luma/Cache/Patches`: patched bytecode and an index of which hashes each recipe matched and didn't, keyed by recipe content, environment values and patcher version. A later boot patches nothing it has an answer for.
* Recipe switches and environment values as settings with `Source::Recipe` and `Kind::Recipe` (`[Recipes]`, the Recipes section). A change re-patches.
* Recipe resource bindings resolved by name. Until Step 4 only Luma's own resources resolve.
* `System::Memory`: the pattern scan FF7R's bytecode patch uses (`System::ScanMemoryForPattern`).
* Dev UI: Patches tab (recipes, matched shaders, state, errors). Patch progress in the status bar.
* MCP: `luma_patch_state`, `luma_set_recipe`, `luma_reload_patches`. `luma_trace_list variant=original` finds draws that fell back.
* Patched shader dumping where 1.0 has it.

**Verify (once):** FF7R's "Dithering Fix" recipe on and off changes the matched shaders on both APIs (`luma_patch_state`), toggling it from the overlay and from MCP re-patches without a restart, and a broken recipe reports an error and leaves the originals running. Added with the decisions below: on D3D11 the dithering variants run with `fast_noise` bound (`luma_trace_get` of a patched draw shows it at the recipe's register); on D3D12 they're listed as failed with the bindings reason and the game's pipelines run; a second boot reports every patch "from the cache"; SSAO Temporal binds the noise at t6.

**Decide first:** DXP on DXIL isn't needed for FF7R. Recommended: wire DXBC only and leave DXIL as `LUMA_UNIMPLEMENTED`.

**Decided with the owner (before building):**
* 1.0's reference: FF7R builds with `UseLumaDXP` and `UseLumaFastNoise` (its `.vcxproj`), so it runs async DXP recipes only; its `PatchShaderBytecodeSync` is dead (`LUMA_PATCH_BYTECODE_SYNC 0`, and with recipes it returns early). The Step 1 notes calling `ENABLE_FAST_NOISE_TEXTURES` "off in 1.0" are wrong: 1.0 loads "FAST Noise" and runs `OnDraw_SSAO_Temporal`, which this step ports.
* DXBC only; DXIL is `LUMA_UNIMPLEMENTED`.
* DXP comes from 1.0's vcpkg port (`Source/External/vcpkg-ports/dxp`), opted into per game with a `UseLumaDXP` prop in `Luma2.Game.props` (section 7: optional SDKs by props). FF7R opts in.
* No `Game::Shaders::AddRecipe`: a recipe describes itself. DXP gains a recipe-level `meta:` (reported verbatim, as declaration `meta`), and Luma reads its keys: `name`, `description`, `default_enabled`, `visibility`, `env: {VAR: {label, tooltip, visibility}}`. A recipe without them loads enabled and is visible in Development only (1.0's unregistered recipe). Env types and defaults are the recipe's `env:`.
* Patches are variants: `Variants::Publish(Source::Patch, ...)`, `PATCH_PRIORITY` below `FILE_PRIORITY` (1.0 let a file win), the same per (device, hash) objects made at creation or publish (never at bind, unlike 1.0's lazy `TryPatchPipeline`), the same picker, requests and fulfil, and D3D12 pipelines through Luma's own pipeline library (used whether or not the game has one).
* 1.0's sync and async bytecode callbacks are ported for games that patch without DXP: sync runs in the game's create call (the variant is ready before the first bind), async on a job; both publish Patch variants; the hash stays the game's.
* Patching runs on a job when the game creates a shader. A reload, a recipe switch or an env change re-runs the enabled chain over every hash the game created, at once (not lazily at the next bind: the originals are at hand), and publishes the whole Patch source. So the game's bytecode is kept while patching is needed (any recipe or bytecode callback), in Publishing too, as 1.0's pipeline cache kept it.
* One cache: patched bytecode in `Luma/Cache/Shaders` through `Cache::ReadShader`/`WriteShader`, keyed by shader hash, each enabled recipe's content in chain order, env values, DXP version and callback version; plus an index there of which (shader, chain) matched and which didn't. No `Cache/Patches` (Guidelines section 8 to update).
* Recipe bindings bind as 1.0's `OnDrawOrDispatchCustomPipeline`: resolved and bound before `on_variant_draw`; a missing resource puts that stage's original back (`UseOriginal`); Luma's cbuffers bound for a patch count as bound (1.0's `updated_cbuffers`). Bindings are `Request`s resolved by name in `Resources::Database`: `luma_settings`, `luma_data`, and textures. No `"fast_noise"` → `"FAST Noise"` special case.
* Textures are a game option, all or nothing for now (`Game::Options::textures.load`; inclusion and exclusion lists later): every `.dds` in the `Textures` roots is a `Resources::Database` entry named after its file, created on each device, whoever reads it (a patch's binding, a draw callback). Loaded with DirectXTex's `DDSTextureLoader` (vendored). The FAST PNGs become one `Resources/Global/Textures/fast_noise.dds` (2D array). D3D11 now; D3D12 with its heap tail.
* D3D12: patches that add no resource run. A patch that adds one (the dithering fix) or reads Luma's cbuffers falls back to the game's pipeline (`LUMA_UNIMPLEMENTED`): DXP's SM5 output puts added resources in space 0 at a free register, which the game's signature doesn't describe, and Luma's own bindings are in its space, which only SM5.1 can name. See the DXP work below.

**DXP work for D3D12 (later, owner's item):** an "upgrade to SM5.1" feature in DXP doing the grunt work, so Luma can put a patch's added bindings in its space:
1. An output option turning an SM5.0 input into SM5.1: the version token, every resource declaration in 5.1 form (range id, lower and upper bound, space), every resource, sampler, UAV and cbuffer operand with the extra index.
2. A space for `add_resource` (from the recipe or set at execute), which "SM5 is always space 0" rules out now.
3. `new_bindings` with the assigned space; the game's own bindings stay in space 0.
4. RDEF updated for 5.1 (or stripped), and the output target part of what a cache key includes.
SM5.0 and 5.1 stages mix in one pipeline (Step 2A), so Luma needs nothing else for it.

**Notes (built, not verified in the game):**
* DXP: recipe-level `meta:` added to DXP itself (0.10.0, `Recipe::GetMeta()`, SM5 and SM6; DXP commit `da77209`), so a recipe describes itself. Luma reads `name` (its presence makes the recipe a user's: visible in Release unless `visibility` says otherwise), `description`, `default_enabled`, `visibility` (`release`, `test`, `development`) and `env: {VAR: {label, tooltip, visibility}}`; an undescribed env value is Development only. FF7R's `PS/dithering_fix.recipe.yml` has its 1.0 registration as `meta`. Released as `v0.10.0`; the vcpkg port (shared with 1.0) downloads it.
* Build: `UseLumaDXP` in `Luma2.Game.props` (vcpkg manifest mode for that project, `LUMA_USE_DXP=1`, `dxpatcher.lib`), FF7R's `vcpkg.json` and `vcpkg-configuration.json` pointing at 1.0's overlay port. Without it the recipes folder is logged as ignored and only the game's bytecode callbacks patch.
* `Shaders::Patching` (`Shaders/Patching.*`): `Register` (after `Init` and the roots, before the settings load) scans `Recipes/` (root for any stage, `VS/HS/DS/GS/PS/CS` for one; other folders warned), parses with DXP, registers a `Kind::Recipe` setting per recipe (`Recipes.<identity>`) and per env value (`Recipes.<identity>.<VAR>`), saved as `[Recipes."<identity>"]` with `Enabled` and the env names. A change (overlay, MCP, `luma_set_setting`) re-patches everything at the next present.
* When: `Database::Track` calls `Patching::TrackShader` once per hash (the thread whose shader was kept). The sync callback runs there, its variant published before the object attaches; otherwise the shader waits for the present, which queues one "Patching shaders" job for everything created since (or for every shader after a change, a reload or a load). The job publishes with `Variants::Update(Source::Patch, changed, removed)` (new: a few hashes at a time, no copy of the map), `PATCH_PRIORITY` 50 under `FILE_PRIORITY`. D3D11 and D3D12 pick it up as any publication; D3D12 Publishing now copies pipeline descs when patching is needed.
* Order on a shader: the game's sync, then async callback (1.0: the game's patch wins), else the enabled recipes chained in load order. The game's bytecode is kept in every build while patching is needed (Publishing included). Callback results aren't cached (game code, 1.0 didn't either).
* Cache: patched bytecode through `Cache::ReadShader`/`WriteShader` in `Luma/Cache/Shaders`, keyed by `Cache::GetPatchKey(hash, "dxp <version>|<identity>#<content hash>;VAR=value...")`; `Luma/Cache/Shaders/Patches.json` indexes each key (modified or not, the recipes that changed it, its bindings), so a second boot runs no recipe. A failed recipe isn't indexed. `Cache/Patches` isn't used.
* Bindings: a patch's `new_bindings` resolve by `Resources::Database` name (a `texture` to a `Kind::Texture` entry, a `cbuffer` to a `Kind::ConstantBuffer` one: `LumaSettings`, `LumaData`; 1.0's `luma_settings`/`luma_data` and its `fast_noise` → "FAST Noise" special case are gone) into `Request`s. An unknown name, another class (sampler, UAV, buffers) or D3D12 with any binding: the patch isn't published, its entry says why, and the status bar counts it. D3D11 binds them before the events (`Handlers::FulfilBeforeEvents`, `Variants::D3D11::BindPatchResources`); a texture the device couldn't load puts that stage's original back for the call (`UseOriginal`). The stages bound before are left out of the after-events fulfil and get their cbuffers restored as the others.
* Textures: `Resources::Textures` (`Options::textures.load`), `Resources::Database::Kind::Texture`, loaded per D3D11 device in `CreateDeviceObjects` (synchronously, at the device's creation: one 4 MB file for FF7R), bound by `BindRequested`, read by games with `Game::D3D11::GetTexture(device, "name"_h)`. D3D12: `LUMA_UNIMPLEMENTED` at device creation.
* FF7R: `.textures = {.load = true}` (1.0's `UseLumaFastNoise`), `OnDrawSSAOTemporal` (1.0's `OnDraw_SSAO_Temporal`) on D3D11. The Step 1 notes calling `ENABLE_FAST_NOISE_TEXTURES` off in 1.0 were wrong: FF7R's 1.0 project sets `UseLumaFastNoise`.
* Containers: `Container::GetProgramCode`, `ReplaceProgramCode` and `Sign` (1.0's `FindShaderByteCode`, `BuildPatchedContainer` and `CalcDXBCHash`). `System::Memory::ScanForPattern` with wildcards (1.0's `ScanMemoryForPattern`).
* Dev tools: Patches tab (Reload, Unload/Load, recipes with their switch and env rows, shaders patched or failed, "Unchanged" filter, each patch's recipes, bindings, error and step report), status bar item for failed patches, the job's progress and its toast (only a job that patches everything again reports success). Shaders tab lists replacement files only. MCP: `patch_state` (`hash`, `include_unmatched`), `set_recipe` (`identity`, `enabled`, `env` with `value` or `reset`; waits for the patch job), `reload_patches` (`unload`), `status.patches`. `luma_trace_list variant=original` already found fallbacks. Patched shaders dump as `Luma/Dump/0x<hash>.patched.<target>.cso` (Development and Test, with Dump Shaders).
* Offline: DXP 0.10's `dxp sm5 inspect` reads the recipe's `meta`; `dxp sm5 patch` changes 129 of the 194 pixel shaders of 1.0's FF7R dump. All four configurations build.
* Tests (89 pass): FXC's checksum reproduced by `Sign`, a program spliced with one more token taken by WARP, memory patterns, `Variants::Update` priorities and removals, `fast_noise.dds` loading as 1.0's array.
* Not built: DXIL recipes (`LUMA_UNIMPLEMENTED`), a game's own recipe env (1.0's `Patch::SetOptions`, no game uses it), raw buffer, sampler and UAV recipe bindings, texture requests from a replacement's reflection.

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
* `Frame` resolutions: render, output, swapchain and display as four fields, extending Step 1's per-device `Luma::Frame` (output, render, previous render). Output and swapchain are equal in this step, but no code assumes it: composition has the scale stage, and the overlay and input use the swapchain size.
* `Display` composition on both APIs: the 1.0 composition shader and its define-driven behaviour unchanged, plus the encode for the output mode. UI separation for the `UI_DRAW_TYPE` modes FF7R uses.
* A switch per composition stage (`Guidelines.md` section 12), as settings. Cheap runtime flags in the composition cbuffer where the shader allows it, defines otherwise.
* Debug draw of any view of a captured event into the composition (1.0's "Debug Draw Shader Instance" and resource highlight).
* Release UI: the Display category (output mode, peak white, paper white, UI paper white, gamma) built from settings, matching 1.0's options.
* D3D12 passes' bindings: Luma's views in a descriptor table in Luma's space (`Options::bindings.d3d12_space`), its descriptors in the heap tail (2B's design, built here first), the table appended to extended signatures after the two root CBVs (1 DWORD more). Luma's samplers as static samplers in Luma's space: no heap and no root cost. A signature without room stays the game's, and those pipelines keep the game's shaders.
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

**Found before this step (alt-tab in FF7R, Development build):** FF7R creates its swapchain R10G10B10A2 and resizes it to FP16 right away (`Luma.log`); the color space is the game's own (`SetColorSpace1` wasn't hooked before this unit, so its value isn't known yet). Luma2 doesn't change the swapchain before this step: no format, color space, HDR metadata or fullscreen change (`SetFullscreenState` is only logged); this session's log has one resize, at startup, and no fullscreen change. FF7R's output shader writes scRGB in Development builds (`DEVELOPMENT && EARLY_DISPLAY_ENCODING` decodes its PQ to linear BT.709) and PQ in the others, so the image is right only while the game's color space matches that encoding, which nothing in Luma2 ensures. After the focus loss the game's image looked crushed while the overlay (then drawn with plain sRGB values) looked right: the buffer was shown with a gamma (SDR) interpretation, the game or DWM composition (the window leaves independent flip) changing what the FP16 contents are taken as. Not a Luma2 bug to fix in place. The fix is this step's: Luma owns the swapchain and sets the color space matching what composition writes (HDR10 `R10G10B10A2` with `G2084`, scRGB FP16 with `G10`, never PQ in FP16 unless `CheckColorSpaceSupport` allows it), sets it and the HDR metadata again after every resize and fullscreen change (1.0's `OnInitSwapchain`), and keeps it whatever the game sets. Verify by alt-tabbing in each mode. `Luma.log` now logs the color space the game sets ("DXGI: swapchain ... color space ..."), which confirms the cause before then.

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
* `Frame`: the render resolution set through `Game::Frame::SetRenderResolution` (the per-device model from the Step 1 corrections), upscaled resolution, jitter, camera cut, pass markers. Decide here whether the camera model of section 17 is worth it now.
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
| Shader replacement, custom shaders, defines | Step 1 | — | 1 / 2 |
| Resource database (Luma's shaders, cbuffers) | Step 1 | — | 1 / 2 |
| Draw callbacks, state stack, passes | Step 1 | — | 1 / 2 |
| Trace capture | Step 1 (draws, dispatches, Luma passes) | — | 1 / 2 |
| Shader patching (DXBC) | Step 3 | Step 3, patches adding bindings — | 3 |
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
* **Mod and Core version check.** Each game DLL compiles Core: it should log the commit it was built from.
* **Output resolution decoupling.** With a Luma-owned swapchain the real swapchain can be a different size from what the game renders to. That gives borderless upscaling for games with fixed resolutions. Not planned, but Step 5 shouldn't prevent it.
* **Frame generation hooks.** FF7R has a disabled `ENABLE_FRAMEGEN` path with a hudless target. The composition stage in Step 5 is where a hudless output would be exposed.
* **1.0 settings migration.** Users of 1.0 mods have values in the old config. Decide at Step 8 whether to import them once or start from defaults.
* **Global shaders during the transition.** `Resources/Global` is a regrouped copy of 1.0's `Shaders/Global` and `Shaders/Includes`. A fix made on one side has to be made on the other until the swap. File names and contents stay the same so the two can still be compared file by file.
