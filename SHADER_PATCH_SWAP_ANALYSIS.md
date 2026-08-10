# Shader Patch Swap Analysis — Runtime Original ⇄ Patched Toggling

*Analysis of the Luma shader patching mechanism and what it would take to support
conditionally applying / un-applying a patch at draw-call granularity.*

Date: 2026-08-10. Scope: `Source/Core` (patch module, pipeline cache, draw hooks) and the
FFXV recipe provider as the reference implementation.

**Revised scope (per maintainer decision):** the feature is limited to **clone-type patches**
(CLONE sync mode and the async path). INPLACE patches are not toggleable — games that need
runtime toggling must use `LUMA_PATCH_SYNC_MODE_CLONE=1` or an async provider (FFXV already
uses async). The game implementation is responsible for knowing what resources must be
bound/unbound depending on the variant it selects; core only provides the swap mechanism
and the handles to do it with.

---

## 1. Executive summary

Luma today applies shader patches in two fundamentally different ways:

- **INPLACE** (`LUMA_PATCH_SYNC_MODE_CLONE=0`, the default): the shader description is
  rewritten in `OnCreatePipeline` **before D3D compiles it**. The D3D shader object the
  game uses *is* the patched shader. There is no unpatched object anywhere — swap-back is
  impossible without creating one. **Out of scope.**
- **CLONE** (`LUMA_PATCH_SYNC_MODE_CLONE=1`, plus the async path): the original shader
  object is created untouched, and a *second* patched shader object (pipeline clone) is
  compiled and swapped in at **bind time** in `OnBindPipeline`. Both objects exist and are
  identified per pipeline (`cached_pipeline->pipeline` = original, `pipeline_clone` =
  patched). **This is the entire basis of the feature.**

With the clone-only scope, the feature is a small amount of *exposure* on top of what
already exists: give the game the clone handles in `CommandListData`, add a variant-switch
helper, and let the game decide per draw in its existing `OnDrawOrDispatch` callback. No
new objects, no core-side binding gating, no changes to provider/processed semantics.

---

## 2. How patching works today

### 2.1 Providers and modes (`Source/Core/includes/patch.hpp`)

| Provider | Macro | Runs where | Produces |
|---|---|---|---|
| Bytecode (manual) | `LUMA_PATCH_BYTECODE_SYNC/_ASYNC` | game `PatchShaderBytecodeSync/Async` | modified SHEX/SHDR bytecode |
| Recipe (DXP) | `LUMA_PATCH_RECIPE_SYNC/_ASYNC` | game `PatchShaderRecipeSync/Async` | full patched DXBC container + `dxp::RecipeReport` |

- Sync vs async is per-method and mutually exclusive per method (compile-time `#error`).
- One sync + one async can be mixed (FFXV uses bytecode-sync + recipe-async).
- **Definitive-outcome tracking**: `PatchContext::processed_shaders[method]` — a "no patch
  needed" verdict is *terminal* per (method, hash), so providers never re-run. Toggling does
  **not** interact with this (patch bytes stay cached).

### 2.2 Patch storage (`PatchContext` in `patch.hpp`)

```cpp
struct PatchContext {
   std::unordered_map<uint32_t, std::shared_ptr<PatchedShaderData>> patched_shaders; // by luma hash of ORIGINAL container
   std::array<std::unordered_set<uint32_t>, METHOD_COUNT> processed_shaders;
   std::unordered_map<uint32_t, std::shared_ptr<const std::vector<uint8_t>>> stripped_containers;
   mutable std::shared_mutex mutex;
};
```

Key identity fact: everything is keyed by the **luma hash of the original shader container**
(`Shader::BinToHash`, computed *before* patching; for in-place the hash is carried over from
`last_live_patched_shader_hash`). A decision keyed by this hash automatically covers **every
pipeline** sharing that shader (`pipeline_caches_by_shader_hash`).

### 2.3 Application paths

