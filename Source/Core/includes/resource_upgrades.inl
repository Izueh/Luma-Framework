#pragma once

// Implementation of ResourceUpgradeManager (see resource_upgrades.hpp).

std::optional<ResourceDesc> ResourceUpgradeManager::GetOptionalResourceUpgradeDesc(const ResourceDesc& desc, const ResourceUpgradeFrameState& state, bool has_initial_data) const
{
   if (texture_format_upgrades_type < TextureFormatUpgradesType::AllowedEnabled)
   {
      return std::nullopt;
   }

   const bool is_ua = (desc.bind_flags & D3D11_BIND_UNORDERED_ACCESS) != 0;
   const bool is_rt_or_ua = (desc.bind_flags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS)) != 0;
   // Convoluted check to test if the resource is "D3D11_USAGE_DEFAULT" (the only usage type that can be both used as SRV, and be the target of a CopyResource(), otherwise we'd never need to upgrade them).
   // We also check the initial data for extra safety, in case this was a "static" content texture that accidentally wasn't created as immutable.
   // This is needed by "Thumper", and possibly "Watch Dogs 2".
   const bool is_writable_sr = (desc.bind_flags & D3D11_BIND_SHADER_RESOURCE) != 0 && desc.usage == D3D11_USAGE_DEFAULT && !has_initial_data;

   const bool is_depth = (desc.bind_flags & D3D11_BIND_DEPTH_STENCIL) != 0;

   const bool is_cube = desc.IsCube();

   // Color textures should only be upgraded if the GPU writes to them (render targets or unordered access), or if they are writable shader resources (they could be the target of copies) and chain upgrades are on
   const bool is_upgradable_color = is_rt_or_ua || (enable_chain_indirect_texture_format_upgrades >= ChainTextureFormatUpgradesType::DirectDependencies && is_writable_sr);
   const bool usage_filter = is_upgradable_color || is_depth;

   if (!usage_filter)
   {
      return std::nullopt;
   }

   // At least in DX11, any resource that isn't exclusively accessible by the GPU, can't be set as output (render target/unordered access).
   // These probably wouldn't have the RT/UA usage flags set anyway, or they'd fail on creation if they did.
   if (desc.usage != D3D11_USAGE_DEFAULT && desc.usage != D3D11_USAGE_IMMUTABLE)
   {
      return std::nullopt;
   }

   ResourceDesc upgraded_desc = desc;

   bool resize_filter = false;

   auto SetUpgradedValue = []<typename T>(T& value, auto upgraded_value, bool& filter)
   {
      if (value == (T)upgraded_value) return;
      value = (T)upgraded_value;
      filter = true;
   };
   auto SetUpgradedValueConditional = []<typename T>(bool condition, T& value, auto upgraded_value, bool& filter)
   {
      if (!condition) return;
      value = (T)upgraded_value;
      filter = true;
   };

   for (const auto& [sizes_filter, source_format_filter, sizes_override] : texture_custom_dimensions_upgrades)
   {
      // For now we skip textures with inital data. Eventually we could stretch it.
      if (has_initial_data) continue;

      if (source_format_filter != DXGI_FORMAT_UNKNOWN && source_format_filter != desc.format) continue;

      if ((sizes_filter.x <= 0 || sizes_filter.x == desc.width)
         && (sizes_filter.y <= 0 || sizes_filter.y == desc.height)
         && (sizes_filter.z <= 0 || sizes_filter.z == desc.depth_or_layers)
         && (sizes_filter.w <= 0 || sizes_filter.w == desc.samples))
      {
         // Skip any size change that would create an invalid configuration.

         bool wants_width_override = sizes_override.x > 0 && sizes_override.x != desc.width; // Always supported

         bool supports_height_override = (desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D || desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D) && !is_cube;
         bool wants_height_override = sizes_override.y > 0 && sizes_override.y != desc.height;
         if (wants_height_override && !supports_height_override) continue;

         bool supports_depth_layers_override = !is_cube;
         bool wants_depth_layers_override = sizes_override.z > 0 && sizes_override.z != desc.depth_or_layers;
         if (wants_depth_layers_override && !supports_depth_layers_override) continue;

         bool supports_samples_override = desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D && !is_cube && !is_ua && desc.levels == 1; // Only non cube non array 2D textures support MS
         bool wants_samples_override = sizes_override.w > 0 && sizes_override.w != desc.samples;
         if (wants_samples_override && !supports_samples_override) continue;

         SetUpgradedValueConditional(wants_width_override, upgraded_desc.width, sizes_override.x, resize_filter);
         SetUpgradedValueConditional(wants_height_override, upgraded_desc.height, sizes_override.y, resize_filter);
         SetUpgradedValueConditional(wants_depth_layers_override, upgraded_desc.depth_or_layers, sizes_override.z, resize_filter);
         SetUpgradedValueConditional(wants_samples_override, upgraded_desc.samples, sizes_override.w, resize_filter);

         if (!resize_filter)
            break;

         // Cubes can only be overridden by exclusively specifying width upgrades, so spread it to the height too (they need to match)
         if (is_cube)
         {
            SetUpgradedValue(upgraded_desc.height, upgraded_desc.width, resize_filter);
         }

         if (upgraded_desc.levels > 1)
         {
            // Only 3D textures generate mips on their depth as well, array layers and cube faces each have their own (independent) mip chain
            const bool is_3d_texture = desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D;
            const uint32_t original_max_levels = ResourceUpgradeGetTextureMaxMipLevels(desc.width, desc.height, is_3d_texture ? desc.depth_or_layers : 0);
            const uint32_t upgraded_max_levels = ResourceUpgradeGetTextureMaxMipLevels(upgraded_desc.width, upgraded_desc.height, is_3d_texture ? upgraded_desc.depth_or_layers : 0);

            // It had a full mip chain (it went all the way down to 1x1 or 1x1x1), so keep it full at the new size too
            if (desc.levels == original_max_levels)
            {
               SetUpgradedValue(upgraded_desc.levels, upgraded_max_levels, resize_filter);
            }
            // The size shrank (or the chain was already longer than it could be), and mips can't go below 1x1 (or 1x1x1), so clamp them
            if (upgraded_desc.levels > upgraded_max_levels)
            {
               SetUpgradedValue(upgraded_desc.levels, upgraded_max_levels, resize_filter);
            }
         }

         break;
      }
   }

   // The format needs to be in the upgrades list (color and depth textures have separate ones)
   const bool format_filter = (is_upgradable_color && texture_upgrade_formats.contains(desc.format)) || (is_depth && texture_depth_upgrade_formats.contains(desc.format));

   if (!format_filter && !resize_filter)
   {
      return std::nullopt;
   }

   // Note: we can't fully exclude texture 2D arrays here in ReShade, because they might still have 1 layer
   bool type_and_size_format_filter = format_filter && desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D && (desc.depth_or_layers == 1 || is_cube);

   if (texture_format_upgrades_2d_size_filters != (uint32_t)TextureFormatUpgrades2DSizeFilters::All)
   {
      bool size_filter = false;

      uint2 render_resolution = uint2(state.render_resolution.x, state.render_resolution.y);
      uint2 output_resolution = uint2(state.output_resolution.x, state.output_resolution.y);
      uint2 display_resolution = uint2(state.display_resolution.x, state.display_resolution.y);

      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::PadTo4Px) != 0)
      {
         // If the texture is 4px aligned, pad all the resolution checks as well
         bool is_4px_aligned = (desc.width % 4) == 0 && (desc.height % 4) == 0;
         if (is_4px_aligned)
         {
            render_resolution.x = (render_resolution.x + 3u) & ~3u;
            render_resolution.y = (render_resolution.y + 3u) & ~3u;
            output_resolution.x = (output_resolution.x + 3u) & ~3u;
            output_resolution.y = (output_resolution.y + 3u) & ~3u;
            display_resolution.x = (display_resolution.x + 3u) & ~3u;
            display_resolution.y = (display_resolution.y + 3u) & ~3u;
         }
      }

      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::DisplayResolution) != 0)
      {
         size_filter |= desc.width == display_resolution.x && desc.height == display_resolution.y;
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution) != 0)
      {
         size_filter |= desc.width == output_resolution.x && desc.height == output_resolution.y;
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolutionWidth) != 0)
      {
         size_filter |= desc.width == output_resolution.x;
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolutionHeight) != 0)
      {
         size_filter |= desc.height == output_resolution.y;
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::RenderResolution) != 0)
      {
         size_filter |= desc.width == render_resolution.x && desc.height == render_resolution.y;
      }
      // Flipped condition, given we already allowed them above in "type_and_size_format_filter"
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::Cubes) == 0)
      {
         size_filter &= !is_cube;
      }
      // No limits on the size, we can assume it was square
      else
      {
         size_filter |= is_cube;
      }

      // Always scale from the smallest dimension, as that gives up more threshold, depending on how the devs scaled down textures (they can use multiple rounding models)
      float min_aspect_ratio = desc.width <= desc.height ? ((float)(desc.width - texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)desc.height) : ((float)desc.width / (float)(desc.height + texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
      float max_aspect_ratio = desc.width <= desc.height ? ((float)(desc.width + texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)desc.height) : ((float)desc.width / (float)(desc.height - texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
      bool generating_manual_mips = false;
#if DEVELOPMENT
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio) != 0
         || (texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::RenderAspectRatio) != 0
         || (texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio) != 0)
      {
         static thread_local UINT last_texture_width = desc.width;
         static thread_local UINT last_texture_height = desc.height;
         // If this was a chain of downscaling, don't send a warning! This is just a heuristics based check... The creation order might have been random, or inverted (from smaller to bigger mips).
         // Note that this isn't thread safe but whatever
         if (max(desc.width, desc.height) == 1)
         {
            generating_manual_mips = (last_texture_width / 2) == desc.width && (last_texture_height / 2) == desc.height;
         }
         last_texture_width = desc.width;
         last_texture_height = desc.height;
      }
#endif
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio) != 0)
      {
         float target_aspect_ratio = (float)output_resolution.x / (float)output_resolution.y;
         bool aspect_ratio_filter = target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON);
         size_filter |= aspect_ratio_filter;
