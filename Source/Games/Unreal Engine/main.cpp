#define GAME_UNREAL_ENGINE 1
#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1
#define UPGRADE_SAMPLERS 1
#define ENABLE_NGX 1

#include "..\..\Core\core.hpp"
#include "includes\shader_detect.hpp"

namespace
{
   static inline bool UE4_NearZero(float v, float eps)
   {
      return std::fabs(v) <= eps;
   }

   // Row-major projection shape check; falls back to transpose if needed
   static inline bool MatrixLikeProjection(const Math::Matrix44F& m, float eps = 1e-3f)
   {
      auto check = [&](const Math::Matrix44F& p) -> bool
      {
         // Rows 0–1 off-diagonals ~ 0
         if (!UE4_NearZero(p.m01, eps) || !UE4_NearZero(p.m02, eps) || !UE4_NearZero(p.m03, eps))
            return false;
         if (!UE4_NearZero(p.m10, eps) || !UE4_NearZero(p.m12, eps) || !UE4_NearZero(p.m13, eps))
            return false;

         // Perspective term and last row
         if (std::fabs(p.m23) < 0.95f)
            return false; // m23 ≈ ±1
         if (!UE4_NearZero(p.m30, eps) || !UE4_NearZero(p.m31, eps) || !UE4_NearZero(p.m33, eps))
            return false;

         // Depth terms (normal/reversed/infinite)
         if (UE4_NearZero(p.m22, eps) && UE4_NearZero(p.m32, eps))
            return false;

         return true;
      };

      if (check(m))
         return true;
      return false;
   }

   static inline bool ProjectionHasJitter(const Math::Matrix44F& m, float2 max_jitter, float eps = 1e-3f)
   {
      if ((m.m20 == 0.0f && m.m21 == 0.0f) || std::fabs(m.m20) > max_jitter.x || std::fabs(m.m21) > max_jitter.y)
      {
         return false;
      }
      return true;
   }

   static inline bool IsViewSizeInvSize(const float4& v, float aspect_ratio, float eps = 1e-3f)
   {
      if (v.x > 0.0f && v.y > 0.0f &&
          v.z > 0.0f && v.w > 0.0f)
      {
         const float inv_w = 1.0f / v.x;
         const float inv_h = 1.0f / v.y;
         if (std::fabs(v.z - inv_w) < FLT_EPSILON &&
             std::fabs(v.w - inv_h) < FLT_EPSILON)
         {
            if (std::fabs((v.x / v.y) - aspect_ratio) < eps)
            {
               return true;
            }
         }
      }
      return false;
   }

   ShaderHashesList shader_hashes_TAA;
   ShaderHashesList shader_hashes_TAA_Candidates;
   GlobalCBInfo     global_cb_info;
} // namespace

struct GameDeviceDataUnrealEngine final : public GameDeviceData
{
#if ENABLE_SR
   // SR
   com_ptr<ID3D11Texture2D>        sr_motion_vectors;
   com_ptr<ID3D11Resource>         sr_source_color;
   com_ptr<ID3D11Resource>         depth_buffer;
   com_ptr<ID3D11RenderTargetView> sr_motion_vectors_rtv;
   std::atomic<int32_t>            taa_motion_vector_texture_srv_index = -1;
   std::atomic<int32_t>            taa_depth_texture_srv_index         = -1;
   std::atomic<int32_t>            taa_source_color_texture_srv_index  = -1;
   std::atomic<bool>               found_per_view_globals              = false;
#endif // ENABLE_SR
   float4    render_resolution = {0.0f, 0.0f, 0.0f, 0.0f};
   float4    viewport_rect     = {0.0f, 0.0f, 0.0f, 0.0f};
   float2    jitter            = {0.0f, 0.0f};
   Matrix44F view_to_clip_matrix;
   Matrix44F clip_to_prev_clip_matrix;
};

class UnrealEngine final : public Game // ### Rename this to your game's name ###
{
   static GameDeviceDataUnrealEngine& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataUnrealEngine*>(device_data.game);
   }

public:
   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::map_buffer_region>(UnrealEngine::OnMapBufferRegion);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(UnrealEngine::OnUnmapBufferRegion);
      }
   }
   void OnInit(bool async) override
   {
      // ### Update these (find the right values) ###
      // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      native_shaders_definitions.emplace(CompileTimeStringHash("Decode MVs"), ShaderDefinition{"Luma_MotionVec_UE4_Decode", reshade::api::pipeline_subobject_type::pixel_shader});
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index     = 12;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game       = new GameDeviceDataUnrealEngine;
      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.view_to_clip_matrix.SetIdentity();
      game_device_data.clip_to_prev_clip_matrix.SetIdentity();
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data      = *swapchain->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      // Start from here, we then update it later in case the game rendered with black bars due to forcing a different aspect ratio from the swapchain buffer
      game_device_data.render_resolution = {device_data.render_resolution.x, device_data.render_resolution.y, 1.0f / device_data.render_resolution.x, 1.0f / device_data.render_resolution.y};
      game_device_data.viewport_rect     = {0.0f, 0.0f, device_data.render_resolution.x, device_data.render_resolution.y};
   }

