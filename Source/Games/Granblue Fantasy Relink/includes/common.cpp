#include "..\..\Core\core.hpp"
#include "common.hpp"

GBFRShaderHashes g_shader_hashes;
GBFRRuntimeSettings g_runtime_settings;

ShaderHashesList<false>& shader_hashes_OutlinePrefilter = g_shader_hashes.outline_prefilter;
ShaderHashesList<false>& shader_hashes_OutlineCS = g_shader_hashes.outline_cs;
ShaderHashesList<false>& shader_hashes_Temporal_Upscale = g_shader_hashes.temporal_upscale;
ShaderHashesList<false>& shader_hashes_TAA = g_shader_hashes.taa;
ShaderHashesList<false>& shader_hashes_Tonemap = g_shader_hashes.tonemap;
ShaderHashesList<false>& shader_hashes_MotionBlur = g_shader_hashes.motion_blur;
ShaderHashesList<false>& shader_hashes_MotionBlurDenoise = g_shader_hashes.motion_blur_denoise;
ShaderHashesList<false>& shader_hashes_CutsceneGamma = g_shader_hashes.cutscene_gamma;
ShaderHashesList<false>& shader_hashes_CutsceneColorGrade = g_shader_hashes.cutscene_color_grade;
ShaderHashesList<false>& shader_hashes_CutsceneOverlayBlend = g_shader_hashes.cutscene_overlay_blend;
ShaderHashesList<false>& shader_hashes_CutsceneOverlayModulate = g_shader_hashes.cutscene_overlay_modulate;

float& render_scale = g_runtime_settings.render_scale;
bool& render_scale_changed = g_runtime_settings.render_scale_changed;

bool CreateOrRecreateTextureIfNeeded(GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, ComPtr<ID3D11Texture2D>& texture)
{
   bool needs_creation = false;

   if (texture.get() == nullptr)
   {
      needs_creation = true;
   }
   else
   {
      D3D11_TEXTURE2D_DESC existing_desc;
      texture->GetDesc(&existing_desc);
      if (existing_desc.Width != desc.Width ||
          existing_desc.Height != desc.Height)
      {
         needs_creation = true;
      }
   }

   if (needs_creation)
   {
      texture = nullptr;
      HRESULT hr = native_device->CreateTexture2D(&desc, nullptr, texture.put());
      if (FAILED(hr) || texture.get() == nullptr)
         ASSERT_ONCE_MSG(false, "Failed to create texture");
   }
   return needs_creation;
}

bool CreateOrRecreateTextureIfNeeded(GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& srv)
{
   if (CreateOrRecreateTextureIfNeeded(game_device_data, native_device, desc, texture))
   {
      srv = nullptr;
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = desc.Format;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      srv_desc.Texture2D.MostDetailedMip = 0;
      HRESULT hr = native_device->CreateShaderResourceView(texture.get(), &srv_desc, srv.put());
      if (FAILED(hr) || srv.get() == nullptr)
         ASSERT_ONCE_MSG(false, "Failed to create shader resource view");

      return true;
   }

   return false;
}

bool CreateOrRecreateTextureIfNeeded(GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& srv, ComPtr<ID3D11RenderTargetView>& rtv)
{
   if (CreateOrRecreateTextureIfNeeded(game_device_data, native_device, desc, texture))
   {
      srv = nullptr;
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = desc.Format;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      srv_desc.Texture2D.MostDetailedMip = 0;
      HRESULT hr = native_device->CreateShaderResourceView(texture.get(), &srv_desc, srv.put());
      if (FAILED(hr) || srv.get() == nullptr)
         ASSERT_ONCE_MSG(false, "Failed to create shader resource view");

      rtv = nullptr;
      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
      rtv_desc.Format = desc.Format;
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      rtv_desc.Texture2D.MipSlice = 0;
      hr = native_device->CreateRenderTargetView(texture.get(), &rtv_desc, rtv.put());
      if (FAILED(hr) || rtv.get() == nullptr)
         ASSERT_ONCE_MSG(false, "Failed to create render target view");

      return true;
   }

   return false;
}

ID3D11ShaderResourceView* GetPostAAColorInputSRV(const DeviceData& device_data, const GameDeviceDataGBFR& game_device_data)
{
   const bool use_sr_input = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
   return use_sr_input ? game_device_data.sr_output_color_srv.get()
                       : game_device_data.taa_temp_output_srv.get();
}

bool CanDrawNativeUIEncodePass(ID3D11ShaderResourceView* input_color_srv, const GameDeviceDataGBFR& game_device_data)
{
   return input_color_srv != nullptr && game_device_data.taa_output_texture_rtv.get() != nullptr;
}
