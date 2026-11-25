# Logic deviations: 1.0 against 2.0

Behaviour that differs for the same feature. Verdict: **intended** (decided, Guidelines section 17 or Plan notes), **to check** (plausible, undocumented: the owner confirms), **likely bug**. Line numbers are 1.0's `core.hpp`.

## D3D11

| Area | 1.0 | 2.0 | Files | Verdict |
|---|---|---|---|---|
| Shader clones | one per game object, made lazily at bind (`OnBindPipeline` 4323), recreated on reload | one per hash and device, made in `PreCreateShader` inside the game's creation call, kept across reloads unless the bytecode changes | `Shaders/D3D11/D3D11Variants.cpp` | intended (§17 "Variant objects"; G6) |
| Clones made after creation | n/a | a clone published by a later build is created at a present, outside any state the game armed (NvAPI extension slot) | `D3D11Variants.cpp` `PublishVariants` | to check (the late-clone limit) |
| Shader creation callback | `PreCreate*Shader` could edit the bytecode | read-only `on_create_shader` after tracking | `Handlers/D3D11/D3D11Handlers.cpp` | intended (§17) |
| Luma cbuffers at a draw | every stage of a draw with a clone, device-wide dirty flag | stages running a variant, per-context stamps | `Resources/D3D11/D3D11LumaCBuffers.cpp` | intended (§17) |
| Cbuffer restoration | all 14 slots of every stage | Luma's two slots | `D3D11LumaCBuffers.cpp` | intended (§17) |
| Draw callbacks by hash | all listed stages must match | any listed hash on any stage | `Game/D3D11/D3D11DrawEvents.cpp` | intended (§17) |
| Sampler mirrors | made at `OnInitSampler` 7172 for the current offset, all made again when the offset changes | made at creation; for a new offset at the next present, binds keep the old mirror until then | `Resources/D3D11/D3D11Samplers.cpp` | intended (G6) |
| Sampler mirror desc | Publishing: modes 3 to 5; a mirror even when nothing changes | mode 4 only; no mirror when the desc would be equal | `Resources/SamplerMirrors.hpp` `GetMirrorDesc` | intended (Step 4 notes) |
| Copy compatibility | R11G11B10 and R9G9B9E5 counted compatible | same typeless family or two 8 bit RGBA/BGRA | `Utils/DXGI/DXGIFormat.cpp` | intended (§17) |
| Region copies between formats | converted (`OnCopyResource_Internal` 8304) | converted only for a whole single subresource, else asserts and copies the originals | `Resources/D3D11/D3D11Mirrors.cpp` | to check |
| Resolve between families | resolved through a converted temporary (8435) | `LUMA_UNIMPLEMENTED`, counted (`unimplemented.resolves_between_formats`) | `D3D11Mirrors.cpp` `RedirectResolve` | to check (Persona and Prey candidates) |
| CPU uploads into mirrors | 8 bit RGBA/BGRA to RGBA16F | also R10G10B10A2 and R11G11B10, pitches kept | `Utils/DXGI/DXGIFormat.cpp` | intended (§17) |
| `Map` of upgraded textures | handled per region (8003) | not handled: matched textures are GPU-only | `D3D11Mirrors.cpp` | to check (a game mapping a staging-like RT) |
| Writes while upgrades are off | n/a (no runtime toggle) | render targets written while off leave a stale mirror until rewritten | `D3D11Mirrors.cpp` | intended (Step 4 B) |
| Stream-output geometry shaders | clones made | never replaced, counted | `D3D11Variants.cpp` | to check |

## DXGI and swapchain

| Area | 1.0 | 2.0 | Files | Verdict |
|---|---|---|---|---|
| Swapchain ownership | upgraded in place (`OnCreateSwapchain` 2863) | Mirror by default, InPlace and Native per game | `Swapchain/DXGI/DXGISwapchain.cpp` | intended (§17) |
| HDR default | scRGB in Development | HDR10, scRGB selectable | `Swapchain/Swapchain.cpp` | intended (§17) |
| Latency and tearing flags | waitable object added | game's flag kept, tearing flag added at sync 0 | `DXGISwapchain.cpp` | intended (§17) |
| Composition input | each game's output shader encoded | linear in, final stage encodes | `Display/DisplayComposition.cpp` | intended (§17) |

## Shaders, settings, UI, the rest

| Area | 1.0 | 2.0 | Files | Verdict |
|---|---|---|---|---|
| Compile errors | shader code cleared, original runs | last good bytecode stays | `Shaders/Shaders.cpp` | intended (§17) |
| Boot compilation | blocks shader creation | jobs, published at a present | `System/JobQueue.cpp` | intended (§17) |
| Patch cache | re-scanned each boot | cached with an index of non-matches | `Shaders/ShaderPatching.cpp` | intended (§17) |
| Recipe `upgrade_to_sm51` | n/a | the recipe decides; Development and Test report one that puts Luma's bindings below SM5.1 or outside its space | `Shaders/ShaderInvariants.cpp` | intended (batch 2) |
| Define keys | by name in the config | `Defines.<NAME>` | `Shaders/ShaderDefines.cpp` | intended (§17) |
| FXC optimisation | /O0 commented as /O3 | /O0 kept for goldens | `Shaders/ShaderCompiler.cpp` | intended, idea to switch |
| Sliders | live | on release | `UI/Widgets.cpp` | intended (§17) |
| Focus loss | Development define | setting | `System/WindowPolicy.cpp` | intended (§17) |

## D3D12 (2.0 only, design choices to watch)

| Area | Behaviour | Files | Verdict |
|---|---|---|---|
| Converting copies | drawn on the game's list with its graphics root signature; assumes COPY_SOURCE/COPY_DEST, a texture promoted from COMMON by the copy isn't handled | `Resources/D3D12/D3D12Mirrors.cpp` `ConvertingCopy` | to check (debug layer) |
| Static samplers | opt-in per game | `Shaders/D3D12/D3D12RootSignatures.cpp` | intended (batch 2) |
| Heap tail | full, then fixed section, then none | `Resources/D3D12/D3D12HeapTail.cpp` | intended (batch 2) |
| `UseOriginal` | reverts the whole PSO | `Shaders/D3D12/D3D12Variants.cpp` | intended (§17) |
