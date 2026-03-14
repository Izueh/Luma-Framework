# Granblue Fantasy Relink - DLSS Resolution Upscaling Plan

## Executive Summary

This document provides a comprehensive implementation plan for adding **true resolution upscaling** to Granblue Fantasy Relink's Luma implementation. The solution leverages the game's frame graph architecture to render at scaled resolution (50%-100%) and use DLSS Super Resolution to upscale to native output.

**Key Achievement**: Only **2 passes run after DLSS** (Esp2DPass/TAA and Final Resolve), making this ideal for upscaling with minimal visual artifacts.

---

## 1. Pass Order Analysis

### Current Frame Graph Execution (ExecuteFrameGraphPasses @ 0x140727D10)

| Pass | Address | Purpose | Resolution |
|------|---------|---------|------------|
| 0 | StartPass | Initialization | Scaled |
| 1 | DepthPrePass | Depth buffer | Scaled |
| 2 | PreModelOpacityPass | Opacity pass | Scaled |
| 3-9 | Model Passes (7x) | Main rendering | Scaled |
| 10 | PlayerSilhouettePass | Character silhouette | Scaled (conditional) |
| 11 | ScalingPass | Resolution scaling check | Scaled |
| 12 | BooleanMaskPass | Boolean masking | **Native** |
| 13 | ResolveAfter3DPass | 3D resolve | **Native** |
| 14 | **Esp2DPass (TAA)** | Temporal AA | **Native** |
| 15 | **Final Resolve** | Output compositing | **Native** |

### DLSS Integration Point

**Current Flow**:
```
ScalingPass → Esp2DPass(TAA) → Final Resolve
```

**Modified Flow**:
```
ScalingPass → DLSSUpscale → Esp2DPass → Final Resolve
```

**Post-DLSS Passes** (run at native resolution):
1. **Esp2DPass** (TAA) - Will run on upscaled image
2. **Final Resolve** - Final blending

**Critical Insight**: Since DLSS replaces TAA, we can run DLSS at the ScalingPass position (0x1407281ae), and all subsequent passes will automatically use native resolution via our constant buffer hook.

---

## 2. Critical Addresses & Functions

### Core Hook Points

| Address | Function | Purpose | Hook Type | Priority |
|---------|----------|---------|-----------|----------|
| **0x140710070** | sub_140710070 | CB data copy | Inline | **CRITICAL** |
| **0x140745510** | InitializeDX11RenderingPipeline | RT creation | Inline | **CRITICAL** |
| **0x140727D10** | ExecuteFrameGraphPasses | Frame loop | Mid | **CRITICAL** |
| **0x1405F7960** | UpdateScreenResolution | Resolution update | Inline | HIGH |
| **0x1405F7A20** | CreateRenderTargets | RT recreation | Inline | HIGH |

### Resolution Globals

| Address | Variable | Value (Hex) | Value (Dec) | Purpose |
|---------|----------|-------------|-------------|---------|
| **0x145AA41E8** | RenderWidth | 0x780 | 1920 | Primary render width |
| **0x145AA41EC** | RenderHeight | 0x438 | 1080 | Primary render height |
| 0x145AA41E0 | RenderWidth_v2 | 0x780 | 1920 | Used in ScalingPass |
| 0x145AA41F0 | BackupWidth | 0x780 | 1920 | Change detection |
| 0x145AA41F4 | BackupHeight | 0x438 | 1080 | Change detection |

### Constant Buffer Structure (sub_140710070)

**Total Size**: 0x140 bytes (320 bytes)

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0x000 | 128 | ViewProjection[4] | float4x4 | View/projection matrices |
| 0x080 | 48 | Unknown | float4[3] | Vector data |
| **0x0B8** | 8 | **RenderWidth** | **float** | **Render width** |
| 0x0C0 | 8 | Padding | float | Alignment |
| **0x0C8** | 8 | **RenderHeight** | **float** | **Render height** |
| 0x0D0 | 64 | Unknown | float4[4] | Camera/light data |
| 0x110 | 16 | Unknown | float4[1] | Additional data |
| 0x120 | 16 | Unknown | float4[1] | Additional data |
| 0x130 | 16 | Unknown | float4[1] | Additional data |

