#define GAME_METAPHOR 1

#define ALLOW_SHADERS_DUMPING 0
#define DISABLE_DISPLAY_COMPOSITION 1
#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1
#define ENABLE_DRAW_DISPATCH_DATA_CACHE 1

#include "..\..\Core\core.hpp"

#include "ShaderPatches\ShaderPatches.h"
#include "stretchy_buffer.h"
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxhash.h"

enum class FramePhase
{
   DEPTH_PREPASS,
   OPAQUE_RENDERING,
   PARTICLES,
   POSTPROCESSING_AND_UI
};

struct GFD_VSCONST_TRANSFORM
{
   float4x4 mtxLocalToWorld;
   float4x4 mtxLocalToWorldViewProj;
   float4x4 mtxLocalToWorldViewProjPrev;
   float4x4 mtxModelToLocal;
};

struct GFD_VSCONST_VIEWPROJ
{
   float4x4 mtxViewProj;
   float4x4 mtxView;
   float4x4 mtxInvView;
   float3 eyePosition;
   float fovy;
};

struct GFD_PSCONST_SYSTEM
{
   float2 resolution;
   float2 resolutionRev;
   float4x4 mtxView;
   float4x4 mtxInvView;
   float4x4 mtxProj;
   float4x4 mtxInvProj;
   float4 invProjParams;
};

struct GFD_VSCONST_SKIN_CACHE
{
   uint offset;
   uint stride;
};

struct GFD_VSCONST_OUTLINE_PREV_DATA
{
   float4x4 mtxLocalToWorldPrev;
   float4x4 mtxViewProjPrev;
   float3 eyePositionPrev;
   uint skinned_mesh;
};

struct CB_PREPARE_OCEAN
{
   float4x4 mtxLocalToWorldPrev;
   float4x4 mtxViewProjPrev;
   bool useCurrentTexShift;
   uint TexShiftOffset;
};

struct TransformCacheEntry
{
   uint64_t transform_hash;
   float4x4 mtxLocalToWorldViewProj;
   float4x4 mtxLocalToWorld;
};

struct TransformCacheGroup
{
   std::vector<TransformCacheEntry> current;
   std::vector<TransformCacheEntry> prev;
};

struct OceanCacheEntry
{
   float4x4 mtxLocalToWorld;
   uint32_t TexShiftOffset;
};

struct SkinCacheItem
{
   com_ptr<ID3D11Buffer> buffer;
   uint32_t size;
   uint32_t stride;
};

struct SkinCacheEntry
{
   uint32_t offset;
   uint32_t stride;
};

M_INLINE float3 TransformPoint(const float4x4 m, const float3& b)
{
   float3 v;
   v.x = m.m00 * b.x + m.m01 * b.y + m.m02 * b.z + m.m03;
   v.y = m.m10 * b.x + m.m11 * b.y + m.m12 * b.z + m.m13;
   v.z = m.m20 * b.x + m.m21 * b.y + m.m22 * b.z + m.m23;
   return v;
}

namespace
{
   float2 projection_jitters = {0, 0};
   ShaderHashesList shader_hashes_tonemap;
   ShaderHashesList shader_hashes_merge_particles;
   ShaderHashesList shader_hashes_fxaa;
   ShaderHashesList shader_hashes_smaa_edge_detection;
   ShaderHashesList shader_hashes_smaa_weight_calculation;
   ShaderHashesList shader_hashes_smaa_blending;
   ShaderHashesList shader_hashes_lut;
   ShaderHashesList shader_hashes_outline;
   ShaderHashesList shader_hashes_ocean;
} // namespace

struct GameDeviceDataMetaphor final : public GameDeviceData
{
#if ENABLE_SR
   // SR
   std::atomic<bool> has_drawn_upscaling = false;

   // resources used to identify the deferred context used for scene drawing
   com_ptr<ID3D11CommandList> remainder_command_list;
   std::atomic<ID3D11DeviceContext*> draw_device_context = nullptr;
   std::set<ID3D11DeviceContext*> draw_device_context_candidates;
   std::mutex draw_device_context_mutex;

   // textures we got from the game
   com_ptr<ID3D11Texture2D> source_color;
   com_ptr<ID3D11Texture2D> depth_texture;
   com_ptr<ID3D11Texture2D> particle_texture;

   // the command list we split to interject dlss
   com_ptr<ID3D11CommandList> partial_command_list;

   // resources used to apply sr
   com_ptr<ID3D11Texture2D> motion_vectors;
   com_ptr<ID3D11RenderTargetView> motion_vectors_rtv;
   com_ptr<ID3D11ShaderResourceView> motion_vectors_srv;
   com_ptr<ID3D11Texture2D> scaled_motion_vectors;
   com_ptr<ID3D11UnorderedAccessView> scaled_motion_vectors_uav;
   com_ptr<ID3D11Texture2D> bias_mask;
   com_ptr<ID3D11UnorderedAccessView> bias_mask_uav;
   com_ptr<ID3D11Texture2D> resolve_texture;
   com_ptr<ID3D11Texture2D> merged_texture;
   com_ptr<ID3D11UnorderedAccessView> merged_texture_uav;
   com_ptr<ID3D11ShaderResourceView> merged_texture_srv;
   com_ptr<ID3D11RenderTargetView> merged_texture_rtv;

   // constant buffers
   com_ptr<ID3D11Buffer> cbuffer_outline_prev_data;
   com_ptr<ID3D11Buffer> cbuffer_prepare_ocean_data;
   com_ptr<ID3D11Buffer> cbuffer_ocean_prev_data;
   com_ptr<ID3D11Buffer> cbuffer_skin_cache;
   com_ptr<ID3D11Buffer> cbuffer_motion_vector;

   // used to store cbuffer data when it's not clear yet which ones we want to watch
   std::unordered_map<ID3D11Buffer*, std::array<uint8_t, 288>> cbuffer_cache;

   // the constant buffer we watch for transform updates
   std::atomic<ID3D11Buffer*> cb_transform = nullptr;

   GFD_VSCONST_TRANSFORM vsconst_transform_data;
   bool vsconst_transform_data_changed = false;

   // values extracted from ps system cbuffer
   float4x4 inv_proj;
   float4x4 proj;
   float4x4 proj_with_jitter;
   float4x4 view;
   float3 eye_pos = {};
   float fov = 0.0f;

   // cached values
   float4x4 prev_inv_proj;
   float4x4 prev_proj_with_current_jitter;
   float4x4 prev_view_proj;
   float3 prev_eye_pos = {};

   // duplicates of their counter parts with sr_ needed until SR finished
   // created when command list finishes, so they aren't
   // overriden by the command list recording for the next frame
   com_ptr<ID3D11Texture2D> sr_source_color;
   com_ptr<ID3D11Texture2D> sr_depth_texture;
   com_ptr<ID3D11Texture2D> sr_particle_texture;
   float2 sr_projection_jitters = {0, 0};

   uint2 render_resolution = {};
   uint2 target_resolution = {};

   // cache transform, swapped each frame
   std::unordered_map<uint64_t, TransformCacheGroup> transform_lookup;

   // cache ocean data, swapped each frame
   std::unique_ptr<StretchyBuffer> prev_ocean_buffer;
   std::unique_ptr<StretchyBuffer> ocean_buffer;
   std::vector<OceanCacheEntry> prev_ocean_lookup;
   std::vector<OceanCacheEntry> ocean_lookup;

   // cache skinning data, swapped each frame
   std::unordered_map<ID3D11Buffer*, SkinCacheItem> pending_skin_cache;
   std::unique_ptr<StretchyBuffer> skin_buffer;
   std::unordered_map<ID3D11Buffer*, SkinCacheEntry> skin_lookup;
#endif // ENABLE_SR
   com_ptr<ID3D11Buffer> scratch_constant_buffer;
   com_ptr<ID3D11UnorderedAccessView> scratch_constant_buffer_uav;
   FramePhase frame_phase = FramePhase::OPAQUE_RENDERING;

   std::unordered_map<uint32_t, std::array<uint32_t, 2>> vertex_shader_ndc_coord_indices;
   std::unordered_map<uint32_t, com_ptr<ID3D11VertexShader>> original_vertex_shaders;
   std::unordered_map<uint32_t, com_ptr<ID3D11VertexShader>> modified_vertex_shaders;
   std::unordered_map<uint32_t, std::vector<std::byte>> pixel_shader_code;
   std::unordered_map<uint32_t, com_ptr<ID3D11PixelShader>> modified_pixel_shaders;
};

class Metaphor final : public Game
{
   static GameDeviceDataMetaphor& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataMetaphor*>(device_data.game);
   }

   static bool SrActive(const DeviceData& device_data)
   {
      return device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
   }