#if ENABLE_SR
   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash = -1, const std::byte* shader_object = nullptr, size_t shader_object_size = 0) override
   {
      // TAA was already detected
      if (type != reshade::api::pipeline_subobject_type::pixel_shader && type != reshade::api::pipeline_subobject_type::compute_shader)
         return nullptr;
      if (!shader_hashes_TAA.Empty())
         return nullptr;
      bool is_taa_candidate = IsUE4TAACandidate(code, size);
      if (is_taa_candidate)
      {
         reshade::log::message(reshade::log::level::info, std::format("UE4: Detected UE4 TAA shader. Hash: {:016X}", shader_hash).c_str());
         if (type == reshade::api::pipeline_subobject_type::pixel_shader)
            shader_hashes_TAA_Candidates.pixel_shaders.emplace(static_cast<unsigned long>(shader_hash));
         else if (type == reshade::api::pipeline_subobject_type::compute_shader)
            shader_hashes_TAA_Candidates.compute_shaders.emplace(static_cast<unsigned long>(shader_hash));

         if (global_cb_info.clip_to_prev_clip_start_index == -1)
            FindGlobalCBInfo(code, size, global_cb_info);
         return nullptr;
      }
      return nullptr; // Return nullptr to use the original shader
   }
#endif // ENABLE_SR

   bool OnDrawCustom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers) override
   {
      GameDeviceDataUnrealEngine& game_device_data = GetGameDeviceData(device_data);
      bool                        is_taa           = original_shader_hashes.Contains(shader_hashes_TAA);
      bool                        is_taa_candidate = !is_taa && original_shader_hashes.Contains(shader_hashes_TAA_Candidates);
      // this is the first time we detected this shader as TAA, we should verify it's really TAA
#if ENABLE_SR
      if (is_taa_candidate)
      {
         // verify it's really TAA by checking the SRV signatures, there should be 2 color textures, a depth texture(R32G8X24 or other depth stencil formats) and a velocity texture(unorm RG)
         // we can also check the sampler states, there should be point and linear filtering samplers (we can do this later)

         com_ptr<ID3D11ShaderResourceView> shader_resources[16];
         ASSERT_ONCE(shader_hashes_TAA_Candidates.pixel_shaders.empty() != shader_hashes_TAA_Candidates.compute_shaders.empty());
         bool is_compute_shader = shader_hashes_TAA_Candidates.compute_shaders.empty() ? false : true;
         if (is_compute_shader)
            native_device_context->CSGetShaderResources(0, ARRAYSIZE(shader_resources), &shader_resources[0]);
         else
            native_device_context->PSGetShaderResources(0, ARRAYSIZE(shader_resources), &shader_resources[0]);
         size_t color_texture_count    = 0;
         size_t depth_texture_count    = 0;
         size_t velocity_texture_count = 0;
         for (size_t i = 0; i < ARRAYSIZE(shader_resources); i++)
         {
            if (shader_resources[i] == nullptr)
               continue;
            com_ptr<ID3D11Resource> resource;
            shader_resources[i]->GetResource(&resource);
            if (resource == nullptr)
               continue;
            D3D11_RESOURCE_DIMENSION res_type;
            resource->GetType(&res_type);
            if (res_type != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
               continue;
            com_ptr<ID3D11Texture2D> texture2d = (ID3D11Texture2D*)resource.get();
            D3D11_TEXTURE2D_DESC     desc;
            texture2d->GetDesc(&desc);
            // check format
            switch (desc.Format)
            {
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            case DXGI_FORMAT_R10G10B10A2_UNORM:
            case DXGI_FORMAT_R11G11B10_FLOAT:
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
               color_texture_count++;
               // assume lowest index is the main color texture
               if (game_device_data.taa_source_color_texture_srv_index == -1)
                  game_device_data.taa_source_color_texture_srv_index = (uint32_t)i;
               break;
            case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
            case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            case DXGI_FORMAT_R24G8_TYPELESS:
            case DXGI_FORMAT_R32G8X24_TYPELESS:
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
            case DXGI_FORMAT_D16_UNORM:
               depth_texture_count++;
               if (game_device_data.taa_depth_texture_srv_index == -1)
                  game_device_data.taa_depth_texture_srv_index = (uint32_t)i;
               break;
            case DXGI_FORMAT_R16G16_UNORM:
               velocity_texture_count++;
               game_device_data.taa_motion_vector_texture_srv_index = (uint32_t)i;
               break;
            default:
               break;
            }
         }
         // we should have at least 2 color textures, 1 depth texture and 1 velocity texture
         if (color_texture_count >= 2 && depth_texture_count >= 1 && velocity_texture_count >= 1)
         {
            is_taa = true;
            // add to the confirmed TAA shaders
            if (is_compute_shader)
               shader_hashes_TAA.compute_shaders.emplace(static_cast<unsigned long>(*original_shader_hashes.compute_shaders.begin()));
            else
               shader_hashes_TAA.pixel_shaders.emplace(static_cast<unsigned long>(*original_shader_hashes.pixel_shaders.begin()));
         }
         else
         {
            game_device_data.taa_source_color_texture_srv_index  = -1;
            game_device_data.taa_depth_texture_srv_index         = -1;
            game_device_data.taa_motion_vector_texture_srv_index = -1;
            if (is_compute_shader)
               shader_hashes_TAA_Candidates.compute_shaders.erase(static_cast<unsigned long>(*original_shader_hashes.compute_shaders.begin()));
            else
               shader_hashes_TAA_Candidates.pixel_shaders.erase(static_cast<unsigned long>(*original_shader_hashes.pixel_shaders.begin()));
            ASSERT_ONCE(!(shader_hashes_TAA_Candidates.Empty() && shader_hashes_TAA.Empty()));
            // shader_hashes_TAA_Rejected_Candidates.pixel_shaders.insert(original_shader_hashes.pixel_shaders.begin(), original_shader_hashes.pixel_shaders.end());
         }
      }

      // if we already drew SR this frame, copy dlss output to shader output (some games run different quality settings in the same frame?)
      if (is_taa && device_data.has_drawn_sr)
      {
         ASSERT_ONCE(shader_hashes_TAA.pixel_shaders.empty() != shader_hashes_TAA.compute_shaders.empty());
         bool                               is_compute_shader = shader_hashes_TAA.compute_shaders.empty() ? false : true;
         com_ptr<ID3D11RenderTargetView>    render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]; // There should only be 1 or 2
         com_ptr<ID3D11DepthStencilView>    depth_stencil_view;
         com_ptr<ID3D11UnorderedAccessView> unordered_access_views[D3D11_PS_CS_UAV_REGISTER_COUNT];
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
         native_device_context->CSGetUnorderedAccessViews(0, ARRAYSIZE(unordered_access_views), &unordered_access_views[0]);
         com_ptr<ID3D11Resource> output_color_resource;

         if (is_compute_shader)
            unordered_access_views[0]->GetResource(&output_color_resource);
         else
            render_target_views[0]->GetResource(&output_color_resource);

         com_ptr<ID3D11Texture2D> output_color;
         HRESULT                  hr = output_color_resource->QueryInterface(&output_color);
         ASSERT_ONCE(SUCCEEDED(hr));
         if (device_data.sr_output_color.get() && output_color.get())
         {
            native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get());
         }
         return true;
      }

      if (is_taa && device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
      {
         if (device_data.native_pixel_shaders[CompileTimeStringHash("Decode MVs")].get() == nullptr)
         {
            device_data.force_reset_sr = true;
            return false;
         }
         ASSERT_ONCE(shader_hashes_TAA.pixel_shaders.empty() != shader_hashes_TAA.compute_shaders.empty());
         bool                              is_compute_shader = shader_hashes_TAA_Candidates.compute_shaders.empty() ? false : true;
         com_ptr<ID3D11ShaderResourceView> shader_resources[16];

         if (is_compute_shader)
            native_device_context->CSGetShaderResources(0, ARRAYSIZE(shader_resources), &shader_resources[0]);
         else
            native_device_context->PSGetShaderResources(0, ARRAYSIZE(shader_resources), &shader_resources[0]);

         com_ptr<ID3D11RenderTargetView>    render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]; // There should only be 1 or 2
         com_ptr<ID3D11DepthStencilView>    depth_stencil_view;
         com_ptr<ID3D11UnorderedAccessView> unordered_access_views[D3D11_PS_CS_UAV_REGISTER_COUNT];
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
         native_device_context->CSGetUnorderedAccessViews(0, ARRAYSIZE(unordered_access_views), &unordered_access_views[0]);

         if (global_cb_info.size == 0)
         {
            // The first time we run TAA, we can get the global cbuffer size now
            // we can then use this to detect the cbuffer in the CPU during OnMapBufferRegion and OnUnmapBufferRegion hooks
            com_ptr<ID3D11Buffer> global_cbuffer;
            if (is_compute_shader)
               native_device_context->CSGetConstantBuffers(global_cb_info.register_index, 1, &global_cbuffer);
            else
               native_device_context->PSGetConstantBuffers(global_cb_info.register_index, 1, &global_cbuffer);
            ASSERT_ONCE(global_cbuffer != nullptr);
            D3D11_BUFFER_DESC global_cbuffer_desc;
            global_cbuffer->GetDesc(&global_cbuffer_desc);
            global_cb_info.size = global_cbuffer_desc.ByteWidth;

            return false; // Skip this draw call, we will run DLSS next frame after we detected the global cbuffer on the CPU
         }
         const bool dlss_inputs_valid = shader_resources[game_device_data.taa_source_color_texture_srv_index].get() != nullptr && shader_resources[game_device_data.taa_depth_texture_srv_index].get() != nullptr && shader_resources[game_device_data.taa_motion_vector_texture_srv_index].get() != nullptr && (render_target_views[0].get() != nullptr || unordered_access_views[0].get() != nullptr);
         ASSERT_ONCE(dlss_inputs_valid);
         if (dlss_inputs_valid)
         {
            if (game_device_data.found_per_view_globals.load() == false || global_cb_info.clip_to_prev_clip_start_index == -1)
               return false;
            auto* sr_instance_data = device_data.GetSRInstanceData();
            ASSERT_ONCE(sr_instance_data);

            com_ptr<ID3D11Resource> output_color_resource;
            if (is_compute_shader)
               unordered_access_views[0]->GetResource(&output_color_resource);
            else
               render_target_views[0]->GetResource(&output_color_resource);
            com_ptr<ID3D11Texture2D> output_color;
            HRESULT                  hr = output_color_resource->QueryInterface(&output_color);
            ASSERT_ONCE(SUCCEEDED(hr));

            D3D11_TEXTURE2D_DESC taa_output_texture_desc;
            output_color->GetDesc(&taa_output_texture_desc);

            if (taa_output_texture_desc.Width != device_data.render_resolution.x || taa_output_texture_desc.Height != device_data.render_resolution.y)
               return false;

            D3D11_VIEWPORT viewport;
            uint32_t       num_viewports = 1;
            native_device_context->RSGetViewports(&num_viewports, &viewport);
            // game_device_data.viewport_rect         = {viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height};
            // game_device_data.render_resolution     = {(float)taa_output_texture_desc.Width, (float)taa_output_texture_desc.Height, 1.0f / (float)taa_output_texture_desc.Width, 1.0f / (float)taa_output_texture_desc.Height};
            device_data.sr_render_resolution_scale = 1.0f; // DLLA only

            SR::SettingsData settings_data;
            settings_data.output_width              = game_device_data.render_resolution.x;
            settings_data.output_height             = game_device_data.render_resolution.y;
            settings_data.render_width              = game_device_data.render_resolution.x;
            settings_data.render_height             = game_device_data.render_resolution.y;
            settings_data.dynamic_resolution        = false;
            settings_data.hdr                       = true; // Unreal Engine does DLSS before tonemapping, in HDR linear space
            settings_data.inverted_depth            = true;
            settings_data.mvs_jittered              = false;
            settings_data.auto_exposure             = true; // Unreal Engine does TAA before tonemapping
            settings_data.use_experimental_features = sr_user_type == SR::UserType::DLSS_TRANSFORMER;
            sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

            constexpr bool dlss_use_native_uav      = true;
            bool           dlss_output_supports_uav = dlss_use_native_uav && (taa_output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;

            bool skip_dlss           = taa_output_texture_desc.Width < sr_instance_data->min_resolution || taa_output_texture_desc.Height < sr_instance_data->min_resolution;
            bool dlss_output_changed = false;
            // Create a copy that supports Unordered Access if it wasn't already supported
            if (!dlss_output_supports_uav)
            {
               D3D11_TEXTURE2D_DESC dlss_output_texture_desc = taa_output_texture_desc;
               dlss_output_texture_desc.Width                = std::lrintf(game_device_data.render_resolution.x);
               dlss_output_texture_desc.Height               = std::lrintf(game_device_data.render_resolution.y);
               dlss_output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

               if (device_data.sr_output_color.get())
               {
                  D3D11_TEXTURE2D_DESC prev_dlss_output_texture_desc;
                  device_data.sr_output_color->GetDesc(&prev_dlss_output_texture_desc);
                  dlss_output_changed = prev_dlss_output_texture_desc.Width != dlss_output_texture_desc.Width || prev_dlss_output_texture_desc.Height != dlss_output_texture_desc.Height || prev_dlss_output_texture_desc.Format != dlss_output_texture_desc.Format;
               }
               if (!device_data.sr_output_color.get() || dlss_output_changed)
               {
                  device_data.sr_output_color = nullptr; // Make sure we discard the previous one
                  hr                          = native_device->CreateTexture2D(&dlss_output_texture_desc, nullptr, &device_data.sr_output_color);
                  ASSERT_ONCE(SUCCEEDED(hr));
               }
               // Texture creation failed, we can't proceed with DLSS
               if (!device_data.sr_output_color.get())
               {
                  skip_dlss = true;
               }
            }
            else
            {
               ASSERT_ONCE(device_data.sr_output_color == nullptr);
               device_data.sr_output_color = output_color;
            }
            if (!skip_dlss)
            {
               game_device_data.sr_source_color = nullptr;
               shader_resources[game_device_data.taa_source_color_texture_srv_index.load()]->GetResource(&game_device_data.sr_source_color);
               game_device_data.depth_buffer = nullptr;
               shader_resources[game_device_data.taa_depth_texture_srv_index.load()]->GetResource(&game_device_data.depth_buffer);
               com_ptr<ID3D11Resource> object_velocity;
               shader_resources[game_device_data.taa_motion_vector_texture_srv_index.load()]->GetResource(&object_velocity);
               {
                  if (!AreResourcesEqual(object_velocity.get(), game_device_data.sr_motion_vectors.get(), false /*check_format*/))
                  {
                     com_ptr<ID3D11Texture2D> object_velocity_texture;
                     hr = object_velocity->QueryInterface(&object_velocity_texture);
                     ASSERT_ONCE(SUCCEEDED(hr));
                     D3D11_TEXTURE2D_DESC object_velocity_texture_desc;
                     object_velocity_texture->GetDesc(&object_velocity_texture_desc);
                     ASSERT_ONCE((object_velocity_texture_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == D3D11_BIND_RENDER_TARGET);
#if 1 // Use the higher quality for MVs, the game's one were R16G16F. This has a ~1% cost on performance but helps with reducing shimmering on fine lines (stright lines looking segmented, like Bart's hair or Shark's teeth) when the camera is moving in a linear fashion.
                     object_velocity_texture_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
#else // Note: for FF7, 16bit might be enough, to be tried and compared, but the extra precision won't hurt
                     object_velocity_texture_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
#endif

                     game_device_data.sr_motion_vectors = nullptr; // Make sure we discard the previous one
                     hr                                 = native_device->CreateTexture2D(&object_velocity_texture_desc, nullptr, &game_device_data.sr_motion_vectors);
                     ASSERT_ONCE(SUCCEEDED(hr));

                     game_device_data.sr_motion_vectors_rtv = nullptr; // Make sure we discard the previous one
                     if (SUCCEEDED(hr))
                     {
                        hr = native_device->CreateRenderTargetView(game_device_data.sr_motion_vectors.get(), nullptr, &game_device_data.sr_motion_vectors_rtv);
                        ASSERT_ONCE(SUCCEEDED(hr));
                     }
                  }

                  com_ptr<ID3D11VertexShader>  prev_shader_vx;
                  com_ptr<ID3D11PixelShader>   prev_shader_px;
                  com_ptr<ID3D11ComputeShader> prev_shader_cs;
                  native_device_context->VSGetShader(&prev_shader_vx, nullptr, nullptr);
                  native_device_context->PSGetShader(&prev_shader_px, nullptr, nullptr);
                  native_device_context->CSGetShader(&prev_shader_cs, nullptr, nullptr);
                  D3D11_PRIMITIVE_TOPOLOGY primitive_topology;
                  native_device_context->IAGetPrimitiveTopology(&primitive_topology);

                  // Set up for motion vector shader
                  ID3D11RenderTargetView* const dlss_motion_vectors_rtv_const = game_device_data.sr_motion_vectors_rtv.get();
                  native_device_context->OMSetRenderTargets(1, &dlss_motion_vectors_rtv_const, nullptr);

                  // We only need to swap the pixel/vertex shaders, depth and blend were already in the right state
                  native_device_context->VSSetShader(device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get(), nullptr, 0);
                  native_device_context->PSSetShader(device_data.native_pixel_shaders[CompileTimeStringHash("Decode MVs")].get(), nullptr, 0);
                  native_device_context->CSSetShader(nullptr, nullptr, 0);

                  // We could probably keep the original vertex shader too, but whatever
                  native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                  // native_device_context->IASetInputLayout(nullptr); // Seemengly not needed
                  // native_device_context->RSSetState(nullptr); // Seemengly not needed

                  // If TAA is a compute shader we need to copy the relevant SRVs to the pixel shader stage
                  com_ptr<ID3D11ShaderResourceView> srv_copy_0                 = nullptr;
                  com_ptr<ID3D11ShaderResourceView> srv_copy_1                 = nullptr;
                  com_ptr<ID3D11SamplerState>       sampler_state              = nullptr;
                  com_ptr<ID3D11Buffer>             prev_cbuffer               = nullptr;
                  com_ptr<ID3D11Buffer>             prev_luma_settings_cbuffer = nullptr;
                  com_ptr<ID3D11Buffer>             prev_luma_data_cbuffer     = nullptr;
                  com_ptr<ID3D11Buffer>             global_cbuffer;

                  if (is_compute_shader)
                     native_device_context->CSGetConstantBuffers(global_cb_info.register_index, 1, &global_cbuffer);
                  else
                     native_device_context->PSGetConstantBuffers(global_cb_info.register_index, 1, &global_cbuffer);
                  native_device_context->PSGetShaderResources(0, 1, &srv_copy_0);
                  native_device_context->PSGetShaderResources(1, 1, &srv_copy_1);
                  ID3D11ShaderResourceView* const depth_srv = shader_resources[game_device_data.taa_depth_texture_srv_index.load()].get();
                  native_device_context->PSSetShaderResources(0, 1, &depth_srv);
                  ID3D11ShaderResourceView* const motion_vector_srv = shader_resources[game_device_data.taa_motion_vector_texture_srv_index.load()].get();
                  native_device_context->PSSetShaderResources(1, 1, &motion_vector_srv);

                  native_device_context->PSGetSamplers(0, 1, &sampler_state);
                  // point sampler from core.hpp
                  ID3D11SamplerState* const sampler_state_point = device_data.sampler_state_point.get();
                  native_device_context->PSSetSamplers(0, 1, &sampler_state_point);

                  native_device_context->PSGetConstantBuffers(1, 1, &prev_cbuffer);
                  native_device_context->PSGetConstantBuffers(luma_settings_cbuffer_index, 1, &prev_luma_settings_cbuffer);
                  native_device_context->PSGetConstantBuffers(luma_data_cbuffer_index, 1, &prev_luma_data_cbuffer);
                  ID3D11Buffer* const cb1             = global_cbuffer.get();
                  ID3D11Buffer* const settings_buffer = device_data.luma_global_settings.get();
                  ID3D11Buffer* const data_buffer     = device_data.luma_instance_data.get();

                  native_device_context->PSSetConstantBuffers(1, 1, &cb1);
                  native_device_context->PSSetConstantBuffers(luma_settings_cbuffer_index, 1, &settings_buffer);
                  native_device_context->PSSetConstantBuffers(luma_data_cbuffer_index, 1, &data_buffer);

                  // Finally draw:
                  native_device_context->Draw(4, 0);
                  // native_device_context->DrawIndexed(3, 6, 0); // Original call would have been this, but we swap the pixel and vertex shaders

#if DEVELOPMENT
                  const std::shared_lock lock_trace(s_mutex_trace);
                  if (trace_running)
                  {
                     const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                     TraceDrawCallData      trace_draw_call_data;
                     trace_draw_call_data.type         = TraceDrawCallData::TraceDrawCallType::Custom;
                     trace_draw_call_data.command_list = native_device_context;
                     trace_draw_call_data.custom_name  = "SR Decode Motion Vectors";
                     // Re-use the RTV data for simplicity
                     GetResourceInfo(game_device_data.sr_motion_vectors.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                     cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                  }
#endif

                  // Restore the state
                  native_device_context->VSSetShader(prev_shader_vx.get(), nullptr, 0);
                  native_device_context->PSSetShader(prev_shader_px.get(), nullptr, 0);
                  native_device_context->CSSetShader(prev_shader_cs.get(), nullptr, 0);

                  // Restore SRVs
                  ID3D11ShaderResourceView* const copy0 = srv_copy_0.get();
                  native_device_context->PSSetShaderResources(0, 1, &copy0);
                  ID3D11ShaderResourceView* const copy1 = srv_copy_1.get();
                  native_device_context->PSSetShaderResources(1, 1, &copy1);
                  // Restore sampler
                  ID3D11SamplerState* const sampler_state_ptr = sampler_state.get();
                  native_device_context->PSSetSamplers(0, 1, &sampler_state_ptr);
                  // Restore cbuffer
                  ID3D11Buffer* const prev_cbuffer_ptr = prev_cbuffer.get();
                  native_device_context->PSSetConstantBuffers(1, 1, &prev_cbuffer_ptr);
                  ID3D11Buffer* const prev_luma_settings_cbuffer_ptr = prev_luma_settings_cbuffer.get();
                  native_device_context->PSSetConstantBuffers(luma_settings_cbuffer_index, 1, &prev_luma_settings_cbuffer_ptr);
                  ID3D11Buffer* const prev_luma_data_cbuffer_ptr = prev_luma_data_cbuffer.get();
                  native_device_context->PSSetConstantBuffers(luma_data_cbuffer_index, 1, &prev_luma_data_cbuffer_ptr);

                  native_device_context->IASetPrimitiveTopology(primitive_topology);
               }

               bool reset_sr              = device_data.force_reset_sr || dlss_output_changed;
               device_data.force_reset_sr = false;

               SR::SuperResolutionImpl::DrawData draw_data;
               draw_data.source_color   = game_device_data.sr_source_color.get();
               draw_data.output_color   = device_data.sr_output_color.get();
               draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
               draw_data.depth_buffer   = game_device_data.depth_buffer.get();
               draw_data.pre_exposure   = 0.0f; // automatic exposure
               draw_data.jitter_x       = game_device_data.jitter.x * game_device_data.render_resolution.x * 0.5f;
               draw_data.jitter_y       = game_device_data.jitter.y * game_device_data.render_resolution.y * -0.5f;
               draw_data.reset          = reset_sr;
               draw_data.render_width   = game_device_data.render_resolution.x;
               draw_data.render_height  = game_device_data.render_resolution.y;

               bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
               if (dlss_succeeded)
               {
                  device_data.has_drawn_sr = true;
               }
               game_device_data.sr_source_color = nullptr;
               game_device_data.depth_buffer    = nullptr;

               // Restore the previous state
               ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(render_target_views[0]);
               native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, depth_stencil_view.get());

               if (device_data.has_drawn_sr)
               {
#if DEVELOPMENT
                  const std::shared_lock lock_trace(s_mutex_trace);
                  if (trace_running)
                  {
                     const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                     TraceDrawCallData      trace_draw_call_data;
                     trace_draw_call_data.type         = TraceDrawCallData::TraceDrawCallType::Custom;
                     trace_draw_call_data.command_list = native_device_context;
                     trace_draw_call_data.custom_name  = "DLSS";
                     // Re-use the RTV data for simplicity
                     GetResourceInfo(device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                     cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                  }
#endif

                  if (!dlss_output_supports_uav)
                  {
                     native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get()); // DX11 doesn't need barriers
                  }
                  else
                  {
                     device_data.sr_output_color = nullptr;
                  }

                  return true;
               }
               else
               {
                  // ASSERT_ONCE(false);
                  // cb_luma_global_settings.SRType = 0;
                  // device_data.cb_luma_global_settings_dirty = true;
                  // device_data.sr_suppressed = true;
                  device_data.force_reset_sr = true;
               }
            }
            if (dlss_output_supports_uav)
            {
               device_data.sr_output_color = nullptr;
            }
         }
      }
#endif
      return false;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data                  = GetGameDeviceData(device_data);
      game_device_data.found_per_view_globals = false;
      device_data.has_drawn_sr                = false;
      game_device_data.jitter                 = {0.0f, 0.0f};
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      auto& game_device_data            = GetGameDeviceData(device_data);
      data.GameData.ViewportRect        = game_device_data.viewport_rect;
      data.GameData.RenderResolution    = game_device_data.render_resolution;
      data.GameData.ClipToPrevClip      = game_device_data.clip_to_prev_clip_matrix;
      data.GameData.JitterOffset.x      = game_device_data.jitter.x;
      data.GameData.JitterOffset.y      = game_device_data.jitter.y;
      data.GameData.ClipToPrevClipIndex = global_cb_info.clip_to_prev_clip_start_index;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Unreal Engine Generic Luma mod - about and credits section", ""); // ### Rename this ###
   }

   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      ID3D11Device* native_device    = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer           = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData&   device_data      = *device->get_private_data<DeviceData>();
      auto&         game_device_data = GetGameDeviceData(device_data);

      if (game_device_data.found_per_view_globals)
      {
         return;
      }

      if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard || access == reshade::api::map_access::read_write)
      {
         D3D11_BUFFER_DESC buffer_desc;
         buffer->GetDesc(&buffer_desc);

         if (buffer_desc.ByteWidth == global_cb_info.size)
         {
            device_data.cb_per_view_global_buffer = buffer;
#if DEVELOPMENT
            // These are the classic "features" of cbuffer 13 (the one we are looking for), in case any of these were different, it could possibly mean we are looking at the wrong buffer here.
            ASSERT_ONCE(buffer_desc.Usage == D3D11_USAGE_DYNAMIC && buffer_desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER && buffer_desc.CPUAccessFlags == D3D11_CPU_ACCESS_WRITE && buffer_desc.MiscFlags == 0 && buffer_desc.StructureByteStride == 0);
#endif // DEVELOPMENT
            ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data);
            device_data.cb_per_view_global_buffer_map_data = *data;
         }
      }
   }

   static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
   {
      ID3D11Device* native_device    = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer           = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData&   device_data      = *device->get_private_data<DeviceData>();
      auto&         game_device_data = GetGameDeviceData(device_data);

      // Already decided this frame
      if (game_device_data.found_per_view_globals)
      {
         device_data.cb_per_view_global_buffer_map_data = nullptr;
         device_data.cb_per_view_global_buffer          = nullptr;
         return;
      }

      const bool is_global_cbuffer = device_data.cb_per_view_global_buffer != nullptr &&
                                     device_data.cb_per_view_global_buffer == buffer;
      ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data || is_global_cbuffer);
      if (!is_global_cbuffer || device_data.cb_per_view_global_buffer_map_data == nullptr)
         return;

      float4*      float_data = reinterpret_cast<float4*>(device_data.cb_per_view_global_buffer_map_data);
      const size_t size_float = static_cast<size_t>(global_cb_info.size) / sizeof(float4);

      const bool have_offsets =
         (global_cb_info.view_to_clip_start_index >= 0 && global_cb_info.view_size_and_inv_size_index >= 0);

      // If offsets are known, never rescan; just validate and return.
      if (have_offsets)
      {
         float4    vsize_and_inv_size = float_data[global_cb_info.view_size_and_inv_size_index];
         Matrix44F matrix_a;
         std::memcpy(&matrix_a, &float_data[global_cb_info.view_to_clip_start_index], sizeof(Matrix44F));
         bool is_global_cb = IsViewSizeInvSize(vsize_and_inv_size, device_data.render_resolution.x / device_data.render_resolution.y) && MatrixLikeProjection(matrix_a) && ProjectionHasJitter(matrix_a, {vsize_and_inv_size.z, vsize_and_inv_size.w});
         if (is_global_cb)
         {
            game_device_data.jitter.x               = matrix_a.m20;
            game_device_data.jitter.y               = matrix_a.m21;
            device_data.render_resolution.x         = vsize_and_inv_size.x;
            device_data.render_resolution.y         = vsize_and_inv_size.y;
            game_device_data.found_per_view_globals = true;
         }
      }
      else
      {
         global_cb_info.view_size_and_inv_size_index = -1;
         global_cb_info.view_to_clip_start_index     = -1;

         for (size_t i = 0; i + 1 < size_float; ++i)
         {
            const float4 vsize_and_inv_size = float_data[i];
            bool         is_vsize_inv_size  = IsViewSizeInvSize(vsize_and_inv_size, device_data.render_resolution.x / device_data.render_resolution.y);
            if (is_vsize_inv_size)
            {
               global_cb_info.view_size_and_inv_size_index = static_cast<int>(i);
               break;
            }
         }

         if (global_cb_info.view_size_and_inv_size_index < 0)
         {
            device_data.cb_per_view_global_buffer_map_data = nullptr;
            device_data.cb_per_view_global_buffer          = nullptr;
            return;
         }

         // Now scan for adjacent matrix pairs that look like ViewToClip / ClipToView
         // Should be before the view size index so we can stop then, specially because previous matrices are sometimes towards the end.
         Matrix44F matrix_a;
         size_t    stopping_index     = static_cast<size_t>(global_cb_info.view_size_and_inv_size_index);
         float4    vsize_and_inv_size = float_data[global_cb_info.view_size_and_inv_size_index];
         for (size_t i = 0; i + 4 <= stopping_index; ++i)
         {
            std::memcpy(&matrix_a, &float_data[i], sizeof(Matrix44F));
            bool is_projection = MatrixLikeProjection(matrix_a) && ProjectionHasJitter(matrix_a, {vsize_and_inv_size.z, vsize_and_inv_size.w});
            if (is_projection)
            {
               game_device_data.jitter.x               = matrix_a.m20;
               game_device_data.jitter.y               = matrix_a.m21;
               global_cb_info.view_to_clip_start_index = static_cast<int>(i);
               device_data.render_resolution.x         = vsize_and_inv_size.x;
               device_data.render_resolution.y         = vsize_and_inv_size.y;
               game_device_data.found_per_view_globals = true;
               break;
            }
         }
      }
      UpdateLODBias(device);
      device_data.cb_per_view_global_buffer_map_data = nullptr;
      device_data.cb_per_view_global_buffer          = nullptr;
   }

   static void UpdateLODBias(reshade::api::device* device)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);

         const auto prev_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.taa_detected && device_data.cloned_pipeline_count != 0)
         {
            device_data.texture_mip_lod_bias_offset = std::log2(device_data.render_resolution.y / device_data.output_resolution.y) - 1.f; // This results in -1 at output res
         }
         else
         {
            // Reset to best fallback value.
            // This bias offset replaces the value from the game (see "samplers_upgrade_mode" 5), which was based on the "r_AntialiasingTSAAMipBias" cvar for most textures (it doesn't apply to all the ones that would benefit from it, and still applies to ones that exhibit moire patterns),
            // but only if TAA was engaged (not SMAA or SMAA+TAA) (it might persist on SMAA after once using TAA, due to a bug).
            // Prey defaults that to 0 but Luma's configs set it to -1.
            device_data.texture_mip_lod_bias_offset = device_data.taa_detected ? -1.f : 0.f;
         }
         const auto new_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;

         bool texture_mip_lod_bias_offset_changed = prev_texture_mip_lod_bias_offset != new_texture_mip_lod_bias_offset;
         // Re-create all samplers immediately here instead of doing it at the end of the frame.
         // This allows us to avoid possible (but very unlikely) hitches that could happen if we re-created a new sampler for a new resolution later on when samplers descriptors are set.
         // It also allows us to use the right samplers for this frame's resolution.
         if (texture_mip_lod_bias_offset_changed)
         {
            ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
            for (auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
            {
               if (samplers_handle.second.contains(new_texture_mip_lod_bias_offset))
                  continue; // Skip "resolutions" that already got their custom samplers created
               ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(samplers_handle.first);
               D3D11_SAMPLER_DESC  native_desc;
               native_sampler->GetDesc(&native_desc);
               shared_lock_samplers.unlock(); // This is fine!
               {
                  std::unique_lock unique_lock_samplers(s_mutex_samplers);
                  samplers_handle.second[new_texture_mip_lod_bias_offset] = CreateCustomSampler(device_data, native_device, native_desc);
               }
               shared_lock_samplers.lock();
            }
         }
      }
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Unreal Engine Generic Luma mod"); // ### Rename this ###
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type        = SwapchainUpgradeType::None;
      texture_format_upgrades_type  = TextureFormatUpgradesType::None;
      // ### Check which of these are needed and remove the rest ###
      // texture_upgrade_formats = {
      //       reshade::api::format::r8g8b8a8_unorm,
      //       reshade::api::format::r8g8b8a8_unorm_srgb,
      //       reshade::api::format::r8g8b8a8_typeless,
      //       reshade::api::format::r8g8b8x8_unorm,
      //       reshade::api::format::r8g8b8x8_unorm_srgb,
      //       reshade::api::format::b8g8r8a8_unorm,
      //       reshade::api::format::b8g8r8a8_unorm_srgb,
      //       reshade::api::format::b8g8r8a8_typeless,
      //       reshade::api::format::b8g8r8x8_unorm,
      //       reshade::api::format::b8g8r8x8_unorm_srgb,
      //       reshade::api::format::b8g8r8x8_typeless,

      //       reshade::api::format::r11g11b10_float,
      // };
      // ### Check these if textures are not upgraded ###
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      game = new UnrealEngine();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(UnrealEngine::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(UnrealEngine::OnUnmapBufferRegion);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}