### Resource Creation

| Address | Function | Purpose | Resolution Dependency |
|---------|----------|---------|----------------------|
| **0x140745510** | InitializeDX11RenderingPipeline | Create render targets | **Direct** |
| **0x1405F7A20** | CreateRenderTargets | Recreate on change | **Direct** |
| **0x1438CD3B0** | CreateDX11Texture | Texture allocation | **Direct** |

**Resources Created**:
- Main render target (full resolution)
- Downsampling textures (1/4 resolution for bloom/TAA)
- Shader resource views
- Render target views

### Resource Array Structure

| Address | Variable | Description |
|---------|----------|-------------|
| **0x145FB4918** | ResourceArray | Start of resource pool |
| **0x145FB4A08** | ResourceCount | Number of resources |

**Resource Handle Layout**:
| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 16 | Handle | DX11 resource pointer |
| 0x08 | 8 | RefCount | Reference count |
| 0x10 | 16 | Next | Linked list pointer |

---

## 3. Implementation Strategy

### Overview

The implementation uses a **dual-resolution state machine** that:
1. Renders all passes before DLSS at scaled resolution
2. Upscales via DLSS Super Resolution
3. Runs all post-DLSS passes at native resolution
4. Automatically handles resolution changes

### Architecture

```
┌─────────────────────────────────────────────────┐
│ DLSSResolutionState                             │
│ - enabled: bool                                  │
│ - scale_percent: float (50-100)                  │
│ - native_width: uint32                           │
│ - native_height: uint32                          │
│ - scaled_width: uint32                           │
│ - scaled_height: uint32                          │
│ - current_phase: enum {PRE_DLSS, POST_DLSS}     │
└─────────────────┬───────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────┐
│ sub_140710070 Hook (CB Copy)                    │
│ - Override RenderWidth/RenderHeight             │
│ - Based on current_phase                        │
└─────────────────┬───────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────┐
│ ExecuteFrameGraphPasses                         │
│ - Pass 0-11: Render at scaled resolution        │
│ - Pass 11 (ScalingPass): Call DLSS Upscale      │
│ - Pass 12-15: Render at native resolution       │
└─────────────────┬───────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────┐
│ Output                                          │
│ - Native resolution with DLSS upscaling         │
└─────────────────────────────────────────────────┘
```

---

## 4. Detailed Implementation

### Step 1: Add DLSS Resolution State

**Location**: Add to `GameDeviceDataGBFR` in `main.cpp`

```cpp
struct GameDeviceDataGBFR final : public GameDeviceData
{
    // ... existing members ...
    
    enum class DLSSPhase {
        PRE_DLSS,      // Rendering at scaled resolution
        POST_DLSS,     // Post-processing at native resolution
    };
    
    // DLSS Resolution Scaler State
    struct DLSSResolutionState {
        std::atomic<bool> enabled{false};
        std::atomic<float> scale_percent{100.0f};
        uint32_t native_width = 1920;
        uint32_t native_height = 1080;
        uint32_t scaled_width = 1920;
        uint32_t scaled_height = 1080;
        std::atomic<DLSSPhase> current_phase{DLSSPhase::PRE_DLSS};
    };
    
    DLSSResolutionState dlss_res_state;
    
    // Post-DLSS Effect Controls
    struct PostDLSSEffectConfig {
        bool enable_motion_blur = true;
        bool enable_dof = true;
        float min_scale_for_mb = 0.75f;
        float min_scale_for_dof = 0.75f;
    };
    
    PostDLSSEffectConfig post_dlss_effects;
};
```

### Step 2: Hook Constant Buffer Copy (CRITICAL)

