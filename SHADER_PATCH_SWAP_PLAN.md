# Implementation Plan — Runtime Patch Toggle (Original ⇄ Patched per draw)

**Status: IMPLEMENTED** (2026-08-10). All phases landed; build matrix validated:
Core (no providers), FFXV Development-Debug + Development-Release (bytecode-sync +
recipe-async), DX:HR Development-Debug (INPLACE legacy). Heavy Rain fails to build on
`main.cpp` with C1128 (missing `/bigobj` in its project) — verified pre-existing by
building with these changes stashed.

Scope: **clone-type patches only** (CLONE sync + async). INPLACE patches stay always-on and
are documented as non-toggleable. Default behavior: **patch ON**; games selectively disable
it per draw at runtime. The game is responsible for resource bindings of the variant it
selects. Frame capture (DEVELOPMENT) must show whether a patched shader ran disabled
(original) for each captured draw.

---

## A. API surface (what game code will use)

```cpp
// Shader namespace — shaders.h
enum class PatchVariant : uint8_t { None = 0, Original = 1, Patched = 2 };

// CommandListData — instance_data.h. The raw fields below are INTERNAL plumbing:
// set by core at bind time, read by the methods. Games should use the methods,
// not the fields (fields stay public only because CommandListData is a plain
// data struct and the codebase convention is public state + helper methods).
//   pipeline_state_patched_compute_shader / _vertex_shader / _pixel_shader  — clone handles, 0 = none
//   patch_variant_compute / _vertex / _pixel                                — active-variant tracker

// CommandListData methods — instance_data.cpp. This is the game-facing API.
// Ownership split: the per-context/per-bind state and operations live on
// CommandListData (handles are only valid for the CURRENT bind on THIS
// context); per-hash persistent settings live on PatchContext (global, survives
// binds/reloads). PatchContext has no concept of a bound pipeline, so the
// variant API cannot live there.

// (1) APPLY — the 95% case. Idempotent: rebind the requested variant on the
// native context only if the tracked active variant differs (see rules in B3).
// SELF-GUARDING: no-op for stages without a toggleable clone (INPLACE patches,
// async-pending window) — the game does NOT need to check availability first.
// Returns true when the requested variant is now bound (or already was);
// false when it could not be applied (no toggleable clone / no original).
// "Unavailable" is a NORMAL state, not an error (see rules) — most callers
// ignore the return; pre-check IsPatchToggleable when the logic branches on it.
bool CommandListData::EnsureShaderVariant(ID3D11DeviceContext* native_device_context,
                                          reshade::api::shader_stage stages,
                                          Shader::PatchVariant variant);

// (2) QUERY — semantic availability, for when game logic branches on it
// ("can I toggle this pass right now?"). True iff a clone is applied to the
// currently bound pipeline for this stage. Wraps the raw handle check.
bool CommandListData::IsPatchToggleable(reshade::api::shader_stage stage) const;

// (3) QUERY — which variant is actually bound for this stage right now
// (None = no patch involved). Useful for state-dependent logic and debugging.
Shader::PatchVariant CommandListData::GetActivePatchVariant(reshade::api::shader_stage stage) const;

// (4) ESCAPE HATCH — for Replaced draws / binding both variants inside one
// callback. Pure getter, no side effects: Original -> pipeline_state_original_*,
// Patched -> pipeline_state_patched_*, **0 when unavailable** (codebase-wide
// "no pipeline" sentinel; the only failure mode is "unavailable", so 0 encodes
// it unambiguously — no std::expected wrapper needed). Games bind the handle
// themselves (cast to the native shader type, as the codebase does).
reshade::api::pipeline CommandListData::GetPipelineVariant(reshade::api::shader_stage stage,
                                                            Shader::PatchVariant variant) const;

// PatchContext — patch.hpp (per-hash, global, persistent)
bool HasPatch(uint32_t shader_hash) const;      // patch bytes stored (core plumbing semantics)
bool IsPatchEnabled(uint32_t shader_hash) const;   // default true; bind-time default only
bool SetPatchEnabled(uint32_t shader_hash, bool);  // coarse UI/default toggle
```