#if DEVELOPMENT
         ASSERT_ONCE_MSG(!aspect_ratio_filter || max(desc.width, desc.height) > 1 || generating_manual_mips || ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0), "Upgrading 1x1 resource by aspect ratio, this is possibly unwanted"); // TODO: add a min size for upgrades? Like >1 or >32 on the smallest axis? Or ... scan if the allocations shrink in size over time
#endif
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::RenderAspectRatio) != 0)
      {
         float target_aspect_ratio = (float)render_resolution.x / (float)render_resolution.y;
         bool aspect_ratio_filter = target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON);
         size_filter |= aspect_ratio_filter;
#if DEVELOPMENT
         ASSERT_ONCE_MSG(!aspect_ratio_filter || max(desc.width, desc.height) > 1 || generating_manual_mips || ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0), "Upgrading 1x1 resource by aspect ratio, this is possibly unwanted");
#endif
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::DisplayAspectRatio) != 0)
      {
         float target_aspect_ratio = (float)display_resolution.x / (float)display_resolution.y;
         bool aspect_ratio_filter = target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON);
         size_filter |= aspect_ratio_filter;
#if DEVELOPMENT
         ASSERT_ONCE_MSG(!aspect_ratio_filter || max(desc.width, desc.height) > 1 || generating_manual_mips || ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0), "Upgrading 1x1 resource by aspect ratio, this is possibly unwanted");
