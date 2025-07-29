// Specify a define with the name of the game here (GAME_*).
// This is optional but can be used to hardcode custom (per game) behaviours in the core library,
// or other reasons. We can't automate it through the project define system (based on the project name) because
// we need these defines to be upper case and space free.
#define GAME_TEMPLATE 1

// Define all the global "core" defines before including its files:
// Enable this to be able to use DLSS's code
#define ENABLE_NGX 0
// Update samples to override the bias, based on the rendering resolution etc
#define UPGRADE_SAMPLERS 0
#define GEOMETRY_SHADER_SUPPORT 0
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
// one version of them for all other projects to use. For performance and tidiness reasons, we are interestested in
// having global defines that can be turned on and off per game, as opposed to runtime (static) parameters.
// Hence why we specify the global defines before including the core Luma file (where near all of the generic Luma implementation is).
// If we wanted to use a library, we'd also need to add a core "main" definition in a cpp file, to link it properly.
// All externs that are currently defined in core would also need to be manually defined in each game's implementation (e.g. see "RESHADE_EXTERNS" above).
// The only disadvantage of not actually including the core as a library, is that we'll have to add the same include/library dependencies in our
// game project (e.g. add ReShade, DLSS, etc), and manually add all cpp files too.
//
// To compile in different modes (e.g. "DEVELOPMENT", "TEST" etc see "global_defines.h").
#include "..\..\Core\core.hpp"

struct GameDeviceDataTemplate final : public GameDeviceData
{
   bool has_drawn_tonemap = false;
};

class GameTemplate final : public Game
{
   // Optional helper to hide ugly casts
   static GameDeviceDataTemplate& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataTemplate*>(device_data.game);
   }

public:
   // You can define any data to initialize once here
   void OnInit(bool async) override
   {
      // You can add shader defines that will end up in the advanced settings for users to modify here.
		// These will be defined in the shaders, so they can be used to do static branches in them.
      // Ideally they should also be defined in the game's Settings.hlsl file.
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', false, false, "0 - Vanilla SDR\n1 - Luma HDR (Vanilla+)"},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES); // Make sure there's room for at least one extra custom define to add for development (this isn't really relevant outside of development)

      // Define these according to the game's original technical details and the mod's implementation (see their declarations for more).
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0'); // What space are the colors in? Was the swapchain linear (sRGB texture format)? Did we change post processing to store in linear space?
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0'); // Whether we do gamma correction and paper white scaling during post processing or we delay them until the final display composition pass
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0'); // What SDR transfer curve was the game using?
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1'); // What SDR transfer curve to we want to emulate? This is relevant even if we work in linear space, as there can be a gamma mismatch on it
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0'); // How does the UI draw in?

      // Customize the cbuffers indexes here. They should be between 0 and 13 ("D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1"),
      // set them to invalid values (e.g. -1) to not set them (they won't be uploaded to the GPU, and thus not usable in shaders). All their values need to be different if they are valid.
      // These will be automatically sent to the GPU for every shader pass the mod overrides.
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12; // Needed for debugging textures and having custom per pass data (including handling the final pass properly).
      luma_ui_cbuffer_index = -1; // Optional, see "UI_DRAW_TYPE" (this is for type 1)

      // Init the game settings here, you could also load them from ini in "LoadConfigs()"
      cb_luma_frame_settings.GameSetting01 = 0.5f;
      cb_luma_frame_settings.GameSetting02 = 33;
   }

   // This needs to be overridden with your own "GameDeviceData" sub-class (destruction is automatically handled)
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataTemplate;
   }

   bool OnDrawCustom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Random pixel shader hash as an example
      static uint32_t pixel_shader_hash_tonemap = Shader::Hash_StrToNum("8B2321A2");

      // Here you can track and customize shader passes by hash, you can do whatever you want in it
      if (!game_device_data.has_drawn_tonemap && original_shader_hashes.Contains(pixel_shader_hash_tonemap, reshade::api::shader_stage::pixel))
      {
         game_device_data.has_drawn_tonemap = true;

         device_data.has_drawn_main_post_processing = true;
      }

      return false; // Don't cancel the original draw call
   }
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      game_device_data.has_drawn_tonemap = false;
   }

   void PrintImGuiAbout() override
   {
      // Remember to credit Luma developers, the game mod creators, and all third party code that is used (plus, optionally, testers too)
      ImGui::Text("Template Luma mod - about and credits section", "");
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
      
      // Setup the globals (e.g. name etc). It's good to do this stuff before registering the ReShade addon, to make sure the names are up to date.
      Globals::GAME_NAME = use_generic_name ? file_name.c_str() : PROJECT_NAME; // Can include spaces!
      Globals::DESCRIPTION = use_generic_name ? "Generic Luma mod" : "Template Luma mod";
      Globals::WEBSITE = ""; // E.g. Nexus link
      Globals::VERSION = 1; // Increase this to reset the game settings and shader binaries after making large changes to your mod

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
      game = new GameTemplate();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}