> **Semantics — "patch exists" vs "patch toggleable":**
> - `PatchContext::HasPatch(hash)` = **patch bytes stored** (per hash, global). True even when
>   no clone is bound for the current pass: async-pending window, after `UnloadCustomShaders`,
>   and permanently for INPLACE patches (no clone ever exists). Used by core plumbing
>   (queue gate, self-heal of stored-but-uncloned pipelines) — **do not change its meaning.**
> - `IsPatchToggleable(stage)` = **toggleable clone available for the bound pass** (per
>   command list). True implies a patch exists; this is the game-facing "can I toggle this
>   pass" check. False for INPLACE patches — toggling no-ops.
> - `EnsureShaderVariant(Patched)` with no toggleable clone is a no-op (never binds null),
>   so the common path needs no availability check at all.

Game draw pattern (in existing `Game::OnDrawOrDispatch`) — the common path needs NO
availability check; `EnsureShaderVariant` self-guards:

```cpp
// Optional: only branch on availability when your logic actually depends on it
// (e.g. "patch this pass only when it can actually be toggled"):
// const bool toggleable = cmd_list_data.IsPatchToggleable(reshade::api::shader_stage::pixel);

const bool use_patch = <runtime condition>; // default true — patch stays on
cmd_list_data.EnsureShaderVariant(native_device_context, stages,
                                  use_patch ? Shader::PatchVariant::Patched
                                            : Shader::PatchVariant::Original);
if (use_patch) { BindPatchedResources(...); }  // game's responsibility, gated on same condition
```

Must be called on **every** draw of a toggled shader (engine binds only on state change).
`EnsureShaderVariant` is idempotent: one enum compare per stage; native rebind only on
mismatch.

Manual variant binding (for `DrawOrDispatchOverrideType::Replaced` draws, or binding
both variants inside one callback) — the escape hatch; only needed when the game drives
the draws itself:

```cpp
// Return Replaced and drive the draws yourself — get either handle, bind when you want it:
const reshade::api::pipeline original = cmd_list_data.GetPipelineVariant(reshade::api::shader_stage::pixel,
                                                                         Shader::PatchVariant::Original);
const reshade::api::pipeline patched   = cmd_list_data.GetPipelineVariant(reshade::api::shader_stage::pixel,
                                                                         Shader::PatchVariant::Patched);

// e.g. scene with the original, then an extra custom pass with the patch:
native_device_context->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(original.handle), nullptr, 0);
if (original_draw_dispatch_func) (*original_draw_dispatch_func)();  // original pass
native_device_context->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(patched.handle), nullptr, 0);
<draw your custom pass>;
return DrawOrDispatchOverrideType::Replaced; // you already ran the draw(s)
```

Caveat: after manual binds, the per-stage tracker (`patch_variant_*`, queried via
`GetActivePatchVariant`) reflects the LAST `EnsureShaderVariant` call, not the manual
bind. Either call `EnsureShaderVariant` after manual binding to resync the tracker, or
treat manual binds as the escape hatch and keep the tracker best-effort (it only affects
redundant-rebind avoidance and the frame-capture stamp, both of which re-derive from the
tracker on the next bind/draw).

---

## B. Explicit change list

### B1. `Source/Core/includes/shaders.h` — new enum

After `PatchApplicationMode` (~line 42):

```cpp
// Which variant of a patched pipeline is (or should be) bound. None = no patch involved.
enum class PatchVariant : uint8_t
{
   None = 0,
   Original = 1,  // the game's original object (patch disabled)
   Patched = 2,   // the pipeline clone (patch applied)
};
```

### B2. `Source/Core/includes/patch.hpp` — PatchContext toggle state

In `PatchContext` (next to `patched_shaders`):

```cpp
// Coarse bind-time default per shader: absent = enabled (patch on). Per-draw decisions
// in EnsureShaderVariant override this. Never re-runs providers / clears processed.
std::unordered_map<uint32_t, bool> patch_enabled_by_default;

bool IsPatchEnabled(uint32_t shader_hash) const
{
   const std::shared_lock lock(mutex);
   auto it = patch_enabled_by_default.find(shader_hash);
   return it == patch_enabled_by_default.end() || it->second;
}
bool SetPatchEnabled(uint32_t shader_hash, bool enabled)
{
   const std::unique_lock lock(mutex);
   auto [it, inserted] = patch_enabled_by_default.try_emplace(shader_hash, enabled);
   if (!inserted && it->second == enabled) return false;
   it->second = enabled;
   return true;
}
// Reset all toggles (to be called by any future patch-context clear path; none exists yet)
void ResetPatchToggles() { const std::unique_lock lock(mutex); patch_enabled_by_default.clear(); }
```