#endif
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio) != 0)
      {
         const std::shared_lock lock_texture_upgrades(mutex);
         for (auto texture_format_upgrades_2d_custom_aspect_ratio : texture_format_upgrades_2d_custom_aspect_ratios)
         {
            float target_aspect_ratio = texture_format_upgrades_2d_custom_aspect_ratio;
            bool aspect_ratio_filter = target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON);
            size_filter |= aspect_ratio_filter;
#if DEVELOPMENT
            ASSERT_ONCE_MSG(!aspect_ratio_filter || max(desc.width, desc.height) > 1 || generating_manual_mips || ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0), "Upgrading 1x1 resource by aspect ratio, this is possibly unwanted");
#endif
         }
      }
      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomSize) != 0)
      {
         const std::shared_lock lock_texture_upgrades(mutex);
         for (auto texture_format_upgrades_2d_custom_size : texture_format_upgrades_2d_custom_sizes)
         {
            size_filter |= desc.width == texture_format_upgrades_2d_custom_size.x && desc.height == texture_format_upgrades_2d_custom_size.y;
            if (size_filter) break;
         }
      }

      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::Mips) != 0)
      {
         auto max_resolution = output_resolution.y >= render_resolution.y ? output_resolution : render_resolution;
         size_filter |= ResourceUpgradeIsMipOf(max_resolution.x, max_resolution.y, desc.width, desc.height);
      }

      if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0)
      {
         size_filter &= desc.width != 1 || desc.height != 1;
      }

      type_and_size_format_filter &= size_filter;
   }

#if DEVELOPMENT && 0 // TODO: WIP size upgrades. We can't really do this without upgrading every single render target and depth/stencil texture, as all RTs need to have the same size. We'd also need to scale the viewport.
   //upgraded_desc.samples = max(upgraded_desc.samples, 4); // Unlikely MSAA will work
   if (indirect_upgraded_textures_size_scaling != 1.f && upgraded_desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
   {
      // Ideally we'd either scale by 0.5 or 2, to keep them compatibile with linear sampler,
      // and to be able to scale mips
      upgraded_desc.width *= uint(indirect_upgraded_textures_size_scaling);
      upgraded_desc.height *= uint(indirect_upgraded_textures_size_scaling);
      // Make one more mip if we duplicate the size
      if (upgraded_desc.levels > 1)
      {
         if (indirect_upgraded_textures_size_scaling > 1.f)
         {
            upgraded_desc.levels++;
         }
         else
         {
            upgraded_desc.levels--;
         }
      }
   }
#endif

   if (!is_depth)
   {
      switch (texture_format_upgrades_lut_dimensions)
      {
      case LUTDimensions::_1D:
      {
         // For 1D, "texture_format_upgrades_lut_size" is the whole width (usually they extend in width)
         type_and_size_format_filter |= format_filter && desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D && desc.width == texture_format_upgrades_lut_size && desc.height == 1 && desc.depth_or_layers == 1 && desc.levels == 1;
         break;
      }
      default:
      case LUTDimensions::_2D:
      {
         // For 2D, "texture_format_upgrades_lut_size" is the height, usually they extend in width and that's squared
         type_and_size_format_filter |= format_filter && desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D && desc.width == (texture_format_upgrades_lut_size * texture_format_upgrades_lut_size) && desc.height == texture_format_upgrades_lut_size && desc.depth_or_layers == 1 && desc.levels == 1 && desc.samples == 1;
         break;
      }
      case LUTDimensions::_3D:
      {
         // For 3D, all the dimensions usually match
         type_and_size_format_filter |= format_filter && desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D && desc.width == texture_format_upgrades_lut_size && desc.height == texture_format_upgrades_lut_size && desc.depth_or_layers == texture_format_upgrades_lut_size && desc.levels == 1;
         break;
      }
      }
   }

   if (type_and_size_format_filter)
   {
      upgraded_desc.format = GetBestResourceUpgradeFormat(upgraded_desc);
   }

   if (type_and_size_format_filter || resize_filter)
   {
      return upgraded_desc;
   }
   return std::nullopt;
}

