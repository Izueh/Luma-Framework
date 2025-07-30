// Specify a define with the name of the game here (GAME_*).
// This is optional but can be used to hardcode custom (per game) behaviours in the core library,
// or other reasons. We can't automate it through the project define system (based on the project name) because
// we need these defines to be upper case and space free.
#define GAME_FF7_REMAKE 1

// Define all the global "core" defines before including its files:
// Enable this to be able to use DLSS's code
#define ENABLE_NGX 1
// Update samples to override the bias, based on the rendering resolution etc
#define UPGRADE_SAMPLERS 0
#define GEOMETRY_SHADER_SUPPORT 0
#define ALLOW_SHADERS_DUMPING 0

// Define the game specific cbuffer settings here (up to a fixed limited number), these are automatically compiled into shaders without editing them, and can be read in game specific shaders.
// You can call them however they want, and make them of any types.
// Avoid types that are not mutliples of 64 bits (with the exception of 32 bit types, which can be used).
#define LUMA_GAME_SETTING_01 float GameSetting01
#define LUMA_GAME_SETTING_02 uint GameSetting02

// Alternative implementation in case we used "Core" as static library
#if defined(RESHADE_EXTERNS) && 0
#define RESHADE_EXTERNS
extern "C" __declspec(dllexport) const char* NAME = Globals::MOD_NAME;
extern "C" __declspec(dllexport) const char* DESCRIPTION = Globals::DESCRIPTION;
extern "C" __declspec(dllexport) const char* WEBSITE = Globals::WEBSITE;
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module)
{
	Init(true);
	return true;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module)
{
	Uninit();
}
#endif

// Instead of "manually" including the "core" library, we simply include its main code file (which is a header).
// The library in itself is standalone, as in, it compiles fine and could directly be used as a template addon etc if built as dll but,
// there's a major limitation in how libraries dependencies work by nature, and that is that you can only make
// one version of them for all other projects to use. For performance and tidiness reasons, we are interested in
// having global defines that can be turned on and off per game, as opposed to runtime (static) parameters.
// Hence why we specify the global defines before including the core Luma file (where near all of the generic Luma implementation is).
// If we wanted to use a library, we'd also need to add a core "main" definition in a cpp file, to link it properly.
// All externs that are currently defined in core would also need to be manually defined in each game's implementation (e.g. see "RESHADE_EXTERNS" above).
// The only disadvantage of not actually including the core as a library, is that we'll have to add the same include/library dependencies in our
// game project (e.g. add ReShade, DLSS, etc), and manually add all cpp files too.
//
// To compile in different modes (e.g. "DEVELOPMENT", "TEST" etc see "global_defines.h").
#include "..\..\Core\core.hpp"

#include "..\..\Core\dlss\DLSS.cpp"
#include "includes/cbuffers.h"


namespace
{
	float2 projection_jitters = { 0, 0 };
	const uint32_t shader_hash_mvec_pixel = std::stoul("FFFFFFF3", nullptr, 16);
	ShaderHashesList shader_hashes_TAA;

#if DEVELOPMENT
	float cb1_debug_buffer[140 * 4] = { 0 };
#endif
}
struct GameDeviceDataFF7Remake final : public GameDeviceData
{
#if ENABLE_NGX
	// DLSS SR
	com_ptr<ID3D11Texture2D> dlss_motion_vectors;
	com_ptr<ID3D11Resource> dlss_source_color;
	com_ptr<ID3D11Resource> depth_buffer;
	com_ptr<ID3D11RenderTargetView> dlss_motion_vectors_rtv;
#endif // ENABLE_NGX
	std::atomic<bool> has_drawn_upscaling = false;
	com_ptr<ID3D11PixelShader> motion_vectors_ps;
};

class FF7Remake final : public Game
{
	// Optional helper to hide ugly casts
	static GameDeviceDataFF7Remake& GetGameDeviceData(DeviceData& device_data)
	{
		return *static_cast<GameDeviceDataFF7Remake*>(device_data.game);
	}

public:
	// You can define any data to initialize once here
	void OnInit(bool async) override
	{
		// You can add shader defines that will end up in the advanced settings for users to modify here.
		  // These will be defined in the shaders, so they can be used to do static branches in them.
		// Ideally they should also be defined in the game's Settings.hlsl file.

		// Define these according to the game's original technical details and the mod's implementation (see their declarations for more).
		GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0'); // What space are the colors in? Was the swapchain linear (sRGB texture format)? Did we change post processing to store in linear space?
		GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0'); // Whether we do gamma correction and paper white scaling during post processing or we delay them until the final display composition pass
		GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0'); // What SDR transfer curve was the game using?
		GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1'); // What SDR transfer curve to we want to emulate? This is relevant even if we work in linear space, as there can be a gamma mismatch on it
		GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0'); // How does the UI draw in?

		// Customize the cbuffers indexes here. They should be between 0 and 13 ("D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1"),
		// set them to invalid values (e.g. -1) to not set them (they won't be uploaded to the GPU, and thus not usable in shaders). All their values need to be different if they are valid.
		// These will be automatically sent to the GPU for every shader pass the mod overrides.
	}

