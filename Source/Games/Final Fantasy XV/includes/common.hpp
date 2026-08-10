#pragma once

#include <atomic>
#include <memory>
#include <numbers>
#include "log.hpp"
#include "native_cbuffers.hpp"
#include "upscale_tracking.hpp"

struct GameDeviceDataFFXV final : public GameDeviceData
{
#if ENABLE_SR
   // SR - Resources extracted from TAA pass (may be reused by game)
   ComPtr<ID3D11Resource> sr_motion_vectors;
   ComPtr<ID3D11Resource> sr_source_color;
   ComPtr<ID3D11Resource> depth_buffer;
   ComPtr<ID3D11UnorderedAccessView> sr_motion_vectors_uav;
   ComPtr<ID3D11Texture2D> exposure_texture;
   ComPtr<ID3D11Texture2D> sr_depth_backup;
   ComPtr<ID3D11ShaderResourceView> sr_output_srv;
#endif // ENABLE_SR
   std::atomic<bool> has_drawn_upscaling = false;
   std::atomic<bool> has_drawn_autoexposure = false;
   std::atomic<bool> has_drawn_tonemap = false;
   std::atomic<bool> found_taa_cb = false;
   std::atomic<bool> found_exposure_cb = false;
   std::atomic<bool> found_per_view_globals = false;
   std::atomic<bool> use_exposure_texture = false;
   std::unique_ptr<cbTemporalAA> taa_cb_data;
   std::unique_ptr<cbExposure> exposure_cb_data;
   float dlss_pre_exposure_raw = 1.f;
   float dlss_pre_exposure_smoothed = 1.f;
   float dlss_pre_exposure_smoothing = 0.15f;
   bool has_dlss_pre_exposure_history = false;
   bool dlss_use_inverse_pre_exposure = true;

   ComPtr<ID3D11Buffer> cb_taa_buffer;
   void* cb_taa_buffer_map_data = nullptr;

   // Cached view buffer (once found)
   ID3D11Buffer* cached_view_buffer = nullptr;

   // Extracted camera data
   float camera_fov = 60.0f * std::numbers::pi_v<float> / 180.0f;
   float camera_near = 0.1f;
   float camera_far = 1000.0f;

   bool has_processed_view_buffer = false;

   float2 taa_jitters = {0, 0};
   float2 projection_jitters = {0, 0};

   // Post-TAA upscale tracking
   UpscaleTrackingState upscale_tracking;

   ComPtr<ID3D11Buffer> dxp_frame_constants_cb;
   uint32_t dxp_frame_constants_frame_index = UINT32_MAX;

   // Recipe patch bindings resolved once at patch-apply time (see
   // ApplyDepthDitheringShaderPatch). Read per-draw under dxp_bindings_mutex;
   // written by whichever provider applied the patch (sync: creation thread,
   // async: worker thread).
   struct FFXVDxpBindingSet
   {
      uint32_t shader_hash = 0;
      uint32_t fast_noise_bind_point = UINT32_MAX;
      uint32_t frame_constants_bind_point = UINT32_MAX;
   };
   std::vector<FFXVDxpBindingSet> dxp_binding_sets;
   mutable std::mutex dxp_bindings_mutex;

   // Runtime patch toggle (reference): ON by default, disabled per draw via
   // EnsureShaderVariant. Atomic (UI writes, render reads).
   std::atomic<bool> dithering_patch_enabled = true;

   // Luma bloom
   ComPtr<ID3D11ShaderResourceView> bloom_scene_srv;
   ComPtr<ID3D11Buffer> bloom_globals_cb;   // game's _Globals b0 captured at highpass
   bool captured_bloom_scene = false;

#if DEVELOPMENT || TEST
   uint32_t dbg_replaced_srvs = 0;
   uint32_t dbg_replaced_rtvs = 0;
   uint32_t dbg_replaced_viewports = 0;
   uint32_t dbg_replaced_scissors = 0;
   // When true, log unmodified viewport/scissor state for intermediate passes between TAA and Upscale
   bool dbg_log_baseline_state = false;
#endif

   void ResetPerFrameData()
   {

      // Clear per-frame source links while keeping persistent pooled allocations.
      upscale_tracking.ResetFrame();

#if DEVELOPMENT || TEST
      dbg_replaced_srvs = 0;
      dbg_replaced_rtvs = 0;
      dbg_replaced_viewports = 0;
      dbg_replaced_scissors = 0;
#endif

      has_drawn_upscaling = false;
      has_drawn_autoexposure = false;
      has_drawn_tonemap = false;
      found_taa_cb = false;
      has_processed_view_buffer = false;
      found_per_view_globals = false;

      captured_bloom_scene = false;
      bloom_scene_srv.reset();
      bloom_globals_cb.reset();
   }
};