**INPLACE sync** (`core.hpp` ~3480 `OnCreatePipeline`): rewrites `subobjects[i].data->code`
to the patched container before D3D compiles — the only shader object is the patched one.
The original container survives only as a deep copy in `cached_pipeline->subobjects_cache`.
**Not toggleable; out of scope.**

**CLONE sync** (`core.hpp` ~3944 in `OnInitPipeline`): runs `TrySyncPatch`, then
`Patch::ClonePipelineWithPatches` compiles a second pipeline from the original subobjects
with the patch injected. Stored as `cached_pipeline->pipeline_clone`, `cloned=true`,
`patch_application_mode=SyncClone`.

**Async** (`core.hpp` ~10170): `AutoLoadShaders` worker thread → `GeneratePatchedShadersAsync`
(runs providers, stores into `patch_context`) → `ProcessAsyncCloneBatch` (compiles clones off
the render thread) → `PublishReadyAsyncClones` (registers clones at the present boundary,
with handle-identity verification). `patch_application_mode=AsyncClone`.

`LoadCustomShaders` (~2325) is the shared clone-apply routine (file-based custom shaders and
patch re-clones). Its clone lambda resolves **file first, then patch** — a file shader
shadows a patch on that clone (see limitation 4).

### 2.4 The bind-time swap (`OnBindPipeline`, core.hpp ~4670)

In DX11 a "pipeline" is a single shader object; `bind_pipeline` fires per stage.

```cpp
// game bound the ORIGINAL handle:
cached_pipeline = pipeline_cache_by_pipeline_handle[pipeline]   // or clone map when ENABLE_GAME_PIPELINE_STATE_READBACK
...
cmd_list_data.pipeline_state_original_*_shader = pipeline;      // original handle tracked here
cmd_list_data.pipeline_state_has_custom_* = cached_pipeline->cloned;
...
if (cached_pipeline->cloned && custom_shaders_enabled)
   cmd_list->bind_pipeline(stages, cached_pipeline->pipeline_clone);  // THE swap
```

Facts that matter for the feature:
- `pipeline_state_original_*` keeps the **game's** handle even though the bound object is the
  clone — every draw hook therefore sees original hashes. Good: hash-based conditionals work.
- The **clone handle is currently not exposed anywhere** outside `CachedPipeline`; the game
  callback receives neither the cmd_list nor the CachedPipeline — only
  `cmd_list_data.pipeline_state_*` (original handles) and `pipeline_state_has_custom_*`
  (bool). **This is the main gap.**
- `cmd_list->bind_pipeline` (ReShade API) calls the original trampoline directly and does
  **not** re-dispatch addon events → no recursion; a draw-time rebind will not corrupt
  `pipeline_state_*`. Native context calls (`native_device_context->PSSetShader` etc.) have
  the same property and are already used by the DEV draw-override machinery (purple/NaN
  shader swaps) — a working precedent for draw-time variant swapping.
- `pipeline_state_has_custom_*` derives from `cached_pipeline->cloned`, i.e. "has a clone",
  not "clone is currently bound". Keep this meaning; the *active variant* is tracked
  separately (new field).
- If the game re-binds the **clone** itself (state readback mods, DX:HR case), the clone map
  path finds the `CachedPipeline` and re-swaps to the clone (no-op).

### 2.5 The per-draw hook

`Game::OnDrawOrDispatch` (game.h) is called from `OnDrawOrDispatch_Custom` (core.hpp 6046)
before every draw/dispatch when `ENABLE_POST_DRAW_DISPATCH_CALLBACK` (or DEVELOPMENT). It
receives `native_device_context`, `cmd_list_data`, the **original** shader hashes, and can
cancel/replace the draw (`DrawOrDispatchOverrideType`). FFXV uses it to bind recipe-introduced
resources every draw of a patched shader (`BindPatchedResources`, main.cpp ~143): the fast
noise SRV at the recipe's new bind point and a frame-constants cbuffer (with `UpdatePatchedCB`,
so `updated_cbuffers=true`).

