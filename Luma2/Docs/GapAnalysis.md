# Gap analysis: 1.0 against 2.0

What 1.0 (`Source/Core`) has, for the areas 2.0 (`Luma2/Source/Core`) has ported so far, and where 2.0 stands. Status: **present**, **partial**, **missing**, **dropped** (on purpose, a row in Guidelines section 17). Line numbers are `core.hpp`'s unless a file is named.

## Hooks, device and process

| Feature | 1.0 | 2.0 | Status | Note |
|---|---|---|---|---|
| D3D11 device and context detours | `hooks/d3d11_device_hooks.cpp`, `hooks/d3d11_context_hooks.cpp` | `Hooks/D3D11/D3D11DeviceHooks.cpp`, `D3D11ContextHooks.cpp` | present | hooks installed on demand per feature |
| DXGI factory, swapchain hooks | `hooks/dxgi_hooks.cpp` | `Hooks/DXGI/DXGIHooks.cpp` | present | |
| Device proxy, swapchain proxy | `hooks/device_proxy.cpp`, `hooks/swapchain_proxy.cpp` | `Hooks/DeviceProxy.cpp`, `Hooks/DXGI/DXGISwapchainProxy.cpp` | present | |
| Vtable copies, slots | `hooks/vtables.cpp`, `hooks/slots.c` | `Hooks/Vtables.cpp`, `VtableSlots.h` | present | |
| Luma scope, depth guard | `hooks/scope.cpp`, `utils/depth_guard.hpp` | `Hooks/HookScope.cpp`, `Hooks/DepthGuard.hpp` | present | |
| Third-party module detection | `hooks/third_party_modules.cpp` | `Hooks/ThirdPartyModules.cpp` | present | |
| Unhook by proof, dll pin | `hooks/hooks.cpp` | `Hooks/HookManager.cpp` | present | |
| Loader and exports | `system/loader.cpp`, `system/dxgi_exports.cpp` | `System/DllLoader.cpp`, `System/DXGI/DXGIExports.cpp` | present | init moved out of the loader lock (§17) |
| Input blocking | `overlay/input_hooks.cpp`, `input_windows.cpp` | `Hooks/InputHooks.cpp`, `Hooks/User32/`, `Hooks/DInput/` | present | |
| D3D12 device, list, queue hooks | none (D3D12 unsupported in 1.0 Core) | `Hooks/D3D12/*` | 2.0 only | |
| Game virtuals (`class Game`) | `includes/game.h` | `Game/Game.hpp`, `GameEvents.hpp`, `GameCallbacks.hpp` | present | events and callbacks (§17) |
| `OnInitDevice`, `OnDestroyDeviceData` | 2418, 2697 | `Handlers/D3D11/D3D11Handlers.cpp` `PostCreateDevice`, `OnDeviceDestroyed` | present | |
| `DeviceData` / `GameDeviceData` | `includes/instance_data.h` | `Game::GameDeviceData`, `Frame/FrameState` | present | |

## Shaders

| Feature | 1.0 | 2.0 | Status | Note |
|---|---|---|---|---|
| Replacement files by hash | `CompileCustomShaders` 1262 | `Shaders/Shaders.cpp` `Run`, `ShaderFiles.cpp` | present | |
| Shader clones (variants) | per object, at bind (`OnBindPipeline` 4323) | per hash and device, made at `PreCreateShader` (`D3D11Variants::CreateVariant`) | present | creation time since batch 2 |
| `redirected_shader_hashes` | 473 | `Game::Shaders::RedirectHashes` | present | |
| `forced_shader_names` | 477 | `Game::Shaders::SetDebugName` | present | |
| Native (custom) shaders | `native_shaders_definitions` 506 | `// Luma:` lines, `Resources::Database` | present | |
| Shader defines | `shader_defines_data` 618 | `Shaders/ShaderDefines.cpp`, settings of kind `Define` | present | |
| Compile cache | `.cso` next to `.hlsl` | `Shaders/ShaderCache.cpp` (`Luma/Cache/Shaders`) | present | |
| Shader dumping | `DumpShader` 801, `AutoDumpShaders` 806 | `Database::QueueDumps`, `Dump` | present | |
| Raw `.cso` replacements | `0x<hash>` / `!` files | none | dropped | §17 "Raw .cso replacements" |
| DXP recipes | `includes/patch.hpp`, `LUMA_PATCH_RECIPE_*` | `Shaders/ShaderPatching.cpp` | present | recipes describe themselves (`meta:`) |
| Game bytecode patch callbacks | `LUMA_PATCH_BYTECODE_*` | `Options::patching` | present | |
| DXIL patching | n/a | none | missing | `LUMA_UNIMPLEMENTED` |
| D3D12 pipelines and root signatures | none | `Shaders/D3D12/*` | 2.0 only | |
| Publish checks for D3D12 variants | none | `Shaders/ShaderInvariants.cpp` | 2.0 only | Development and Test |
| Decompiler | external | `Shaders/ShaderDecompiler.cpp` | present | |

## Luma cbuffers, draws, state

