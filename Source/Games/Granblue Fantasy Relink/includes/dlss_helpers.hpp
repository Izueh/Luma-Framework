// =============================================================================
// Granblue Fantasy Relink - DLAA/FSRAA Helper Functions
// Adapted from FFXV implementation for Granblue Fantasy Relink
// =============================================================================
// Extract shader resources from the TAA shader state and store in game_device_data
// Granblue TAA slots: source_color=3, depth=5, motion_vectors=23
// Motion vectors are already decoded and not jittered in Granblue
// Returns true if all required resources are present and valid
static bool ExtractTAAShaderResources(
   ID3D11DeviceContext* native_device_context,
   GameDeviceDataGBFR& game_device_data)
{

   // Get all pixel shader resources (need up to slot 23)
   com_ptr<ID3D11ShaderResourceView> ps_shader_resources[24];
   native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));

   // Validate that required SRVs are present
   // t3 = current color, t23 = motion vectors (required); t5 = depth (optional — absent for
   // NewAA at <100% scale; depth_buffer is pre-populated from the outline CS intercept).
   if (!ps_shader_resources[3].get() || !ps_shader_resources[23].get())
   {
      return false;
   }

   // Extract resources from known Granblue TAA slots
   // t3 = current color (source for SR)
   game_device_data.sr_source_color = nullptr;
   ps_shader_resources[3]->GetResource(&game_device_data.sr_source_color);

   // t5 = depth buffer (optional — NewAA at <100% scale does not bind t5;
   // depth was cached from the outline CS dispatch (0xDA85F5BB) earlier this frame).
   if (ps_shader_resources[5].get())
   {
      game_device_data.depth_buffer = nullptr;
      ps_shader_resources[5]->GetResource(&game_device_data.depth_buffer);
   }

   // t23 = motion vectors (already decoded, not jittered)
   game_device_data.sr_motion_vectors = nullptr;
   ps_shader_resources[23]->GetResource(&game_device_data.sr_motion_vectors);

   // Depth is either freshly extracted from t5 or pre-cached from the outline CS.
   return game_device_data.sr_source_color.get() != nullptr &&
          game_device_data.sr_motion_vectors.get() != nullptr;
}

// Setup DLSS/FSR output texture.
static bool SetupSROutput(
   ID3D11Device* native_device,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data)
{
   game_device_data.output_changed = false;
   game_device_data.output_supports_uav = false;

   // Get output texture from render target

   com_ptr<ID3D11Texture2D> out_texture;
   HRESULT hr;

   if (game_device_data.render_scale == 1.f)
   {
      // In DLAA mode we can write directly to the TAA output RT when UAV-capable, so get the underlying texture.
      hr = game_device_data.taa_rt1_resource->QueryInterface(&out_texture);
   }
   else
   {
      // In upscaling mode we need a separate output texture to avoid overwriting the TAA input (and because the TUP shader is recorded with t3 = sr_output_color_srv, so we must fill sr_output_color with the DLSS output).
      hr = game_device_data.taa_upscale_rt_resource->QueryInterface(&out_texture);
   }
   if (FAILED(hr))
      return false;

   D3D11_TEXTURE2D_DESC out_texture_desc;

   out_texture->GetDesc(&out_texture_desc);
   game_device_data.taa_rt1_desc = out_texture_desc;

   // Prefer writing directly to the native TAA output when UAV-capable.
   constexpr bool use_native_uav = true;
   game_device_data.output_supports_uav = use_native_uav && (out_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;

   // Get SR instance data for min resolution check
   auto* sr_instance_data = device_data.GetSRInstanceData();
   if (sr_instance_data)
   {
      if (out_texture_desc.Width < sr_instance_data->min_resolution ||
          out_texture_desc.Height < sr_instance_data->min_resolution)
         return false;
   }
   // Create or reuse output texture if needed
   if (!game_device_data.output_supports_uav)
   {
      D3D11_TEXTURE2D_DESC dlss_output_desc = out_texture_desc;
      dlss_output_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

      if (device_data.sr_output_color.get())
      {
         D3D11_TEXTURE2D_DESC prev_desc;
         device_data.sr_output_color->GetDesc(&prev_desc);
         game_device_data.output_changed = prev_desc.Width != dlss_output_desc.Width ||
                                           prev_desc.Height != dlss_output_desc.Height ||
                                           prev_desc.Format != dlss_output_desc.Format;
      }

      if (!device_data.sr_output_color.get() || game_device_data.output_changed)
      {
         device_data.sr_output_color = nullptr;
         hr = native_device->CreateTexture2D(&dlss_output_desc, nullptr, &device_data.sr_output_color);
         if (FAILED(hr))
            return false;
      }

      if (!device_data.sr_output_color.get())
         return false;
   }
   else
   {
      device_data.sr_output_color = out_texture;
   }

   return true;
}
