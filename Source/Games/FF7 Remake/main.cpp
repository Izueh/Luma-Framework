#define GAME_FF7_REMAKE 1

#define ENABLE_NGX 1

#define UPGRADE_SWAPCHAIN_TYPE 1
#define UPGRADE_RESOURCES_8UNORM 1
#define UPGRADE_RESOURCES_11FLOAT 1
#define UPGRADE_SAMPLERS 1

// Not used by Dishonored 2?
#define ENABLE_SHADER_CLASS_INSTANCES 1

#include "..\..\Core\core.hpp"

// Hack: we need to include this cpp file here because it's part of the core library but we actually don't include it as a library, due to limitations (see the game template for more)
#include "..\..\Core\dlss\DLSS.cpp"

#include "includes/cbuffers.h"

struct GameDeviceDataFF7Remake final : public GameDeviceData
{
#if ENABLE_NGX
    // DLSS SR
    com_ptr<ID3D11Texture2D> dlss_motion_vectors;
    com_ptr<ID3D11RenderTargetView> dlss_motion_vectors_rtv;
#endif // ENABLE_NGX
    std::atomic<bool> found_per_view_globals = false;
    std::atomic<bool> has_drawn_upscaling = false;
    com_ptr<ID3D11PixelShader> taa_compute_shader;
};

namespace
{
    CBPerViewGlobal cb_per_view_global = { };
    CBPerViewGlobal cb_per_view_global_previous = cb_per_view_global;
    Matrix44F projection_matrix;
    Matrix44F nearest_projection_matrix; // For first person weapons (view model)
    Matrix44F previous_projection_matrix;
    Matrix44F previous_nearest_projection_matrix;
    ShaderHashesList shader_hashes_TAA;
    float2 previous_projection_jitters = { 0, 0 };
    float2 projection_jitters = { 0, 0 };
    const uint32_t shader_hash_taa_compute = std::stoul("FFFFFFF3", nullptr, 16);
#if DEVELOPMENT
    std::vector<std::string> cb_per_view_globals_last_drawn_shader; // Not exactly thread safe but it's fine...
    std::vector<CBPerViewGlobal> cb_per_view_globals;
    std::vector<CBPerViewGlobal> cb_per_view_globals_previous;
#endif
}

class FF7Remake final : public Game
{
    static GameDeviceDataFF7Remake& GetGameDeviceData(DeviceData& device_data)
    {
        return *static_cast<GameDeviceDataFF7Remake*>(device_data.game);
    }

public:
    // This needs to be overridden with your own "GameDeviceData" sub-class (destruction is automatically handled)

    void OnLoad(std::filesystem::path& file_path, bool failed = false) override 
    {
        reshade::register_event<reshade::addon_event::map_buffer_region>(FF7Remake::OnMapBufferRegion);
        reshade::register_event<reshade::addon_event::unmap_buffer_region>(FF7Remake::OnUnmapBufferRegion);
    }

    void OnInit(bool async) override
    {
        shader_hashes_TAA.pixel_shaders.emplace(std::stoul("4729683B", nullptr, 16));
    }

    void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
    {
        device_data.game = new GameDeviceDataFF7Remake;
    }