**Location**: `0x140710070` - `sub_140710070`

**Implementation**:
```cpp
SafetyHookInline g_cb_copy_hook;

void Hooked_sub_140710070(void* pass_data, int pass_index)
{
    DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
    if (!device_data) {
        Original_sub_140710070(pass_data, pass_index);
        return;
    }
    
    auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data->game);
    auto& state = game_device_data.dlss_res_state;
    
    // Skip if DLSS scaler not enabled or at 100%
    if (!state.enabled.load() || state.scale_percent >= 100.0f) {
        Original_sub_140710070(pass_data, pass_index);
        return;
    }
    
    // Override resolution scalars in pass data
    // RenderWidth at offset 0xB8, RenderHeight at offset 0xC8
    float* width_ptr = (float*)((char*)pass_data + 0xB8);
    float* height_ptr = (float*)((char*)pass_data + 0xC8);
    
    // Apply dual-resolution logic
    if (state.current_phase.load() == DLSSPhase::PRE_DLSS) {
        *width_ptr = (float)state.scaled_width;
        *height_ptr = (float)state.scaled_height;
    } else {
        *width_ptr = (float)state.native_width;
        *height_ptr = (float)state.native_height;
    }
    
    // Call original function
    Original_sub_140710070(pass_data, pass_index);
}
```

### Step 3: Hook Render Target Creation (CRITICAL)

**Location**: `0x140745510` - `InitializeDX11RenderingPipeline`

**Implementation**:
```cpp
SafetyHookInline g_rt_creation_hook;

void Hooked_InitializeDX11RenderingPipeline(unsigned int& screenWidth, 
                                             unsigned int& screenHeight)
{
    DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
    if (!device_data) {
        Original_InitializeDX11RenderingPipeline(screenWidth, screenHeight);
        return;
    }
    
    auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data->game);
    auto& state = game_device_data.dlss_res_state;
    
    // Override resolution before creation
    if (state.enabled.load() && state.scale_percent < 100.0f) {
        screenWidth = state.scaled_width;
        screenHeight = state.scaled_height;
    }
    
    // Call original function
    Original_InitializeDX11RenderingPipeline(screenWidth, screenHeight);
}
```

### Step 4: Insert DLSS Upscaling Call (CRITICAL)

**Location**: `ExecuteFrameGraphPasses` at `0x140727D10`

**Insertion Point**: After ScalingPass (0x1407281ae), before BooleanMaskPass (0x140728239)

**Implementation**:
```cpp
// In ExecuteFrameGraphPasses, after ScalingPass call:
// Original call at 0x1407281ae: sub_140715EA0(v0, v27, v42, &v44);

// Insert DLSS call here:
auto& state = game_device_data.dlss_res_state;

// Switch to POST_DLSS phase
state.current_phase.store(DLSSPhase::POST_DLSS);

// Call DLSS Super Resolution (not DLAA)
SR::SettingsData settings_data;
settings_data.output_width = state.native_width;
settings_data.output_height = state.native_height;
settings_data.render_width = state.scaled_width;
settings_data.render_height = state.scaled_height;
settings_data.dynamic_resolution = false;
settings_data.hdr = true;
settings_data.auto_exposure = true;
settings_data.inverted_depth = false;
settings_data.mvs_jittered = false;
settings_data.mvs_x_scale = -(float)state.scaled_width;
settings_data.mvs_y_scale = -(float)state.scaled_height;
settings_data.render_preset = dlss_render_preset;

// Update settings
sr_implementations[device_data.sr_type]->UpdateSettings(
    sr_instance_data, native_device_context, settings_data);

// Prepare draw data
SR::SuperResolutionImpl::DrawData draw_data;
draw_data.source_color = game_device_data.sr_source_color.get();
draw_data.output_color = device_data.sr_output_color.get();
draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
draw_data.depth_buffer = game_device_data.depth_buffer.get();
draw_data.pre_exposure = 0.0f;
draw_data.jitter_x = game_device_data.jitter.x;
draw_data.jitter_y = game_device_data.jitter.y;
draw_data.vert_fov = game_device_data.camera_fov;
draw_data.far_plane = game_device_data.camera_far;
draw_data.near_plane = game_device_data.camera_near;
draw_data.reset = device_data.force_reset_sr;
draw_data.render_width = state.scaled_width;
draw_data.render_height = state.scaled_height;

// Execute DLSS
device_data.has_drawn_sr = sr_implementations[device_data.sr_type]->Draw(
    sr_instance_data, native_device_context, draw_data);

// Continue with remaining passes (they'll use native resolution)
```

