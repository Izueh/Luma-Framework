#pragma once

struct GBFRBufferInfo
{
   ComPtr<ID3D11Buffer> buffer;
   UINT num_constants = 0;
   UINT first_constant = 0;
};

struct GBFRCutsceneOverlayModulateReplayState
{
   static constexpr UINT kCapturedVertexBufferCount = 2;

   bool valid = false;
   ComPtr<ID3D11VertexShader> vertex_shader;
   ComPtr<ID3D11PixelShader> pixel_shader;
   ComPtr<ID3D11InputLayout> input_layout;
   std::array<ComPtr<ID3D11Buffer>, kCapturedVertexBufferCount> vertex_buffers;
   std::array<UINT, kCapturedVertexBufferCount> vertex_buffer_strides = {};
   std::array<UINT, kCapturedVertexBufferCount> vertex_buffer_offsets = {};
   GBFRBufferInfo vs_constant_buffer_slot1;
   GBFRBufferInfo ps_constant_buffer_slot1;

   void Reset()
   {
      valid = false;
      vertex_shader = nullptr;
      pixel_shader = nullptr;
      input_layout = nullptr;
      for (auto& buffer : vertex_buffers)
      {
         buffer = nullptr;
      }
      vertex_buffer_strides.fill(0);
      vertex_buffer_offsets.fill(0);
      vs_constant_buffer_slot1.buffer = nullptr;
      vs_constant_buffer_slot1.num_constants = 0;
      vs_constant_buffer_slot1.first_constant = 0;
      ps_constant_buffer_slot1.buffer = nullptr;
      ps_constant_buffer_slot1.num_constants = 0;
      ps_constant_buffer_slot1.first_constant = 0;
   }
};

struct GBFRCutsceneOverlayBlendReplayState
{
   static constexpr UINT kCapturedVertexBufferCount = 2;

   bool valid = false;
   ComPtr<ID3D11VertexShader> vertex_shader;
   ComPtr<ID3D11PixelShader> pixel_shader;
   ComPtr<ID3D11InputLayout> input_layout;
   std::array<ComPtr<ID3D11Buffer>, kCapturedVertexBufferCount> vertex_buffers;
   std::array<UINT, kCapturedVertexBufferCount> vertex_buffer_strides = {};
   std::array<UINT, kCapturedVertexBufferCount> vertex_buffer_offsets = {};
   GBFRBufferInfo vs_constant_buffer_slot1;
   GBFRBufferInfo ps_constant_buffer_slot1;
   ComPtr<ID3D11ShaderResourceView> ps_shader_resource_slot1;

   void Reset()
   {
      valid = false;
      vertex_shader = nullptr;
      input_layout = nullptr;
      for (auto& buffer : vertex_buffers)
      {
         buffer = nullptr;
      }
      vertex_buffer_strides.fill(0);
      vertex_buffer_offsets.fill(0);
      vs_constant_buffer_slot1.buffer = nullptr;
      vs_constant_buffer_slot1.num_constants = 0;
      vs_constant_buffer_slot1.first_constant = 0;
      ps_constant_buffer_slot1.buffer = nullptr;
      ps_constant_buffer_slot1.num_constants = 0;
      ps_constant_buffer_slot1.first_constant = 0;
      ps_shader_resource_slot1 = nullptr;
   }
};

struct GBFRMotionBlurReplayState
{
   static constexpr UINT kCapturedVertexBufferCount = 2;

   bool valid = false;
   ComPtr<ID3D11VertexShader> vertex_shader;
   ComPtr<ID3D11PixelShader> pixel_shader;
   ComPtr<ID3D11InputLayout> input_layout;
   D3D11_PRIMITIVE_TOPOLOGY primitive_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
   std::array<ComPtr<ID3D11Buffer>, kCapturedVertexBufferCount> vertex_buffers;
   std::array<UINT, kCapturedVertexBufferCount> vertex_buffer_strides = {};
   std::array<UINT, kCapturedVertexBufferCount> vertex_buffer_offsets = {};
   GBFRBufferInfo ps_cbuffer_b10;
   GBFRBufferInfo ps_cbuffer_b12;
   ComPtr<ID3D11ShaderResourceView> ps_srv_t0;
   ComPtr<ID3D11ShaderResourceView> ps_srv_t1;
   ComPtr<ID3D11ShaderResourceView> ps_srv_t4;
   ComPtr<ID3D11ShaderResourceView> ps_srv_t5;

   void Reset()
   {
      valid = false;
      vertex_shader = nullptr;
      pixel_shader = nullptr;
      input_layout = nullptr;
      primitive_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      for (auto& buffer : vertex_buffers)
      {
         buffer = nullptr;
      }
      vertex_buffer_strides.fill(0);
      vertex_buffer_offsets.fill(0);
      ps_cbuffer_b10.buffer = nullptr;
      ps_cbuffer_b10.num_constants = 0;
      ps_cbuffer_b10.first_constant = 0;
      ps_cbuffer_b12.buffer = nullptr;
      ps_cbuffer_b12.num_constants = 0;
      ps_cbuffer_b12.first_constant = 0;
      ps_srv_t0 = nullptr;
      ps_srv_t1 = nullptr;
      ps_srv_t4 = nullptr;
      ps_srv_t5 = nullptr;
   }
};

