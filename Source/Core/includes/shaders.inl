#pragma once

// Included by core.hpp after "CommandListData" and the patch types are complete (see shaders.h for the declarations).

namespace Shader
{
   ShaderStages GetShaderStage(D3D11_SHADER_VERSION_TYPE type)
   {
      switch (type)
      {
      case D3D11_SHVER_VERTEX_SHADER: return ShaderStages::Vertex;
      case D3D11_SHVER_PIXEL_SHADER: return ShaderStages::Pixel;
      case D3D11_SHVER_COMPUTE_SHADER: return ShaderStages::Compute;
#if GEOMETRY_SHADER_SUPPORT
      case D3D11_SHVER_GEOMETRY_SHADER: return ShaderStages::Geometry;
#endif
      default: return ShaderStages::None;
      }
   }

   void RecordCloneShader(CustomPipelineData& data, D3D11_SHADER_VERSION_TYPE type,
      const std::shared_ptr<Patch::PatchedShaderData>& patch)
   {
      const ShaderStages stage = GetShaderStage(type);
      if (stage == ShaderStages::None) return;
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

   CloneOrigin GetCloneOrigin(const CachedPipeline& pipeline, ShaderStages stage)
   {
      if (!pipeline.cloned) return CloneOrigin::None;
      const auto& data = pipeline.custom_pipeline_data;
      if ((data.file_stages & stage) != 0) return CloneOrigin::File;
      if ((data.patch_stages & stage) != 0) return CloneOrigin::Patch;
      return CloneOrigin::None;
   }

   CloneOrigin GetShaderOrigin(const CommandListData& cmd_list_data, ShaderStages stage)
   {
      const CustomPipelineData* data = nullptr;
      switch (stage)
      {
      case ShaderStages::Vertex:
         if (cmd_list_data.pipeline_state_has_custom_vertex_shader) data = &cmd_list_data.custom_vertex_pipeline;
         break;
      case ShaderStages::Pixel:
         if (cmd_list_data.pipeline_state_has_custom_pixel_shader) data = &cmd_list_data.custom_pixel_pipeline;
         break;
      case ShaderStages::Compute:
         if (cmd_list_data.pipeline_state_has_custom_compute_shader) data = &cmd_list_data.custom_compute_pipeline;
         break;
      default: return CloneOrigin::None;
      }
      if (!data) return CloneOrigin::None;
      if ((data->file_stages & stage) != 0) return CloneOrigin::File;
      if ((data->patch_stages & stage) != 0) return CloneOrigin::Patch;
      return CloneOrigin::None;
   }

   void UsePipelineVariant(CommandListData& cmd_list_data, ID3D11DeviceContext* native_device_context, ShaderStages stages, PipelineVariant variant)
   {
      const bool custom = variant == PipelineVariant::Custom;
      auto Use = [&](ShaderStages stage, Shader::Pipeline original, Shader::Pipeline clone, bool& bound, auto set_shader)
      {
         if ((stages & stage) == 0 || clone.handle == 0 || bound == custom) return;
         set_shader(custom ? clone.handle : original.handle);
         bound = custom;
      };
      Use(ShaderStages::Vertex, cmd_list_data.pipeline_state_original_vertex_shader, cmd_list_data.pipeline_state_custom_vertex_shader, cmd_list_data.pipeline_state_has_custom_vertex_shader,
         [&](ID3D11DeviceChild* shader) { native_device_context->VSSetShader(static_cast<ID3D11VertexShader*>(shader), nullptr, 0); });
      Use(ShaderStages::Pixel, cmd_list_data.pipeline_state_original_pixel_shader, cmd_list_data.pipeline_state_custom_pixel_shader, cmd_list_data.pipeline_state_has_custom_pixel_shader,
         [&](ID3D11DeviceChild* shader) { native_device_context->PSSetShader(static_cast<ID3D11PixelShader*>(shader), nullptr, 0); });
      Use(ShaderStages::Compute, cmd_list_data.pipeline_state_original_compute_shader, cmd_list_data.pipeline_state_custom_compute_shader, cmd_list_data.pipeline_state_has_custom_compute_shader,
         [&](ID3D11DeviceChild* shader) { native_device_context->CSSetShader(static_cast<ID3D11ComputeShader*>(shader), nullptr, 0); });
      cmd_list_data.pipeline_state_has_custom_graphics_shader = cmd_list_data.pipeline_state_has_custom_vertex_shader || cmd_list_data.pipeline_state_has_custom_pixel_shader;
   }
} // namespace Shader
