# Mirror memory: spoofed small originals or virtual originals

Every upgraded texture is held twice today: the game's original, at its size and format, and Luma's FP16 mirror (`Resources/D3D11/D3D11Mirrors.cpp`, `Resources/D3D12/D3D12Mirrors.cpp`: a second `CreateTexture*` or `CreateCommittedResource` after the game's). While mirrors are swapped in, the original only stands for the game's handle. This compares two ways of not paying for it, from Microsoft's documentation and field reports; nothing here is measured yet.

## The two approaches

* **A. Small original, spoofed description.** The original is created at the smallest size its mip chain allows (1x1 for a single-mip render target; 2^(mips-1) per side otherwise, with the same format, array size and sample count). The game is told the size it asked for: `GetDesc`/`GetDesc1` of `ID3D11Texture2D(1)`, `IDXGISurface::GetDesc`, and `ID3D12Resource::GetDesc`/`GetDesc1` (returned by value) are replaced, on a private copy of the vtable of only those objects, or on the shared vtable with a cheap "is it one of ours" check.
* **B. Virtual original.** The original is created with its real description as a tiled resource (D3D11.2, `D3D11_RESOURCE_MISC_TILED`) or a reserved resource (D3D12, `CreateReservedResource`, "virtual memory only, no backing store"), with no memory behind it. Tier 1 hardware maps every tile to one shared 64 KB default page; Tier 2 and up leave them unmapped.

## What decides it: what happens when Luma misses a path

Luma redirects the game's binds, copies, clears and CPU writes from an original to its mirror. A path it doesn't catch (a copy kind not handled, a third-party library, a new hook not written yet) still reaches the original.

* **A:** the runtime and the driver keep the real, small description; only callers of `GetDesc` see the spoofed one. A missed copy with the game's box, an `UpdateSubresource` region, a D3D12 `CopyTextureRegion` with footprints from the game's (spoofed) description, or a view of a mip the small original lacks, is out of bounds: a debug-layer error or a dropped call on D3D11, undefined and possibly device removal on D3D12. Today the same miss reads or writes a stale but valid texture.
* **B:** the description is the real one, so every call is valid. On Tier 2 and up, "reading from NULL-mapped tiles treat that sampled value as zero. Writes to NULL-mapped tiles are discarded." On Tier 1, accessing NULL-mapped tiles is "undefined behavior, which includes device-removed", hence the shared default page, which the documentation names as the remedy ("map all tiles to a single default tile"). A miss is a wrong or black image, not a crash.

That difference is the main reason to prefer B.

## Comparison

| | A. Small original, spoofed desc | B. Virtual original |
|---|---|---|
| Memory saved | the original, but its mip chain's smallest size | the whole original (one shared 64 KB page on Tier 1) |
| A missed path | out of bounds: debug-layer error, dropped call, possible device removal | valid call; zero reads and discarded writes (Tier 2+), the default page's contents (Tier 1) |
| What the game sees | the size it asked for, from every patched `GetDesc` (several interfaces per object, D3D12's by-value `GetDesc`) | its own size and format; but D3D11 `MiscFlags` gains `TILED`, D3D12's layout likely becomes `64KB_UNDEFINED_SWIZZLE` (field reports; Microsoft's page doesn't say), and `ID3D12Resource::GetHeapProperties` fails with `E_INVALIDARG` on reserved resources |
| Hooks needed | `GetDesc` family on textures (per-object vtable or shared with a check) | none on the hot path; at creation, the original's flags; a desc mask only where a game reads them (below) |
| Where it can't be used | nothing extra, but every hook must be complete | D3D11: Texture1D, Texture3D (Tier 3 only, and one page says not at all), shared resources (`_SHARED*`, `_GDI_COMPATIBLE`), non-DEFAULT usage; MSAA and some formats "with some exceptions" (check `FORMAT_SUPPORT2_TILED` and the tiled MSAA quality flag per device; field reports of depth-with-stencil refused). D3D12: the same checks. Falls back to a full original |
| Live toggle (`Luma.Enabled`, upgrades off) | impossible without recreating the original | map memory into the original on demand (`UpdateTileMappings`), one hitch |
| Mirrors made later (chains, seeds) | no: the original held the game's data | no, same reason; only creation-time mirrors benefit |
| D3D12 placed resources | no saving (the game's heap) | no saving (the game's heap) |
| Per-call cost | the `GetDesc` hook | none; creation only |
| RenderDoc | it records the spoofed or the real desc, either way inconsistent at replay | tiled resources in captures are a known weak spot in tools (a Nsight report on D3D11 tiled buffers); PIX supports reserved resources |
| ReShade | its add-on API reads the spoofed desc, consistent; its own work is on the real swapchain buffers | sees a tiled or reserved resource, real description |
| Documented behaviour | no: replacing a driver object's vtable pointer is undocumented | yes: tiled resources since D3D11.2, reserved resources in D3D12 |

## What B still has to hide

* **D3D11 `TILED` in the game's `GetDesc`.** A game that clones a texture from `GetDesc` would create a tiled texture with no memory. Luma hooks `CreateTexture*` already: it strips `TILED` from a game's request when the game has created no tile pool (`D3D11_RESOURCE_MISC_TILE_POOL`), and the clone then goes through the normal upgrade rules.
* **D3D12 layout and `GetHeapProperties`.** If reserved textures do need the 64 KB layout (to confirm in step 1 below), the same strip at creation for it. `GetHeapProperties` on a virtual original needs an answer (the committed heap the game asked for): a shared-vtable hook with an "is it ours" check, or nothing if no game calls it on a render target. To find out before deciding.
* **Initial data.** The documentation doesn't say whether a tiled texture takes initial data; Luma gives the game's data to the mirror (converted) and the original needs none.

## Recommendation

**B, virtual originals, with a fallback to today's full original.** It saves more, a miss stays a valid call, it uses documented API, and it costs nothing per call. The small amount of hiding it needs is at creation, where Luma already hooks. A replaces documented behaviour with an undocumented vtable change and turns every gap in Luma's redirection into a possible device removal.

Decisions: B only where mirrors are made at creation; chain and seed mirrors keep a real original. Swapchain game buffers in Mirror use B too. The live upgrade toggle maps memory on demand.

## Research step (before building)

1. Query, on the owner's GPUs and both APIs: the tiled tier, `FORMAT_SUPPORT2_TILED` for every format FF7R's rules upgrade, tiled MSAA quality levels, depth formats.
2. Prototype B for FF7R's creation-time mirrors on D3D12 (committed render targets only), Development only, behind a setting.
3. Measure: VRAM before and after (the Info tab, `luma_status`), and count misses (a debug counter on reads from a virtual original that has no mirror swapped in).
4. Check that `GetDesc` of the original is unchanged on both APIs, that no clone of a virtual original is created without memory, and whether FF7R ever calls `GetHeapProperties` on one.
5. Captures in PIX and RenderDoc with the prototype on, noting what fails (fixing tools is later).

## Sources

* [Tiled resource creation parameters (D3D11)](https://learn.microsoft.com/en-us/windows/win32/direct3d11/tiled-resource-creation-parameters)
* [D3D11_TILED_RESOURCES_TIER](https://learn.microsoft.com/en-us/windows/desktop/api/D3D11/ne-d3d11-d3d11_tiled_resources_tier)
* [Tiled resources features tiers (D3D11)](https://learn.microsoft.com/en-us/windows/win32/direct3d11/tiled-resources-features-tiers)
* [ID3D12Device::CreateReservedResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createreservedresource)
* [D3D12_TILED_RESOURCES_TIER](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_tiled_resources_tier)
* [ID3D12Resource::GetHeapProperties](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-getheapproperties)
* [D3D12 Resource Heaps spec](https://microsoft.github.io/DirectX-Specs/d3d/ResourceHeaps.html)
* [PIX: how GPU captures support placed and reserved resources](https://devblogs.microsoft.com/pix/gpu-captures-how-we-support-placed-and-reserved-resources/)
* [Nsight issue with D3D11 tiled resources](https://forums.developer.nvidia.com/t/nsight-2020-4-issues-with-dx-11-tiled-resources/154056)
* Field reports on format support for reserved resources: [rojo-rhi #92](https://github.com/AmanThuL/rojo-rhi/pull/92), [xcl #951](https://github.com/ivall/xcl/pull/951)