This callback is the decision point: it already runs per draw with full context (hashes,
RTVs, viewport, cbuffer contents, frame state — everything a game needs for runtime
conditionals).

---

## 3. Design: variant swap for clone-type patches

### 3.1 Model

Every `CachedPipeline` in clone modes already holds both variants:

```
cached_pipeline->pipeline        // original D3D object (the game's)
cached_pipeline->pipeline_clone  // patched D3D object (ours)
```

The feature adds:

1. **Exposure**: per-stage clone handles in `CommandListData`, populated in
   `OnBindPipeline` (zero when the pipeline has no clone).
2. **Active-variant tracker**: per-command-list enum ("original / patched") so a draw-time
   decision can be a cheap compare instead of a blind rebind every draw.
3. **A switch helper** the game calls from `OnDrawOrDispatch`.
4. **Optional coarse default**: a per-hash "patch enabled" flag consulted at bind time so
   games/UI can set the *default* variant; the per-draw decision overrides it.

### 3.2 Decision flow per draw

```
engine binds shader            → OnBindPipeline: record both handles,
                                 swap to default variant (clone if patch enabled)
draw fires                     → OnDrawOrDispatch (game):
    condition = <runtime info: RTV format, viewport, cbuffer, frame, ...>
    variant   = condition ? Patched : Original
    Patch::EnsureShaderVariant(native_ctx, cmd_list_data, stages, variant)  // compare + rebind on mismatch
    if (variant == Patched)  BindPatchedResources(...);                      // game's responsibility
    else                     <optional: unbind recipe resources>             // game's responsibility
    return None;              // let the draw proceed
```

Key property: the game must call `EnsureShaderVariant` **on every draw** of a toggled shader
(not only when its condition changes), because the engine rebinds shaders only on state
change and may bind something else in between draws. The helper makes this cheap: one enum
compare; the native bind happens only on an actual variant mismatch.

### 3.3 Why this is safe (no event recursion, no tracking corruption)

- Native rebinds (`VSSetShader`/`PSSetShader`/`CSSetShader` on `native_device_context`)
  bypass ReShade's event dispatch — no re-entry into `OnBindPipeline`, and
  `pipeline_state_original_*` (the identity used by all core logic and by the game's own
  hash checks) stays intact.
- Draws themselves are executed through the native context by the existing hooks, so the
  rebind and the draw are on the same context, immediately adjacent.
- Precedent: the DEV draw-override machinery (`replace_draw_type` purple/NaN/skip) already
  swaps native shader state per draw this way.

---

## 4. Suggested changes (general)

### 4.1 Core — `instance_data.h` / `shaders.h`

```cpp
// CommandListData — new fields (mirroring the existing pipeline_state_* pattern):
reshade::api::pipeline pipeline_state_patched_compute_shader = reshade::api::pipeline(0);
reshade::api::pipeline pipeline_state_patched_vertex_shader  = reshade::api::pipeline(0);
reshade::api::pipeline pipeline_state_patched_pixel_shader   = reshade::api::pipeline(0);
// Which variant is currently bound (per stage bit or one value; DX11 binds one stage at a time):
Shader::PatchVariant pipeline_state_active_patch_variant = Shader::PatchVariant::None;
```

```cpp
// Shader namespace (shaders.h or patch.hpp):
enum class PatchVariant : uint8_t { None = 0, Original, Patched };
```

### 4.2 Core — `core.hpp` `OnBindPipeline`

- Populate the `pipeline_state_patched_*_shader` handles from
  `cached_pipeline->pipeline_clone` (zero when `!cloned`).
- Keep the default swap, but route it through a single resolution function so the optional
  coarse per-hash toggle and the readback path agree:

```cpp
// Default variant for bind time (coarse; per-draw decisions override it):
PatchVariant ResolveBindTimeVariant(cached_pipeline);
//   Patched  if cloned && custom_shaders_enabled && Patch::IsPatchEnabled(hash)
//   Original otherwise
```