bool ResourceUpgradeManager::FindOrCreateIndirectUpgradedResource(
   ID3D11Device* device,
   const uint64_t in_source_resource,
   const uint64_t in_resource,
   uint64_t& out_resource,
   bool allow_create,
   std::shared_lock<std::shared_mutex>& lock_device_read,
   const ResourceUpgradeFrameState& state,
   bool should_scale,
   bool leave_locked,
   UINT additional_bind_flags,
   bool* scaled)
{
   bool replaced = false;

   auto original_resource_to_mirrored_upgraded_resource = original_resources_to_mirrored_upgraded_resources.find(in_resource); // Note: no null check here, given the case should be rare
   if (original_resource_to_mirrored_upgraded_resource != original_resources_to_mirrored_upgraded_resources.end())
   {
      out_resource = original_resource_to_mirrored_upgraded_resource->second.mirror_handle;
      replaced = true;
      if (scaled)
         *scaled = original_resource_to_mirrored_upgraded_resource->second.is_scaled;
   }
   // Ignore all swapchain textures, we can't directly upgrade these (even in case they weren't directly upgraded), given that it's the ultimate target and somehow we'll need to write the values in it // TODO: not true, we could still swap its texture and then copy it back on the og swapchain on presentation
   else if (allow_create && in_resource != 0 && !upgraded_resources.contains(in_resource))
   {
      lock_device_read.unlock(); // Avoids deadlocks with the device

      ID3D11Resource* mirrored_upgraded_resource = nullptr;
      ResourceDesc source_desc = GetResourceDesc(in_resource);
      ResourceDesc target_desc = source_desc;
      // Keep the original (render) size for the mirror bookkeeping, before the scale override below.
      const uint32_t original_width = source_desc.width;
      const uint32_t original_height = source_desc.height;
      bool needs_upgraded_resource = false;
      // Chained upgrades. Take part of the desc from the alternative source. // TODO: polish this, it's incomplete.
      if (in_source_resource)
      {
         source_desc = GetResourceDesc(in_source_resource);

         float min_aspect_ratio = target_desc.width <= target_desc.height ? ((float)(target_desc.width - texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)target_desc.height) : ((float)target_desc.width / (float)(target_desc.height + texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
         float max_aspect_ratio = target_desc.width <= target_desc.height ? ((float)(target_desc.width + texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)target_desc.height) : ((float)target_desc.width / (float)(target_desc.height - texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
         float target_aspect_ratio = (float)source_desc.width / (float)source_desc.height;
         bool is_1x_square = target_desc.width == 1 && target_desc.height == 1;
         bool aspect_ratio_filter = source_desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D && !is_1x_square && target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON); // Note: we don't check the aspect ratio on the depth, we only do it on 2D textures. We also ignore 1x1 and 2x2 for extra safety
         bool size_filter = source_desc.width == target_desc.width && source_desc.height == target_desc.height && source_desc.depth_or_layers == target_desc.depth_or_layers;
         size_filter |= aspect_ratio_filter;
         //size_filter |= source_desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D && target_desc.width == uint(state.output_resolution.x + 0.5) && target_desc.height == uint(state.output_resolution.y + 0.5); // Force upgrade if it's equal to the swapchain resolution, this pass could be one that converts from a constrained to a fullscreen aspect ratio

         // Avoid upgrading textures that don't have the same number of channels (unless they'd now have more!), we wouldn't want to automatically turn 1 channel to 4 channel textures.
         // Also prevent upgrades if the size isn't compatible (aspect ratio matching).
         // And don't upgrade int formats for now, they could only cause troubles.
         // See "enable_chain_indirect_texture_format_upgrades" for more.
         needs_upgraded_resource = !AreFormatsCopyCompatible(source_desc.format, target_desc.format)
            && IsRGBAFormat(source_desc.format, true) == IsRGBAFormat(target_desc.format, true)
            && !IsIntFormat(target_desc.format)
            && size_filter;

         // TODO: instead of checking the formats for compatibility, also check if the source was upgraded and in that case force the target to be upgraded (faster checks)
         if (needs_upgraded_resource)
         {
            target_desc.format = source_desc.format;
         }

         needs_upgraded_resource &= target_desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D; // Filter out false positives (UAVs can be buffers)
      }
      else // Upgrade texture desc
      {
#if 1
         if (std::optional<ResourceDesc> upgraded_desc = GetOptionalResourceUpgradeDesc(source_desc, state, false))
         {
            target_desc = upgraded_desc.value();
            needs_upgraded_resource = true;
         }
#else // TODO: delete older simpler branch
         target_desc.format = GetBestResourceUpgradeFormat(source_desc);
         needs_upgraded_resource = source_desc.format != target_desc.format;
#endif
      }

      // Resolution scaling (upscale only): scale the mirror from render_resolution to output_resolution
      bool needs_scale = false;
      // Only scale when SR engaged and render resolution is smaller
      if (target_desc.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D
         && should_scale
         && state.render_resolution.x > 0 && state.render_resolution.y > 0 // TODO: pointless check? It defaults to swapchain
         && state.render_resolution.x < state.output_resolution.x
         && state.render_resolution.y < state.output_resolution.y)
      {
         const bool is_1x1 = target_desc.width == 1 && target_desc.height == 1;
         const float min_aspect = target_desc.width <= target_desc.height
            ? ((float)(target_desc.width - texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)target_desc.height)
            : ((float)target_desc.width / (float)(target_desc.height + texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
         const float max_aspect = target_desc.width <= target_desc.height
            ? ((float)(target_desc.width + texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)target_desc.height)
            : ((float)target_desc.width / (float)(target_desc.height - texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
         const float render_aspect = state.render_resolution.x / state.render_resolution.y;
         const bool matches_render = !is_1x1 && render_aspect >= (min_aspect - FLT_EPSILON) && render_aspect <= (max_aspect + FLT_EPSILON);
         if (matches_render)
         {
            if (in_source_resource == 0) // TODO: why this check here? Also, we should always check "allow_scale" to avoid random scaling positives??? It's a fallback value in case we have no source desc specified?
               needs_scale = true; // seed: per-shader toggle only
            else
               needs_scale = source_desc.width == (uint32_t)state.output_resolution.x && source_desc.height == (uint32_t)state.output_resolution.y; // chain: source already at output res
         }
      }
      if (needs_scale)
      {
         // Scale to full output resolution (matches FFXV's native upscale), so mirrors are the same
         // size as the swapchain and downstream copies to it stay size-matched.
         target_desc.width  = (uint32_t)state.output_resolution.x;
         target_desc.height = (uint32_t)state.output_resolution.y;
         needs_upgraded_resource = true;
      }

      target_desc.bind_flags |= additional_bind_flags;

      if (needs_upgraded_resource && SUCCEEDED(CreateResource(device, target_desc, nullptr, &mirrored_upgraded_resource)))
      {
         std::unique_lock lock_device_write(*lock_device_read.mutex());
         if (!original_resources_to_mirrored_upgraded_resources.contains(in_resource))
         {
            original_resources_to_mirrored_upgraded_resources[in_resource] = IndirectUpgradedResource{ reinterpret_cast<uint64_t>(mirrored_upgraded_resource), needs_scale, original_width, original_height, target_desc.width, target_desc.height };
            out_resource = reinterpret_cast<uint64_t>(mirrored_upgraded_resource);

            // TODO: optionally copy the content of "in_resource"?
         }
         else // Destroy it if it was accidentally created at the same time by another thread
         {
            out_resource = original_resources_to_mirrored_upgraded_resources[in_resource].mirror_handle;
            lock_device_write.unlock();
            mirrored_upgraded_resource->Release();
         }

         replaced = true;
      }
      else if (needs_upgraded_resource)
      {
         ASSERT_ONCE_MSG(false, "Failed to create an indirect upgraded texture");
      }

      if (leave_locked)
         lock_device_read.lock();
   }

   // Let the upgrades happen above, but ignore the override
   if (ignore_indirect_upgraded_textures)
   {
      if (out_resource)
         out_resource = in_resource;
      return false;
   }

   return replaced;
}

bool ResourceUpgradeManager::FindOrCreateIndirectUpgradedResourceView(
   ID3D11Device* device,
   const uint64_t in_rv,
   uint64_t& out_rv,
   bool allow_create,
   ViewKind view_kind,
   std::shared_lock<std::shared_mutex>& lock_device_read)
{
   bool replaced = false;

   // See if we already have a indirect resource view mapped to this resource view
   auto original_resource_view_to_mirrored_upgraded_resource_view = in_rv ? original_resource_views_to_mirrored_upgraded_resource_views.find(in_rv) : original_resource_views_to_mirrored_upgraded_resource_views.end();
   if (original_resource_view_to_mirrored_upgraded_resource_view != original_resource_views_to_mirrored_upgraded_resource_views.end())
   {
      replaced = true;
      out_rv = original_resource_view_to_mirrored_upgraded_resource_view->second;
   }
   // Otherwise, create it.
   // For example, sometimes we upgrade resources after creation and we can't know all the views that were previously created for the original resource (well, we could cache them on creation based on the list of formats we ever upgrade, if ever...),
   // so we need to create a mirrored upgraded view for every view it had.
   // TODO: just cache all the views for any resource we might ever upgrade later (e.g. through "auto_texture_format_upgrade_shader_hashes"), as mentioned above, so we could skip many of these checks.
   else if (allow_create && in_rv != 0 && !original_resources_to_mirrored_upgraded_resources.empty())
   {
      const uint64_t resource = GetCachedResourceFromView(in_rv, true, false, true, &lock_device_read);
      auto original_resource_to_mirrored_upgraded_resource = original_resources_to_mirrored_upgraded_resources.find(resource);
      if (original_resource_to_mirrored_upgraded_resource != original_resources_to_mirrored_upgraded_resources.end())
      {
         const auto original_resource_to_mirrored_upgraded_resource_ptr = original_resource_to_mirrored_upgraded_resource->second.mirror_handle;

         lock_device_read.unlock(); // Avoids deadlocks with the device

         ResourceViewDesc resource_view_desc = GetResourceViewDesc(in_rv);
         ASSERT_ONCE(resource_view_desc.kind == view_kind);
         const ResourceDesc mirrored_upgraded_resource_desc = GetResourceDesc(original_resource_to_mirrored_upgraded_resource_ptr); // The format should match previous calls to "GetBestResourceUpgradeFormat()"
         // Depth/Stencil resources are the only ones "GetBestResourceUpgradeFormat()" upgrades to a typeless format (so they can be cast to both a depth/stencil write view and a shader resource view),
         // and views can't use typeless formats, so here we can't let the format be determined automatically from the resource, we need to explicitly pick the view format matching the upgrade.
         // Note that this applies to their SRVs as well, not just their DSVs.
         if (IsTypelessFormat(mirrored_upgraded_resource_desc.format))
         {
            const ResourceDesc original_resource_desc = GetResourceDesc(resource);
            // We need an explicit format to tell which plane (and thus which view format) the original view was reading/writing.
            // At least in DX11 that's always the case for depth/stencil resources, given neither DSVs nor SRVs can inherit a typeless resource format.
            ASSERT_ONCE(resource_view_desc.Format() != DXGI_FORMAT_UNKNOWN);
            // For "depth_stencil" usage this returns "d32_float" or "d32_float_s8_uint" (depending on whether the original resource had a stencil).
            // Note that DX11 has no stencil only DSV: a DSV format always covers the depth, and the stencil too if the resource has one (read only depth/stencil is expressed through the "D3D11_DSV_READ_ONLY_*" flags, not through the format).
            // For any other usage (e.g. SRVs) it returns the depth read view ("r32_float" / "r32_float_x8_uint") or the stencil read view ("x32_float_g8_uint"), mirroring whichever plane the original view read.
            resource_view_desc.Format() = GetBestResourceViewUpgradeFormat(resource_view_desc, view_kind, original_resource_desc, mirrored_upgraded_resource_desc);
            ASSERT_ONCE(!IsTypelessFormat(resource_view_desc.Format())); // We failed to pick a castable format, the view creation below would fail
         }
         // Use the mirror's format. All the other formats returned by "GetBestResourceUpgradeFormat()" are not typeless, so we can make views of (almost) all of them directly.
         else
         {
            resource_view_desc.Format() = mirrored_upgraded_resource_desc.format;
         }

         ID3D11View* mirrored_upgraded_resource_view = nullptr;
         if (SUCCEEDED(CreateResourceView(device, reinterpret_cast<ID3D11Resource*>(original_resource_to_mirrored_upgraded_resource_ptr), resource_view_desc, &mirrored_upgraded_resource_view)))
         {
            std::unique_lock lock_device_write(*lock_device_read.mutex());
            if (!original_resource_views_to_mirrored_upgraded_resource_views.contains(in_rv))
            {
               original_resource_views_to_mirrored_upgraded_resource_views[in_rv] = reinterpret_cast<uint64_t>(mirrored_upgraded_resource_view);
               mirror_views_by_mirror_resource[original_resource_to_mirrored_upgraded_resource_ptr].emplace(reinterpret_cast<uint64_t>(mirrored_upgraded_resource_view));
               mirror_views_to_mirror_resources[reinterpret_cast<uint64_t>(mirrored_upgraded_resource_view)] = original_resource_to_mirrored_upgraded_resource_ptr;
               out_rv = reinterpret_cast<uint64_t>(mirrored_upgraded_resource_view);
            }
            else // Destroy it if it was accidentally created at the same time by another thread
            {
               out_rv = original_resource_views_to_mirrored_upgraded_resource_views[in_rv];
               lock_device_write.unlock();
               mirrored_upgraded_resource_view->Release();
            }
            replaced = true;
         }
         else
         {
            ASSERT_ONCE_MSG(false, "Failed to create an indirect upgraded texture view (maybe some format mismatch)");
         }

         lock_device_read.lock();
      }
   }

   // Let the upgrades happen above, but ignore the override
   if (ignore_indirect_upgraded_textures)
   {
      if (out_rv)
         out_rv = in_rv;
      return false;
   }

   return replaced;
}

void ResourceUpgradeManager::UnlinkMirror(uint64_t mirror_handle)
{
   // Find and erase the original->mirror entry for this mirror.
   for (auto it = original_resources_to_mirrored_upgraded_resources.begin(); it != original_resources_to_mirrored_upgraded_resources.end(); ++it)
   {
      if (it->second.mirror_handle != mirror_handle)
         continue;

      // Invalidate stale view mappings for this mirror while the lock is held.
      std::vector<uint64_t> unlinked_mirror_views;
      if (auto mirror_views_it = mirror_views_by_mirror_resource.find(mirror_handle); mirror_views_it != mirror_views_by_mirror_resource.end())
      {
         const auto& mirror_views = mirror_views_it->second;
         for (auto view_map_it = original_resource_views_to_mirrored_upgraded_resource_views.begin(); view_map_it != original_resource_views_to_mirrored_upgraded_resource_views.end();)
         {
            if (mirror_views.contains(view_map_it->second))
            {
               unlinked_mirror_views.push_back(view_map_it->second);
               mirror_views_to_mirror_resources.erase(view_map_it->second);
               view_map_it = original_resource_views_to_mirrored_upgraded_resource_views.erase(view_map_it);
            }
            else
            {
               ++view_map_it;
            }
         }
         mirror_views_by_mirror_resource.erase(mirror_views_it);
      }

      original_resources_to_mirrored_upgraded_resources.erase(it);

      // Defer freeing to present: the mirror may still be in flight in hooks or bound on recorded lists.
      for (const uint64_t unlinked_mirror_view : unlinked_mirror_views)
      {
         pending_mirror_view_destructions.push_back({ unlinked_mirror_view });
      }
      pending_mirror_resource_destructions.push_back({ mirror_handle });
      break;
   }
}

// Lock-free: caller/core.hpp holds the single device mutex.
void ResourceUpgradeManager::InvalidateAllIndirectUpgradedResources()
{
   std::vector<uint64_t> invalidated_mirrors;
   invalidated_mirrors.reserve(original_resources_to_mirrored_upgraded_resources.size());
   for (const auto& [orig, mirror] : original_resources_to_mirrored_upgraded_resources)
   {
      invalidated_mirrors.push_back(mirror.mirror_handle);
   }
   for (const uint64_t mirror_handle : invalidated_mirrors)
   {
      UnlinkMirror(mirror_handle);
   }
}

void ResourceUpgradeManager::FlushPendingDestructions(std::shared_mutex& device_mutex)
{
   // The caller must NOT hold "device_mutex": the pending lists are taken under it, but the destructions happen outside of it,
   // as destroying resources can call back into our destruction callbacks (which lock it).
   std::vector<uint64_t> pending_views;
   std::vector<uint64_t> pending_resources;
   {
      std::unique_lock lock(device_mutex);
      pending_views = std::move(pending_mirror_view_destructions);
      pending_resources = std::move(pending_mirror_resource_destructions);
   }
   Luma::Hooks::Scope::Guard scope;
   for (const uint64_t view : pending_views)
   {
      reinterpret_cast<ID3D11View*>(view)->Release();
   }
   for (const uint64_t resource : pending_resources)
   {
      reinterpret_cast<ID3D11Resource*>(resource)->Release();
   }
}

// Lock-free: caller/core.hpp holds the single device mutex.
void ResourceUpgradeManager::OnResourceDestroyed(uint64_t resource_handle)
{
   auto original_resource_to_mirrored_upgraded_resource = original_resources_to_mirrored_upgraded_resources.find(resource_handle);
   if (original_resource_to_mirrored_upgraded_resource != original_resources_to_mirrored_upgraded_resources.end())
   {
      const auto mirrored_upgraded_resource = original_resource_to_mirrored_upgraded_resource->second.mirror_handle;
      original_resources_to_mirrored_upgraded_resources.erase(original_resource_to_mirrored_upgraded_resource);

      // Invalidate stale view mappings for this mirror while the lock is held.
      std::vector<uint64_t> unlinked_mirror_views;
      if (auto mirror_views_it = mirror_views_by_mirror_resource.find(mirrored_upgraded_resource); mirror_views_it != mirror_views_by_mirror_resource.end())
      {
         const auto& mirror_views = mirror_views_it->second;
         for (auto view_map_it = original_resource_views_to_mirrored_upgraded_resource_views.begin(); view_map_it != original_resource_views_to_mirrored_upgraded_resource_views.end();)
         {
            if (mirror_views.contains(view_map_it->second))
            {
               unlinked_mirror_views.push_back(view_map_it->second);
               mirror_views_to_mirror_resources.erase(view_map_it->second);
               view_map_it = original_resource_views_to_mirrored_upgraded_resource_views.erase(view_map_it);
            }
            else
            {
               ++view_map_it;
            }
         }
         mirror_views_by_mirror_resource.erase(mirror_views_it);
      }
      
      constexpr bool delayed_destruction = true;
      // Defer freeing to present: the mirror may still be in flight in hooks or bound on recorded lists.
      if (delayed_destruction)
      {
         for (const uint64_t unlinked_mirror_view : unlinked_mirror_views)
         {
            pending_mirror_view_destructions.push_back({unlinked_mirror_view});
         }
         pending_mirror_resource_destructions.push_back({mirrored_upgraded_resource});
      }
#if 0 // TODO: add back and try or delete branches
      else
      {
         lock.unlock();
   
         for (const uint64_t unlinked_mirror_view : unlinked_mirror_views)
         {
            device->destroy_resource_view({ unlinked_mirror_view });
         }
         device->destroy_resource({ mirrored_upgraded_resource });
      }
#endif
   }
   upgraded_resources.erase(resource_handle);
#if DEVELOPMENT
   original_upgraded_resources_formats.erase(resource_handle);
#endif
}

// Lock-free: caller/core.hpp holds the single device mutex.
void ResourceUpgradeManager::OnResourceViewDestroyed(uint64_t view_handle)
{
#if DEVELOPMENT
   original_upgraded_resource_views_formats.erase(view_handle);
#endif
   original_views_to_original_resources.erase(view_handle);

   auto original_resource_view_to_mirrored_upgraded_resource_view = original_resource_views_to_mirrored_upgraded_resource_views.find(view_handle);
   if (original_resource_view_to_mirrored_upgraded_resource_view != original_resource_views_to_mirrored_upgraded_resource_views.end())
   {
      const auto mirrored_upgraded_resource_view = original_resource_view_to_mirrored_upgraded_resource_view->second;
      original_resource_views_to_mirrored_upgraded_resource_views.erase(original_resource_view_to_mirrored_upgraded_resource_view);
      // No device call (get_resource_from_view) under the luma lock: this callback runs inside the D3D11
      // runtime's final Release (destruction notifier) on an arbitrary thread, so taking the luma lock here and
      // then calling back into the runtime inverts the lock order vs the draw path (which holds the shared luma
      // lock while making device calls) and can deadlock. The mirror resource handle is cached at insert time.
      uint64_t mirror_resource = 0;
      if (auto mirror_res_it = mirror_views_to_mirror_resources.find(mirrored_upgraded_resource_view); mirror_res_it != mirror_views_to_mirror_resources.end())
      {
         mirror_resource = mirror_res_it->second;
         mirror_views_to_mirror_resources.erase(mirror_res_it);
      }
      if (auto mirror_views_it = mirror_views_by_mirror_resource.find(mirror_resource); mirror_views_it != mirror_views_by_mirror_resource.end())
      {
         mirror_views_it->second.erase(mirrored_upgraded_resource_view);
         if (mirror_views_it->second.empty())
            mirror_views_by_mirror_resource.erase(mirror_views_it);
      }
      // Defer freeing to present: the mirror view may still be in flight.
      pending_mirror_view_destructions.push_back({ mirrored_upgraded_resource_view });
   }
}

void ResourceUpgradeManager::ReUpgradeResource(uint64_t original_or_upgraded_resource_handle, UINT additional_bind_flags)
{
#if 0 // TODO: finish up! Split into a destroy and re-create func etc.
   const uint64_t old_upgraded_resource_handle = original_or_upgraded_resource_handle;
   uint64_t original_resource_handle = original_or_upgraded_resource_handle;

   ID3D11Device* native_device = device_data.native_device;

   std::unique_lock lock_device_write(device_data.mutex);

   // Inverted map search to find the original resource from the upgraded one (if there was one)
   for (const auto& original_resource_to_mirrored_upgraded_resource : original_resources_to_mirrored_upgraded_resources)
   {
      if (original_resource_to_mirrored_upgraded_resource.second.mirror_handle == old_upgraded_resource_handle)
      {
         original_resource_handle = original_resource_to_mirrored_upgraded_resource.first;

         auto original_resource_to_mirrored_upgraded_resource = original_resources_to_mirrored_upgraded_resources.find(original_resource_handle);
         if (original_resource_to_mirrored_upgraded_resource != original_resources_to_mirrored_upgraded_resources.end())
         {
            const auto mirrored_upgraded_resource = original_resource_to_mirrored_upgraded_resource->second.mirror_handle;
            original_resources_to_mirrored_upgraded_resources.erase(original_resource_to_mirrored_upgraded_resource);

            // Invalidate stale view mappings for this mirror while the lock is held.
            std::vector<uint64_t> unlinked_mirror_views;
            if (auto mirror_views_it = mirror_views_by_mirror_resource.find(mirrored_upgraded_resource); mirror_views_it != mirror_views_by_mirror_resource.end())
            {
               const auto& mirror_views = mirror_views_it->second;
               for (auto view_map_it = original_resource_views_to_mirrored_upgraded_resource_views.begin(); view_map_it != original_resource_views_to_mirrored_upgraded_resource_views.end();)
               {
                  if (mirror_views.contains(view_map_it->second))
                  {
                     unlinked_mirror_views.push_back(view_map_it->second);
                     mirror_views_to_mirror_resources.erase(view_map_it->second);
                     view_map_it = original_resource_views_to_mirrored_upgraded_resource_views.erase(view_map_it);
                  }
                  else
                  {
                     ++view_map_it;
                  }
               }
               mirror_views_by_mirror_resource.erase(mirror_views_it);
            }

            constexpr bool delayed_destruction = true;
            // Defer freeing to present: the mirror may still be in flight in hooks or bound on recorded lists.
            if (delayed_destruction)
            {
               for (const uint64_t unlinked_mirror_view : unlinked_mirror_views)
               {
                  pending_mirror_view_destructions.push_back({ unlinked_mirror_view });
               }
               pending_mirror_resource_destructions.push_back({ mirrored_upgraded_resource });
            }
            else
            {
               lock_device_write.unlock();
               for (const uint64_t unlinked_mirror_view : unlinked_mirror_views)
               {
                  reinterpret_cast<ID3D11View*>(unlinked_mirror_view)->Release();
               }
               reinterpret_cast<ID3D11Resource*>(mirrored_upgraded_resource)->Release();
               lock_device_write.lock();
            }
         }

         break;
      }
   }

   lock_device_write.unlock();
   std::shared_lock lock_device_read(device_data.mutex);

   uint64_t new_upgraded_resource;
   if (::FindOrCreateIndirectUpgradedResource(native_device, 0, original_resource_handle, new_upgraded_resource, device_data, true, lock_device_read, false, false, false, D3D11_BIND_UNORDERED_ACCESS))
   {
      // Carry the current content over, just in case it was used for multi frame motion blur or something (very unlikely)
      native_device_context->CopyResource((ID3D11Resource*)new_upgraded_resource, (ID3D11Resource*)old_upgraded_resource_handle);
   }
#endif
}