	// This needs to be overridden with your own "GameDeviceData" sub-class (destruction is automatically handled)
	void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
	{
		device_data.game = new GameDeviceDataFF7Remake;
	}

	bool OnDrawCustom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers) override
	{
		auto& game_device_data = GetGameDeviceData(device_data);

		const bool had_drawn_upscaling = game_device_data.has_drawn_upscaling;
		if (!game_device_data.has_drawn_upscaling && device_data.dlss_sr && !device_data.dlss_sr_suppressed && original_shader_hashes.Contains(shader_hashes_TAA))
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
			ASSERT_ONCE(dlss_inputs_valid);

			if (dlss_inputs_valid)
			{

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
					game_device_data.dlss_source_color = nullptr;
					ps_shader_resources[2]->GetResource(&game_device_data.dlss_source_color);
					game_device_data.depth_buffer = nullptr;
					ps_shader_resources[1]->GetResource(&game_device_data.depth_buffer);
					com_ptr<ID3D11Resource> object_velocity_buffer_temp;
					ps_shader_resources[4]->GetResource(&object_velocity_buffer_temp);
					com_ptr<ID3D11Texture2D> object_velocity_buffer;
					hr = object_velocity_buffer_temp->QueryInterface(&object_velocity_buffer);
					ASSERT_ONCE(SUCCEEDED(hr));

					//TODO: add exposure texture support

					// Decode the motion vector from pixel shader
					{
						D3D11_TEXTURE2D_DESC object_velocity_texture_desc;
						object_velocity_buffer->GetDesc(&object_velocity_texture_desc);
						ASSERT_ONCE((object_velocity_texture_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == D3D11_BIND_RENDER_TARGET);
#if 1 // Use the higher quality for MVs, the game's one were R16G16F. This has a ~1% cost on performance but helps with reducing shimmering on fine lines (stright lines looking segmented, like Bart's hair or Shark's teeth) when the camera is moving in a linear fashion. Generating MVs from the depth is still a limited technique so it can't be perfect.
						object_velocity_texture_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
#else
						object_velocity_texture_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
#endif
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
						ID3D11RenderTargetView* const dlss_motion_vectors_rtv_const = game_device_data.dlss_motion_vectors_rtv.get();
						// Set up for motion vector shader
						native_device_context->OMSetRenderTargets(1, &dlss_motion_vectors_rtv_const, depth_stencil_view.get());
						ID3D11PixelShader* prev_shader_px = nullptr;

						native_device_context->PSGetShader(&prev_shader_px, nullptr, nullptr);
						native_device_context->PSSetShader(game_device_data.motion_vectors_ps.get(), nullptr, 0);
						native_device_context->DrawIndexed(3, 6, 0);
						native_device_context->PSSetShader(prev_shader_px, nullptr, 0);
					}
					// Extract jitter from constant buffer 1
					{
						ID3D11Buffer* cb1_buffer = nullptr;
						native_device_context->PSGetConstantBuffers(1, 1, &cb1_buffer); // slot 1 = b1

						if (cb1_buffer)
						{
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
										if (jitter_x != 0 || jitter_y != 0)
										{
											projection_jitters.x = jitter_x;
											projection_jitters.y = jitter_y;
										}
										native_device_context->Unmap(staging_cb1, 0);
										staging_cb1->Release();
										cb1_buffer->Release();
									}
								}
								else
								{
									cb1_buffer->Release();
								}
							}
						}
					}

					bool reset_dlss = false;
					uint32_t render_width_dlss = std::lrintf(device_data.render_resolution.x);
					uint32_t render_height_dlss = std::lrintf(device_data.render_resolution.y);
					float dlss_pre_exposure = 0.f;
					bool dlss_succeeded = NGX::DLSS::Draw(device_data.dlss_sr_handle, native_device_context, device_data.dlss_output_color.get(), game_device_data.dlss_source_color.get(), game_device_data.dlss_motion_vectors.get(), game_device_data.depth_buffer.get(), nullptr, dlss_pre_exposure, projection_jitters.x, projection_jitters.y, reset_dlss, render_width_dlss, render_height_dlss);
					if (dlss_succeeded)
					{
						device_data.has_drawn_dlss_sr = true;
					}
					game_device_data.dlss_motion_vectors_rtv = nullptr;
					game_device_data.dlss_motion_vectors = nullptr;
					game_device_data.dlss_source_color = nullptr;
					game_device_data.depth_buffer = nullptr;

					ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(render_target_views[0]);
					native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, depth_stencil_view.get());

					if (device_data.has_drawn_dlss_sr)
					{
						if (!dlss_output_supports_uav)
						{
							native_device_context->CopyResource(output_color.get(), device_data.dlss_output_color.get()); // DX11 doesn't need barriers
						}
						else
						{
							device_data.dlss_output_color = nullptr;
						}
						return true;
					}
					else
					{
						//ASSERT_ONCE(false);
						//cb_luma_frame_settings.DLSS = 0;
						//device_data.cb_luma_frame_settings_dirty = true;
						//device_data.dlss_sr_suppressed = true;
						device_data.force_reset_dlss_sr = true;
					}
				}
				if (dlss_output_supports_uav)
				{
					device_data.dlss_output_color = nullptr;
				}
			}

		}
		return false; // Don't cancel the original draw call
	}
	void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
	{
		auto& game_device_data = GetGameDeviceData(device_data);

		game_device_data.has_drawn_upscaling = false;
	}

	void PrintImGuiAbout() override
	{
		// Remember to credit Luma developers, the game mod creators, and all third party code that is used (plus, optionally, testers too)
		ImGui::Text("Template Luma mod - about and credits section", "");
	}

	void CreateShaderObjects(DeviceData& device_data, const std::optional<std::unordered_set<uint32_t>>& shader_hashes_filter) override
	{
		auto& game_device_data = GetGameDeviceData(device_data);
		CreateShaderObject(device_data.native_device, shader_hash_mvec_pixel, game_device_data.motion_vectors_ps, shader_hashes_filter);
	}
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		// TODO: move this to a "Generic" mod project?
		wchar_t file_path_char[MAX_PATH] = L"";
		GetModuleFileNameW(hModule, file_path_char, ARRAYSIZE(file_path_char));
		std::filesystem::path file_path = file_path_char;
		std::string file_name = file_path.stem().string();
		bool use_generic_name = false;
		// Retrieve the dll name and use it as the addon name, so we can re-use this template mod for all games by simply renaming the executable (as long as the mod just involves replacing shaders).
		if (file_name.starts_with(std::string(Globals::MOD_NAME) + "-"))
		{
			// This is the D3D11 proxy DLL, we don't want to initialize the game here
			file_name.erase(0, std::string(Globals::MOD_NAME).length() + 1);
			if (file_name != "Template")
			{
				use_generic_name = true;
			}
		}
		luma_settings_cbuffer_index = 13;
		luma_data_cbuffer_index = 12;
		// Setup the globals (e.g. name etc). It's good to do this stuff before registering the ReShade addon, to make sure the names are up to date.
		const char* project_name = PROJECT_NAME;
		const char* cleared_project_name = (project_name[0] == '_') ? (project_name + 1) : project_name; // Remove the potential "_" at the beginning
		Globals::GAME_NAME = use_generic_name ? file_name.c_str() : cleared_project_name; // Can include spaces!
		Globals::DESCRIPTION = use_generic_name ? "Generic Luma mod" : "Template Luma mod";
		Globals::WEBSITE = ""; // E.g. Nexus link
		Globals::VERSION = 1; // Increase this to reset the game settings and shader binaries after making large changes to your mod

		shader_hashes_TAA.pixel_shaders.emplace(std::stoul("4729683B", nullptr, 16));

		enable_swapchain_upgrade = true;
		swapchain_upgrade_type = 1;
		enable_texture_format_upgrades = true;
		// Texture upgrades (8 bit unorm and 11 bit float etc to 16 bit float)
		texture_upgrade_formats = {
				reshade::api::format::r8g8b8a8_unorm,
				reshade::api::format::r8g8b8a8_unorm_srgb,
				reshade::api::format::r8g8b8a8_typeless,
				reshade::api::format::r8g8b8x8_unorm,
				reshade::api::format::r8g8b8x8_unorm_srgb,
				reshade::api::format::b8g8r8a8_unorm,
				reshade::api::format::b8g8r8a8_unorm_srgb,
				reshade::api::format::b8g8r8a8_typeless,
				reshade::api::format::b8g8r8x8_unorm,
				reshade::api::format::b8g8r8x8_unorm_srgb,
				reshade::api::format::b8g8r8x8_typeless,

				reshade::api::format::r10g10b10a2_unorm,
				reshade::api::format::r10g10b10a2_typeless,

				//reshade::api::format::r16g16b16a16_unorm,

				reshade::api::format::r11g11b10_float,
		};
		texture_format_upgrades_lut_size = 32;
		texture_format_upgrades_lut_dimensions = LUTDimensions::_2D;

		// Create your game sub-class instance (it will be automatically destroyed on exit).
		// You do not need to do this if you have no custom data to store.
		game = new FF7Remake();
	}

	CoreMain(hModule, ul_reason_for_call, lpReserved);

	return TRUE;
}