struct GBFROutlineReplayState
{
   static constexpr UINT kCapturedConstantBufferCount = 14;
   static constexpr UINT kCapturedSamplerCount = 16;

   bool valid = false;
   ComPtr<ID3D11ComputeShader> compute_shader;
   std::array<ComPtr<ID3D11Buffer>, kCapturedConstantBufferCount> cs_constant_buffers;
   std::array<ComPtr<ID3D11SamplerState>, kCapturedSamplerCount> cs_samplers;
   ComPtr<ID3D11ShaderResourceView> cs_depth_srv;
   ComPtr<ID3D11ShaderResourceView> cs_stencil_srv;

   void Reset()
   {
      valid = false;
      compute_shader = nullptr;
      for (auto& buffer : cs_constant_buffers)
      {
         buffer = nullptr;
      }
      for (auto& sampler : cs_samplers)
      {
         sampler = nullptr;
      }
      cs_depth_srv = nullptr;
      cs_stencil_srv = nullptr;
   }
};

struct GBFRCutscenePostPassReplayState
{
   bool valid = false;
   ComPtr<ID3D11VertexShader> vertex_shader;
   ComPtr<ID3D11PixelShader> pixel_shader;
   GBFRBufferInfo ps_constant_buffer_slot1;
   ComPtr<ID3D11SamplerState> ps_sampler_slot0;

   void Reset()
   {
      valid = false;
      vertex_shader = nullptr;
      pixel_shader = nullptr;
      ps_constant_buffer_slot1.buffer = nullptr;
      ps_constant_buffer_slot1.num_constants = 0;
      ps_constant_buffer_slot1.first_constant = 0;
      ps_sampler_slot0 = nullptr;
   }
};

struct GBFRPostProcessState
{
   GBFROutlineReplayState outline_replay_state;
   std::atomic<bool> outline_pending = false;
   ComPtr<ID3D11Texture2D> outline_resource;
   ComPtr<ID3D11ShaderResourceView> outline_srv;
   ComPtr<ID3D11UnorderedAccessView> outline_uav;

   std::array<GBFRMotionBlurReplayState, 2> motion_blur_replay_states;
   ComPtr<ID3D11Texture2D> motion_blur_intermediate_resource;
   ComPtr<ID3D11ShaderResourceView> motion_blur_intermediate_srv;
   ComPtr<ID3D11RenderTargetView> motion_blur_intermediate_rtv;
   ComPtr<ID3D11Texture2D> motion_blur_output_resource;
   ComPtr<ID3D11ShaderResourceView> motion_blur_output_srv;
   ComPtr<ID3D11RenderTargetView> motion_blur_output_rtv;
   std::atomic<bool> motion_blur_pending = false;
   bool motion_blur_seen = false;
   bool motion_blur_first_pass_seen = false;
   bool motion_blur_second_pass_seen = false;
   bool motion_blur_output_ready = false;
   uint32_t motion_blur_invocation_count = 0;

   std::atomic<bool> cutscene_overlay_blend_pending = false;
   std::atomic<bool> cutscene_overlay_modulate_pending = false;
   std::atomic<bool> cutscene_gamma_pending = false;
   std::atomic<bool> cutscene_color_grade_pending = false;

   ComPtr<ID3D11Texture2D> cutscene_intermediate_resource;
   ComPtr<ID3D11ShaderResourceView> cutscene_intermediate_srv;
   ComPtr<ID3D11RenderTargetView> cutscene_intermediate_rtv;

   ComPtr<ID3D11Texture2D> cutscene_gamma_resource;
   ComPtr<ID3D11ShaderResourceView> cutscene_gamma_srv;
   ComPtr<ID3D11RenderTargetView> cutscene_gamma_rtv;

   ComPtr<ID3D11Texture2D> cutscene_color_grade_resource;
   ComPtr<ID3D11ShaderResourceView> cutscene_color_grade_srv;
   ComPtr<ID3D11RenderTargetView> cutscene_color_grade_rtv;

   GBFRCutsceneOverlayBlendReplayState cutscene_overlay_blend_replay_state;
   GBFRCutsceneOverlayModulateReplayState cutscene_overlay_modulate_replay_state;
   GBFRCutscenePostPassReplayState cutscene_gamma_replay_state;
   GBFRCutscenePostPassReplayState cutscene_color_grade_replay_state;

   ComPtr<ID3D11Texture2D> cutscene_overlay_modulate_resource;
   ComPtr<ID3D11ShaderResourceView> cutscene_overlay_modulate_srv;
   ComPtr<ID3D11RenderTargetView> cutscene_overlay_modulate_rtv;
};