### B3. `Source/Core/includes/instance_data.h` — CommandListData + TraceDrawCallData

CommandListData, next to `pipeline_state_original_*` (~line 243):

```cpp
// Clone (patched) handles for the currently bound pipelines; 0 when the pipeline has no
// clone. Populated in OnBindPipeline. Read-only afterwards (lifetime owned by CachedPipeline).
reshade::api::pipeline pipeline_state_patched_compute_shader = reshade::api::pipeline(0);
reshade::api::pipeline pipeline_state_patched_vertex_shader  = reshade::api::pipeline(0);
reshade::api::pipeline pipeline_state_patched_pixel_shader   = reshade::api::pipeline(0);
// Which variant is actually bound per stage (None = no patch involved). Updated by
// OnBindPipeline and EnsureShaderVariant. Never affects pipeline_state_original_* /
// pipeline_state_has_custom_* semantics ("has a clone", not "patch active").
Shader::PatchVariant patch_variant_compute = Shader::PatchVariant::None;
Shader::PatchVariant patch_variant_vertex  = Shader::PatchVariant::None;
Shader::PatchVariant patch_variant_pixel   = Shader::PatchVariant::None;
```

Method declaration in `struct CommandListData` + definition in `instance_data.cpp`:

```cpp
// Idempotent per-stage variant switch on the native context. Rules:
//  - None        -> no-op, returns false
//  - Original    -> bind pipeline_state_original_<stage>, unless already that variant
//  - Patched     -> bind pipeline_state_patched_<stage>, unless already that variant
//  - target handle == 0 -> no-op for that stage (never binds null / unbinds)
//  - Uses native *SetShader only; no ReShade event dispatch; does not touch
//    pipeline_state_original_* or pipeline_state_has_custom_*.
//  - If the desired variant for a stage is already tracked as active, no call is made.
// Returns: true when the requested variant is bound for all requested stages
// (or already was); false when ANY requested stage could not be applied
// (no toggleable clone / no original). "Unavailable" is a normal state, not an
// error — pre-check IsPatchToggleable when the logic branches on it.
// DEVELOPMENT-only: ASSERT_ONCE when the tracker says the variant should be
// available but the handle is 0 (that combination is a real bug).
bool EnsureShaderVariant(ID3D11DeviceContext* native_device_context,
                         reshade::api::shader_stage stages,
                         Shader::PatchVariant variant);
// Semantic availability: toggleable clone applied to the bound pipeline for this
// stage? Wraps the raw handle check — the game-facing "can I toggle this pass".
bool IsPatchToggleable(reshade::api::shader_stage stage) const;
// Which variant is actually bound for this stage (None/Original/Patched).
Shader::PatchVariant GetActivePatchVariant(reshade::api::shader_stage stage) const;
// Pure getter (no side effects): Original -> pipeline_state_original_<stage>,
// Patched -> pipeline_state_patched_<stage>, **0 when unavailable** — the
// codebase-wide "no pipeline" sentinel. Escape hatch for games that bind
// variants manually (Replaced draws, both variants in one callback).
reshade::api::pipeline GetPipelineVariant(reshade::api::shader_stage stage,
                                          Shader::PatchVariant variant) const;
```

(No `std::expected` / error-enum wrapper: the only failure mode is "unavailable",
which is a normal state (INPLACE patches, async-pending window) — not an error. The
`0` sentinel and the `bool` return encode it fully; error types are reserved for
exceptional one-shot operations elsewhere in the codebase, not per-draw idempotent
ensures.)

Implementation sketch (per stage bit; DX11 handles are native object pointers, same
cast pattern as `OnDestroyPipeline`):