| Feature | 1.0 | 2.0 | Status | Note |
|---|---|---|---|---|
| LumaSettings upload | `cb_luma_global_settings_dirty` 2481 | `Resources/D3D11/D3D11LumaCBuffers.cpp`, stamp per context | present | |
| LumaData and `UpdateLumaInstanceDataCB` | 4905 | `Options::cbuffers.update_luma_instance_data` | present | |
| Binding to the variant's stages | `SetLumaConstantBuffers` 4768 | request and fulfil (`BindRequested`) | present | |
| `ENABLE_AUTO_CBUFFER_RESTORATION` | 127 | `Options::cbuffers.restore_after_draw` | present | Luma's two slots only |
| Draw callbacks by hash | `draw_callbacks_by_shader_hashes` 651 | `Game::D3D11::OnShaderDraw` | present | any stage matches (§17) |
| `OnDrawOrDispatchCustomPipeline` | `game.h` | `Events::on_variant_draw` | present | |
| `DrawStateStack` | `utils/draw.hpp` 29 | `Draw/D3D11/D3D11Draw.cpp` (mask of groups) | present | D3D12 has none (passes use own lists) |
| Bound-state cache | `ENABLE_DRAW_DISPATCH_DATA_CACHE` | `Options::draw` groups | partial | views and buffers not cached (§17) |
| Deferred contexts, command lists | `OnFinishCommandList` 4963 | `ForgetContextState`, per-context stamps | partial | no merge of deferred lists (§17) |
| Vertex buffer cache | `ENABLE_DRAW_STATE_VERTEX_BUFFER_CACHE` | none | missing | Step 9.3 |

## Resources

| Feature | 1.0 | 2.0 | Status | Note |
|---|---|---|---|---|
| Upgrade matching | `GetOptionalResourceUpgradeDesc` 7286 | `Resources/ResourceUpgrades.cpp` `Match` | partial | writable SRVs, custom dimensions not ported |
| Direct (in place) upgrades | `enable_indirect_texture_format_upgrades` 370 off | none | dropped | §17 "Texture upgrades" |
| Indirect upgrades (mirrors) | `resource_upgrades.inl` | `Resources/D3D11/D3D11Mirrors.cpp`, `D3D12/D3D12Mirrors.cpp` | present | |
| View format mapping | `GetBestResourceViewUpgradeFormat` 7210 | `Upgrade::GetBestViewFormat` | present | |
| Copies of upgraded textures | `OnCopyResource_Internal` 8304 | `D3D11Mirrors::RedirectCopy*`, `D3D12Mirrors::SwapCopy` | present | D3D12 converting copy since batch 2, single 2D subresources |
| Resolves | 8435, 8791 | `RedirectResolve`, D3D12 `SwapCopy` | partial | between families `LUMA_UNIMPLEMENTED` (counted) |
| Clears of upgraded views | `OnClearRenderTargetView` 8221 | `RedirectClear*`, D3D12 `SwapHandle`, `SwapClearUnorderedAccess` | present | |
| CPU uploads converted | `ConvertResourceData` 7321, `OnMapTextureRegion` 8003 | `WriteToMirror`, `TrackMappedMirror` | present | D3D12 uploads into mirrors not converted |
| Sampler upgrades | `CreateCustomSampler` 7048, `OnInitSampler` 7172 | `Resources/D3D11/D3D11Samplers.cpp`, D3D12 sampler descriptors | present | at creation since batch 2; Development modes 5 to 7 not ported |
| Anisotropic static samplers | n/a | none | missing | counted, `LUMA_UNIMPLEMENTED` |
| Textures from disk | `LoadTexture2DArrays` 894 | `Resources/TextureLoader.cpp` (`.dds`) | present | |
| Managed resources | `includes/managed_resources.h` | `Resources::Database` | present | |
| Chains, scaling | `enable_chain_indirect_texture_format_upgrades` 4685, `indirect_upgraded_textures_size_scaling` 376 | none | missing | Step 6 |

## Swapchain, display, UI, tools

| Feature | 1.0 | 2.0 | Status | Note |
|---|---|---|---|---|
| Swapchain upgrade | `OnCreateSwapchain` 2863, `swapchain_upgrade_type` 347 | `Swapchain/DXGI/DXGISwapchain.cpp` (Mirror, InPlace, Native) | present | |
| Fullscreen prevention | `prevent_fullscreen_state` 357, `OnSetFullscreenState` 3347 | `Swapchain` borderless policy | present | |
| HDR metadata | `swapchain_proxy.cpp` 329 | Swapchain `SetHDRMetaData` | present | |
| Display composition | `Luma_DisplayComposition` 506 | `Display/DisplayComposition.cpp`, D3D11 and D3D12 renderers | present | stages switchable (§17) |
| Monitor display info | `utils/display.inl` | `System/MonitorDisplay.cpp` | present | |
| Spoofing | `spoofing/DXGIHooks.cpp` | `Display/DXGI/DXGISpoofing.cpp` | partial | FidelityFX and system spoofing not ported |
| Settings and config | `utils/config.cpp`, `LoadConfigs` 11843 | `Settings/*`, `Luma.toml` | present | one registry |
| Overlay | `overlay/overlay.cpp`, `d3d11_renderer.cpp` | `UI/*`, D3D11 and D3D12 renderers | present | |
| Game ImGui hooks | `DrawImGuiSettings` 10792 | UI callbacks | present | |
| Trace capture | `includes/trace.hpp` | `Trace/FrameCapture.cpp`, `D3D11Capture`, `D3D12Capture` | present | binds and deferred merge not traced |
| Cbuffer tracking | `track_buffer_data` (`instance_data.h` 464) | `Trace/CbufferTracker.cpp` | present | |
| Debug draw | `CopyDebugDrawTexture` (`trace.hpp` 367) | `Trace/DebugDraw.cpp` | present | |
| MCP server | `includes/mcp_server.inl` | `Trace/McpServer.cpp`, `Trace/Mcp/*` | present | |
| ReShade | host/add-on | `ReShade/*` guest and bridge | dropped and replaced | §17 "ReShade" |
| DLSS, FSR, XeSS | `dlss/`, `fsr/`, `xess/` | none | missing | Step 7 |
| NVAPI display modes | n/a | none | missing | Step G8 |
