#pragma once

// Included by core.hpp after "CommandListData" and the patch types are complete (see shaders.h for the declarations).

namespace Shader
{
   reshade::api::shader_stage GetShaderStage(reshade::api::pipeline_subobject_type type)
   {
      switch (type)
      {
      case reshade::api::pipeline_subobject_type::vertex_shader: return reshade::api::shader_stage::vertex;
      case reshade::api::pipeline_subobject_type::pixel_shader: return reshade::api::shader_stage::pixel;
      case reshade::api::pipeline_subobject_type::compute_shader: return reshade::api::shader_stage::compute;
#if GEOMETRY_SHADER_SUPPORT
      case reshade::api::pipeline_subobject_type::geometry_shader: return reshade::api::shader_stage::geometry;
#endif
      default: return reshade::api::shader_stage(0);
      }
   }

   void RecordCloneShader(CustomPipelineData& data, reshade::api::pipeline_subobject_type type,
      const std::shared_ptr<Patch::PatchedShaderData>& patch)
   {
      const reshade::api::shader_stage stage = GetShaderStage(type);
      if (stage == reshade::api::shader_stage(0)) return;
      if (!patch) data.file_stages |= stage;
      else
      {
         data.patch_stages |= stage;
#if LUMA_USE_DXP
         if (!patch->recipe_results.empty())
         {
            if (!data.patch_data) data.patch_data = std::make_shared<Patch::ClonePatchData>();
            data.patch_data->recipe_results.insert(data.patch_data->recipe_results.end(), patch->recipe_results.begin(), patch->recipe_results.end());
         }
#endif
      }
   }

   CloneOrigin GetCloneOrigin(const CachedPipeline& pipeline, reshade::api::shader_stage stage)
   {
      if (!pipeline.cloned) return CloneOrigin::None;
      const auto& data = pipeline.custom_pipeline_data;
      if ((data.file_stages & stage) != 0) return CloneOrigin::File;
      if ((data.patch_stages & stage) != 0) return CloneOrigin::Patch;
      return CloneOrigin::None;
   }

   CloneOrigin GetShaderOrigin(const CommandListData& cmd_list_data, reshade::api::shader_stage stage)
   {
      const CustomPipelineData* data = nullptr;
      switch (stage)
      {
      case reshade::api::shader_stage::vertex:
         if (cmd_list_data.pipeline_state_has_custom_vertex_shader) data = &cmd_list_data.custom_vertex_pipeline;
         break;
      case reshade::api::shader_stage::pixel:
         if (cmd_list_data.pipeline_state_has_custom_pixel_shader) data = &cmd_list_data.custom_pixel_pipeline;
         break;
      case reshade::api::shader_stage::compute:
         if (cmd_list_data.pipeline_state_has_custom_compute_shader) data = &cmd_list_data.custom_compute_pipeline;
         break;
      default: return CloneOrigin::None;
      }
      if (!data) return CloneOrigin::None;
      if ((data->file_stages & stage) != 0) return CloneOrigin::File;
      if ((data->patch_stages & stage) != 0) return CloneOrigin::Patch;
      return CloneOrigin::None;
   }

   void UsePipelineVariant(CommandListData& cmd_list_data, ID3D11DeviceContext* native_device_context, reshade::api::shader_stage stages, PipelineVariant variant)
   {
      const bool custom = variant == PipelineVariant::Custom;
      auto Use = [&](reshade::api::shader_stage stage, reshade::api::pipeline original, reshade::api::pipeline clone, bool& bound, auto set_shader)
      {
         if ((stages & stage) == 0 || clone.handle == 0 || bound == custom) return;
         set_shader(custom ? clone.handle : original.handle);
         bound = custom;
      };
      Use(reshade::api::shader_stage::vertex, cmd_list_data.pipeline_state_original_vertex_shader, cmd_list_data.pipeline_state_custom_vertex_shader, cmd_list_data.pipeline_state_has_custom_vertex_shader,
         [&](uint64_t handle) { native_device_context->VSSetShader(reinterpret_cast<ID3D11VertexShader*>(handle), nullptr, 0); });
      Use(reshade::api::shader_stage::pixel, cmd_list_data.pipeline_state_original_pixel_shader, cmd_list_data.pipeline_state_custom_pixel_shader, cmd_list_data.pipeline_state_has_custom_pixel_shader,
         [&](uint64_t handle) { native_device_context->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(handle), nullptr, 0); });
      Use(reshade::api::shader_stage::compute, cmd_list_data.pipeline_state_original_compute_shader, cmd_list_data.pipeline_state_custom_compute_shader, cmd_list_data.pipeline_state_has_custom_compute_shader,
         [&](uint64_t handle) { native_device_context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(handle), nullptr, 0); });
      cmd_list_data.pipeline_state_has_custom_graphics_shader = cmd_list_data.pipeline_state_has_custom_vertex_shader || cmd_list_data.pipeline_state_has_custom_pixel_shader;
   }
} // namespace Shader
