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
	std::atomic<bool> found_jitter = true;
	com_ptr<ID3D11PixelShader> motion_vectors_ps;
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
	ShaderHashesList shader_hashes_jitter;
	float2 previous_projection_jitters = { 0, 0 };
	float2 projection_jitters = { 0, 0 };
	const uint32_t shader_hash_mvec_pixel = std::stoul("FFFFFFF3", nullptr, 16);
	const uint32_t shader_hash_jitter_pixel = std::stoul("FFFFFFF5", nullptr, 16);
#if DEVELOPMENT
	std::vector<std::string> cb_per_view_globals_last_drawn_shader; // Not exactly thread safe but it's fine...
	std::vector<CBPerViewGlobal> cb_per_view_globals;
	std::vector<CBPerViewGlobal> cb_per_view_globals_previous;
	float cb1_debug_buffer[140 * 4] = { 0 };

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


	void OnInit(bool async) override
	{
		shader_hashes_TAA.pixel_shaders.emplace(std::stoul("4729683B", nullptr, 16));
		//shader_hashes_jitter.compute_shaders.emplace(std::stoul("DEAD68E5", nullptr, 16));
		shader_hashes_jitter.pixel_shaders.emplace(std::stoul("BE8F73B4", nullptr, 16));
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

		if (!game_device_data.has_drawn_upscaling && device_data.dlss_sr && !device_data.dlss_sr_suppressed && is_custom_pass && original_shader_hashes.Contains(shader_hashes_TAA))
		{
			game_device_data.has_drawn_upscaling = true;
			// 1 depth
			// 2 current color source ()
			// 3 previous color source (previous frame)
			// 4 motion vectors 
			com_ptr<ID3D11ShaderResourceView> ps_shader_resources[11];
			native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));

			com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
			com_ptr<ID3D11DepthStencilView> depth_stencil_view;
			native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
			const bool dlss_inputs_valid = ps_shader_resources[1].get() != nullptr && ps_shader_resources[2].get() != nullptr && ps_shader_resources[4].get() != nullptr && render_target_views[0] != nullptr;
			if (dlss_inputs_valid)
			{

				// Create a new 2d texture, bind to render target, create new render target view add newly created target view to list, set render targets, call draw function
				// Use that 2d texture later for the motion vectors
				com_ptr<ID3D11Resource> output_colorTemp;
				render_target_views[0]->GetResource(&output_colorTemp);
				com_ptr<ID3D11Texture2D> output_color;
				HRESULT hr = output_colorTemp->QueryInterface(&output_color);
				ASSERT_ONCE(SUCCEEDED(hr));
				D3D11_TEXTURE2D_DESC output_texture_desc;
				output_color->GetDesc(&output_texture_desc);

				//ASSERT_ONCE(std::lrintf(device_data.output_resolution.x) == output_texture_desc.Width && std::lrintf(device_data.output_resolution.y) == output_texture_desc.Height);
				std::array<uint32_t, 2> dlss_render_resolution = FindClosestIntegerResolutionForAspectRatio((double)output_texture_desc.Width * (double)device_data.dlss_render_resolution_scale, (double)output_texture_desc.Height * (double)device_data.dlss_render_resolution_scale, (double)output_texture_desc.Width / (double)output_texture_desc.Height);
				bool dlss_hdr = true;

				NGX::DLSS::UpdateSettings(device_data.dlss_sr_handle, native_device_context, output_texture_desc.Width, output_texture_desc.Height, dlss_render_resolution[0], dlss_render_resolution[1], dlss_hdr, false); //TODO: figure out dsr later
				
				bool skip_dlss = output_texture_desc.Width < 32 || output_texture_desc.Height < 32; // DLSS doesn't support output below 32x32
				bool dlss_output_changed = false;
				constexpr bool dlss_use_native_uav = true;
				bool dlss_output_supports_uav = dlss_use_native_uav && (output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
				if (!dlss_output_supports_uav)
				{

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
					// TODO: optimize this for the "DLSS_RELATIVE_PRE_EXPOSURE" false case! Avoid re-creating the texture every frame the exposure changes and instead make it                                                                                    and re-write it from the CPU? Or simply make our exposure calculation shader write to a texture directly
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
						object_velocity_texture_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
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
						if (!game_device_data.dlss_motion_vectors.get() || dlss_motion_vectors_changed || true)
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

						//SetLumaConstantBuffers(native_device_context, device_data, stages, LumaConstantBufferType::LumaSettings);
						//SetLumaConstantBuffers(native_device_context, device_data, stages, LumaConstantBufferType::LumaData);
						
						ID3D11RenderTargetView* const dlss_motion_vectors_rtv_const = game_device_data.dlss_motion_vectors_rtv.get();
						// Set up for motion vector shader
						native_device_context->OMSetRenderTargets(1, &dlss_motion_vectors_rtv_const, depth_stencil_view.get());
						ID3D11PixelShader* prev_shader_px = nullptr;
						//ID3D11VertexShader* prev_shader_vx = nullptr;

						native_device_context->PSGetShader(&prev_shader_px, nullptr, nullptr);
						//native_device_context->VSGetShader(&prev_shader_vx, nullptr, nullptr);
						native_device_context->PSSetShader(game_device_data.motion_vectors_ps.get(), nullptr, 0);
						// native_device_context->PSSetShader(game_device_data.motion_vectors_ps.get(), nullptr, 0);
						native_device_context->DrawIndexed(3, 6, 0);
						native_device_context->PSSetShader(prev_shader_px, nullptr, 0); // Restore previous shader
						// native_device_context->VSSetShader(prev_shader_vx, nullptr, 0); // Restore previous shader
							
						// Restore previous state
					}
					// Reset the render target, just to make sure there's no conflicts with the same texture being used as RWTexture UAV or Shader Resources
					native_device_context->OMSetRenderTargets(0, nullptr, nullptr);

////					// --- Extract cb1[123].xy (jitter) from the constant buffer bound to PS slot 1 (b1) ---
					ID3D11Buffer* cb1_buffer = nullptr;
					native_device_context->PSGetConstantBuffers(1, 1, &cb1_buffer); // slot 1 = b1

					if (cb1_buffer)
					{
						// Create a staging buffer for CPU read if needed
						D3D11_BUFFER_DESC cb1_desc = {};
						cb1_buffer->GetDesc(&cb1_desc);

						ID3D11Buffer* staging_cb1 = cb1_buffer;
						com_ptr<ID3D11Buffer> staging_cb1_buf;
						if (cb1_desc.Usage != D3D11_USAGE_STAGING || !(cb1_desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ))
						{
							cb1_desc.Usage = D3D11_USAGE_STAGING;
							cb1_desc.BindFlags = 0;
							cb1_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
							cb1_desc.MiscFlags = 0;
							cb1_desc.StructureByteStride = 0;
							HRESULT hr_staging = native_device->CreateBuffer(&cb1_desc, nullptr, &staging_cb1_buf);
							if (SUCCEEDED(hr_staging))
							{
								native_device_context->CopyResource(staging_cb1_buf.get(), cb1_buffer);
								staging_cb1 = staging_cb1_buf.get();
							}
							else
							{
								cb1_buffer->Release();
								goto cb1_jitter_end;
							}
						}

						D3D11_MAPPED_SUBRESOURCE mapped_cb1 = {};
						if (SUCCEEDED(native_device_context->Map(staging_cb1, 0, D3D11_MAP_READ, 0, &mapped_cb1)))
						{
							// cb1 is float4[140], so each element is 16 bytes
							const float* cb1_floats = reinterpret_cast<const float*>(mapped_cb1.pData);
#if DEVELOPMENT
						if (mapped_cb1.pData)
						{
							std::memcpy(cb1_debug_buffer, cb1_floats, sizeof(cb1_debug_buffer));
						}
#endif
							size_t base = 118 * 4;
							float jitter_x = cb1_floats[base + 0];
							float jitter_y = cb1_floats[base + 1];

							// Use jitter_x and jitter_y as needed, e.g.:
							if (jitter_x != 0 || jitter_y != 0)
							{
								projection_jitters.x = jitter_x;
								projection_jitters.y = jitter_y;
							}
							

							native_device_context->Unmap(staging_cb1, 0);
						}

						if (staging_cb1 != cb1_buffer)
							staging_cb1->Release();
						cb1_buffer->Release();
					}
				cb1_jitter_end:

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
					if (NGX::DLSS::Draw(device_data.dlss_sr_handle, native_device_context, device_data.dlss_output_color.get(), source_color.get(), game_device_data.dlss_motion_vectors.get(), depth_buffer.get(), device_data.dlss_exposure.get(), dlss_pre_exposure, projection_jitters.x - previous_projection_jitters.x, projection_jitters.y - previous_projection_jitters.y, reset_dlss, render_width_dlss, render_height_dlss))
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
						//ASSERT_ONCE(false);
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
		game_device_data.found_jitter = false;
		device_data.previous_render_resolution = device_data.render_resolution;
		previous_projection_jitters = projection_jitters;

	}

	void PrintImGuiAbout() override
	{
		// Remember to credit Luma developers, the game mod creators, and all third party code that is used (plus, optionally, testers too)
		ImGui::Text("FF7 Remake Luma mod - about and credits section", "");
	}

	void CreateShaderObjects(DeviceData& device_data, const std::optional<std::unordered_set<uint32_t>>& shader_hashes_filter) override
	{
		auto& game_device_data = GetGameDeviceData(device_data);
		CreateShaderObject(device_data.native_device, shader_hash_mvec_pixel, game_device_data.motion_vectors_ps, shader_hashes_filter);


	}
	void DrawImGuiSettings(DeviceData& device_data) override
	{
		#if DEVELOPMENT
		static bool show_cb1_popup = false;
		ImGui::NewLine();
		if (ImGui::Button("Show cb1 buffer (float4[140])"))
			show_cb1_popup = true;

		if (show_cb1_popup)
		{
			ImGui::OpenPopup("cb1_buffer_popup");
		}
	
		// Make the popup window much larger
		if (ImGui::BeginPopupModal("cb1_buffer_popup", &show_cb1_popup, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("cb1 buffer (float4[140]):");
			// Increase the child window size for easier viewing
			ImGui::BeginChild("cb1_buffer_scroll", ImVec2(1200, 800), true, ImGuiWindowFlags_HorizontalScrollbar);
			for (int i = 0; i < 140; ++i)
			{
				ImGui::Text("cb1[%3d] = { %+0.6f, %+0.6f, %+0.6f, %+0.6f }", i,
					cb1_debug_buffer[i * 4 + 0],
					cb1_debug_buffer[i * 4 + 1],
					cb1_debug_buffer[i * 4 + 2],
					cb1_debug_buffer[i * 4 + 3]);
			}
			ImGui::EndChild();
			if (ImGui::Button("Close"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
#endif
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