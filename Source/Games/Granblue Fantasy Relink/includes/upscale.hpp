#pragma once

struct GBFRUpscaleState
{
#if ENABLE_SR
   ComPtr<ID3D11Resource> sr_source_color;
   ComPtr<ID3D11ShaderResourceView> sr_source_color_srv;
   ComPtr<ID3D11Texture2D> pre_sr_encode_texture;
   ComPtr<ID3D11ShaderResourceView> pre_sr_encode_srv;
   ComPtr<ID3D11RenderTargetView> pre_sr_encode_rtv;
   ComPtr<ID3D11Texture2D> post_sr_encode_texture;
   ComPtr<ID3D11ShaderResourceView> post_sr_encode_srv;
   ComPtr<ID3D11RenderTargetView> post_sr_encode_rtv;
   ComPtr<ID3D11Resource> depth_buffer;
   ComPtr<ID3D11Resource> sr_motion_vectors;

   ComPtr<ID3D11Texture2D> sr_output_color;
   ComPtr<ID3D11ShaderResourceView> sr_output_color_srv;
   std::atomic<ID3D11DeviceContext*> draw_device_context = nullptr;

   std::atomic<ID3D11CommandList*> remainder_command_list = nullptr;
   ComPtr<ID3D11CommandList> partial_command_list;
   ComPtr<ID3D11Buffer> modifiable_index_vertex_buffer;
   std::atomic<bool> output_supports_uav = false;
   std::atomic<bool> output_changed = false;
   std::atomic<bool> tonemap_draw_pending = false;
#endif

   ComPtr<ID3D11Texture2D> exposure_texture;
   ComPtr<ID3D11ShaderResourceView> exposure_texture_srv;
   ComPtr<ID3D11ShaderResourceView> bloom_texture_srv;
   ComPtr<ID3D11Texture2D> taa_temp_output_resource;
   ComPtr<ID3D11ShaderResourceView> taa_temp_output_srv;
   ComPtr<ID3D11RenderTargetView> taa_temp_output_rtv;
   ComPtr<ID3D11Texture2D> taa_output_texture;
   ComPtr<ID3D11RenderTargetView> taa_output_texture_rtv;
};