    bool OnDrawCustom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);
        const bool had_drawn_main_post_processing = device_data.has_drawn_main_post_processing;
        const bool had_drawn_upscaling = game_device_data.has_drawn_upscaling;
        if (!game_device_data.has_drawn_upscaling && original_shader_hashes.Contains(shader_hashes_TAA))
        {
            game_device_data.has_drawn_upscaling = true;

            com_ptr<ID3D11ShaderResourceView> ps_shader_resources[5];
            native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));

            com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            com_ptr<ID3D11DepthStencilView> depth_stencil_view;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
            com_ptr<ID3D11Resource> output_colorTemp;
            render_target_views[0]->GetResource(&output_colorTemp);
            com_ptr<ID3D11Texture2D> output_color;
            HRESULT hr = output_colorTemp->QueryInterface(&output_color);
            ASSERT_ONCE(SUCCEEDED(hr));
            D3D11_TEXTURE2D_DESC output_texture_desc;
            output_color->GetDesc(&output_texture_desc);
            std::array<uint32_t, 2> dlss_render_resolution = FindClosestIntegerResolutionForAspectRatio((double)output_texture_desc.Width * (double)device_data.dlss_render_resolution_scale, (double)output_texture_desc.Height * (double)device_data.dlss_render_resolution_scale, (double)output_texture_desc.Width / (double)output_texture_desc.Height);
            NGX::DLSS::UpdateSettings(device_data.dlss_sr_handle, native_device_context, output_texture_desc.Width, output_texture_desc.Height, dlss_render_resolution[0], dlss_render_resolution[1], 0, 0);
            bool skip_dlss = output_texture_desc.Width < 32 || output_texture_desc.Height < 32; // DLSS doesn't support output below 32x32
            bool dlss_output_changed = false;
            constexpr bool dlss_use_native_uav = true;
            bool dlss_output_supports_uav = dlss_use_native_uav && (output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
            if (!dlss_output_supports_uav)
            {
#if ENABLE_NATIVE_PLUGIN
                ASSERT_ONCE(!dlss_use_native_uav); // Should never happen anymore ("FORCE_DLSS_SMAA_UAV" is true)
#endif

                output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

                if (device_data.dlss_output_color.get())
                {
                    D3D11_TEXTURE2D_DESC dlss_output_texture_desc;
                    device_data.dlss_output_color->GetDesc(&dlss_output_texture_desc);
                    dlss_output_changed = dlss_output_texture_desc.Width != output_texture_desc.Width || dlss_output_texture_desc.Height != output_texture_desc.Height || dlss_output_texture_desc.Format != output_texture_desc.Format;
                }
                if (!device_data.dlss_output_color.get() || dlss_output_changed)
                {
                    device_data.dlss_output_color = nullptr; // Make sure we discard the previous one
                    hr = native_device->CreateTexture2D(&output_texture_desc, nullptr, &device_data.dlss_output_color);
                    ASSERT_ONCE(SUCCEEDED(hr));
                }
                if (!device_data.dlss_output_color.get())
                {
                    skip_dlss = true;
                }
            }
            else
            {
                device_data.dlss_output_color = output_color;
            }

            if (!skip_dlss)
            {
                com_ptr<ID3D11Resource> source_color;
                ps_shader_resources[2]->GetResource(&source_color);
                com_ptr<ID3D11Resource> depth_buffer;
                ps_shader_resources[1]->GetResource(&depth_buffer);
                com_ptr<ID3D11Resource> object_velocity_buffer_temp;
                ps_shader_resources[4]->GetResource(&object_velocity_buffer_temp);
                com_ptr<ID3D11Texture2D> object_velocity_buffer;
                hr = object_velocity_buffer_temp->QueryInterface(&object_velocity_buffer);
                ASSERT_ONCE(SUCCEEDED(hr));

                // Generate "fake" exposure texture
                bool exposure_changed = false;
                float dlss_scene_exposure = device_data.dlss_scene_exposure;

                exposure_changed = dlss_scene_exposure != device_data.dlss_exposure_texture_value;
                device_data.dlss_exposure_texture_value = dlss_scene_exposure;
                // TODO: optimize this for the "DLSS_RELATIVE_PRE_EXPOSURE" false case! Avoid re-creating the texture every frame the exposure changes and instead make it dynamic and re-write it from the CPU? Or simply make our exposure calculation shader write to a texture directly
                // (though in that case it wouldn't have the same delay as the CPU side pre-exposure buffer readback)
                if (!device_data.dlss_exposure.get() || exposure_changed)
                {
                    D3D11_TEXTURE2D_DESC exposure_texture_desc; // DLSS fails if we pass in a 1D texture so we have to make a 2D one
                    exposure_texture_desc.Width = 1;
                    exposure_texture_desc.Height = 1;
                    exposure_texture_desc.MipLevels = 1;
                    exposure_texture_desc.ArraySize = 1;
                    exposure_texture_desc.Format = DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT; // FP32 just so it's easier to initialize data for it
                    exposure_texture_desc.SampleDesc.Count = 1;
                    exposure_texture_desc.SampleDesc.Quality = 0;
                    exposure_texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
                    exposure_texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    exposure_texture_desc.CPUAccessFlags = 0;
                    exposure_texture_desc.MiscFlags = 0;

                    // It's best to force an exposure of 1 given that DLSS runs after the auto exposure is applied (in tonemapping).
                    // Theoretically knowing the average exposure of the frame would still be beneficial to it (somehow) so maybe we could simply let the auto exposure in,
                    D3D11_SUBRESOURCE_DATA exposure_texture_data;
                    exposure_texture_data.pSysMem = &dlss_scene_exposure; // This needs to be "static" data in case the texture initialization was somehow delayed and read the data after the stack destroyed it
                    exposure_texture_data.SysMemPitch = 32;
                    exposure_texture_data.SysMemSlicePitch = 32;

                    device_data.dlss_exposure = nullptr; // Make sure we discard the previous one
                    hr = native_device->CreateTexture2D(&exposure_texture_desc, &exposure_texture_data, &device_data.dlss_exposure);
                    assert(SUCCEEDED(hr));
                }
                {
                    D3D11_TEXTURE2D_DESC object_velocity_texture_desc;
                    object_velocity_buffer->GetDesc(&object_velocity_texture_desc);
                    ASSERT_ONCE((object_velocity_texture_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == D3D11_BIND_RENDER_TARGET);
#if 1 // Use the higher quality for MVs, the game's one were R16G16F. This has a ~1% cost on performance but helps with reducing shimmering on fine lines (stright lines looking segmented, like Bart's hair or Shark's teeth) when the camera is moving in a linear fashion. Generating MVs from the depth is still a limited technique so it can't be perfect.
                    object_velocity_texture_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
#else
                    object_velocity_texture_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
#endif

                    // Update the "dlss_output_changed" flag if we hadn't already (we wouldn't have had a previous copy to compare against above)
                    bool dlss_motion_vectors_changed = dlss_output_changed;
                    if (dlss_output_supports_uav)
                    {
                        if (game_device_data.dlss_motion_vectors.get())
                        {
                            D3D11_TEXTURE2D_DESC dlss_motion_vectors_desc;
                            game_device_data.dlss_motion_vectors->GetDesc(&dlss_motion_vectors_desc);
                            dlss_output_changed = dlss_motion_vectors_desc.Width != output_texture_desc.Width || dlss_motion_vectors_desc.Height != output_texture_desc.Height;
                            dlss_motion_vectors_changed = dlss_output_changed;
                        }
                    }
                    // We assume the conditions of this texture (and its render target view) changing are the same as "dlss_output_changed"
                    if (!game_device_data.dlss_motion_vectors.get() || dlss_motion_vectors_changed)
                    {
                        game_device_data.dlss_motion_vectors = nullptr; // Make sure we discard the previous one
                        hr = native_device->CreateTexture2D(&object_velocity_texture_desc, nullptr, &game_device_data.dlss_motion_vectors);
                        ASSERT_ONCE(SUCCEEDED(hr));

                        D3D11_RENDER_TARGET_VIEW_DESC object_velocity_render_target_view_desc;
                        render_target_views[0]->GetDesc(&object_velocity_render_target_view_desc);
                        object_velocity_render_target_view_desc.Format = object_velocity_texture_desc.Format;
                        object_velocity_render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2D;
                        object_velocity_render_target_view_desc.Texture2D.MipSlice = 0;

                        game_device_data.dlss_motion_vectors_rtv = nullptr; // Make sure we discard the previous one
                        native_device->CreateRenderTargetView(game_device_data.dlss_motion_vectors.get(), &object_velocity_render_target_view_desc, &game_device_data.dlss_motion_vectors_rtv);
                    }

                    SetLumaConstantBuffers(native_device_context, device_data, stages, LumaConstantBufferType::LumaSettings);
                    SetLumaConstantBuffers(native_device_context, device_data, stages, LumaConstantBufferType::LumaData);

                    ID3D11RenderTargetView* const dlss_motion_vectors_rtv_const = game_device_data.dlss_motion_vectors_rtv.get();
                    native_device_context->OMSetRenderTargets(1, &dlss_motion_vectors_rtv_const, depth_stencil_view.get());

                    // This should be the same draw type that the shader would have used if we went through with it (SMAA 2TX/TAA).
                    native_device_context->Draw(3, 0);
                }
                // Reset the render target, just to make sure there's no conflicts with the same texture being used as RWTexture UAV or Shader Resources
                native_device_context->OMSetRenderTargets(0, nullptr, nullptr);

                // Reset DLSS history if we did not draw motion blur (and we previously did). Based on CryEngine source code, mb is skipped on the first frame after scene cuts, so we want to re-use that information (this works even if MB was disabled).
                // Reset DLSS history if for one frame we had stopped tonemapping. This might include some scene cuts, but also triggers when entering full screen UI menus or videos and then leaving them (it shouldn't be a problem).
                // Reset DLSS history if the output resolution or format changed (just an extra safety mechanism, it might not actually be needed).
                bool reset_dlss = device_data.force_reset_dlss_sr || dlss_output_changed || !device_data.has_drawn_main_post_processing_previous;
                device_data.force_reset_dlss_sr = false;

                uint32_t render_width_dlss = std::lrintf(device_data.render_resolution.x);
                uint32_t render_height_dlss = std::lrintf(device_data.render_resolution.y);

                // These configurations store the image already multiplied by paper white from the beginning of tonemapping, including at the time DLSS runs.
                // The other configurations run DLSS in "SDR" Gamma Space so we couldn't safely change the exposure.
                const bool dlss_use_paper_white_pre_exposure = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) >= 1;

                float dlss_pre_exposure = 0.f; // 0 means it's ignored
                if (dlss_use_paper_white_pre_exposure)
                {
#if 1 // Alternative that considers a value of 1 in the DLSS color textures to match the SDR output nits range (whatever that is)
                    dlss_pre_exposure = cb_luma_frame_settings.ScenePaperWhite / default_paper_white;
#else // Alternative that considers a value of 1 in the DLSS color textures to match 203 nits
                    dlss_pre_exposure = cb_luma_frame_settings.ScenePaperWhite / srgb_white_level;
#endif
                    dlss_pre_exposure *= device_data.dlss_scene_pre_exposure;
                }

                // There doesn't seem to be a need to restore the DX state to whatever we had before (e.g. render targets, cbuffers, samplers, UAVs, texture shader resources, viewport, scissor rect, ...), CryEngine always sets everything it needs again for every pass.
                // DLSS internally keeps its own frames history, we don't need to do that ourselves (by feeding in an output buffer that was the previous frame's output, though we do have that if needed, it should be in ps_shader_resources[1]).
                if (NGX::DLSS::Draw(device_data.dlss_sr_handle, native_device_context, device_data.dlss_output_color.get(), source_color.get(), game_device_data.dlss_motion_vectors.get(), depth_buffer.get(), device_data.dlss_exposure.get(), dlss_pre_exposure, projection_jitters.x, projection_jitters.y, reset_dlss, render_width_dlss, render_height_dlss))
                {
                    device_data.has_drawn_dlss_sr = true;
                }

                // Fully reset the state of the RTs given that CryEngine is very delicate with it and uses some push and pop technique (simply resetting caching and resetting the first RT seemed fine for DLSS in case optimization is needed).
                // The fact that it could changes cbuffers or texture resources bindings or viewport seems fines.
                ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(render_target_views[0]);
                native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, depth_stencil_view.get());

                if (device_data.has_drawn_dlss_sr)
                {
                    if (!dlss_output_supports_uav)
                    {
                        native_device_context->CopyResource(output_color.get(), device_data.dlss_output_color.get()); // DX11 doesn't need barriers
                    }
                    // In this case it's not our business to keep alive this "external" texture
                    else
                    {
                        device_data.dlss_output_color = nullptr;
                    }

                    return true; // "Cancel" the previously set draw call, DLSS has taken care of it
                }
                // DLSS Failed, suppress it for this frame and fall back on SMAA/TAA, hoping that anything before would have been rendered correctly for it already (otherwise it will start being correct in the next frame, given we suppress it (until manually toggled again, given that it'd likely keep failing))
                else
                {
                    ASSERT_ONCE(false);
                    cb_luma_frame_settings.DLSS = 0;
                    device_data.cb_luma_frame_settings_dirty = true;
                    device_data.dlss_sr_suppressed = true;
                    device_data.force_reset_dlss_sr = true; // We missed frames so it's good to do this, it might also help prevent further errors
                }

            }
            if (dlss_output_supports_uav)
            {
                device_data.dlss_output_color = nullptr;
            }
        }
        return false; // Don't cancel the original draw call
    }

    void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);
        device_data.taa_detected = false;
        if (cb_luma_frame_settings.DLSS)
        {
            cb_luma_frame_settings.DLSS = 0; // No need for "s_mutex_reshade" here, given that they are generally only also changed by the user manually changing the settings in ImGUI, which runs at the very end of the frame
            device_data.cb_luma_frame_settings_dirty = true;
        }
        device_data.dlss_sr_suppressed = false;
        device_data.dlss_scene_exposure = 1.f;
        device_data.dlss_scene_pre_exposure = 1.f;
        device_data.has_drawn_main_post_processing_previous = device_data.has_drawn_main_post_processing;
        device_data.has_drawn_main_post_processing = false;
        device_data.has_drawn_dlss_sr_imgui = device_data.has_drawn_dlss_sr;
        device_data.has_drawn_dlss_sr = false;
        game_device_data.found_per_view_globals = false;
        game_device_data.has_drawn_upscaling = false;
        device_data.previous_render_resolution = device_data.render_resolution;
        previous_projection_matrix = projection_matrix;
        previous_nearest_projection_matrix = nearest_projection_matrix;
        //previous_projection_jitters = projection_jitters;
        cb_per_view_global_previous = cb_per_view_global;
    }

    void PrintImGuiAbout() override
    {
        // Remember to credit Luma developers, the game mod creators, and all third party code that is used (plus, optionally, testers too)
        ImGui::Text("FF7 Remake Luma mod - about and credits section", "");
    }

    bool UpdateGlobalCB(const void* global_buffer_data_ptr, reshade::api::device* device) override
    {
        const CBPerViewGlobal& global_buffer_data = *((const CBPerViewGlobal*)global_buffer_data_ptr);


        float cb_output_resolution_x = std::round(0.5f / global_buffer_data.CV_ScreenSize.z); // Round here already as it would always meant to be integer
        float cb_output_resolution_y = std::round(0.5f / global_buffer_data.CV_ScreenSize.w);

#if DEVELOPMENT
        cb_per_view_globals.emplace_back(global_buffer_data);
        cb_per_view_globals_last_drawn_shader.emplace_back(last_drawn_shader); // The shader hash could we unspecified if we didn't replace the shader
#endif // DEVELOPMENT
        // Shadow maps and other things temporarily change the values in the global cbuffer,
        // like not use inverse depth (which affects the projection matrix, and thus many other matrices?),
        // use different render and output resolutions, etc etc.
        // We could also base our check on "CV_ProjRatio" (x and y) and "CV_FrustumPlaneEquation" and "CV_DecalZFightingRemedy" as these are also different for alternative views.
        // "CV_PrevViewProjMatr" is not a raw projection matrix when rendering shadow maps, so we can easily detect that.
        // Note: we can check if the matrix is identity to detect whether we are currently in a menu (the main menu?)
        bool is_custom_draw_version = !MatrixIsProjection(global_buffer_data.CV_PrevViewProjMatr.GetTransposed());
        if (is_custom_draw_version)
        {
            return false;
        }

        DeviceData& device_data = *device->get_private_data<DeviceData>();
        auto& game_device_data = GetGameDeviceData(device_data);

        bool output_resolution_matches = AlmostEqual(device_data.output_resolution.x, cb_output_resolution_x, 0.5f) && AlmostEqual(device_data.output_resolution.y, cb_output_resolution_y, 0.5f);

        cb_per_view_global = global_buffer_data;

        // Re-use the current cbuffer as the previous one if we didn't draw the scene in the frame before
        const CBPerViewGlobal& cb_per_view_global_actual_previous = device_data.has_drawn_main_post_processing_previous ? cb_per_view_global_previous : cb_per_view_global;

        auto current_projection_matrix = cb_per_view_global.CV_PrevViewProjMatr;
        auto current_nearest_projection_matrix = cb_per_view_global.CV_PrevViewProjNearestMatr;

        const auto projection_jitters_copy = projection_jitters;

        // These are called "m_vProjMatrixSubPixoffset" in CryEngine.
        // The matrix is transposed so we flip the matrix x and y indices.
        projection_jitters.x = current_projection_matrix(1, 2);
        projection_jitters.y = current_projection_matrix(0, 2);
        if (!device_data.has_drawn_main_post_processing_previous)
        {
            previous_projection_matrix = current_projection_matrix;
            previous_nearest_projection_matrix = current_nearest_projection_matrix;
        }
        return true;

#if DEVELOPMENT
        if (!custom_texture_mip_lod_bias_offset)
#endif
        {
            std::shared_lock shared_lock_samplers(s_mutex_samplers);

            const auto prev_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;
            if (device_data.dlss_sr && !device_data.dlss_sr_suppressed && device_data.taa_detected && device_data.cloned_pipeline_count != 0)
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
                    if (samplers_handle.second.contains(new_texture_mip_lod_bias_offset)) continue; // Skip "resolutions" that already got their custom samplers created
                    ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(samplers_handle.first);
                    D3D11_SAMPLER_DESC native_desc;
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
        game_device_data.found_per_view_globals = true;

    }

    static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
    {
        ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
        ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
        // No need to convert to native DX11 flags
        if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard || access == reshade::api::map_access::read_write)
        {
            D3D11_BUFFER_DESC buffer_desc;
            buffer->GetDesc(&buffer_desc);
            DeviceData& device_data = *device->get_private_data<DeviceData>();

            // There seems to only ever be one buffer type of this size, but it's not guaranteed (we might have found more, but it doesn't matter, they are discarded later)...
            // They seemingly all happen on the same thread.
            // Some how these are not marked as "D3D11_BIND_CONSTANT_BUFFER", probably because it copies them over to some other buffer later?
            if (buffer_desc.ByteWidth == CBPerViewGlobal_buffer_size)
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
        ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
        ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
        DeviceData& device_data = *device->get_private_data<DeviceData>();
        // We assume this buffer is always unmapped before destruction.
        bool is_global_cbuffer = device_data.cb_per_view_global_buffer != nullptr && device_data.cb_per_view_global_buffer == buffer;
        ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data || is_global_cbuffer);
        if (is_global_cbuffer && device_data.cb_per_view_global_buffer_map_data != nullptr)
        {
            // The whole buffer size is theoretically "CBPerViewGlobal_buffer_size" but we actually don't have the data for the excessive (padding) bytes,
            // they are never read by shaders on the GPU anyway.
            char global_buffer_data[CBPerViewGlobal_buffer_size];
            std::memcpy(&global_buffer_data[0], device_data.cb_per_view_global_buffer_map_data, CBPerViewGlobal_buffer_size);
            if (game->UpdateGlobalCB(&global_buffer_data[0], device))
            {
                // Write back the cbuffer data after we have fixed it up (we always do!)
                std::memcpy(device_data.cb_per_view_global_buffer_map_data, &cb_per_view_global, sizeof(CBPerViewGlobal));
#if DEVELOPMENT
                device_data.cb_per_view_global_buffers.emplace(buffer);
#endif // DEVELOPMENT
            }
            device_data.cb_per_view_global_buffer_map_data = nullptr;
            device_data.cb_per_view_global_buffer = nullptr; // No need to keep this cached
        }
    }

    void CreateShaderObjects(DeviceData& device_data, const std::optional<std::unordered_set<uint32_t>>& shader_hashes_filter) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);
        CreateShaderObject(device_data.native_device, shader_hash_taa_compute, game_device_data.taa_compute_shader, shader_hashes_filter);

    }

};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::GAME_NAME = PROJECT_NAME;
      Globals::DESCRIPTION = "FF7 Remake Luma mod";
      Globals::WEBSITE = "";
      Globals::VERSION = 1;

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      game = new FF7Remake();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}