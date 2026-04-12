#pragma once

#include "upscale.hpp"
#include "postprocess.hpp"

struct GameDeviceDataGBFR final : public GameDeviceData, public GBFRUpscaleState, public GBFRPostProcessState
{
   using BufferInfo = GBFRBufferInfo;
   using CutsceneOverlayModulateReplayState = GBFRCutsceneOverlayModulateReplayState;
   using CutsceneOverlayBlendReplayState = GBFRCutsceneOverlayBlendReplayState;
   using MotionBlurReplayState = GBFRMotionBlurReplayState;
   using OutlineReplayState = GBFROutlineReplayState;
   using CutscenePostPassReplayState = GBFRCutscenePostPassReplayState;

   float camera_fov = 60.0f * (3.14159265f / 180.0f);
   float camera_near = 0.1f;
   float camera_far = 10000.0f;
   float2 jitter = {0, 0};
   float2 prev_jitter = {0, 0};
   float2 table_jitter = {0, 0};
   float2 prev_table_jitter = {0, 0};
   std::atomic<ID3D11DeviceContext*> tonemap_detected_context = nullptr;

#if TEST || DEVELOPMENT
   bool taa_detected_this_frame = false;
   bool use_table_jitter_for_dlss = true;
#endif

#if DEVELOPMENT
   struct PausedFrameSnapshot
   {
      bool valid = false;
      float2 render_resolution = {0, 0};
      float2 output_resolution = {0, 0};
      float render_scale_pct = 0.0f;
      float2 jitter = {0, 0};
      float2 prev_jitter = {0, 0};
      float2 table_jitter = {0, 0};
      float2 prev_table_jitter = {0, 0};
      bool taa_enabled = false;
      bool upscaling_disabled = false;
      bool drs_active = false;
      bool taa_output_ready = false;
      bool settings_obj_valid = false;
   };
   PausedFrameSnapshot pause_snapshot;
   bool pause_trace_key_down = false;
   int pause_trace_delay_frames = 0;
   int pause_trace_delay_countdown = -1;
#endif

   ComPtr<ID3D11Buffer> scratch_scene_buffer;
   ComPtr<ID3D11UnorderedAccessView> scratch_scene_buffer_uav;
   std::atomic<bool> scene_buffer_patched_this_frame = false;

   std::atomic<bool> scene_buffer_collect_guard = false;
   std::atomic<bool> scene_buffer_info_collected = false;
   ID3D11Buffer* pending_scene_buffer = nullptr;
   UINT pending_first_constant = 0;
   UINT pending_num_constants = 0;
   std::mutex scene_buffer_bindings_mutex;
   std::set<UINT> scene_buffer_offsets_this_frame;
};

struct GBFRShaderHashes
{
   ShaderHashesList<false> outline_prefilter;
   ShaderHashesList<false> outline_cs;
   ShaderHashesList<false> temporal_upscale;
   ShaderHashesList<false> taa;
   ShaderHashesList<false> tonemap;
   ShaderHashesList<false> motion_blur;
   ShaderHashesList<false> motion_blur_denoise;
   ShaderHashesList<false> cutscene_gamma;
   ShaderHashesList<false> cutscene_color_grade;
   ShaderHashesList<false> cutscene_overlay_blend;
   ShaderHashesList<false> cutscene_overlay_modulate;
};

struct GBFRRuntimeSettings
{
   float render_scale = 1.0f;
   bool render_scale_changed = false;
};

extern GBFRShaderHashes g_shader_hashes;
extern GBFRRuntimeSettings g_runtime_settings;

extern ShaderHashesList<false>& shader_hashes_OutlinePrefilter;
extern ShaderHashesList<false>& shader_hashes_OutlineCS;
extern ShaderHashesList<false>& shader_hashes_Temporal_Upscale;
extern ShaderHashesList<false>& shader_hashes_TAA;
extern ShaderHashesList<false>& shader_hashes_Tonemap;
extern ShaderHashesList<false>& shader_hashes_MotionBlur;
extern ShaderHashesList<false>& shader_hashes_MotionBlurDenoise;
extern ShaderHashesList<false>& shader_hashes_CutsceneGamma;
extern ShaderHashesList<false>& shader_hashes_CutsceneColorGrade;
extern ShaderHashesList<false>& shader_hashes_CutsceneOverlayBlend;
extern ShaderHashesList<false>& shader_hashes_CutsceneOverlayModulate;

extern float& render_scale;
extern bool& render_scale_changed;

bool CreateOrRecreateTextureIfNeeded(GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, ComPtr<ID3D11Texture2D>& texture);
bool CreateOrRecreateTextureIfNeeded(GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& srv);
bool CreateOrRecreateTextureIfNeeded(GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& srv, ComPtr<ID3D11RenderTargetView>& rtv);
ID3D11ShaderResourceView* GetPostAAColorInputSRV(const DeviceData& device_data, const GameDeviceDataGBFR& game_device_data);
bool CanDrawNativeUIEncodePass(ID3D11ShaderResourceView* input_color_srv, const GameDeviceDataGBFR& game_device_data);