### Step 5: Hook Resolution Update (Runtime Changes)

**Location**: `0x1405F7960` - `UpdateScreenResolution`

**Implementation**:
```cpp
void Hooked_UpdateScreenResolution(unsigned int& newWidth, 
                                    unsigned int& newHeight)
{
    DeviceData* device_data = g_device_data_ptr.load(std::memory_order_acquire);
    if (!device_data) {
        Original_UpdateScreenResolution(newWidth, newHeight);
        return;
    }
    
    auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data->game);
    auto& state = game_device_data.dlss_res_state;
    
    // Update native resolution
    if (state.enabled.load()) {
        float scale = state.scale_percent.load() / 100.0f;
        state.native_width = newWidth;
        state.native_height = newHeight;
        state.scaled_width = (uint32_t)(newWidth * scale);
        state.scaled_height = (uint32_t)(newHeight * scale);
        
        // Signal force_reset for DLSS
        device_data->force_reset_sr = true;
    }
    
    // Call original function
    Original_UpdateScreenResolution(newWidth, newHeight);
}
```

### Step 6: Handle Post-DLSS Effects

**Motion Blur & DOF Strategy**:

For shaders that depend on depth/motion vectors at scaled resolution:

**Option A: Linear Sampling (User's Choice)**
```hlsl
// In motion blur shader:
float2 scaled_uv = input_uv * (scaled_resolution / native_resolution);
float depth = linear_sample_depth(scaled_uv);
// Use depth for DOF calculation
```

**Option B: Disable Below Threshold**
```cpp
// In effect setup:
if (scale_percent < post_dlss_effects.min_scale_for_mb) {
    // Set motion blur amount to 0
    // Or skip the effect entirely
}
```

**Implementation in main.cpp**:
```cpp
void DrawImGuiSettings(DeviceData& device_data) override
{
    auto& game_device_data = GetGameDeviceData(device_data);
    
    // DLSS Resolution Scaler
    if (ImGui::CollapsingHeader("DLSS Resolution Scaler", 
                                 ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox("Enabled", &game_device_data.dlss_scaler_enabled))
        {
            game_device_data.dlss_res_state.enabled.store(
                game_device_data.dlss_scaler_enabled);
            device_data.force_reset_sr = true;
        }
        
        if (ImGui::SliderFloat("Scale (%)", 
                               &game_device_data.resolution_scale_percent, 
                               50.0f, 100.0f, "%.0f%%",
                               ImGuiSliderFlags_AlwaysClamp))
        {
            auto& state = game_device_data.dlss_res_state;
            state.scale_percent.store(game_device_data.resolution_scale_percent);
            
            // Update scaled resolution
            float scale = game_device_data.resolution_scale_percent / 100.0f;
            state.scaled_width = (uint32_t)(device_data.output_resolution.x * scale);
            state.scaled_height = (uint32_t)(device_data.output_resolution.y * scale);
            
            device_data.force_reset_sr = true;
        }
        
        // Warning for low scales
        if (game_device_data.resolution_scale_percent < 75.0f)
        {
            ImGui::TextColored(ImColor(255, 200, 200),
                "Warning: Motion Blur and DOF may be disabled at low scales");
        }
    }
    
    // Post-DLSS Effect Controls
    if (ImGui::CollapsingHeader("Post-DLSS Effects", 
                                 ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox("Motion Blur", 
                           &game_device_data.post_dlss_effects.enable_motion_blur))
        {
            device_data.force_reset_sr = true;
        }
        
        if (ImGui::SliderFloat("Min Scale for MB", 
                               &game_device_data.post_dlss_effects.min_scale_for_mb,
                               0.5f, 1.0f, "%.2f"))
        {
            device_data.force_reset_sr = true;
        }
        
        if (ImGui::Checkbox("Depth of Field", 
                           &game_device_data.post_dlss_effects.enable_dof))
        {
            device_data.force_reset_sr = true;
        }
        
        if (ImGui::SliderFloat("Min Scale for DOF", 
                               &game_device_data.post_dlss_effects.min_scale_for_dof,
                               0.5f, 1.0f, "%.2f"))
        {
            device_data.force_reset_sr = true;
        }
    }
}
```

---

## 5. Runtime Resolution Changes

### Automatic Recreation via Game Engine

**Yes, the game engine handles this automatically!**

**Flow**:
```
User changes resolution in game menu
    ↓
UpdateScreenResolution (0x1405F7960)
    ↓
Atomic update of resolution globals
    ↓
CreateRenderTargets flag set
    ↓
Next frame: CreateRenderTargets (0x1405F7A20)
    ↓
InitializeDX11RenderingPipeline (0x140745510)
    ↓
New render targets created at new resolution
    ↓
Resource array updated
```

**Our Hook's Role**:
- Intercept `UpdateScreenResolution` to update `dlss_res_state`
- Set `force_reset_sr = true` to reset DLSS state
- Hook `InitializeDX11RenderingPipeline` to use scaled resolution

**No Manual Recreation Needed**: The game's existing resource management handles everything!

---

## 6. DLSS Mode Selection

### Strategy

| Scale | DLSS Mode | Reason |
|-------|-----------|--------|
| 100% | DLAA | No upscaling needed, just AA |
| 50-99% | Super Resolution | Upscaling required |

**Implementation**:
```cpp
// In OnPresent or before DLSS call:
if (state.scale_percent >= 100.0f) {
    device_data.sr_type = SR::Type::DLAA;
} else {
    device_data.sr_type = SR::Type::DLSS;  // Super Resolution
}
```

---

## 7. Motion Blur & DOF Handling

### Recommended Approach

Since you can decompile shaders and do linear sampling:

**Implementation Plan**:

1. **Identify affected shaders**:
   - Motion blur shader (likely uses depth + motion vectors)
   - DOF shader (likely uses depth buffer)

2. **Modify sampling**:
   ```hlsl
   // Original:
   float depth = texture(depth_tex, input_uv).r;
   
   // Modified:
   float2 scaled_uv = input_uv * (scaled_resolution / native_resolution);
   float depth = texture(depth_tex, scaled_uv).r;  // Linear sampling
   ```

3. **Add runtime switch**:
   - Enable/disable based on scale threshold
   - Or always use linear sampling if performance allows

**Threshold Strategy**:
```cpp
if (scale_percent < min_scale_for_mb && enable_motion_blur) {
    // Use linear sampling or disable
}
```

---

## 8. Testing Plan

### Phase 1: Hook Verification
- [ ] Verify `sub_140710070` hook works
- [ ] Log resolution values per pass
- [ ] Confirm PRE_DLSS/POST_DLSS phase switching

### Phase 2: Render Target Testing
- [ ] Verify render targets created at scaled resolution
- [ ] Test resolution changes at runtime
- [ ] Check for resource leaks

### Phase 3: DLSS Integration
- [ ] Test DLSS Super Resolution at each scale (50%, 60%, 70%, 80%, 90%, 100%)
- [ ] Verify upscaling quality
- [ ] Check for artifacts at resolution boundary

### Phase 4: Post-DLSS Effects
- [ ] Test Esp2DPass (TAA) at native resolution
- [ ] Test Final Resolve at native resolution
- [ ] Verify motion blur/DOF handling

### Phase 5: Performance
- [ ] Measure FPS at each scale
- [ ] Profile GPU utilization
- [ ] Compare to native rendering

---

## 9. Risk Assessment

| Component | Risk | Mitigation |
|-----------|------|------------|
| CB Hook | Low | Well-defined hook point, easy to test |
| RT Hook | Low | Game handles recreation automatically |
| DLSS Integration | Medium | SDK already works, just need upscaling mode |
| Runtime Changes | Low | Engine already has resolution update logic |
| Post-DLSS Effects | Medium | Need shader modifications for MB/DOF |
| Overall | Medium | Extensive testing required |

---

## 10. Summary

### What We're Changing

1. **Hook `sub_140710070`** (0x140710070)
   - Override RenderWidth/RenderHeight in constant buffers
   - Based on PRE_DLSS/POST_DLSS phase

2. **Hook `InitializeDX11RenderingPipeline`** (0x140745510)
   - Create render targets at scaled resolution
   - Game engine handles recreation automatically

3. **Insert DLSS Call** in `ExecuteFrameGraphPasses` (0x140727D10)
   - Between ScalingPass and BooleanMaskPass
   - Use Super Resolution mode (not DLAA) for scales < 100%

4. **Hook `UpdateScreenResolution`** (0x1405F7960)
   - Update dlss_res_state on resolution changes
   - Set force_reset_sr for DLSS

### What We're NOT Changing

- **Post-DLSS effects** (Esp2DPass, Final Resolve) - Run at native automatically
- **Resource recreation** - Game engine handles this
- **Most shaders** - Only MB/DOF need linear sampling

### Expected Results

✅ **Functional**:
- True dual-resolution rendering
- DLSS upscales from scaled to native
- Runtime resolution changes work
- Post-DLSS effects at native resolution

✅ **Visual**:
- No artifacts at resolution boundary
- DLSS quality acceptable at all scales
- Post-DLSS effects look correct

✅ **Performance**:
- FPS improvement at scales < 100%
- Smooth transition on resolution changes

---

## 11. Quick Reference

### Critical Addresses

| Purpose | Address | Function |
|---------|---------|----------|
| CB Copy Hook | 0x140710070 | sub_140710070 |
| RT Creation Hook | 0x140745510 | InitializeDX11RenderingPipeline |
| DLSS Insertion | 0x140727D10 | ExecuteFrameGraphPasses |
| Resolution Update | 0x1405F7960 | UpdateScreenResolution |

### Resolution Globals

| Variable | Address | Purpose |
|----------|---------|---------|
| RenderWidth | 0x145AA41E8 | Primary width |
| RenderHeight | 0x145AA41EC | Primary height |
| BackupWidth | 0x145AA41F0 | Change detection |
| BackupHeight | 0x145AA41F4 | Change detection |

### Constant Buffer Offsets

| Field | Offset | Description |
|-------|--------|-------------|
| RenderWidth | 0xB8 | 184 bytes from struct start |
| RenderHeight | 0xC8 | 200 bytes from struct start |
| Total Size | 0x140 | 320 bytes |

---

## 12. Implementation Checklist

- [ ] Add DLSSResolutionState to GameDeviceDataGBFR
- [ ] Implement sub_140710070 hook
- [ ] Implement InitializeDX11RenderingPipeline hook
- [ ] Insert DLSS call in ExecuteFrameGraphPasses
- [ ] Implement UpdateScreenResolution hook
- [ ] Add UI controls for scaler
- [ ] Implement motion blur/DOF linear sampling
- [ ] Test all scales (50%-100%)
- [ ] Profile performance
- [ ] Fix any bugs

**Ready to implement when you confirm!**