```cpp
bool CommandListData::EnsureShaderVariant(ID3D11DeviceContext* ctx, reshade::api::shader_stage stages, Shader::PatchVariant variant)
{
   if (variant == Shader::PatchVariant::None) return false;
   bool all_applied = true;
   if ((stages & reshade::api::shader_stage::vertex) != 0)
   {
      if (patch_variant_vertex == variant) { /* already active */ }
      else if (pipeline_state_original_vertex_shader.handle != 0
               && (variant != Shader::PatchVariant::Patched || pipeline_state_patched_vertex_shader.handle != 0))
      {
         const uint64_t h = variant == Shader::PatchVariant::Patched
            ? pipeline_state_patched_vertex_shader.handle : pipeline_state_original_vertex_shader.handle;
         ctx->VSSetShader(reinterpret_cast<ID3D11VertexShader*>(h), nullptr, 0);
         patch_variant_vertex = variant;
      }
      else
      {
#if DEVELOPMENT
         ASSERT_ONCE(patch_variant_vertex != Shader::PatchVariant::Patched); // tracker claims available but handle is 0 -> real bug
#endif
         all_applied = false;
      }
   }
   // ... same for pixel (PSSetShader) and compute (CSSetShader)
   return all_applied;
}

bool CommandListData::IsPatchToggleable(reshade::api::shader_stage stage) const
{
   switch (stage)
   {
   case reshade::api::shader_stage::vertex:  return pipeline_state_patched_vertex_shader.handle != 0;
   case reshade::api::shader_stage::pixel:   return pipeline_state_patched_pixel_shader.handle != 0;
   case reshade::api::shader_stage::compute: return pipeline_state_patched_compute_shader.handle != 0;
   default: return false;
   }
}

Shader::PatchVariant CommandListData::GetActivePatchVariant(reshade::api::shader_stage stage) const
{
   switch (stage)
   {
   case reshade::api::shader_stage::vertex:  return patch_variant_vertex;
   case reshade::api::shader_stage::pixel:   return patch_variant_pixel;
   case reshade::api::shader_stage::compute: return patch_variant_compute;
   default: return Shader::PatchVariant::None;
   }
}
```

TraceDrawCallData (~line 75 struct): add

```cpp
// Actual variant that ran for this draw (set post-decision in OnDrawOrDispatch_Custom;
// None = no patch involved). Enables "was the patch disabled for this frame" in capture.
Shader::PatchVariant patch_variant = Shader::PatchVariant::None;
```

### B4. `Source/Core/core.hpp` — OnBindPipeline (~4670–4865)

In each stage branch (compute/vertex/pixel), after the existing `pipeline_state_original_*`
assignment:

```cpp
// Expose the patched (clone) handle for this stage.
cmd_list_data.pipeline_state_patched_compute_shader =
   (cached_pipeline && cached_pipeline->cloned) ? cached_pipeline->pipeline_clone
                                                : reshade::api::pipeline(0);
```

Replace the tail swap block (~4843):

```cpp
std::shared_lock lock(s_mutex_generic);
// Bind-time default: clone when cloned && custom_shaders_enabled && patch default ON.
// The game can override per draw via EnsureShaderVariant.
if (cached_pipeline->cloned && custom_shaders_enabled
    && device_data.patch_context.IsPatchEnabled(cached_pipeline->shader_hashes[0]))
{
   cmd_list->bind_pipeline(stages, cached_pipeline->pipeline_clone);
   <set this stage's patch_variant_* = Patched>;
}
else
{
   <set this stage's patch_variant_* = (cached_pipeline && cached_pipeline->cloned)
        ? Original : None>;
}
```

Notes:
- Readback path (game binds the clone; lookup via `pipeline_cache_by_pipeline_clone_handle`):
  populate `pipeline_state_patched_*` and set the tracker to `Patched`; **keep the bound
  object as-is** (the game explicitly bound it — do not force-swap to Original).
- Reset branch (`stages == all && pipeline.handle == 0`, ~4718): zero the three
  `pipeline_state_patched_*` handles and set trackers to `None` (matches `reset_command_list`).
- Perf: the new work is two field writes per bind + one map lookup only when `cloned` —
  nothing on the non-patched fast path.

### B5. `Source/Core/core.hpp` — OnDrawOrDispatch_Custom (~6046)

1. Before the `#if DEVELOPMENT` trace block (~6155): capture the pre-capture size.