public:
   void OnInit(bool async) override
   {
      native_shaders_definitions.emplace(CompileTimeStringHash("Prepare Motion Vector"), ShaderDefinition{"Luma_PrepareMotionVector", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Prepare Ocean Data"), ShaderDefinition{"Luma_PrepareOceanData", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Create Bias Mask"), ShaderDefinition{"Luma_CreateBiasMask", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Merge"), ShaderDefinition{"Luma_CopyDsrResult", reshade::api::pipeline_subobject_type::compute_shader});

      reshade::register_event<reshade::addon_event::clear_render_target_view>(Metaphor::OnClearRenderTargetView);
      reshade::register_event<reshade::addon_event::execute_secondary_command_list>(Metaphor::OnExecuteSecondaryCommandList);
      reshade::register_event<reshade::addon_event::update_buffer_region_command>(Metaphor::OnUpdateBufferRegionCommand);
      reshade::register_event<reshade::addon_event::create_pipeline>(Metaphor::OnCreatePipeline);
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
   }

   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      {
         D3D11_BUFFER_DESC bd;
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         bd.ByteWidth = 144;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, &game_device_data.cbuffer_outline_prev_data);
      }

      {
         D3D11_BUFFER_DESC bd;
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         bd.ByteWidth = 144;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, &game_device_data.cbuffer_prepare_ocean_data);
      }

      {
         D3D11_BUFFER_DESC bd;
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         bd.ByteWidth = 144;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, &game_device_data.cbuffer_ocean_prev_data);
      }

      {
         D3D11_BUFFER_DESC bd;
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         bd.ByteWidth = 16;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, &game_device_data.cbuffer_skin_cache);
      }

      {
         D3D11_BUFFER_DESC bd;
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         bd.ByteWidth = 64;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, &game_device_data.cbuffer_motion_vector);
      }

      {
         D3D11_BUFFER_DESC bd;
         bd.ByteWidth = 144;
         bd.Usage = D3D11_USAGE_DEFAULT;
         bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
         bd.CPUAccessFlags = 0;
         bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
         bd.StructureByteStride = 144;
         native_device->CreateBuffer(&bd, nullptr, &game_device_data.scratch_constant_buffer);
      }

      {
         D3D11_UNORDERED_ACCESS_VIEW_DESC uavd;
         uavd.Format = DXGI_FORMAT_UNKNOWN;
         uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
         uavd.Buffer.FirstElement = 0;
         uavd.Buffer.Flags = 0;
         uavd.Buffer.NumElements = 1;
         native_device->CreateUnorderedAccessView(game_device_data.scratch_constant_buffer.get(), &uavd, &game_device_data.scratch_constant_buffer_uav);
      }

      com_ptr<ID3D11DeviceContext> context;
      native_device->GetImmediateContext(&context);
      // walking around Grand Trad 28 MB seems to be the max used
      game_device_data.skin_buffer = std::make_unique<StretchyBuffer>(native_device, context.get(), 32 * 1024 * 1024);

      game_device_data.ocean_buffer = std::make_unique<StretchyBuffer>(native_device, context.get(), 32);
      game_device_data.prev_ocean_buffer = std::make_unique<StretchyBuffer>(native_device, context.get(), 32);
   }

   void SetupMotionVectorTexture(ID3D11Device* device, GameDeviceDataMetaphor& game_device_data, uint32_t width, uint32_t height)
   {
      if (width == 0 ||
          height == 0)
      {
         return;
      }
      if (game_device_data.motion_vectors)
      {
         D3D11_TEXTURE2D_DESC mv_desc;
         game_device_data.motion_vectors->GetDesc(&mv_desc);
         if (mv_desc.Width == width &&
             mv_desc.Height == height)
         {
            return;
         }
      }
      {
         D3D11_TEXTURE2D_DESC motion_vector_desc;
         motion_vector_desc.Width = width;
         motion_vector_desc.Height = height;
         motion_vector_desc.Usage = D3D11_USAGE_DEFAULT;
         motion_vector_desc.ArraySize = 1;
         motion_vector_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
         motion_vector_desc.SampleDesc.Count = 1;
         motion_vector_desc.SampleDesc.Quality = 0;
         motion_vector_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
         motion_vector_desc.CPUAccessFlags = 0;
         motion_vector_desc.MiscFlags = 0;
         motion_vector_desc.MipLevels = 1;

         device->CreateTexture2D(&motion_vector_desc,
            nullptr,
            &game_device_data.motion_vectors);
      }
      {
         D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
         rtv_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
         rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
         rtv_desc.Texture2D.MipSlice = 0;

         device->CreateRenderTargetView(game_device_data.motion_vectors.get(),
            &rtv_desc,
            &game_device_data.motion_vectors_rtv);
      }
      {
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
         srv_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
         srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
         srv_desc.Texture2D.MostDetailedMip = 0;
         srv_desc.Texture2D.MipLevels = 1;

         device->CreateShaderResourceView(game_device_data.motion_vectors.get(),
            &srv_desc,
            &game_device_data.motion_vectors_srv);
      }
   }

   void SetupSr(ID3D11DeviceContext* native_device_context, GameDeviceDataMetaphor& game_device_data, DeviceData& device_data)
   {
      com_ptr<ID3D11Device> device;
      native_device_context->GetDevice(&device);

      D3D11_TEXTURE2D_DESC target_desc;
      game_device_data.source_color->GetDesc(&target_desc);

      uint32_t width = target_desc.Width;
      uint32_t height = target_desc.Height;

      uint32_t output_width;
      uint32_t output_height;

      output_width = width;
      output_height = height;

      if (game_device_data.target_resolution.x != output_width ||
          game_device_data.target_resolution.y != output_height ||
          game_device_data.render_resolution.x != width ||
          game_device_data.render_resolution.y != height)
      {
         cb_luma_global_settings.GameSettings.RenderRes = {(float)width, (float)height};
         cb_luma_global_settings.GameSettings.InvRenderRes = {1.0f / (float)width, 1.0f / (float)height};
         cb_luma_global_settings.GameSettings.OutputRes = {(float)output_width, (float)output_height};
         cb_luma_global_settings.GameSettings.InvOutputRes = {1.0f / (float)output_width, 1.0f / (float)output_height};
         cb_luma_global_settings.GameSettings.RenderScale = (float)width / (float)output_width;
         cb_luma_global_settings.GameSettings.InvRenderScale = 1.0f / cb_luma_global_settings.GameSettings.RenderScale;
         device_data.cb_luma_global_settings_dirty = true;
         {
            D3D11_TEXTURE2D_DESC motion_vector_desc;
            motion_vector_desc.Width = width;
            motion_vector_desc.Height = height;
            motion_vector_desc.Usage = D3D11_USAGE_DEFAULT;
            motion_vector_desc.ArraySize = 1;
            motion_vector_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
            motion_vector_desc.SampleDesc.Count = 1;
            motion_vector_desc.SampleDesc.Quality = 0;
            motion_vector_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            motion_vector_desc.CPUAccessFlags = 0;
            motion_vector_desc.MiscFlags = 0;
            motion_vector_desc.MipLevels = 1;

            device->CreateTexture2D(&motion_vector_desc,
               nullptr,
               &game_device_data.scaled_motion_vectors);
         }
         {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
            uav_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
            uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uav_desc.Texture2D.MipSlice = 0;

            device->CreateUnorderedAccessView(game_device_data.scaled_motion_vectors.get(),
               &uav_desc,
               &game_device_data.scaled_motion_vectors_uav);
         }
         {
            D3D11_TEXTURE2D_DESC bias_mask_desc;
            bias_mask_desc.Width = width;
            bias_mask_desc.Height = height;
            bias_mask_desc.Usage = D3D11_USAGE_DEFAULT;
            bias_mask_desc.ArraySize = 1;
            bias_mask_desc.Format = DXGI_FORMAT_R16_FLOAT;
            bias_mask_desc.SampleDesc.Count = 1;
            bias_mask_desc.SampleDesc.Quality = 0;
            bias_mask_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            bias_mask_desc.CPUAccessFlags = 0;
            bias_mask_desc.MiscFlags = 0;
            bias_mask_desc.MipLevels = 1;

            device->CreateTexture2D(&bias_mask_desc,
               nullptr,
               &game_device_data.bias_mask);
         }
         {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
            uav_desc.Format = DXGI_FORMAT_R16_FLOAT;
            uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uav_desc.Texture2D.MipSlice = 0;

            device->CreateUnorderedAccessView(game_device_data.bias_mask.get(),
               &uav_desc,
               &game_device_data.bias_mask_uav);
         }
         {
            D3D11_TEXTURE2D_DESC desc;
            desc.Width = output_width;
            desc.Height = output_height;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.ArraySize = 1;
            desc.Format = target_desc.Format;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            desc.MipLevels = 1;

            device->CreateTexture2D(&desc,
               nullptr,
               &game_device_data.resolve_texture);
         }
         {
            D3D11_TEXTURE2D_DESC desc;
            desc.Width = output_width;
            desc.Height = output_height;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.ArraySize = 1;
            desc.Format = target_desc.Format;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            desc.MipLevels = 1;

            device->CreateTexture2D(&desc,
               nullptr,
               &game_device_data.merged_texture);
         }
         {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
            srv_desc.Format = target_desc.Format;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MostDetailedMip = 0;
            srv_desc.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(game_device_data.merged_texture.get(),
               &srv_desc,
               &game_device_data.merged_texture_srv);
         }
         {
            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
            rtv_desc.Format = target_desc.Format;
            rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtv_desc.Texture2D.MipSlice = 0;
            device->CreateRenderTargetView(game_device_data.merged_texture.get(),
               &rtv_desc,
               &game_device_data.merged_texture_rtv);
         }
         {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
            uavDesc.Format = target_desc.Format;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = 0;

            device->CreateUnorderedAccessView(game_device_data.merged_texture.get(),
               &uavDesc,
               &game_device_data.merged_texture_uav);
         }

         float clear[] = {0.0f, 0.0f, 0.0f, 0.0f};
         native_device_context->ClearUnorderedAccessViewFloat(game_device_data.scaled_motion_vectors_uav.get(), clear);

         game_device_data.render_resolution.x = width;
         game_device_data.render_resolution.y = height;
         game_device_data.target_resolution.x = output_width;
         game_device_data.target_resolution.y = output_height;
      }
   }

   void CommitSkinCache(ID3D11DeviceContext* native_device_context, GameDeviceDataMetaphor& game_device_data)
   {
      game_device_data.skin_buffer->Reset();
      game_device_data.skin_lookup.clear();
      for (auto it : game_device_data.pending_skin_cache)
      {
         SkinCacheItem& pending_item = it.second;

         SkinCacheEntry cache_entry = {};
         cache_entry.offset = game_device_data.skin_buffer->size;
         cache_entry.stride = pending_item.stride;

         game_device_data.skin_buffer->CopyFromBuffer(native_device_context, pending_item.buffer.get(), 0, pending_item.size);

         game_device_data.skin_lookup[it.first] = cache_entry;
      }
      game_device_data.pending_skin_cache.clear();
   }

   static bool HandleTransformUpdate(ID3D11Buffer* buffer, const void* data, ID3D11DeviceContext* native_device_context, GameDeviceDataMetaphor& game_device_data, DeviceData& device_data)
   {
      game_device_data.vsconst_transform_data = *(GFD_VSCONST_TRANSFORM*)data;
      game_device_data.vsconst_transform_data_changed = true;
      return true;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      if ((stages & reshade::api::shader_stage::vertex) == 0)
      {
         return DrawOrDispatchOverrideType::None;
      }
      auto& game_device_data = GetGameDeviceData(device_data);

      DrawOrDispatchOverrideType overrideType = DrawOrDispatchOverrideType::None;
      if (game_device_data.draw_device_context == nullptr)
      {
         std::unique_lock lock(game_device_data.draw_device_context_mutex);
         com_ptr<ID3D11RenderTargetView> render_target_views[4];
         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         native_device_context->OMGetRenderTargets(4, &render_target_views[0], &depth_stencil_view);

         if (!depth_stencil_view)
         {
            return DrawOrDispatchOverrideType::None;
         }

         if (render_target_views[0] &&
             render_target_views[1] &&
             render_target_views[2] &&
             render_target_views[3])
         {
            // planar reflections are rendered with front face culling enabled on a separate thread/context
            // so ignore those draw calls
            {
               com_ptr<ID3D11RasterizerState> rasterizer_state;
               native_device_context->RSGetState(&rasterizer_state);
               D3D11_RASTERIZER_DESC rd;
               rasterizer_state->GetDesc(&rd);

               if (rd.CullMode == D3D11_CULL_FRONT)
               {
                  return DrawOrDispatchOverrideType::None;
               }
            }

            game_device_data.frame_phase = FramePhase::OPAQUE_RENDERING;
            game_device_data.draw_device_context = native_device_context;

            if (SrActive(device_data))
            {
               {
                  ID3D11Buffer* cb = game_device_data.cbuffer_outline_prev_data.get();
                  native_device_context->VSSetConstantBuffers(5, 1, &cb);
                  cb = game_device_data.cbuffer_skin_cache.get();
                  native_device_context->VSSetConstantBuffers(9, 1, &cb);
               }
               {
                  ID3D11ShaderResourceView* srv = game_device_data.skin_buffer->srv.get();
                  native_device_context->VSSetShaderResources(1, 1, &srv);
               }

               com_ptr<ID3D11Resource> depthResource;
               depth_stencil_view->GetResource(&depthResource);

               depthResource->QueryInterface(&game_device_data.depth_texture);

               com_ptr<ID3D11Resource> render_target_resource;
               render_target_views[0]->GetResource(&render_target_resource);

               com_ptr<ID3D11Texture2D> texture;
               render_target_resource->QueryInterface(&texture);

               D3D11_TEXTURE2D_DESC target_desc;
               texture->GetDesc(&target_desc);

               com_ptr<ID3D11Device> device;
               native_device_context->GetDevice(&device);
               SetupMotionVectorTexture(device.get(), game_device_data, target_desc.Width, target_desc.Height);

               com_ptr<ID3D11Buffer> transform_constant_buffer;
               native_device_context->VSGetConstantBuffers(1, 1, &transform_constant_buffer);

               com_ptr<ID3D11Buffer> view_proj_constant_buffer;
               native_device_context->VSGetConstantBuffers(2, 1, &view_proj_constant_buffer);

               com_ptr<ID3D11Buffer> ps_system_constant_buffer;
               native_device_context->PSGetConstantBuffers(0, 1, &ps_system_constant_buffer);

               if (transform_constant_buffer && view_proj_constant_buffer && ps_system_constant_buffer)
               {
                  game_device_data.prev_inv_proj = game_device_data.inv_proj;
                  game_device_data.prev_proj_with_current_jitter = game_device_data.proj;
                  game_device_data.prev_proj_with_current_jitter.m02 -= 2.0f * projection_jitters.x / (float)target_desc.Width;
                  game_device_data.prev_proj_with_current_jitter.m12 += 2.0f * projection_jitters.y / (float)target_desc.Height;
                  game_device_data.prev_view_proj = game_device_data.prev_proj_with_current_jitter * game_device_data.view;

                  game_device_data.prev_eye_pos = game_device_data.eye_pos;

                  D3D11_BUFFER_DESC ps_system_constant_buffer_desc;
                  ps_system_constant_buffer->GetDesc(&ps_system_constant_buffer_desc);

                  if (ps_system_constant_buffer_desc.ByteWidth == 288)
                  {
                     auto it = game_device_data.cbuffer_cache.find(ps_system_constant_buffer.get());
                     if (it != game_device_data.cbuffer_cache.cend())
                     {
                        const GFD_PSCONST_SYSTEM* ps_const_system = (GFD_PSCONST_SYSTEM*)it->second.data();
                        game_device_data.inv_proj = ps_const_system->mtxInvProj;
                        game_device_data.proj = ps_const_system->mtxProj;
                        game_device_data.proj_with_jitter = game_device_data.proj;

                        game_device_data.proj_with_jitter.m02 -= 2.0f * projection_jitters.x / (float)target_desc.Width;
                        game_device_data.proj_with_jitter.m12 += 2.0f * projection_jitters.y / (float)target_desc.Height;

                        game_device_data.view = ps_const_system->mtxView;
                     }
                  }

                  D3D11_BUFFER_DESC transform_constant_buffer_desc;
                  transform_constant_buffer->GetDesc(&transform_constant_buffer_desc);
                  if (transform_constant_buffer_desc.ByteWidth == 256)
                  {
                     game_device_data.cb_transform = transform_constant_buffer.get();
                     {
                        auto it = game_device_data.cbuffer_cache.find(transform_constant_buffer.get());
                        if (it != game_device_data.cbuffer_cache.cend())
                        {
                           HandleTransformUpdate(transform_constant_buffer.get(), it->second.data(), native_device_context, game_device_data, device_data);
                        }
                     }
                  }

                  D3D11_BUFFER_DESC view_proj_constant_buffer_desc;
                  view_proj_constant_buffer->GetDesc(&view_proj_constant_buffer_desc);

                  if (view_proj_constant_buffer_desc.ByteWidth == 208)
                  {
                     auto it = game_device_data.cbuffer_cache.find(view_proj_constant_buffer.get());
                     if (it != game_device_data.cbuffer_cache.cend())
                     {
                        GFD_VSCONST_VIEWPROJ vs_const_viewproj = *((GFD_VSCONST_VIEWPROJ*)it->second.data());
                        game_device_data.eye_pos = vs_const_viewproj.eyePosition;
                        game_device_data.fov = vs_const_viewproj.fovy;

                        vs_const_viewproj.mtxViewProj = game_device_data.proj_with_jitter * game_device_data.view;
                        native_device_context->UpdateSubresource(view_proj_constant_buffer.get(), 0, nullptr, &vs_const_viewproj, 0, 0);
                     }
                  }
               }

               if (game_device_data.motion_vectors_rtv)
               {
                  float clear_value[] = {0.0f, 0.0f, 0.0f, 0.0f};
                  native_device_context->ClearRenderTargetView(game_device_data.motion_vectors_rtv.get(), clear_value);
               }
            }
         }
         else if (game_device_data.draw_device_context_candidates.contains(native_device_context) && depth_stencil_view)
         {
            // apply jitter to depth pre-pass
            com_ptr<ID3D11Buffer> transform_constant_buffer;
            native_device_context->VSGetConstantBuffers(1, 1, &transform_constant_buffer);

            com_ptr<ID3D11Buffer> ps_system_constant_buffer;
            native_device_context->PSGetConstantBuffers(0, 1, &ps_system_constant_buffer);

            com_ptr<ID3D11Resource> depth_stencil_resource;
            depth_stencil_view->GetResource(&depth_stencil_resource);

            com_ptr<ID3D11Texture2D> texture;
            depth_stencil_resource->QueryInterface(&texture);

            D3D11_TEXTURE2D_DESC target_desc;
            texture->GetDesc(&target_desc);

            if (transform_constant_buffer && ps_system_constant_buffer)
            {
               float4x4 inv_proj;
               float4x4 proj;
               float4x4 proj_with_jitter;

               D3D11_BUFFER_DESC ps_system_constant_buffer_desc;
               ps_system_constant_buffer->GetDesc(&ps_system_constant_buffer_desc);
               if (ps_system_constant_buffer_desc.ByteWidth == 288)
               {
                  auto it = game_device_data.cbuffer_cache.find(ps_system_constant_buffer.get());
                  if (it != game_device_data.cbuffer_cache.cend())
                  {
                     const GFD_PSCONST_SYSTEM* ps_const_system = (GFD_PSCONST_SYSTEM*)it->second.data();
                     inv_proj = ps_const_system->mtxInvProj;
                     proj = ps_const_system->mtxProj;
                     proj_with_jitter = proj;

                     proj_with_jitter.m02 -= 2.0f * projection_jitters.x / (float)target_desc.Width;
                     proj_with_jitter.m12 += 2.0f * projection_jitters.y / (float)target_desc.Height;
                  }
                  else
                  {
                     return DrawOrDispatchOverrideType::None;
                  }
               }
               else
               {
                  return DrawOrDispatchOverrideType::None;
               }

               D3D11_BUFFER_DESC transform_constant_buffer_desc;
               transform_constant_buffer->GetDesc(&transform_constant_buffer_desc);
               if (transform_constant_buffer_desc.ByteWidth == 256)
               {
                  {
                     auto it = game_device_data.cbuffer_cache.find(transform_constant_buffer.get());
                     if (it != game_device_data.cbuffer_cache.cend())
                     {
                        // not used for motion vectors so no point in updating mtxLocalToWorldViewProjPrev
                        GFD_VSCONST_TRANSFORM vs_consts = *(GFD_VSCONST_TRANSFORM*)it->second.data();
                        vs_consts.mtxLocalToWorldViewProj = proj_with_jitter * inv_proj * vs_consts.mtxLocalToWorldViewProj;

                        if (transform_constant_buffer)
                        {
                           native_device_context->UpdateSubresource(transform_constant_buffer.get(), 0, nullptr, &vs_consts, 0, 0);
                        }
                     }
                  }
               }
            }

            return DrawOrDispatchOverrideType::None;
         }
         else
         {
            return DrawOrDispatchOverrideType::None;
         }
      }
      else if (native_device_context != game_device_data.draw_device_context)
      {
         return DrawOrDispatchOverrideType::None;
      }

      if (game_device_data.frame_phase == FramePhase::OPAQUE_RENDERING)
      {
         if (original_shader_hashes.Contains(shader_hashes_tonemap))
         {
            game_device_data.frame_phase = FramePhase::PARTICLES;
            return DrawOrDispatchOverrideType::None;
         }
         if (!SrActive(device_data) ||
             original_shader_hashes.vertex_shaders.empty() ||
             original_shader_hashes.pixel_shaders.empty())
         {
            return DrawOrDispatchOverrideType::None;
         }

         com_ptr<ID3D11Buffer> vertex_buffer;
         uint32_t stride;
         native_device_context->IAGetVertexBuffers(0, 1, &vertex_buffer, &stride, nullptr);

         D3D11_BUFFER_DESC bd = {};
         vertex_buffer->GetDesc(&bd);
         bool is_skinned_mesh = ((bd.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0);
         bool is_outline_pass = original_shader_hashes.Contains(shader_hashes_outline);

         if (is_skinned_mesh)
         {
            if (game_device_data.pending_skin_cache.find(vertex_buffer.get()) == game_device_data.pending_skin_cache.cend())
            {
               // only queue vertex buffer copies here and execute them in a single batch in CommitSkinCache
               // inter-mixing draw and copy too much negatively affects performance in scenes with lots of skinned objects
               // e.g. on some parts of Virga Island I saw a difference of up to 2ms on a RTX 4080
               SkinCacheItem cache_item = {};
               cache_item.buffer = vertex_buffer;
               cache_item.size = bd.ByteWidth;
               cache_item.stride = stride;

               game_device_data.pending_skin_cache[vertex_buffer.get()] = cache_item;
            }
         }

         auto restoreOriginalShader = [&game_device_data, is_outline_pass, native_device_context, &original_shader_hashes]()
         {
            if (!is_outline_pass)
            {
               auto shader_it = game_device_data.original_vertex_shaders.find(original_shader_hashes.vertex_shaders[0]);
               if (shader_it != game_device_data.original_vertex_shaders.cend())
               {
                  native_device_context->VSSetShader(shader_it->second.get(), nullptr, 0);
               }
            }
         };

         if (original_shader_hashes.Contains(shader_hashes_ocean))
         {
            GFD_VSCONST_TRANSFORM vs_consts = game_device_data.vsconst_transform_data;

            if (game_device_data.prev_ocean_lookup.size())
            {
               OceanCacheEntry* cache_data = nullptr;
               float shortest_distance = FLT_MAX;
               float3 a = TransformPoint(vs_consts.mtxLocalToWorld, float3(1.0f, 1.0f, 1.0f));
               for (uint32_t i = 0; i < game_device_data.prev_ocean_lookup.size(); ++i)
               {
                  float3 b = TransformPoint(game_device_data.prev_ocean_lookup[i].mtxLocalToWorld, float3(1.0f, 1.0f, 1.0f));
                  float dist = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
                  if (dist < shortest_distance)
                  {
                     cache_data = &game_device_data.prev_ocean_lookup[i];
                     shortest_distance = dist;
                  }
               }

               D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
               native_device_context->Map(game_device_data.cbuffer_prepare_ocean_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
               CB_PREPARE_OCEAN* cb_prepare_ocean_data = (CB_PREPARE_OCEAN*)mapped_cbuffer.pData;
               cb_prepare_ocean_data->mtxLocalToWorldPrev = cache_data->mtxLocalToWorld;
               cb_prepare_ocean_data->mtxViewProjPrev = game_device_data.prev_view_proj;
               cb_prepare_ocean_data->useCurrentTexShift = false;
               cb_prepare_ocean_data->TexShiftOffset = cache_data->TexShiftOffset;
               native_device_context->Unmap(game_device_data.cbuffer_prepare_ocean_data.get(), 0);
            }
            else
            {
               D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
               native_device_context->Map(game_device_data.cbuffer_prepare_ocean_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
               CB_PREPARE_OCEAN* cb_prepare_ocean_data = (CB_PREPARE_OCEAN*)mapped_cbuffer.pData;
               cb_prepare_ocean_data->mtxLocalToWorldPrev = vs_consts.mtxLocalToWorld;
               cb_prepare_ocean_data->mtxViewProjPrev = game_device_data.prev_view_proj;
               cb_prepare_ocean_data->useCurrentTexShift = true;
               native_device_context->Unmap(game_device_data.cbuffer_prepare_ocean_data.get(), 0);
            }

            com_ptr<ID3D11Buffer> ocean_constant_buffer;
            native_device_context->VSGetConstantBuffers(7, 1, &ocean_constant_buffer);

            {
               ID3D11Buffer* cbs[] = {game_device_data.cbuffer_prepare_ocean_data.get(), ocean_constant_buffer.get()};
               ID3D11ShaderResourceView* srvs[] = {game_device_data.prev_ocean_buffer->srv.get()};
               ID3D11UnorderedAccessView* uavs[] = {game_device_data.scratch_constant_buffer_uav.get()};

               native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Prepare Ocean Data")].get(), 0, 0);
               native_device_context->CSSetConstantBuffers(0, 2, cbs);
               native_device_context->CSSetShaderResources(0, 1, srvs);
               native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
               native_device_context->Dispatch(1, 1, 1);
            }
            native_device_context->CopySubresourceRegion(game_device_data.cbuffer_ocean_prev_data.get(), 0, 0, 0, 0, game_device_data.scratch_constant_buffer.get(), 0, nullptr);

            com_ptr<ID3D11DepthStencilView> depth_stencil_view;
            com_ptr<ID3D11RenderTargetView> render_target_views[6];
            native_device_context->OMGetRenderTargets(6, &render_target_views[0], &depth_stencil_view);
            if (render_target_views[5] != game_device_data.motion_vectors_rtv)
            {
               ID3D11RenderTargetView* updated_render_target_views[] = {render_target_views[0].get(),
                  render_target_views[1].get(),
                  render_target_views[2].get(),
                  render_target_views[3].get(),
                  render_target_views[4].get(),
                  game_device_data.motion_vectors_rtv.get()};
               native_device_context->OMSetRenderTargets(6, updated_render_target_views, depth_stencil_view.get());
            }

            vs_consts.mtxLocalToWorldViewProj = game_device_data.proj_with_jitter * game_device_data.inv_proj * vs_consts.mtxLocalToWorldViewProj;
            vs_consts.mtxLocalToWorldViewProjPrev = game_device_data.prev_proj_with_current_jitter * game_device_data.prev_inv_proj * vs_consts.mtxLocalToWorldViewProjPrev;

            if (game_device_data.cb_transform)
            {
               native_device_context->UpdateSubresource(game_device_data.cb_transform, 0, nullptr, &vs_consts, 0, 0);
            }
            ID3D11Buffer* cb = game_device_data.cbuffer_ocean_prev_data.get();
            native_device_context->VSSetConstantBuffers(4, 1, &cb);

            {
               bool addToCache = true;
               for (uint32_t i = 0; i < game_device_data.ocean_lookup.size(); ++i)
               {
                  if (memcmp(&game_device_data.ocean_lookup[i].mtxLocalToWorld, &vs_consts.mtxLocalToWorld, sizeof(vs_consts.mtxLocalToWorld)) == 0)
                  {
                     addToCache = false;
                     break;
                  }
               }

               if (addToCache)
               {
                  OceanCacheEntry cache_entry = {};
                  cache_entry.mtxLocalToWorld = vs_consts.mtxLocalToWorld;
                  cache_entry.TexShiftOffset = game_device_data.ocean_buffer->size;

                  game_device_data.ocean_buffer->CopyFromBuffer(native_device_context, ocean_constant_buffer.get(), 64, 16);

                  game_device_data.ocean_lookup.push_back(cache_entry);
               }
            }

            return DrawOrDispatchOverrideType::None;
         }

         bool previous_skin_set = false;
         if (is_skinned_mesh)
         {
            auto cache_it = game_device_data.skin_lookup.find(vertex_buffer.get());

            if (cache_it != game_device_data.skin_lookup.cend())
            {
               D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
               native_device_context->Map(game_device_data.cbuffer_skin_cache.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
               GFD_VSCONST_SKIN_CACHE* vs_consts_skin = (GFD_VSCONST_SKIN_CACHE*)mapped_cbuffer.pData;
               vs_consts_skin->offset = cache_it->second.offset;
               vs_consts_skin->stride = cache_it->second.stride;
               native_device_context->Unmap(game_device_data.cbuffer_skin_cache.get(), 0);

               previous_skin_set = true;

               // outline shaders are manually overriden
               if (!is_outline_pass)
               {
                  auto shader_it = game_device_data.modified_vertex_shaders.find(original_shader_hashes.vertex_shaders[0]);
                  if (shader_it != game_device_data.modified_vertex_shaders.cend())
                  {
                     com_ptr<ID3D11VertexShader> vertex_shader;
                     native_device_context->VSGetShader(&vertex_shader, nullptr, nullptr);

                     if (vertex_shader != shader_it->second.get())
                     {
                        game_device_data.original_vertex_shaders[original_shader_hashes.vertex_shaders[0]] = vertex_shader;
                        native_device_context->VSSetShader(shader_it->second.get(), nullptr, 0);
                     }
                  }
                  else
                  {
                     restoreOriginalShader();
                  }
               }
            }
            else
            {
               restoreOriginalShader();
            }
         }
         else
         {
            restoreOriginalShader();
         }

         if (game_device_data.vsconst_transform_data_changed ||
             is_outline_pass)
         {
            GFD_VSCONST_TRANSFORM vs_consts = game_device_data.vsconst_transform_data;

            if ((stages & reshade::api::shader_stage::pixel) != 0)
            {
               auto hash_transform = [](float4x4 transform)
               {
                  return XXH3_64bits((const uint8_t*)&transform.m30, 4 * sizeof(float));
               };

               auto hash_draw_call = [](uint64_t pixel_shader, ID3D11Buffer* vertex_buffer, uint32_t vertex_count)
               {
                  uint8_t buffer[sizeof(pixel_shader) + sizeof(vertex_buffer) + sizeof(vertex_count)] = {};
                  memcpy(&buffer[0], &pixel_shader, sizeof(pixel_shader));
                  memcpy(&buffer[sizeof(pixel_shader)], &vertex_buffer, sizeof(vertex_buffer));
                  memcpy(&buffer[sizeof(pixel_shader) + sizeof(vertex_buffer)], &vertex_count, sizeof(vertex_count));
                  return XXH3_64bits(buffer, sizeof(buffer));
               };

               uint64_t draw_call_hash = hash_draw_call(original_shader_hashes.pixel_shaders[0], vertex_buffer.get(), max(last_draw_dispatch_data.index_count, last_draw_dispatch_data.vertex_count));
               uint64_t transform_hash = hash_transform(vs_consts.mtxLocalToWorldViewProj);

               auto& stored_transforms = game_device_data.transform_lookup[draw_call_hash];
               bool found = false;
               for (uint32_t i = 0; i < stored_transforms.current.size(); ++i)
               {
                  if (stored_transforms.current[i].transform_hash == transform_hash)
                  {
                     found = true;
                     break;
                  }
               }
               if (!found)
               {
                  stored_transforms.current.push_back({transform_hash, vs_consts.mtxLocalToWorldViewProj, vs_consts.mtxLocalToWorld});
               }

               if (stored_transforms.prev.size() > 0)
               {
                  uint64_t prev_transform_hash = hash_transform(vs_consts.mtxLocalToWorldViewProjPrev);

                  TransformCacheEntry* cache_data = nullptr;
                  for (uint32_t i = 0; i < stored_transforms.prev.size(); ++i)
                  {
                     if (stored_transforms.prev[i].transform_hash == prev_transform_hash)
                     {
                        cache_data = &stored_transforms.prev[i];
                        break;
                     }
                  }
                  if (!cache_data)
                  {
                     float shortest_distance = FLT_MAX;
                     float3 a = TransformPoint(vs_consts.mtxLocalToWorldViewProjPrev, float3(1.0f, 1.0f, 1.0f));
                     for (uint32_t i = 0; i < stored_transforms.prev.size(); ++i)
                     {
                        float3 b = TransformPoint(stored_transforms.prev[i].mtxLocalToWorldViewProj, float3(1.0f, 1.0f, 1.0f));
                        float dist = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
                        if (dist < shortest_distance)
                        {
                           cache_data = &stored_transforms.prev[i];
                           shortest_distance = dist;
                        }
                     }
                  }

                  vs_consts.mtxLocalToWorldViewProjPrev = cache_data->mtxLocalToWorldViewProj;

                  if (is_outline_pass)
                  {
                     D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
                     native_device_context->Map(game_device_data.cbuffer_outline_prev_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
                     GFD_VSCONST_OUTLINE_PREV_DATA* vs_outline_prev_data = (GFD_VSCONST_OUTLINE_PREV_DATA*)mapped_cbuffer.pData;
                     vs_outline_prev_data->mtxLocalToWorldPrev = cache_data->mtxLocalToWorld;
                     vs_outline_prev_data->mtxViewProjPrev = game_device_data.prev_view_proj;
                     vs_outline_prev_data->eyePositionPrev = game_device_data.prev_eye_pos;
                     vs_outline_prev_data->skinned_mesh = previous_skin_set ? 1 : 0;
                     native_device_context->Unmap(game_device_data.cbuffer_outline_prev_data.get(), 0);
                  }
               }
               else if (is_outline_pass)
               {
                  D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
                  native_device_context->Map(game_device_data.cbuffer_outline_prev_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
                  GFD_VSCONST_OUTLINE_PREV_DATA* vs_outline_prev_data = (GFD_VSCONST_OUTLINE_PREV_DATA*)mapped_cbuffer.pData;
                  vs_outline_prev_data->mtxLocalToWorldPrev = vs_consts.mtxLocalToWorld;
                  vs_outline_prev_data->mtxViewProjPrev = game_device_data.prev_view_proj;
                  vs_outline_prev_data->eyePositionPrev = game_device_data.prev_eye_pos;
                  vs_outline_prev_data->skinned_mesh = previous_skin_set ? 1 : 0;
                  native_device_context->Unmap(game_device_data.cbuffer_outline_prev_data.get(), 0);
               }
            }

            vs_consts.mtxLocalToWorldViewProj = game_device_data.proj_with_jitter * game_device_data.inv_proj * vs_consts.mtxLocalToWorldViewProj;
            vs_consts.mtxLocalToWorldViewProjPrev = game_device_data.prev_proj_with_current_jitter * game_device_data.prev_inv_proj * vs_consts.mtxLocalToWorldViewProjPrev;

            if (game_device_data.cb_transform)
            {
               native_device_context->UpdateSubresource(game_device_data.cb_transform, 0, nullptr, &vs_consts, 0, 0);
            }

            game_device_data.vsconst_transform_data_changed = false;
         }

         const auto pixel_shader_it = game_device_data.modified_pixel_shaders.find(original_shader_hashes.pixel_shaders.front());
         ID3D11PixelShader* shader = nullptr;
         if (pixel_shader_it == game_device_data.modified_pixel_shaders.cend())
         {
            const auto coord_index_it = game_device_data.vertex_shader_ndc_coord_indices.find(original_shader_hashes.vertex_shaders.front());
            if (coord_index_it == game_device_data.vertex_shader_ndc_coord_indices.cend())
            {
               return DrawOrDispatchOverrideType::None;
            }
            const auto shader_code_it = game_device_data.pixel_shader_code.find(original_shader_hashes.pixel_shaders.front());
            if (shader_code_it == game_device_data.pixel_shader_code.cend())
            {
               return DrawOrDispatchOverrideType::None;
            }

            std::vector<std::byte> shader_code = shader_code_it->second;

            uint32_t coord_input_register = coord_index_it->second[0];
            uint32_t prev_coord_input_register = coord_index_it->second[1];

            PatchPixelShader(shader_code, coord_input_register, prev_coord_input_register);

            com_ptr<ID3D11Device> device;
            native_device_context->GetDevice(&device);
            HRESULT hr = device->CreatePixelShader(shader_code.data(), shader_code.size(), nullptr, &shader);
            game_device_data.modified_pixel_shaders[original_shader_hashes.pixel_shaders.front()] = shader;
         }
         else
         {
            shader = pixel_shader_it->second.get();
         }
         if (!shader)
         {
            return DrawOrDispatchOverrideType::None;
         }
         native_device_context->PSSetShader(shader, nullptr, 0);

         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         com_ptr<ID3D11RenderTargetView> render_target_views[6];
         native_device_context->OMGetRenderTargets(6, &render_target_views[0], &depth_stencil_view);
         if (render_target_views[5] != game_device_data.motion_vectors_rtv)
         {
            ID3D11RenderTargetView* updated_render_target_views[] = {render_target_views[0].get(),
               render_target_views[1].get(),
               render_target_views[2].get(),
               render_target_views[3].get(),
               render_target_views[4].get(),
               game_device_data.motion_vectors_rtv.get()};
            native_device_context->OMSetRenderTargets(6, updated_render_target_views, depth_stencil_view.get());
         }
      }
      else if (game_device_data.frame_phase == FramePhase::PARTICLES &&
               original_shader_hashes.Contains(shader_hashes_merge_particles))
      {
         // only apply sr when we have the necessary input resources
         if (SrActive(device_data) &&
             game_device_data.depth_texture)
         {
            native_device_context->Draw(4, 0);

            game_device_data.frame_phase = FramePhase::POSTPROCESSING_AND_UI;

            com_ptr<ID3D11RenderTargetView> render_target_view;
            native_device_context->OMGetRenderTargets(1, &render_target_view, nullptr);

            com_ptr<ID3D11Resource> color_resource;
            render_target_view->GetResource(&color_resource);
            color_resource->QueryInterface(&game_device_data.source_color);

            com_ptr<ID3D11ShaderResourceView> particle_srv;
            native_device_context->PSGetShaderResources(2, 1, &particle_srv);

            com_ptr<ID3D11Resource> particle_resource;
            particle_srv->GetResource(&particle_resource);

            particle_resource->QueryInterface(&game_device_data.particle_texture);

            CommitSkinCache(native_device_context, game_device_data);

            SetupSr(native_device_context, game_device_data, device_data);

            // split the command list since DLSS must be executed on an immediate context
            native_device_context->FinishCommandList(TRUE, &game_device_data.partial_command_list);

            overrideType = DrawOrDispatchOverrideType::Replaced;
         }
      }
      else if (SrActive(device_data) &&
               original_shader_hashes.Contains(shader_hashes_fxaa))
      {
         com_ptr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0, 1, &srv);
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);

         com_ptr<ID3D11Resource> srv_resource;
         srv->GetResource(&srv_resource);

         com_ptr<ID3D11Resource> rtv_resource;
         rtv->GetResource(&rtv_resource);

         native_device_context->CopySubresourceRegion(rtv_resource.get(), 0, 0, 0, 0, srv_resource.get(), 0, nullptr);

         return DrawOrDispatchOverrideType::Skip;
      }
      else if (SrActive(device_data) &&
               original_shader_hashes.Contains(shader_hashes_smaa_blending))
      {
         com_ptr<ID3D11BlendState> blend_state;
         native_device_context->OMGetBlendState(&blend_state, nullptr, nullptr);
         D3D11_BLEND_DESC blend_desc;
         blend_state->GetDesc(&blend_desc);

         // menus that overlay 3D models onto the scene always apply SMAA
         // so we don't skip when alpha blending is enabled
         // that's also why we don't skip SMAA edge detection and weight calculation
         if (!blend_desc.RenderTarget[0].BlendEnable ||
             blend_desc.RenderTarget[0].SrcBlend != D3D11_BLEND_SRC_ALPHA)
         {
            com_ptr<ID3D11ShaderResourceView> srv;
            native_device_context->PSGetShaderResources(0, 1, &srv);
            com_ptr<ID3D11RenderTargetView> rtv;
            native_device_context->OMGetRenderTargets(1, &rtv, nullptr);

            com_ptr<ID3D11Resource> srv_resource;
            srv->GetResource(&srv_resource);

            com_ptr<ID3D11Resource> rtv_resource;
            rtv->GetResource(&rtv_resource);

            native_device_context->CopySubresourceRegion(rtv_resource.get(), 0, 0, 0, 0, srv_resource.get(), 0, nullptr);

            return DrawOrDispatchOverrideType::Skip;
         }
      }
      else if (original_shader_hashes.Contains(shader_hashes_lut))
      {
         // if there's no bloom or particle we use this as a second chance to inject super resolution
         if (game_device_data.frame_phase == FramePhase::PARTICLES &&
             SrActive(device_data) &&
             game_device_data.depth_texture)
         {
            game_device_data.frame_phase = FramePhase::POSTPROCESSING_AND_UI;

            com_ptr<ID3D11ShaderResourceView> srv;
            native_device_context->PSGetShaderResources(0, 1, &srv);

            com_ptr<ID3D11Resource> color_resource;
            srv->GetResource(&color_resource);
            color_resource->QueryInterface(&game_device_data.source_color);

            CommitSkinCache(native_device_context, game_device_data);

            SetupSr(native_device_context, game_device_data, device_data);

            // split the command list since DLSS must be executed on an immediate context
            native_device_context->FinishCommandList(TRUE, &game_device_data.partial_command_list);
         }
         ID3D11SamplerState* sampler = device_data.sampler_state_linear.get();
         native_device_context->PSSetSamplers(0, 1, &sampler);
      }

      return overrideType;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataMetaphor;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      device_data.force_reset_sr = !game_device_data.has_drawn_upscaling;
      game_device_data.has_drawn_upscaling = false;
   }

   static bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      com_ptr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(&native_device_context);

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (game_device_data.draw_device_context == nullptr)
      {
         std::unique_lock lock(game_device_data.draw_device_context_mutex);
         game_device_data.draw_device_context_candidates.insert(native_device_context.get());
      }

      return false;
   }

   static void OnExecuteSecondaryCommandList(reshade::api::command_list* cmd_list, reshade::api::command_list* secondary_cmd_list)
   {
      com_ptr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(&native_device_context);

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (native_device_context)
      {
         com_ptr<ID3D11CommandList> native_command_list;
         ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         HRESULT hr = device_child->QueryInterface(&native_command_list);
         if (native_command_list == game_device_data.remainder_command_list && game_device_data.partial_command_list)
         {
            game_device_data.remainder_command_list.reset();
            native_device_context->ExecuteCommandList(game_device_data.partial_command_list.get(), FALSE);
            game_device_data.partial_command_list.reset();

            if (!game_device_data.sr_source_color || !game_device_data.sr_depth_texture || device_data.sr_type == SR::Type::None)
            {
               return;
            }

            com_ptr<ID3D11Device> device;
            native_device_context->GetDevice(&device);

            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            SetLumaConstantBuffers(native_device_context.get(), cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings);

            D3D11_TEXTURE2D_DESC target_desc;
            game_device_data.sr_source_color->GetDesc(&target_desc);

            auto* sr_instance_data = device_data.GetSRInstanceData();
            {
               SR::SettingsData settings_data;
               settings_data.output_width = game_device_data.target_resolution.x;
               settings_data.output_height = game_device_data.target_resolution.y;
               settings_data.render_width = game_device_data.render_resolution.x;
               settings_data.render_height = game_device_data.render_resolution.y;
               settings_data.dynamic_resolution = false;
               settings_data.hdr = true;
               settings_data.inverted_depth = false;
               settings_data.mvs_jittered = false;
               settings_data.render_preset = dlss_render_preset;
               sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context.get(), settings_data);
            }

            {
               com_ptr<ID3D11ShaderResourceView> depth_texture_srv;
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                  srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MostDetailedMip = 0;
                  srv_desc.Texture2D.MipLevels = 1;
                  device->CreateShaderResourceView(game_device_data.sr_depth_texture.get(),
                     &srv_desc,
                     &depth_texture_srv);
               }

               {
                  D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
                  native_device_context->Map(game_device_data.cbuffer_motion_vector.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
                  float4x4* reprojection_matrix = (float4x4*)mapped_cbuffer.pData;
                  *reprojection_matrix = game_device_data.prev_view_proj * game_device_data.view.GetTransposed().GetInverted().GetTransposed() * game_device_data.proj_with_jitter.GetTransposed().GetInverted().GetTransposed();
                  native_device_context->Unmap(game_device_data.cbuffer_motion_vector.get(), 0);
               }

               ID3D11Buffer* cbs[] = {game_device_data.cbuffer_motion_vector.get()};
               ID3D11ShaderResourceView* srvs[] = {game_device_data.motion_vectors_srv.get(), depth_texture_srv.get()};
               ID3D11UnorderedAccessView* uavs[] = {game_device_data.scaled_motion_vectors_uav.get()};

               native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Prepare Motion Vector")].get(), 0, 0);
               native_device_context->CSSetConstantBuffers(0, 1, cbs);
               native_device_context->CSSetShaderResources(0, 2, srvs);
               native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
               native_device_context->Dispatch((game_device_data.render_resolution.x + 7) / 8, (game_device_data.render_resolution.y + 7) / 8, 1);
            }

            if (game_device_data.sr_particle_texture)
            {
               com_ptr<ID3D11ShaderResourceView> particle_texture_srv;
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                  srv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MostDetailedMip = 0;
                  srv_desc.Texture2D.MipLevels = 1;
                  device->CreateShaderResourceView(game_device_data.sr_particle_texture.get(),
                     &srv_desc,
                     &particle_texture_srv);
               }

               ID3D11ShaderResourceView* srvs[] = {particle_texture_srv.get()};
               ID3D11UnorderedAccessView* uavs[] = {game_device_data.bias_mask_uav.get()};

               native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Create Bias Mask")].get(), 0, 0);
               native_device_context->CSSetShaderResources(0, 1, srvs);
               native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
               native_device_context->Dispatch((game_device_data.render_resolution.x + 7) / 8, (game_device_data.render_resolution.y + 7) / 8, 1);
            }

            {
               SR::SuperResolutionImpl::DrawData draw_data;
               draw_data.source_color = game_device_data.sr_source_color.get();
               draw_data.output_color = game_device_data.resolve_texture.get();
               draw_data.motion_vectors = game_device_data.scaled_motion_vectors.get();
               draw_data.depth_buffer = game_device_data.sr_depth_texture.get();
               draw_data.render_width = game_device_data.render_resolution.x;
               draw_data.render_height = game_device_data.render_resolution.y;
               draw_data.bias_mask = game_device_data.sr_particle_texture ? game_device_data.bias_mask.get() : nullptr;
               draw_data.pre_exposure = 0.0f;
               draw_data.jitter_x = game_device_data.sr_projection_jitters.x;
               draw_data.jitter_y = game_device_data.sr_projection_jitters.y;
               draw_data.vert_fov = game_device_data.fov;
               draw_data.reset = device_data.force_reset_sr;

               bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context.get(), draw_data);
               game_device_data.has_drawn_upscaling = true;
            }
            {
               com_ptr<ID3D11Device> device;
               native_device_context->GetDevice(&device);
               com_ptr<ID3D11ShaderResourceView> resolve_textureSRV;
               com_ptr<ID3D11ShaderResourceView> color_srv;

               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                  srv_desc.Format = target_desc.Format;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MostDetailedMip = 0;
                  srv_desc.Texture2D.MipLevels = 1;
                  device->CreateShaderResourceView(game_device_data.resolve_texture.get(),
                     &srv_desc,
                     &resolve_textureSRV);
               }
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                  srv_desc.Format = target_desc.Format;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MostDetailedMip = 0;
                  srv_desc.Texture2D.MipLevels = 1;
                  device->CreateShaderResourceView(game_device_data.sr_source_color.get(),
                     &srv_desc,
                     &color_srv);
               }

               // some sr methods don't retain the alpha channel - combine sr result with the alpha from the original color texture
               {
                  ID3D11ShaderResourceView* srvs[] = {resolve_textureSRV.get(), color_srv.get()};
                  ID3D11UnorderedAccessView* uavs[] = {game_device_data.merged_texture_uav.get()};
                  ID3D11SamplerState* samplers[] = {device_data.sampler_state_linear.get()};
                  native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Merge")].get(), 0, 0);
                  native_device_context->CSSetShaderResources(0, 2, srvs);
                  native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
                  native_device_context->CSSetSamplers(0, 1, samplers);
                  native_device_context->Dispatch((game_device_data.target_resolution.x + 7) / 8, (game_device_data.target_resolution.y + 7) / 8, 1);
               }

               native_device_context->CopySubresourceRegion(game_device_data.sr_source_color.get(), 0, 0, 0, 0, game_device_data.merged_texture.get(), 0, nullptr);
            }

            game_device_data.sr_source_color.reset();
            game_device_data.sr_depth_texture.reset();
            game_device_data.sr_particle_texture.reset();
            // release all resources from the game we got this frame
            game_device_data.remainder_command_list.reset();
         }
      }

      com_ptr<ID3D11CommandList> native_command_list;
      hr = device_child->QueryInterface(&native_command_list);
      if (native_command_list)
      {
         ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         hr = device_child->QueryInterface(&native_device_context);
         if (native_device_context == game_device_data.draw_device_context)
         {
            std::unique_lock lock(game_device_data.draw_device_context_mutex);
            game_device_data.sr_source_color = game_device_data.source_color;
            game_device_data.sr_depth_texture = game_device_data.depth_texture;
            game_device_data.sr_particle_texture = game_device_data.particle_texture;
            game_device_data.sr_projection_jitters = projection_jitters;

            game_device_data.source_color.reset();
            game_device_data.depth_texture.reset();
            game_device_data.particle_texture.reset();

            game_device_data.frame_phase = FramePhase::DEPTH_PREPASS;

            game_device_data.draw_device_context = nullptr;
            game_device_data.draw_device_context_candidates.clear();
            game_device_data.cbuffer_cache.clear();
            for (auto it = game_device_data.transform_lookup.begin(); it != game_device_data.transform_lookup.end();)
            {
               if (it->second.current.empty())
               {
                  it = game_device_data.transform_lookup.erase(it);
               }
               else
               {
                  std::swap(it->second.current, it->second.prev);
                  it->second.current.clear();
               }
            }
            std::swap(game_device_data.prev_ocean_lookup, game_device_data.ocean_lookup);
            game_device_data.ocean_lookup.clear();
            std::swap(game_device_data.prev_ocean_buffer, game_device_data.ocean_buffer);
            game_device_data.ocean_buffer->Reset();
            game_device_data.cb_transform = nullptr;

            // Update TAA jitters:
            int phases = 8; // A good default
            if (device_data.sr_type != SR::Type::None)
            {
               auto* sr_instance_data = device_data.GetSRInstanceData();
               phases = sr_implementations[device_data.sr_type]->GetJitterPhases(sr_instance_data);
            }
            int temporal_frame = cb_luma_global_settings.FrameIndex % phases;
            projection_jitters.x = SR::HaltonSequence(temporal_frame, 2);
            projection_jitters.y = SR::HaltonSequence(temporal_frame, 3);

            if (!custom_texture_mip_lod_bias_offset)
            {
               std::shared_lock shared_lock_samplers(s_mutex_samplers);
               if (SrActive(device_data) &&
                   game_device_data.render_resolution.y > 0.0f &&
                   game_device_data.target_resolution.y > 0.0f)
               {
                  device_data.texture_mip_lod_bias_offset = std::log2(game_device_data.render_resolution.y / game_device_data.target_resolution.y) - 1.f; // This results in -1 at output res
               }
               else
               {
                  device_data.texture_mip_lod_bias_offset = 0.f;
               }
            }

            if (game_device_data.partial_command_list)
            {
               game_device_data.remainder_command_list = native_command_list.get();
            }
         }
      }
   }

   static bool OnUpdateBufferRegionCommand(reshade::api::command_list* cmd_list, const void* data, reshade::api::resource dest, uint64_t dest_offset, uint64_t size)
   {
      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!SrActive(device_data))
      {
         return false;
      }

      com_ptr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(&native_device_context);

      // store values so we can find changes for the constant buffers we are interested in
      if (game_device_data.draw_device_context == nullptr)
      {
         std::unique_lock lock(game_device_data.draw_device_context_mutex);
         ID3D11Buffer* buffer = (ID3D11Buffer*)dest.handle;
         D3D11_BUFFER_DESC bd;
         ((ID3D11Buffer*)dest.handle)->GetDesc(&bd);
         if (bd.ByteWidth != 208 && // GFD_VSCONST_VIEWPROJ
             bd.ByteWidth != 256 && // GFD_VSCONST_TRANSFORM
             bd.ByteWidth != 288)   // GFD_PSCONST_SYSTEM
         {
            return false;
         }

         memcpy(game_device_data.cbuffer_cache[buffer].data(), data, bd.ByteWidth);

         return false;
      }

      if (native_device_context.get() != game_device_data.draw_device_context)
      {
         return false;
      }

      // early out we don't need any cbuffer values after opaque geometry has finished
      if (game_device_data.frame_phase != FramePhase::DEPTH_PREPASS &&
          game_device_data.frame_phase != FramePhase::OPAQUE_RENDERING)
      {
         return false;
      }

      // game_device_data.frame_phase == FramePhase::GBUFFER
      if ((ID3D11Buffer*)dest.handle == game_device_data.cb_transform)
      {
         com_ptr<ID3D11DeviceContext> native_device_context;
         ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
         HRESULT hr = device_child->QueryInterface(&native_device_context);
         return HandleTransformUpdate((ID3D11Buffer*)dest.handle, data, native_device_context.get(), game_device_data, device_data);
      }

      return false;
   }

   static bool OnCreatePipeline(reshade::api::device* device, reshade::api::pipeline_layout layout, uint32_t subobject_count, const reshade::api::pipeline_subobject* subobjects)
   {
      auto& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const auto& subobject = subobjects[i];
         for (uint32_t j = 0; j < subobject.count; ++j)
         {
            if (subobject.type == reshade::api::pipeline_subobject_type::vertex_shader)
            {
               const auto* original_shader_desc = static_cast<reshade::api::shader_desc*>(subobjects[i].data);
               if (System::ScanMemoryForPattern((const std::byte*)original_shader_desc->code, original_shader_desc->code_size, (std::byte*)"mtxLocalToWorldViewProjPrev", 6, true).size() == 0)
               {
                  continue;
               }
               std::vector<std::byte> shader_code((const std::byte*)original_shader_desc->code, ((const std::byte*)original_shader_desc->code) + original_shader_desc->code_size);

               uint32_t prev_coord_output_register;
               PatchVertexShader(shader_code, prev_coord_output_register);
               if (prev_coord_output_register != 0xFFFFFFFF)
               {
                  uint32_t hash = Shader::BinToHash((const uint8_t*)original_shader_desc->code, original_shader_desc->code_size);
                  game_device_data.vertex_shader_ndc_coord_indices[hash] = {prev_coord_output_register - 1, prev_coord_output_register};

                  ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
                  com_ptr<ID3D11VertexShader> patched_shader;
                  native_device->CreateVertexShader(shader_code.data(), shader_code.size(), nullptr, &patched_shader);

                  game_device_data.modified_vertex_shaders[hash] = patched_shader;
               }
            }
            else if (subobject.type == reshade::api::pipeline_subobject_type::pixel_shader)
            {
               const auto* original_shader_desc = static_cast<reshade::api::shader_desc*>(subobjects[i].data);
               uint32_t hash = Shader::BinToHash((const uint8_t*)original_shader_desc->code, original_shader_desc->code_size);
               std::vector<std::byte> code;
               code.resize(original_shader_desc->code_size);
               memcpy(&code[0], original_shader_desc->code, original_shader_desc->code_size);
               game_device_data.pixel_shader_code[hash] = std::move(code);
            }
            else if (subobject.type == reshade::api::pipeline_subobject_type::blend_state)
            {
               auto* blend_desc = static_cast<reshade::api::blend_desc*>(subobjects[i].data);
               blend_desc->blend_enable[5] = true;
               blend_desc->render_target_write_mask[5] = 15;
               return true;
            }
         }
      }
      return false;
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Metaphor Luma mod - about and credits section", "");
      ImGui::Text("xxHash Library\n"
                  "Copyright (c) 2012-2021 Yann Collet\n"
                  "All rights reserved.\n"
                  "\n"
                  "BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)\n"
                  "\n"
                  "Redistribution and use in source and binary forms, with or without modification,\n"
                  "are permitted provided that the following conditions are met:\n"
                  "\n"
                  "* Redistributions of source code must retain the above copyright notice, this\n"
                  "  list of conditions and the following disclaimer.\n"
                  "\n"
                  "* Redistributions in binary form must reproduce the above copyright notice, this\n"
                  "  list of conditions and the following disclaimer in the documentation and/or\n"
                  "  other materials provided with the distribution.\n"
                  "\n"
                  "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" AND\n"
                  "ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED\n"
                  "WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE\n"
                  "DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR\n"
                  "ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES\n"
                  "(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;\n"
                  "LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON\n"
                  "ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
                  "(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS\n"
                  "SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Metaphor Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      Globals::VERSION = 1;

      enable_samplers_upgrade = true;

      shader_hashes_tonemap.pixel_shaders.emplace(std::stoul("A7108284", nullptr, 16));
      shader_hashes_tonemap.pixel_shaders.emplace(std::stoul("C1787BC6", nullptr, 16));

      shader_hashes_merge_particles.pixel_shaders.emplace(std::stoul("AC103037", nullptr, 16));
      shader_hashes_merge_particles.pixel_shaders.emplace(std::stoul("CD84F54A", nullptr, 16));

      shader_hashes_fxaa.pixel_shaders.emplace(std::stoul("94D1203C", nullptr, 16));

      shader_hashes_smaa_edge_detection.pixel_shaders.emplace(std::stoul("8C9E5C72", nullptr, 16));
      shader_hashes_smaa_weight_calculation.pixel_shaders.emplace(std::stoul("CA15EAC0", nullptr, 16));
      shader_hashes_smaa_blending.pixel_shaders.emplace(std::stoul("5732C405", nullptr, 16));

      shader_hashes_lut.pixel_shaders.emplace(std::stoul("D8196629", nullptr, 16));

      shader_hashes_outline.vertex_shaders.emplace(0xBF5FF106);
      shader_hashes_outline.vertex_shaders.emplace(0x155F917A);
      shader_hashes_outline.vertex_shaders.emplace(0xAC0C30DA);
      shader_hashes_outline.vertex_shaders.emplace(0xAA5FA872);
      shader_hashes_outline.vertex_shaders.emplace(0x4BA795B0);
      shader_hashes_outline.vertex_shaders.emplace(0x06974A0D);
      shader_hashes_outline.vertex_shaders.emplace(0xEF234E0D);
      shader_hashes_outline.vertex_shaders.emplace(0x13CA235D);
      shader_hashes_outline.vertex_shaders.emplace(0x4556846C);
      shader_hashes_outline.vertex_shaders.emplace(0xF9DBB0A3);
      shader_hashes_outline.vertex_shaders.emplace(0x4570848A);
      shader_hashes_outline.vertex_shaders.emplace(0x4738BD67);
      shader_hashes_outline.vertex_shaders.emplace(0x791D21BB);
      shader_hashes_outline.vertex_shaders.emplace(0xE74080FF);
      shader_hashes_outline.vertex_shaders.emplace(0x596A7EF4);
      shader_hashes_outline.vertex_shaders.emplace(0x4F8411AC);
      shader_hashes_outline.vertex_shaders.emplace(0x942BB234);
      shader_hashes_outline.vertex_shaders.emplace(0x60258879);
      shader_hashes_outline.vertex_shaders.emplace(0x40C3609D);
      shader_hashes_outline.vertex_shaders.emplace(0x0626E62A);
      shader_hashes_outline.vertex_shaders.emplace(0xCA9890B9);
      shader_hashes_outline.vertex_shaders.emplace(0xF1C91A88);
      shader_hashes_outline.vertex_shaders.emplace(0x32EA4F16);
      shader_hashes_outline.vertex_shaders.emplace(0x0457B469);
      shader_hashes_outline.vertex_shaders.emplace(0xEAC4051F);
      shader_hashes_outline.vertex_shaders.emplace(0x3BEAEE64);
      shader_hashes_outline.vertex_shaders.emplace(0xBC1CB334);
      shader_hashes_outline.vertex_shaders.emplace(0x5AF9F60E);
      shader_hashes_outline.vertex_shaders.emplace(0xF428E7C9);
      shader_hashes_outline.vertex_shaders.emplace(0x83600C7A);
      shader_hashes_outline.vertex_shaders.emplace(0x8074A956);
      shader_hashes_outline.vertex_shaders.emplace(0xBC35C89E);
      shader_hashes_outline.vertex_shaders.emplace(0x698E18C4);
      shader_hashes_outline.vertex_shaders.emplace(0x4444523E);
      shader_hashes_outline.vertex_shaders.emplace(0x1A870BC3);
      shader_hashes_outline.vertex_shaders.emplace(0x6B4BFF34);
      shader_hashes_outline.vertex_shaders.emplace(0x48B83083);
      shader_hashes_outline.vertex_shaders.emplace(0xD4E02B75);
      shader_hashes_outline.vertex_shaders.emplace(0xFD1E6280);
      shader_hashes_outline.vertex_shaders.emplace(0xCC0E1722);
      shader_hashes_outline.vertex_shaders.emplace(0xB8A229B4);
      shader_hashes_outline.vertex_shaders.emplace(0xD24AA7C9);
      shader_hashes_outline.vertex_shaders.emplace(0xEA628D0C);
      shader_hashes_outline.vertex_shaders.emplace(0xA98201C3);
      shader_hashes_outline.vertex_shaders.emplace(0xE52D3977);
      shader_hashes_outline.vertex_shaders.emplace(0x59100809);
      shader_hashes_outline.vertex_shaders.emplace(0x19F7088B);
      shader_hashes_outline.vertex_shaders.emplace(0x74C65285);
      shader_hashes_outline.vertex_shaders.emplace(0x74CE22E3);
      shader_hashes_outline.vertex_shaders.emplace(0xF48CCAC1);
      shader_hashes_outline.vertex_shaders.emplace(0x42C641AC);
      shader_hashes_outline.vertex_shaders.emplace(0x0D23069A);
      shader_hashes_outline.vertex_shaders.emplace(0x29F2DD5F);
      shader_hashes_outline.vertex_shaders.emplace(0xA7A61EEF);
      shader_hashes_outline.vertex_shaders.emplace(0xAC5361B3);
      shader_hashes_outline.vertex_shaders.emplace(0xB60A4BD4);
      shader_hashes_outline.vertex_shaders.emplace(0x66D1D047);
      shader_hashes_outline.vertex_shaders.emplace(0x8FC43E56);
      shader_hashes_outline.vertex_shaders.emplace(0x8E41F8DB);
      shader_hashes_outline.vertex_shaders.emplace(0xF02623E0);
      shader_hashes_outline.vertex_shaders.emplace(0x5263AEB4);
      shader_hashes_outline.vertex_shaders.emplace(0xE30ADBA6);
      shader_hashes_outline.vertex_shaders.emplace(0x5BD11C4A);
      shader_hashes_outline.vertex_shaders.emplace(0x83245268);

      shader_hashes_ocean.vertex_shaders.emplace(0x43A6029F);
      shader_hashes_ocean.vertex_shaders.emplace(0x90D867D7);
      shader_hashes_ocean.vertex_shaders.emplace(0x10401BC4);
      shader_hashes_ocean.vertex_shaders.emplace(0xA74FE1E5);
      shader_hashes_ocean.vertex_shaders.emplace(0xB2510239);

      // unused cbuffer slots by type
      // VS - 4, 5, 8, 9, 13(not used by shader but set by the game)
      // PS - 8, 10, 12
      // CS - 2, 3 , 5, 6, 7, 8, 9, 10, 11, 12, 13
      luma_settings_cbuffer_index = 8;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      force_disable_display_composition = true;

      game = new Metaphor();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::clear_render_target_view>(Metaphor::OnClearRenderTargetView);
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(Metaphor::OnExecuteSecondaryCommandList);
      reshade::unregister_event<reshade::addon_event::update_buffer_region_command>(Metaphor::OnUpdateBufferRegionCommand);
      reshade::unregister_event<reshade::addon_event::create_pipeline>(Metaphor::OnCreatePipeline);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}