### 4.3 Core — new public helper (draw-time)

```cpp
namespace Patch {
   // Sets the active variant for the given stages on the native context.
   // No-op when the tracked active variant already matches.
   // Never dispatches ReShade events; does not touch pipeline_state_original_*.
   void EnsureShaderVariant(ID3D11DeviceContext* native_device_context,
                            CommandListData& cmd_list_data,
                            reshade::api::shader_stage stages,
                            PatchVariant variant);
}
```

Implementation: for each stage bit, bind `pipeline_state_original_*` or
`pipeline_state_patched_*` via the native context and update
`pipeline_state_active_patch_variant`. (Native `*SetShader` calls, mirroring the existing
DEV overrides; alternatively the bind-time path can keep using `cmd_list->bind_pipeline`.)

Optional coarse toggle (UI/dev tooling):

```cpp
// In PatchContext: bool patch_enabled_by_default per hash (default true).
bool IsPatchEnabled(uint32_t shader_hash) const;   // shared lock
bool SetPatchEnabled(uint32_t shader_hash, bool);  // unique lock
```

### 4.4 Core — no per-draw work required

`OnDrawOrDispatch_Custom` needs **no changes** for the base feature: the game's existing
`OnDrawOrDispatch` override performs the decision + `EnsureShaderVariant` call. (A future
core-level default could be added, but it isn't necessary.)

### 4.5 Game (FFXV as reference)

In `OnDrawOrDispatch`, gate on the same condition as the variant:

```cpp
const bool use_patch = <runtime condition>;
Patch::EnsureShaderVariant(native_device_context, cmd_list_data, stages,
                           use_patch ? Patch::PatchVariant::Patched : Patch::PatchVariant::Original);
if (use_patch)
   BindPatchedResources(native_device_context, device_data, original_shader_hashes, stages, updated_cbuffers);
// else: recipe SRV/cbuffer slots are left bound; original shaders don't read them.
//       Unbinding is the game's call if it knows a slot is reused.
```

### 4.6 Lifecycle / cleanup

- Nothing new to destroy: `EnsureShaderVariant` only ever binds objects whose lifetime is
  already managed (`pipeline` by the game, `pipeline_clone` by `OnDestroyPipeline`/
  `ClearCustomShader`). The active-variant tracker is per-command-list and dies with it.
- The coarse toggle lives in `patch_context` (outlives clones) and must be reset if a
  patch-context clear path is ever added.
- Clones may be re-created/reloaded (`LoadCustomShaders`, async publish) — handles are
  re-read from `CachedPipeline` at each bind, so this is transparent.

---

## 5. Limitations (clone-only scope)

1. **INPLACE patches cannot toggle.** Games needing runtime control must use CLONE sync or
   async providers. Document this on the provider macros (`patch.hpp`). FFXV is unaffected
   (async). This is an accepted limitation.

2. **Per-draw cost.** Every draw of a toggled shader pays one enum compare; the native bind
   happens only on variant change. If the game decides to bind every draw unconditionally
   instead of using the tracker, it pays one native call per draw — measurable on hot
   shaders; use `EnsureShaderVariant`.

3. **Variant re-assertion.** Because the engine binds shaders only on state change, the game
   must run the decision + `EnsureShaderVariant` on **every** draw of a toggled shader. A
   decision made "only when the condition changed" silently sticks to the wrong variant
   after the engine rebinds (or another mod does).

4. **Clone-content ambiguity (file vs patch).** `LoadCustomShaders`' clone lambda prefers a
   custom shader *file* over a stored patch. If a file shadows a patch, the clone is the
   file's shader, not the patch's — toggling to "original" then skips the file too. Either
   define the toggle as "restore the game's original object" (what the mechanism does) and
   let the game decide whether that's acceptable, or record what the clone was built from
   (new `CachedPipeline` field) and no-op the toggle on file-shadowed clones.

5. **Resource bindings are the game's responsibility** (accepted). When the original variant
   runs, recipe-introduced SRV/cbuffer slots remain bound. Unused slots are harmless;
   a slot the original *does* read (or that gets reused by a later pass before rebinding)
   can change behavior — the game must know its recipe's bind points (FFXV already tracks
   them in `dxp_binding_sets`).