```cpp
#if DEVELOPMENT
   size_t trace_start_index = cmd_list_data.trace_draw_calls_data.size(); // set before AddTraceDrawCallData pushes
#endif
```

2. Immediately **after** `game->OnDrawOrDispatch(...)` returns and **before** the early
   `return true` on override (line ~6514–6518), stamp the captured entries with the
   final variant (the game's EnsureShaderVariant calls happened inside the callback):

```cpp
#if DEVELOPMENT
   {
      const std::shared_lock lock_trace(s_mutex_trace); // trace_running check
      if (trace_running)
      {
         const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
         for (size_t i = trace_start_index; i < cmd_list_data.trace_draw_calls_data.size(); ++i)
         {
            auto& entry = cmd_list_data.trace_draw_calls_data[i];
            if (entry.type != TraceDrawCallData::TraceDrawCallType::Shader) continue;
            // Map entry (one per stage, keyed by the original handle) to its stage tracker.
            if (entry.pipeline_handle == cmd_list_data.pipeline_state_original_vertex_shader.handle)
               entry.patch_variant = cmd_list_data.patch_variant_vertex;
            else if (entry.pipeline_handle == cmd_list_data.pipeline_state_original_pixel_shader.handle)
               entry.patch_variant = cmd_list_data.patch_variant_pixel;
            else if (entry.pipeline_handle == cmd_list_data.pipeline_state_original_compute_shader.handle)
               entry.patch_variant = cmd_list_data.patch_variant_compute;
         }
      }
   }
#endif
```

(Stamping runs even when the draw was cancelled/replaced — the entries still describe the
pass that was evaluated; the variant info remains valid.)

### B6. `Source/Core/core.hpp` — frame capture UI (captured commands tab, ~11130–11190)

In the `TraceDrawCallType::Shader` name builder, after the existing text:

```cpp
if (draw_call_data.patch_variant == Shader::PatchVariant::Original)
   name << " (patch disabled)";
```

Plus, at the top or bottom of the captured commands tab: a summary line computed by scanning
the frame's entries, e.g.:

```cpp
// "N patched draw(s) ran with patch disabled (hashes: ...)" — computed in the UI from
// entry.patch_variant == Original && entry has a patched pipeline (patch_application_mode != None)
```

(All of B6 is inside the DEVELOPMENT-gated capture UI; zero shipping cost. The per-entry
marker is the primary requirement; the summary is a convenience.)

### B7. `Source/Core/includes/patch.hpp` — documentation

On the `LUMA_PATCH_SYNC_MODE_CLONE` / provider macro block: add a note that runtime
toggling (original ⇄ patched) requires a clone-based mode (`LUMA_PATCH_SYNC_MODE_CLONE=1`
or an async provider); INPLACE patches are always-on.

### B8. `Source/Games/Final Fantasy XV/main.cpp` — reference implementation (~409)

In `OnDrawOrDispatch`, at the top:

```cpp
// Patch stays on by default; selectively disable per draw (example: only when not
// drawing to the swapchain, or by frame counter, etc.).
const bool use_patch = <game condition>;
cmd_list_data.EnsureShaderVariant(native_device_context, stages,
                                  use_patch ? Shader::PatchVariant::Patched
                                            : Shader::PatchVariant::Original);
#if LUMA_HAS_RECIPE_PROVIDERS
if (use_patch)
   BindPatchedResources(native_device_context, device_data, original_shader_hashes, stages, updated_cbuffers);
// else: recipe slots left bound (original doesn't read them); unbind here if a slot is reused
#endif
```

---

## C. Behavior rules (review these)

1. **Default = ON.** Patch stays applied unless the game's per-draw code says otherwise.
   `SetPatchEnabled(hash, false)` only changes the *bind-time default* (next bind); the
   per-draw decision always wins.
2. **`EnsureShaderVariant` is idempotent** and must be called every draw of a toggled
   shader (the engine binds only on state change — a decision made only "on change" sticks
   to the wrong variant after an engine rebind).
3. **Never binds null / never unbinds**: a stage whose target handle is 0 is skipped.
   Manual binds via `GetPipelineVariant` are the game's own risk; resync the tracker with
   an `EnsureShaderVariant` call if the game mixes both styles.
4. **`HasPatch(hash)` means "patch bytes stored", NOT "toggleable here"** — availability
   for the bound pass is `IsPatchToggleable(stage)`. Core queue/self-heal
   logic depends on the former; do not conflate.
5. **Ownership split**: per-context/per-bind state + methods on `CommandListData`;
   per-hash persistent settings (`HasPatch`/`IsPatchEnabled`/`SetPatchEnabled`) on
   `PatchContext`. PatchContext has no bound-pipeline concept — variant ops cannot
   live there; CommandListData has no per-hash persistence — toggles cannot live there.
6. **Failure semantics**: "variant unavailable" (INPLACE patch, async-pending) is a
   NORMAL state, not an error. `EnsureShaderVariant` returns `bool` (satisfied or not),
   `GetPipelineVariant` returns `0` — no `std::expected` wrapper on this hot path.
   DEVELOPMENT-only `ASSERT_ONCE` when the tracker claims availability but the handle
   is 0 (that combination is a real bug).
   `pipeline_state_has_custom_*` / `is_custom_pass` ("has a clone"), `patch_application_mode`
   (how applied). New state is additive.
7. **No semantic changes to existing fields**: `pipeline_state_original_*` (identity),
   `pipeline_state_has_custom_*` / `is_custom_pass` ("has a clone"), `patch_application_mode`
   (how applied). New state is additive.
8. **File-shadowed clones** (a custom shader file won over the patch in the clone): the
   toggle restores the game's original object; the file is skipped too. Accepted; document.
9. **Provider/processed semantics untouched**: toggling never re-runs providers, never
   clears `processed_shaders`; off→on is instant (patch bytes stay cached).
10. **INPLACE patches cannot toggle** — documented on the macros (B7).
11. **Threading**: `IsPatchEnabled/SetPatchEnabled` use `patch_context.mutex`
   (shared/unique, leaf mutex — safe from UI/render threads). `EnsureShaderVariant` and the
   query methods touch only per-command-list state. Trace stamping runs under the existing
   trace locks.

---

## D. Phased plan

| Phase | Work | Validation |
|---|---|---|
| **1 — Data + API** | B1 (enum), B2 (PatchContext toggle), B3 (fields + `EnsureShaderVariant` + trace field) | Full solution builds; **zero behavior change** (feature inert: nothing consults the new state yet) |
| **2 — Bind-time wiring** | B4 (OnBindPipeline: expose handles, default toggle, tracker, reset + readback paths) | Builds; FFXV behaves identically with default ON; `SetPatchEnabled(hash,false)` flips to original at next bind |
| **3 — Draw-time** | B5 (trace stamping), B8 (FFXV reference impl with a real condition) | Builds; per-draw toggling verified in-game; bindings gate correctly |
| **4 — Frame capture** | B6 (UI marker + summary) | Capture shows "(patch disabled)" per draw and the frame summary |
| **5 — Docs + hardening** | B7; edge-case pass (deferred contexts note, readback path, reload of clones via `LoadCustomShaders`/async publish keeps handles valid since they're re-read per bind) | Full matrix: INPLACE game (e.g. Heavy Rain) unchanged; FFXV (bytecode-sync + recipe-async) toggles; crossed provider configs compile |

---

## E. Open decisions for review

1. **Naming**: `PatchVariant::Original/Patched/None`, `EnsureShaderVariant`,
   `patch_enabled_by_default` — or prefer `SetShaderVariant` / `patch_default_enabled`?
2. **File-shadowed clones** (rule 5): accept "toggle skips file too", or record what the
   clone was built from in `CachedPipeline` and no-op toggling for those pipelines?
3. **UI marker**: plain "(patch disabled)" suffix vs a marker column/color change; is the
   per-frame summary count wanted, or per-entry marker only?
4. **Coarse toggle persistence**: should `SetPatchEnabled` state be saved to the mod ini
   (dev tool) or be code-only (game-driven, e.g. `OnInit`)? Suggested: code-only, dev UI
   checkbox for debugging.
5. **Readback path** (rule in B4): confirm "respect the game's explicit clone bind" is the
   right policy vs forcing the default variant.