6. **`is_custom_pass` / `pipeline_state_has_custom_*` semantics unchanged.** They mean "has a
   clone" (pass identity), not "patch is currently active". Games must base per-draw logic
   on hashes + their own condition, not on these flags.

7. **Native-bind state staleness.** Rebinding via the native context bypasses ReShade's
   internal state tracking. This matches the existing DEV override precedent and works in
   practice, but any future ReShade-side state restoration (its own passes) runs with our
   variant in place — validate on the game's immediate context if anything regresses.

8. **State readback interop.** With `ENABLE_GAME_PIPELINE_STATE_READBACK`, other mods may
   re-bind the clone themselves. `OnBindPipeline`'s clone-map path must resolve the default
   variant with the same `ResolveBindTimeVariant` so readback and draw-time decisions agree.

9. **Deferred contexts.** Draw-time rebinds on a deferred context execute at
   `ExecuteCommandList`; `pipeline_state_*` tracking happens on the game's bind calls. The
   existing DEVELOPMENT asserts already warn about inherited-state assumptions — treat
   deferred rendering as unsupported for this feature.

10. **Per-stage granularity in DX11.** VS/PS/CS are separate pipelines with separate hashes;
    a "pass" toggle applies per stage. Games writing conditionals must do so per stage
    (the `stages` parameter of `OnDrawOrDispatch`).

11. **Toggle timing.** Per-draw decisions take effect at the next draw. The coarse
    bind-time default (`SetPatchEnabled`) takes effect at the next *bind*. Neither is
    retroactive mid-frame; document in UI.

12. **DX11-only.** `OneShaderPerPipeline=true` is baked into the design; a DX12 port needs
    the multi-hash `shader_hashes` paths handled in variant resolution.

13. **Provider/processed semantics are unaffected — good.** Toggling never re-runs providers
    and never clears `processed_shaders`; patch bytes stay cached, so off→on is instant.

---

## 6. Implementation sketch (task list)

1. `shaders.h`/`patch.hpp`: `PatchVariant` enum; `PatchContext::patch_enabled_by_default` +
   `IsPatchEnabled`/`SetPatchEnabled` (reuse existing `mutex`).
2. `instance_data.h`: `pipeline_state_patched_*_shader` ×3 + `pipeline_state_active_patch_variant`.
3. `core.hpp` `OnBindPipeline`: populate patched handles; route default swap through
   `ResolveBindTimeVariant` (honors `custom_shaders_enabled` + `IsPatchEnabled`); update the
   active-variant tracker on every bind (original or clone) including the readback path.
4. `core.hpp`: `Patch::EnsureShaderVariant(...)` (native rebind + tracker compare/update).
5. FFXV `OnDrawOrDispatch`: condition → `EnsureShaderVariant` + gate `BindPatchedResources`
   on the same condition.
6. DEV UI: per-hash "patch default" checkbox (bind-time) + log; frame-capture view shows the
   active variant.
7. Tests: INPLACE build compiles unchanged (feature inert); CLONE/async build toggles per
   draw; file-shadowed clones behave per decided semantics (limitation 4); reload keeps
   handles and toggles valid.

---

## 7. Conclusion

With the clone-only scope the feature is small and low-risk: both variants already exist for
CLONE-sync and async patches, the per-draw callback already runs with full context, and the
native-rebind pattern already has precedent in the DEV draw overrides. The work is:
expose clone handles per stage in `CommandListData`, add a tracker + `EnsureShaderVariant`
helper, route the bind-time default through an optional per-hash toggle, and let games make
the per-draw decision in their existing `OnDrawOrDispatch` — with resource binding
responsibility staying in the game, as decided. INPLACE mode stays the cheap always-on path
and is documented as non-toggleable.
