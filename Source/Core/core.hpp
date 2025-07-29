#pragma once

#include "global_defines.h"

// "_DEBUG" might already be defined in debug?
// Setting it to 0 causes the compiler to still assume it as defined and that thus we are in debug mode (don't change this manually).
#ifndef NDEBUG
#define _DEBUG 1
#endif // !NDEBUG

#define LOG_VERBOSE ((DEVELOPMENT || TEST) && 0)

// Disables loading the ReShade Addon code (useful to test the mod without any ReShade dependencies)
#define DISABLE_RESHADE 0

#pragma comment(lib, "dxguid.lib")

#define _USE_MATH_DEFINES

#ifdef _WIN32
#define ImTextureID ImU64
#endif

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include <semaphore>
#include <utility>
#include <cstdint>
#include <functional>

// DirectX dependencies
// TODO: needed by "XMConvertFloatToHalf" though somehow I couldn't include it and had to re-implement it?
#include <DirectXMath.h>

// ReShade dependencies
#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>
#include <source/com_ptr.hpp>
#include <examples/utils/crc32_hash.hpp>
#if 0 // Not needed atm
#include <source/d3d11/d3d11_impl_type_convert.hpp>
#endif

#ifndef FORCE_KEEP_CUSTOM_SHADERS_LOADED
#define FORCE_KEEP_CUSTOM_SHADERS_LOADED 1
#endif // FORCE_KEEP_CUSTOM_SHADERS_LOADED
#ifndef ALLOW_LOADING_DEV_SHADERS
#define ALLOW_LOADING_DEV_SHADERS 1
#endif // ALLOW_LOADING_DEV_SHADERS
#ifndef UPGRADE_SAMPLERS
#define UPGRADE_SAMPLERS 0
#endif // UPGRADE_SAMPLERS
#ifndef GEOMETRY_SHADER_SUPPORT
#define GEOMETRY_SHADER_SUPPORT 0
#endif // GEOMETRY_SHADER_SUPPORT
// Not used by mod engines (e.g. Prey)
#ifndef ENABLE_SHADER_CLASS_INSTANCES
#define ENABLE_SHADER_CLASS_INSTANCES 0
#endif // ENABLE_SHADER_CLASS_INSTANCES
// 64x only
#ifndef ENABLE_NGX
#define ENABLE_NGX 0
#endif // ENABLE_NGX
#ifndef PROJECT_NAME
// Matches "Globals::MOD_NAME"
#define PROJECT_NAME "Luma"
#endif // PROJECT_NAME

#define DEFINE_NAME_AS_STRING(x) #x
#define DEFINE_VALUE_AS_STRING(x) DEFINE_NAME_AS_STRING(x)

#define LUMA_GAME_SETTINGS_NUM 0
// There's no proper way to do incremental defines (like "#define LUMA_GAME_SETTINGS_NUM (LUMA_GAME_SETTINGS_NUM)+(1)"),
// so we have to do it manually, which means the settings always need to be defined in order (e.g. you can't define setting 4 without setting 1).
#pragma warning(push)
#pragma warning(disable: 4005)
#ifdef LUMA_GAME_SETTING_01
#define LUMA_GAME_SETTINGS_NUM 1
#endif
#ifdef LUMA_GAME_SETTING_02
#define LUMA_GAME_SETTINGS_NUM 2
#endif
#ifdef LUMA_GAME_SETTING_03
#define LUMA_GAME_SETTINGS_NUM 3
#endif
#ifdef LUMA_GAME_SETTING_04
#define LUMA_GAME_SETTINGS_NUM 4
#endif
#ifdef LUMA_GAME_SETTING_05
#define LUMA_GAME_SETTINGS_NUM 5
#endif
#pragma warning(pop)

// This might not disable all shaders dumping related code, but it disables enough to remove any performance cost
#ifndef ALLOW_SHADERS_DUMPING
#define ALLOW_SHADERS_DUMPING (DEVELOPMENT || TEST)
#endif

// Depends on "DEVELOPMENT"
#define TEST_DLSS (DEVELOPMENT && 0)

#include "dlss/DLSS.h" // see "ENABLE_NGX"

#include "includes/globals.h"
#include "includes/debug.h"
#include "includes/cbuffers.h"
#include "includes/math.h"
#include "includes/matrix.h"
#include "includes/recursive_shared_mutex.h"
#include "includes/shaders.h"
#include "includes/shader_define.h"
#include "includes/instance_data.h"
#include "includes/game.h"

#include "utils/format.hpp"
#include "utils/pipeline.hpp"
#include "utils/shader_compiler.hpp"
#include "utils/display.hpp"
#include "utils/resource.hpp"
#include "utils/draw.hpp"

#define ICON_FK_CANCEL reinterpret_cast<const char*>(u8"\uf00d")
#define ICON_FK_OK reinterpret_cast<const char*>(u8"\uf00c")
#define ICON_FK_PLUS reinterpret_cast<const char*>(u8"\uf067")
#define ICON_FK_MINUS reinterpret_cast<const char*>(u8"\uf068")
#define ICON_FK_REFRESH reinterpret_cast<const char*>(u8"\uf021")
#define ICON_FK_UNDO reinterpret_cast<const char*>(u8"\uf0e2")
#define ICON_FK_SEARCH reinterpret_cast<const char*>(u8"\uf002")
#define ICON_FK_WARNING reinterpret_cast<const char*>(u8"\uf071")
#define ICON_FK_FILE_CODE reinterpret_cast<const char*>(u8"\uf1c9")

#ifndef RESHADE_EXTERNS
// These are needed by ReShade
extern "C" __declspec(dllexport) const char* NAME = Globals::MOD_NAME;
extern "C" __declspec(dllexport) const char* DESCRIPTION = Globals::DESCRIPTION;
extern "C" __declspec(dllexport) const char* WEBSITE = Globals::WEBSITE;
#endif

// Make sure we can use com_ptr as c arrays of pointers
static_assert(sizeof(com_ptr<ID3D11Resource>) == sizeof(void*));

using namespace Shader;
using namespace Math;

namespace
{
   const uint32_t HASH_CHARACTERS_LENGTH = 8;
   const std::string NAME_ADVANCED_SETTINGS = std::string(NAME) + " Advanced";

   // A default "template" (empty) game data that we can fall back to in case we didn't specify any
   Game default_game = {};
   // The pointer to the current game implementation (data and code), it can be replaced
   Game* game = &default_game;

   // Mutexes:
   // For "pipeline_cache_by_pipeline_handle", "pipeline_cache_by_pipeline_clone_handle", "pipeline_caches_by_shader_hash", "pipelines_to_destroy", "cloned_pipeline_count"
   recursive_shared_mutex s_mutex_generic;
   // For "shaders_to_dump", "dumped_shaders", "shader_cache". In general for dumping shaders to disk (this almost always needs to be read and write locked together so there's no need for it to be a shared mutex)
   std::recursive_mutex s_mutex_dumping;
   // For "custom_shaders_cache", "pipelines_to_reload". In general for loading shaders from disk and compiling them
   recursive_shared_mutex s_mutex_loading;
   // Mutex for created shader DX objects (and "created_custom_shaders")
   std::shared_mutex s_mutex_shader_objects;
   // Mutex for shader defines ("shader_defines_data", "code_shaders_defines", "shader_defines_data_index")
   std::shared_mutex s_mutex_shader_defines;
   // Mutex to deal with data shader with ReShade, like ini/config saving and loading (including "cb_luma_frame_settings" and "cb_luma_frame_settings_dirty")
   std::shared_mutex s_mutex_reshade;
   // For "custom_sampler_by_original_sampler" and "texture_mip_lod_bias_offset"
   std::shared_mutex s_mutex_samplers;
   // For "global_native_devices", "global_device_datas", "game_window"
   recursive_shared_mutex s_mutex_device;
#if DEVELOPMENT
   // for "trace_count" and "trace_scheduled" and "trace_running"
   std::shared_mutex s_mutex_trace;
#endif

   // Dev or User settings:
   bool auto_dump = (bool)ALLOW_SHADERS_DUMPING;
   bool auto_load = true;
#if DEVELOPMENT
   bool trace_ignore_vertex_shaders = true;
#endif
   constexpr bool precompile_custom_shaders = true; // Async shader compilation on boot
   constexpr bool block_draw_until_device_custom_shaders_creation = true; // Needs "precompile_custom_shaders". Note that drawing (and "Present()") could be blocked anyway due to other mutexes on boot if custom shaders are still compiling
   bool dlss_sr = true; // If true DLSS is enabled by the user (but not necessarily supported+initialized correctly, that's by device)
   bool hdr_enabled_display = false;
   bool hdr_supported_display = false;
   constexpr bool prevent_shader_cache_loading = false;
   bool prevent_shader_cache_saving = false;
#if DEVELOPMENT
   //TODOFT3: clean up the following vars
   int samplers_upgrade_mode = UPGRADE_SAMPLERS ? 5 : 0;
   int samplers_upgrade_mode_2 = 0;
   bool custom_texture_mip_lod_bias_offset = false; // Live edit
   int frame_sleep_ms = 0;
   int frame_sleep_interval = 1;
#endif

   bool enable_separate_ui_drawing = false;

   bool enable_swapchain_upgrade = false;
   // 0 None (keep the original one, SDR or whatnot)
   // 1 scRGB HDR
   uint32_t swapchain_upgrade_type = 0;
   bool enable_texture_format_upgrades = false;
	// List of render targets (and unordered access) textures that we upgrade to R16G16B16A16_FLOAT.
   // Most formats are supported but some might not act well when upgraded.
   std::unordered_set<reshade::api::format> texture_upgrade_formats;
   enum class TextureFormatUpgrades2DSizeFilters : uint32_t
   {
      // If the flags are set to 0, we upgrade all textures independently of their size.
      All = 0,
      // The output resolution (usually matches the window resolution too).
      SwapchainResolution = 1 << 0,
      // The rendering resolution (e.g. for TAA and other types of upscaling).
      RenderResolution = 1 << 1,
      // The aspect ratio of the swapchain or a custom aspect ratio.
      // This can be useful for bloom or resolution scaling etc.
      AspectRatio = 1 << 2,
      // All mip chain sizes based starting from the highest resolution between rendering and swapchain resolution (they should generally have the same aspect ratio anyway) to 1.
      // This can be useful for blur passes etc.
      Mips = 1 << 3,
   };
   uint32_t texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::AspectRatio;
   // Set <= 0 to automatically detect the aspect ratio from the swapchain/window size and upgrade textures with that aspect ratio.
   // Set > 0 to only upgrade textures of a specific aspect ratio, e.g. some games use a full screen
   // swapchain, but force a fixed aspect ratio on rendering and thus have black bars in Ultrawide (or below 16:9).
   float texture_format_upgrades_2d_target_aspect_ratio = -1.f;
   // Most games do resolution scaling properly, with a maximum aspect ratio offset of 1 pixel, though occasionally it goes to 2 pixels of difference.
   // Set to 0 to only accept 100% matching aspect ratio.
   uint32_t texture_format_upgrades_2d_aspect_ratio_pixel_threshold = 1;
   // The size of the LUT we might want to upgrade, whether it's 1D, 2D or 3D.
   // LUTs in most games are 16x or 32x, though in some cases they might be 15x, 31x, 48x, 64x etc.
   uint32_t texture_format_upgrades_lut_size = -1;
   enum class LUTDimensions
   {
      _1D,
      _2D,
      _3D
   };
   LUTDimensions texture_format_upgrades_lut_dimensions = LUTDimensions::_2D;

   // Game specific constants (these are not expected to be changed at runtime)
   uint32_t luma_settings_cbuffer_index = 13;
   uint32_t luma_data_cbuffer_index = -1; // Needed, unless "POST_PROCESS_SPACE_TYPE" is 0 and we don't need the final display composition pass
	uint32_t luma_ui_cbuffer_index = -1; // Optional, for "UI_DRAW_TYPE" 1

   // TODO: define these by name instead of by hash, it might be safer and less confusing
   const uint32_t shader_hash_copy_vertex = Shader::Hash_StrToNum("FFFFFFF0");
   const uint32_t shader_hash_copy_pixel = Shader::Hash_StrToNum("FFFFFFF1");
   const uint32_t shader_hash_transform_function_copy_pixel = Shader::Hash_StrToNum("FFFFFFF2");

   // Optionally add the UI shaders to this list, to make sure they draw to a separate render target for proper HDR composition
   ShaderHashesList shader_hashes_UI;
   // Shaders that might be running after "has_drawn_main_post_processing" has turned true, but that are still not UI (most games don't have a fixed last shader that runs on the scene rendering before UI, e.g. FXAA might add a pass based on user settings etc), so we have to exclude them like this
   ShaderHashesList shader_hashes_UI_excluded;

   // All the shaders the game ever loaded (including the ones that have been unloaded). Only used by shader dumping (if "ALLOW_SHADERS_DUMPING" is on) or to see their binary code in the ImGUI view. By shader hash.
   // The data it contains is fully its own, so it's not by "Device".
   std::unordered_map<uint32_t, CachedShader*> shader_cache;
   // All the shaders the user has (and has had) as custom in the live folder. By shader hash.
   // The data it contains is fully its own, so it's not by "Device".
   std::unordered_map<uint32_t, CachedCustomShader*> custom_shaders_cache;

   // Newly loaded shaders that still need to be (auto) dumped, by shader hash
   std::unordered_set<uint32_t> shaders_to_dump;
   // All the shaders we have already dumped, by shader hash
   std::unordered_set<uint32_t> dumped_shaders;

   std::string shaders_compilation_errors; // errors and warning log

   // List of define values read by our settings shaders
   std::unordered_map<std::string, uint8_t> code_shaders_defines;

   // These default should ideally match shaders values (Settings.hlsl), but it's not necessary because whatever the default values they have they will be overridden.
	// For further descriptions, see their shader declarations.
   // TODO: add grey out conditions (another define, by name, whether its value is > 0), and also add min/max values range (to limit the user insertable values), and "category"
   std::vector<ShaderDefineData> shader_defines_data = {
       {"DEVELOPMENT", DEVELOPMENT ? '1' : '0', true, DEVELOPMENT ? false : true, "Enables some development/debug features that are otherwise not allowed (get a TEST or DEVELOPMENT build if you want to use this)"},
       // Usually if we store in gamma space, we also keep the paper white not multiplied in until we apply it on the final output, while if we store in linear space, we pre-multiply it in (and we might also pre-correct gamma before the final output).
       // NOTE: "POST_PROCESS_SPACE_TYPE" 2 is actually implemented as well but only used by Prey.
       {"POST_PROCESS_SPACE_TYPE", '0', true, DEVELOPMENT ? false : true, "Describes in what \"space\" (encoding) the game post processing color buffers are stored\n0 - SDR Gamma space\n1 - Linear space"},
       {"EARLY_DISPLAY_ENCODING", '0', true, DEVELOPMENT ? false : true, "Whether the main gamma correction and paper white scaling happens early in post processing or in the final display composition pass (only applies if \"POST_PROCESS_SPACE_TYPE\" is set to linear)"},
       // Sadly most games encoded with sRGB (or used linear sRGB buffers, similar thing), so that's the default here
       {"VANILLA_ENCODING_TYPE", '0', true, DEVELOPMENT ? false : true, "0 - sRGB\n1 - Gamma 2.2"},
       {"GAMMA_CORRECTION_TYPE", '1', true, false, "(HDR only) Emulates a specific SDR transfer function\nThis is best left to \"1\" (Gamma 2.2) unless you have crushed blacks or overly saturated colors\n0 - sRGB\n1 - Gamma 2.2\n2 - sRGB (color hues) with gamma 2.2 luminance (corrected by channel)\n3 - sRGB (color hues) with gamma 2.2 luminance (corrected by luminance)\n4 - Gamma 2.2 (corrected by luminance) with per channel correction chrominance"},
       {"GAMUT_MAPPING_TYPE", '0', true, DEVELOPMENT ? false : true, "The type of gamut mapping that is needed by the game.\nIf rendering and post processing don't generate any colors beyond the target gamut, there's no need to do gamut mapping.\n0 - None\n1 - Auto (SDR/HDR)\n2 - SDR (BT.709)\n3 - HDR (BT.2020)"},
       {"UI_DRAW_TYPE", '0', true, DEVELOPMENT ? false : true, "Describes how the UI draws in\n0 - Raw (original linear to linear or gamma to gamma draws) (no custom UI paper white control)\n1 - Direct Custom (gamma to linear adapted blends)\n2 - Direct (inverse scene brightness draws)\n3 - Separate (renders the UI on a separate texture, allows tonemapping of UI background)"},
   };

   // TODO: if at runtime we can't edit "shader_defines_data" (e.g. in non dev modes), then we could directly set these to the index value of their respective "shader_defines_data" and skip the map?
   constexpr uint32_t DEVELOPMENT_HASH = char_ptr_crc32("DEVELOPMENT");
   constexpr uint32_t POST_PROCESS_SPACE_TYPE_HASH = char_ptr_crc32("POST_PROCESS_SPACE_TYPE");
   constexpr uint32_t EARLY_DISPLAY_ENCODING_HASH = char_ptr_crc32("EARLY_DISPLAY_ENCODING");
   constexpr uint32_t VANILLA_ENCODING_TYPE_HASH = char_ptr_crc32("VANILLA_ENCODING_TYPE");
   constexpr uint32_t GAMMA_CORRECTION_TYPE_HASH = char_ptr_crc32("GAMMA_CORRECTION_TYPE");
   constexpr uint32_t GAMUT_MAPPING_TYPE_HASH = char_ptr_crc32("GAMUT_MAPPING_TYPE");
   constexpr uint32_t UI_DRAW_TYPE_HASH = char_ptr_crc32("UI_DRAW_TYPE");

   // uint8_t is enough for MAX_SHADER_DEFINES
   std::unordered_map<uint32_t, uint8_t> shader_defines_data_index;

   // Global data (not device dependent really):

   // Directly from cbuffer
   uint32_t frame_index = 0; // Frame counter, no need for this to be by device or swapchain
   LumaFrameSettings cb_luma_frame_settings = { }; // Not in device data as this stores some users settings too // Set "cb_luma_frame_settings_dirty" when changing within a frame (so it's uploaded again)

   bool has_init = false;
   bool asi_loaded = true; // Whether we've been loaded from an ASI loader or ReShade Addons system
   std::thread thread_auto_dumping;
   std::atomic<bool> thread_auto_dumping_running = false;
   std::thread thread_auto_compiling;
   std::atomic<bool> thread_auto_compiling_running = false;
   bool last_pressed_unload = false;
   bool needs_unload_shaders = false;
   bool needs_load_shaders = false; // Load/compile or reload/recompile shaders, no need to default it to true, we have "auto_load" for that

   // There's only one swapchain and one device in most games (e.g. Prey), but the game changes its configuration from different threads.
   // A new device+swapchain can be created if the user resets the settings as the old one is still rendering (or is it?).
   // These are raw pointers that did not add a reference to the counter.
   std::vector<ID3D11Device*> global_native_devices; // Possibly unused
   std::vector<DeviceData*> global_devices_data;
   HWND game_window = 0; // This is fixed forever (in almost all games (e.g. Prey))
	thread_local bool last_swapchain_linear_space = false;
   thread_local bool waiting_on_upgraded_resource_init = false;
   thread_local reshade::api::resource_desc upgraded_resource_init_desc = {};
   thread_local void* upgraded_resource_init_data = {};

#if DEVELOPMENT
   bool trace_scheduled = false; // For next frame
   bool trace_running = false; // For this frame
   uint32_t trace_count = 0; // Not exactly necessary but... it might help

   uint32_t shader_cache_count = 0; // For dumping

   std::string last_drawn_shader = ""; // Not exactly thread safe but it's fine...

   thread_local reshade::api::command_list* global_cmd_list = nullptr; // Hacky global variable (possibly not cleared, stale)

   uint32_t debug_draw_shader_hash = 0;
   char debug_draw_shader_hash_string[HASH_CHARACTERS_LENGTH + 1] = {};
   uint64_t debug_draw_pipeline = 0;
   std::atomic<int32_t> debug_draw_pipeline_instance = 0; // Theoretically should be within "CommandListData" but this should work for most cases
   int32_t debug_draw_pipeline_target_instance = -1;
   bool debug_draw_replaced_pass = false; // Whether we print the debugging of the original or replaced pass (the resources bindings etc might be different, though this won't forcefully run the original pass if it was skipped by the game's mod custom code)

   DebugDrawMode debug_draw_mode = DebugDrawMode::RenderTarget;
   int32_t debug_draw_view_index = 0;
   uint32_t debug_draw_options = (uint32_t)DebugDrawTextureOptionsMask::Fullscreen | (uint32_t)DebugDrawTextureOptionsMask::RenderResolutionScale;
   bool debug_draw_auto_clear_texture = false;

   LumaFrameDevSettings cb_luma_frame_dev_settings_default_value(0.f);
   LumaFrameDevSettings cb_luma_frame_dev_settings_min_value(0.f);
   LumaFrameDevSettings cb_luma_frame_dev_settings_max_value(1.f);
   std::array<std::string, LumaFrameDevSettings::SettingsNum> cb_luma_frame_dev_settings_names;
#endif

   // Forward declares:
   void DumpShader(uint32_t shader_hash, bool auto_detect_type);
   void AutoDumpShaders();
   void AutoLoadShaders(DeviceData* device_data);

   // Quick and unsafe. Passing in the hash instead of the string is the only way make sure strings hashes are calculate them at compile time.
   __forceinline ShaderDefineData& GetShaderDefineData(uint32_t hash)
   {
#if 0 // We don't lock "s_mutex_shader_defines" here as it wouldn't be particularly relevant (it won't lead to crashes, as generaly they are not edited in random threads, though having it enabled could lead to deadlocks if there's nested locks!).
      const std::shared_lock lock(s_mutex_shader_defines);
#endif
      assert(shader_defines_data_index.contains(hash));
#if DEVELOPMENT // Just to avoid returning a random variable while developing
      if (!shader_defines_data_index.contains(hash))
      {
         static ShaderDefineData defaultShaderDefineData;
         return defaultShaderDefineData;
      }
#endif
      return shader_defines_data[shader_defines_data_index[hash]];
   }
   __forceinline uint8_t GetShaderDefineCompiledNumericalValue(uint32_t hash)
   {
      return GetShaderDefineData(hash).GetCompiledNumericalValue();
   }

   std::filesystem::path GetShadersRootPath()
   {
      wchar_t file_path[MAX_PATH] = L"";
      // We don't pass in any module handle, thus this will return the path of the executable that loaded our dll
      GetModuleFileNameW(nullptr, file_path, ARRAYSIZE(file_path));

      std::filesystem::path shaders_path = file_path;
      shaders_path = shaders_path.parent_path();
      std::string name_safe = Globals::MOD_NAME;
      // Remove the common invalid characters
      std::replace(name_safe.begin(), name_safe.end(), ' ', '-');
      std::replace(name_safe.begin(), name_safe.end(), ':', '-');
      std::replace(name_safe.begin(), name_safe.end(), '"', '-');
      std::replace(name_safe.begin(), name_safe.end(), '?', '-');
      std::replace(name_safe.begin(), name_safe.end(), '*', '-');
      std::replace(name_safe.begin(), name_safe.end(), '/', '-');
      std::replace(name_safe.begin(), name_safe.end(), '\\', '-');
      shaders_path /= name_safe;

#if DEVELOPMENT && defined(SOLUTION_DIR)
      // Fall back on the solution "Shaders" folder if we are in development mode and there's no luma shaders folder created in the game side
      if (!std::filesystem::is_directory(shaders_path))
      {
         std::filesystem::path solution_shaders_path = SOLUTION_DIR;
         solution_shaders_path /= "Shaders";
         if (std::filesystem::is_directory(solution_shaders_path))
         {
            shaders_path = solution_shaders_path;
         }
      }
#endif
      return shaders_path;
   }

   // TODO: if this was ever too slow, given we iterate through the shader folder which also contains (possibly hundres of) dumps and our built binaries,
   // we could split it up in 3 main branches (shaders code, shaders binaries and shaders dump).
   // Alternatively we could make separate iterators for each main shaders folder.
   bool IsValidShadersSubPath(const std::filesystem::path& shader_directory, const std::filesystem::path& entry_path)
   {
      const std::filesystem::path entry_directory = entry_path.parent_path();
      const auto global_shader_directory = shader_directory / "Global";
      if (entry_directory == global_shader_directory)
      {
         return true;
      }
      const auto game_shader_directory = shader_directory / Globals::GAME_NAME;
      if (entry_directory == game_shader_directory)
      {
         return true;
      }
#if DEVELOPMENT && ALLOW_LOADING_DEV_SHADERS
      // WIP and test and unused shaders (they expect ".../" in front of their include dirs, given the nested path)
      const auto dev_directory = game_shader_directory / "Dev";
      const auto unused_directory = game_shader_directory / "Unused";
      if (entry_directory == dev_directory || entry_path == unused_directory)
      {
         return true;
      }
#endif
      return false;
   }

   void ClearCustomShader(uint32_t shader_hash)
   {
      const std::unique_lock lock(s_mutex_loading);
      auto custom_shader = custom_shaders_cache.find(shader_hash);
      if (custom_shader != custom_shaders_cache.end() && custom_shader->second != nullptr)
      {
         custom_shader->second->code.clear();
         custom_shader->second->is_hlsl = false;
         custom_shader->second->preprocessed_hash = 0;
         custom_shader->second->file_path.clear();
         custom_shader->second->compilation_errors.clear();
#if DEVELOPMENT || TEST
         custom_shader->second->compilation_error = false;
         custom_shader->second->preprocessed_code.clear();
#endif
      }
   }

   void UnloadCustomShaders(DeviceData& device_data, const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>(), bool immediate = false, bool clean_custom_shader = true)
   {
      const std::unique_lock lock(s_mutex_generic);
      for (auto& pair : device_data.pipeline_cache_by_pipeline_handle)
      {
         auto& cached_pipeline = pair.second;
         if (cached_pipeline == nullptr || (!pipelines_filter.empty() && !pipelines_filter.contains(cached_pipeline->pipeline.handle))) continue;

         // In case this is a full "unload" of all shaders
         if (pipelines_filter.empty())
         {
            // Clear their compilation state, we might not have any other way of doing it.
            // Disable testing etc here, otherwise we might not always have a way to do it
            if (clean_custom_shader)
            {
#if DEVELOPMENT
               cached_pipeline->skip = false;
               cached_pipeline->redirect_data = CachedPipeline::RedirectData();
#endif
               for (auto shader_hash : cached_pipeline->shader_hashes)
               {
                  ClearCustomShader(shader_hash);
               }
            }
         }

         if (!cached_pipeline->cloned) continue;
         cached_pipeline->cloned = false; // This stops the cloned pipeline from being used in the next frame, allowing us to destroy it
         device_data.cloned_pipeline_count--;
         device_data.cloned_pipelines_changed = true;

         if (immediate)
         {
            cached_pipeline->device->destroy_pipeline(reshade::api::pipeline{ cached_pipeline->pipeline_clone.handle });
         }
         else
         {
            device_data.pipelines_to_destroy[cached_pipeline->pipeline_clone.handle] = cached_pipeline->device;
         }
         cached_pipeline->pipeline_clone = { 0 };
         device_data.pipeline_cache_by_pipeline_clone_handle.erase(cached_pipeline->pipeline_clone.handle);
      }
   }

   // Expects "s_mutex_loading" to make sure we don't try to compile/load any other files we are currently deleting
   void CleanShadersCache()
   {
      const auto directory = GetShadersRootPath();
      if (!std::filesystem::exists(directory))
      {
         return;
      }
      
      for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
      {
         const auto& entry_path = entry.path();
         if (!IsValidShadersSubPath(directory, entry_path))
         {
            continue;
         }
         if (!entry.is_regular_file())
         {
            continue;
         }
         const bool is_cso = entry_path.extension().compare(".cso") == 0;
         if (!entry_path.has_extension() || !entry_path.has_stem() || !is_cso)
         {
            continue;
         }

         const auto filename_no_extension_string = entry_path.stem().string();

#if 1 // Optionally leave any "raw" cso that was likely copied from the dumped shaders folder (these were not compiled from a custom hlsl shader by the same hash)
         if (filename_no_extension_string.length() >= strlen("0x12345678") && filename_no_extension_string[0] == '0' && filename_no_extension_string[1] == 'x')
         {
            continue;
         }
#endif

         std::filesystem::remove(entry_path);
      }
   }

   // Expects "s_mutex_loading" and "s_mutex_shader_objects"
   template<typename T = ID3D11DeviceChild>
   void CreateShaderObject(ID3D11Device* native_device, uint32_t shader_hash, com_ptr<T>& shader_object, const std::optional<std::unordered_set<uint32_t>>& shader_hashes_filter, bool force_delete_previous = !(bool)FORCE_KEEP_CUSTOM_SHADERS_LOADED, bool trigger_assert = false)
   {
      if (!shader_hashes_filter.has_value() || shader_hashes_filter.value().contains(shader_hash))
      {
         if (force_delete_previous)
         {
            // The shader changed, so we should clear its previous version resource anyway (to avoid keeping an outdated version)
            shader_object = nullptr;
         }
         if (custom_shaders_cache.contains(shader_hash))
         {
            // Delay the deletition
            if (!force_delete_previous)
            {
               shader_object = nullptr;
            }

            const CachedCustomShader* custom_shader_cache = custom_shaders_cache[shader_hash];

            if constexpr (typeid(T) == typeid(ID3D11GeometryShader))
            {
               HRESULT hr = native_device->CreateGeometryShader(custom_shader_cache->code.data(), custom_shader_cache->code.size(), nullptr, &shader_object);
               assert(!trigger_assert || SUCCEEDED(hr));
            }
            else if constexpr (typeid(T) == typeid(ID3D11VertexShader))
            {
               HRESULT hr = native_device->CreateVertexShader(custom_shader_cache->code.data(), custom_shader_cache->code.size(), nullptr, &shader_object);
               assert(!trigger_assert || SUCCEEDED(hr));
            }
            else if constexpr (typeid(T) == typeid(ID3D11PixelShader))
            {
               HRESULT hr = native_device->CreatePixelShader(custom_shader_cache->code.data(), custom_shader_cache->code.size(), nullptr, &shader_object);
               assert(!trigger_assert || SUCCEEDED(hr));
            }
            else if constexpr (typeid(T) == typeid(ID3D11ComputeShader))
            {
               HRESULT hr = native_device->CreateComputeShader(custom_shader_cache->code.data(), custom_shader_cache->code.size(), nullptr, &shader_object);
               assert(!trigger_assert || SUCCEEDED(hr));
            }
            else
            {
               static_assert(false);
            }
         }
      }
   }

   // Expects "s_mutex_loading"
   void CreateCustomDeviceShaders(DeviceData& device_data, std::optional<std::unordered_set<uint32_t>> shader_hashes_filter = std::nullopt, bool lock = true)
   {
      // Note that the hash can be "fake" on custom shaders, as we decide it trough the file names.
      if (lock) s_mutex_shader_objects.lock();
      CreateShaderObject(device_data.native_device, shader_hash_copy_vertex, device_data.copy_vertex_shader, shader_hashes_filter);
      CreateShaderObject(device_data.native_device, shader_hash_copy_pixel, device_data.copy_pixel_shader, shader_hashes_filter);
      CreateShaderObject(device_data.native_device, shader_hash_transform_function_copy_pixel, device_data.display_composition_pixel_shader, shader_hashes_filter);
      game->CreateShaderObjects(device_data, shader_hashes_filter);
      device_data.created_custom_shaders = true; // Some of the shader object creations above might have failed due to filtering, but they will likely be compiled soon after anyway
      if (lock) s_mutex_shader_objects.unlock();
   }

   // Compiles all the "custom" shaders we have in our shaders folder
   void CompileCustomShaders(DeviceData* optional_device_data = nullptr, bool warn_about_duplicates = false, const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>())
   {
      std::vector<std::string> shader_defines;
      // Cache them for consistency and to avoid threads from halting
      {
         const std::shared_lock lock(s_mutex_shader_defines);
         constexpr uint32_t cbuffer_defines = 3;
         constexpr uint32_t game_specific_defines = LUMA_GAME_SETTINGS_NUM + 1;
         constexpr uint32_t total_extra_defines = cbuffer_defines + game_specific_defines;
         shader_defines.assign((shader_defines_data.size() + total_extra_defines) * 2, "");

         size_t shader_defines_index = shader_defines.size() - (total_extra_defines * 2);
         
#ifdef LUMA_GAME_SETTING_01
         shader_defines[shader_defines_index++] = "LUMA_GAME_SETTING_01";
         shader_defines[shader_defines_index++] = DEFINE_VALUE_AS_STRING(LUMA_GAME_SETTING_01);
#endif
#ifdef LUMA_GAME_SETTING_02
         shader_defines[shader_defines_index++] = "LUMA_GAME_SETTING_02";
         shader_defines[shader_defines_index++] = DEFINE_VALUE_AS_STRING(LUMA_GAME_SETTING_02);
#endif
#ifdef LUMA_GAME_SETTING_03
         shader_defines[shader_defines_index++] = "LUMA_GAME_SETTING_03";
         shader_defines[shader_defines_index++] = DEFINE_VALUE_AS_STRING(LUMA_GAME_SETTING_03);
#endif
#ifdef LUMA_GAME_SETTING_04
         shader_defines[shader_defines_index++] = "LUMA_GAME_SETTING_04";
         shader_defines[shader_defines_index++] = DEFINE_VALUE_AS_STRING(LUMA_GAME_SETTING_04);
#endif
#ifdef LUMA_GAME_SETTING_05
         shader_defines[shader_defines_index++] = "LUMA_GAME_SETTING_05";
         shader_defines[shader_defines_index++] = DEFINE_VALUE_AS_STRING(LUMA_GAME_SETTING_05);
#endif

         // Clean up the game name from non letter characters (including spaces), and make it all upper case
         std::string game_name = Globals::GAME_NAME;
			RemoveNonLetterOrNumberCharacters(game_name.data(), '_'); // Ideally we should remove all weird characters and turn spaces into underscores
         std::transform(game_name.begin(), game_name.end(), game_name.begin(),
            [](unsigned char c) { return std::toupper(c); });
         shader_defines[shader_defines_index++] = "GAME_" + game_name;
         shader_defines[shader_defines_index++] = "1";

         // Define 3 shader cbuffers indexes (e.g. "(b13)")
         // We automatically generate unique values for each cbuffer to make sure they don't overlap.
         // This is because in case the users disabled some of them, we don't want them to bother to
         // define unique indexes for each of them, but the shader compiler fails if two cbuffers have the same value,
         // so we have to find the "next" unique one.
         uint32_t luma_settings_cbuffer_define_index, luma_data_cbuffer_define_index, luma_ui_cbuffer_define_index;
         std::set<uint32_t> excluded_values;
         if (luma_settings_cbuffer_index <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1)
            luma_settings_cbuffer_define_index = luma_settings_cbuffer_index;
         else
            luma_settings_cbuffer_define_index = FindNextUniqueNumberInRange(D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT / 2, 0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1, excluded_values);
         excluded_values.emplace(luma_settings_cbuffer_define_index);
         if (luma_data_cbuffer_index <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1)
            luma_data_cbuffer_define_index = luma_data_cbuffer_index;
         else
            luma_data_cbuffer_define_index = FindNextUniqueNumberInRange(D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT / 2 - 1, 0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1, excluded_values);
         excluded_values.emplace(luma_data_cbuffer_define_index);
         if (luma_ui_cbuffer_index <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1)
            luma_ui_cbuffer_define_index = luma_ui_cbuffer_index;
         else
            luma_ui_cbuffer_define_index = FindNextUniqueNumberInRange(D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT / 2 - 2, 0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1, excluded_values);
         excluded_values.emplace(luma_ui_cbuffer_define_index);

         shader_defines[shader_defines_index++] = "LUMA_SETTINGS_CB_INDEX";
         shader_defines[shader_defines_index++] = "b" + std::to_string(luma_settings_cbuffer_define_index);
         shader_defines[shader_defines_index++] = "LUMA_DATA_CB_INDEX";
         shader_defines[shader_defines_index++] = "b" + std::to_string(luma_data_cbuffer_define_index);
         shader_defines[shader_defines_index++] = "LUMA_UI_DATA_CB_INDEX";
         shader_defines[shader_defines_index++] = "b" + std::to_string(luma_ui_cbuffer_define_index);

			ASSERT_ONCE(shader_defines_index == shader_defines.size());

         for (uint32_t i = 0; i < shader_defines_data.size(); i++)
         {
            shader_defines[(i * 2)] = shader_defines_data[i].compiled_data.name;
            shader_defines[(i * 2) + 1] = shader_defines_data[i].compiled_data.value;
         }
      }

      // We need to clear this every time "CompileCustomShaders()" is called as we can't clear previous logs from it. We do this even if we have some "pipelines_filter"
      {
         const std::unique_lock lock(s_mutex_loading);
         shaders_compilation_errors.clear();
      }

      const auto directory = GetShadersRootPath();
      bool shaders_directory_created_or_empty = false;
      if (!std::filesystem::exists(directory))
      {
         if (!std::filesystem::create_directories(directory))
         {
            const std::unique_lock lock(s_mutex_loading);
            shaders_compilation_errors = "Cannot find nor create shaders directory";
            return;
         }
         shaders_directory_created_or_empty = true;
      }
      else if (!std::filesystem::is_directory(directory))
      {
         const std::unique_lock lock(s_mutex_loading);
         shaders_compilation_errors = "The shaders path is already taken by a file";
         return;
      }
      else if (std::filesystem::is_empty(directory))
      {
         shaders_directory_created_or_empty = true;
      }

      if (pipelines_filter.empty())
      {
         const std::unique_lock lock_shader_defines(s_mutex_shader_defines);

         code_shaders_defines.clear();
#if DEVELOPMENT
         const auto prev_cb_luma_frame_dev_settings_default_value = cb_luma_frame_dev_settings_default_value;
         cb_luma_frame_dev_settings_default_value = LumaFrameDevSettings(0.f);
         cb_luma_frame_dev_settings_min_value = LumaFrameDevSettings(0.f);
         cb_luma_frame_dev_settings_max_value = LumaFrameDevSettings(1.f);
         cb_luma_frame_dev_settings_names = {};
#endif

         // Add the global (generic) include and the game specific one
         auto settings_directories = { directory / "Includes" / "Settings.hlsl", directory / Globals::GAME_NAME / "Includes" / "Settings.hlsl" };
         bool is_global_settings = true;
         for (auto settings_directory : settings_directories)
         {
            if (std::filesystem::is_regular_file(settings_directory))
            {
               try
               {
                  std::ifstream file;
                  file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
                  file.open(settings_directory.c_str()); // Open file
                  std::stringstream str_stream;
                  str_stream << file.rdbuf(); // Read the file (we could use "D3DReadFileToBlob" to append the hsls includes in the file, but then stuff that is defined out wouldn't be part of the text)
                  std::string str = str_stream.str(); // str holds the content of the file
                  size_t i = -1;
                  int settings_count = 0;
                  while (true)
                  {
                     // Iterate the string line (break) by line (break),
                     // and check for defines values.

                     size_t i0 = i + 1;
                     i = str.find('\n', i0);
                     bool finished = false;
                     if (i0 == i) continue;
                     if (i == std::string::npos)
                     {
                        i = str.length();
                        finished = true;
                     }

                     // TODO: make this more flexible, allowing spaces around "#" and "define" etc,
                     // and defines values that are not numerical (from 0 to 9)
                     std::string_view str_view(&str[i0], i - i0);
                     if (str_view.rfind("#define ", 0) == 0)
                     {
                        str_view = str_view.substr(strlen("#define "));
                        size_t space_index = str_view.find(' ');
                        if (space_index != std::string::npos)
                        {
                           std::string_view define_name = str_view.substr(0, space_index);
                           size_t second_space_index = str_view.find(' ', space_index);
                           if (second_space_index != std::string::npos)
                           {
                              std::string_view define_value = str_view.substr(space_index + 1, second_space_index);
                              uint8_t define_int_value = define_value[0] - '0';
                              if (define_int_value <= 9)
                              {
                                 code_shaders_defines.emplace(define_name, define_int_value);
                              }
                           }
                        }
                     }
#if DEVELOPMENT
                     // Reflections on dev settings.
                     // They can have a comment like "// Default, Min, Max, Name" next to them (e.g. "// 0.5, 0, 1.3, Custom Name").
                     if (is_global_settings && str_view.find("float DevSetting") != std::string::npos)
                     {
                        if (settings_count >= LumaFrameDevSettings::SettingsNum) continue;
                        settings_count++;
                        const auto meta_data_pos = str_view.find("//");
                        if (meta_data_pos == std::string::npos) continue;
                        i0 += meta_data_pos + 2;
                        std::string str_line(&str[i0], i - i0);
                        std::stringstream ss(str_line);
                        if (!ss.good()) continue;

                        int settings_float_count = 0;
                        float str_float;
                        bool reached_end = false;
                        while (ss.peek() == ' ')
                        {
                           ss.ignore();
                           if (!ss.good()) { reached_end = true; break; }
                        }
                        // The float read would seemengly advance some state in the stream buffer even if it failed finding it, so skip it in case the next value is not a number (ignore ".3f" like definitions...).
                        // Float heading spaces are automatically ignored.
                        while (!reached_end && ss.peek() >= '0' && ss.peek() <= '9' && ss >> str_float)
                        {
                           if (settings_float_count == 0) cb_luma_frame_dev_settings_default_value[settings_count - 1] = str_float;
                           else if (settings_float_count == 1) cb_luma_frame_dev_settings_min_value[settings_count - 1] = str_float;
                           else if (settings_float_count == 2) cb_luma_frame_dev_settings_max_value[settings_count - 1] = str_float;
                           settings_float_count++;
                           if (!ss.good()) { reached_end = true; break; };
                           // Remove known (supported) characters to ignore (spaces are already ignored above anyway)
                           while (ss.peek() == ',' || ss.peek() == ' ')
                           {
                              ss.ignore();
                              if (!ss.good()) { reached_end = true; break; }
                           }
                        }

                        std::string str;
                        auto ss_pos = ss.tellg();
                        // If we found a string, read the whole remaining stream buffer, otherwise the "str" string would end at the first space
                        if (!reached_end && ss >> str)
                        {
                           cb_luma_frame_dev_settings_names[settings_count - 1] = ss.str();
                           cb_luma_frame_dev_settings_names[settings_count - 1] = cb_luma_frame_dev_settings_names[settings_count - 1].substr(ss_pos, cb_luma_frame_dev_settings_names[settings_count - 1].length() - ss_pos);
                        }
                     }
   #endif

                     if (finished) break;
                  }
               }
               catch (const std::exception& e)
               {
               }
#if DEVELOPMENT
               // Re-apply the default settings if they changed
               if (is_global_settings && memcmp(&cb_luma_frame_dev_settings_default_value, &prev_cb_luma_frame_dev_settings_default_value, sizeof(cb_luma_frame_dev_settings_default_value)) != 0)
               {
                  const std::unique_lock lock_reshade(s_mutex_reshade);
                  cb_luma_frame_settings.DevSettings = cb_luma_frame_dev_settings_default_value;
               }
#endif
            }
            else
            {
               ASSERT_ONCE(shaders_directory_created_or_empty || !is_global_settings); // Missing "Settings.hlsl" file (the game specific one doesn't need to exist) (ignored if we just created the shaders folder)
            }
            is_global_settings = false; // Only the first instance is the global settings
         }
      }

      std::unordered_set<uint32_t> changed_shaders_hashes;
      for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
      {
         const auto& entry_path = entry.path();
         if (!IsValidShadersSubPath(directory, entry_path))
         {
            continue;
         }
         if (!entry.is_regular_file())
         {
            reshade::log::message(reshade::log::level::warning, "LoadCustomShaders(not a regular file)");
            continue;
         }
         const bool is_hlsl = entry_path.extension().compare(".hlsl") == 0;
         const bool is_cso = entry_path.extension().compare(".cso") == 0;
         if (!entry_path.has_extension() || !entry_path.has_stem() || (!is_hlsl && !is_cso))
         {
            std::stringstream s;
            s << "LoadCustomShaders(Missing extension or stem or unknown extension: ";
            s << entry_path.string();
            s << ")";
            reshade::log::message(reshade::log::level::warning, s.str().c_str());
            continue;
         }

         const auto filename_no_extension_string = entry_path.stem().string();
         std::vector<std::string> hash_strings;
         std::string shader_target;

         if (is_hlsl)
         {
            auto length = filename_no_extension_string.length();
            if (length < strlen("0x12345678.xx_x_x")) continue;
            ASSERT_ONCE(length > strlen("0x12345678.xx_x_x")); // HLSL files are expected to have a name in front of the hash. They can still be loaded, but they won't be distinguishable from raw cso files
            shader_target = filename_no_extension_string.substr(length - strlen("xx_x_x"), strlen("xx_x_x"));
            if (shader_target[2] != '_') continue;
            if (shader_target[4] != '_') continue;
            size_t next_hash_pos = filename_no_extension_string.find("0x");
            if (next_hash_pos == std::string::npos) continue;
            do
            {
               hash_strings.push_back(filename_no_extension_string.substr(next_hash_pos + 2 /*0x*/, HASH_CHARACTERS_LENGTH));
               next_hash_pos = filename_no_extension_string.find("0x", next_hash_pos + 1);
            } while (next_hash_pos != std::string::npos);
         }
         else if (is_cso)
         {
            // As long as cso starts from "0x12345678", it's good, they don't need the shader type specified
            if (filename_no_extension_string.size() < 10)
            {
               std::stringstream s;
               s << "LoadCustomShaders(Invalid cso file format: ";
               s << filename_no_extension_string;
               s << ")";
               reshade::log::message(reshade::log::level::warning, s.str().c_str());
               continue;
            }
            hash_strings.push_back(filename_no_extension_string.substr(2, HASH_CHARACTERS_LENGTH));

            // Only directly load the cso if no hlsl by the same name exists,
            // which implies that we either did not ship the hlsl and shipped the pre-compiled cso(s),
            // or that this is a vanilla cso dumped from the game.
            // If the hlsl also exists, we load the cso though the hlsl code loading below (by redirecting it).
            // 
            // Note that if we have two shaders with the same hash but different overall name (e.g. the description part of the shader),
            // whichever is iterated last will load on top of the previous one (whether they are both cso, or cso and hlsl etc).
            // We don't care about "fixing" that because it's not a real world case.
            const auto filename_hlsl_string = filename_no_extension_string + ".hlsl";
            if (!std::filesystem::exists(filename_hlsl_string)) continue;
         }
         // Any other case (non hlsl non cso) is already earlied out above

         for (const auto& hash_string : hash_strings)
         {
            uint32_t shader_hash;
            try
            {
               shader_hash = Shader::Hash_StrToNum(hash_string);
            }
            catch (const std::exception& e)
            {
               continue;
            }

            // Early out before compiling
            ASSERT_ONCE(pipelines_filter.empty() || optional_device_data); // We can't apply a filter if we didn't pass in the "DeviceData"
            if (!pipelines_filter.empty() && optional_device_data)
            {
               const std::shared_lock lock(s_mutex_generic);
               bool pipeline_found = false;
               for (const auto& pipeline_pair : optional_device_data->pipeline_cache_by_pipeline_handle)
               {
                  if (std::find(pipeline_pair.second->shader_hashes.begin(), pipeline_pair.second->shader_hashes.end(), shader_hash) == pipeline_pair.second->shader_hashes.end()) continue;
                  if (pipelines_filter.contains(pipeline_pair.first))
                  {
                     pipeline_found = true;
                  }
                  break;
               }
               if (!pipeline_found)
               {
                  continue;
               }
            }

            // Add defines to specify the current "target" hash we are building the shader with (some shaders can share multiple permutations (hashes) within the same hlsl)
            std::vector<std::string> local_shader_defines = shader_defines;
            local_shader_defines.push_back("_" + hash_string);
            local_shader_defines.push_back("1");

            char config_name[std::string_view("Shader#").size() + HASH_CHARACTERS_LENGTH + 1] = "";
            sprintf(&config_name[0], "Shader#%s", hash_string.c_str());

            const std::unique_lock lock(s_mutex_loading); // Don't lock until now as we didn't access any shared data
            auto& custom_shader = custom_shaders_cache[shader_hash]; // Add default initialized shader
            const bool has_custom_shader = (custom_shaders_cache.find(shader_hash) != custom_shaders_cache.end()) && (custom_shader != nullptr); // Weird code...
            std::wstring original_file_path_cso; // Only valid for hlsl files
            std::wstring trimmed_file_path_cso; // Only valid for hlsl files

            if (is_hlsl)
            {
               std::wstring file_path_cso = entry_path.c_str();
               std::wstring hash_wstring = std::wstring(hash_string.begin(), hash_string.end());
               if (file_path_cso.ends_with(L".hlsl"))
               {
                  file_path_cso = file_path_cso.substr(0, file_path_cso.size() - 5);
                  file_path_cso += L".cso";
               }
               else if (!file_path_cso.ends_with(L".cso"))
               {
                  file_path_cso += L".cso";
               }
               original_file_path_cso = file_path_cso;

               size_t first_hash_pos = file_path_cso.find(L"0x");
               if (first_hash_pos != std::string::npos)
               {
                  // Remove all the non first shader hashes in the file (and anything in between them),
                  // we then replace the first hash with our target one
                  size_t prev_hash_pos = first_hash_pos;
                  size_t next_hash_pos = file_path_cso.find(L"0x", prev_hash_pos + 1);
                  while (next_hash_pos != std::string::npos && (file_path_cso.length() - next_hash_pos) >= 10)
                  {
                     file_path_cso = file_path_cso.substr(0, prev_hash_pos + 10) + file_path_cso.substr(next_hash_pos + 10);
                     prev_hash_pos = first_hash_pos;
                     next_hash_pos = file_path_cso.find(L"0x", prev_hash_pos + 1);
                  }
                  file_path_cso.replace(first_hash_pos + 2 /*0x*/, HASH_CHARACTERS_LENGTH, hash_wstring.c_str());
               }
               trimmed_file_path_cso = file_path_cso;
            }

            if (!has_custom_shader)
            {
               custom_shader = new CachedCustomShader();

               std::size_t preprocessed_hash = custom_shader->preprocessed_hash;
               // Note that if anybody manually changed the config hash, the data here could mismatch and end up recompiling when not needed or skipping recompilation even if needed (near impossible chance)
               const bool should_load_compiled_shader = is_hlsl && !prevent_shader_cache_loading; // If this shader doesn't have an hlsl, we should never read it or save it on disk, there's no need (we can still fall back on the original .cso if needed)
               if (should_load_compiled_shader && reshade::get_config_value(nullptr, NAME_ADVANCED_SETTINGS.c_str(), &config_name[0], preprocessed_hash))
               {
                  // This will load the matching cso
                  // TODO: move these to a "Bin" sub folder called "cache"? It'd make everything cleaner (and the "CompileCustomShaders()" could simply nuke a directory then, and we could remove the restriction where hlsl files need to have a name in front of the hash),
                  // but it would make it harder to manually remove a single specific shader cso we wanted to nuke for test reasons (especially if we exclusively put the hash in their cso name).
                  // Also it would be a problem due to the custom "native" shaders we have (e.g. "copy") that don't have a target hash they are replacing.
                  if (Shader::LoadCompiledShaderFromFile(custom_shader->code, trimmed_file_path_cso.c_str()))
                  {
                     // If both reading the pre-processor hash from config and the compiled shader from disk succeeded, then we are free to continue as if this shader was working
                     custom_shader->file_path = entry_path;
                     custom_shader->is_hlsl = is_hlsl;
                     custom_shader->preprocessed_hash = preprocessed_hash;
                     changed_shaders_hashes.emplace(shader_hash);
                     // Theoretically at this point, the shader pre-processor below should skip re-compiling this shader unless the hash changed
                  }
               }
            }
            else if (warn_about_duplicates)
            {
               warn_about_duplicates = false;
#if !DEVELOPMENT
               const std::string warn_message = "It seems like you have duplicate shaders in your \"" + std::string(NAME) + "\" folder, please delete it and re-apply the files from the latest version of the mod.";
               MessageBoxA(game_window, warn_message.c_str(), NAME, MB_SETFOREGROUND);
#endif
            }

            CComPtr<ID3DBlob> uncompiled_code_blob;

            if (is_hlsl)
            {
               constexpr bool compile_from_current_path = false; // Set this to true to include headers from the current directory instead of the file root folder

               const auto previous_path = std::filesystem::current_path();
               if (compile_from_current_path)
               {
                  // Set the current path to the shaders directory, it can be needed by the DX compilers (specifically by the preprocess functions)
                  std::filesystem::current_path(directory);
               }

               std::string compilation_errors;

               // Skip compiling the shader if it didn't change
               // Note that this won't replace "custom_shader->compilation_error" unless there was any new error/warning, and that's kind of what we want
               // Note that this will not try to build the shader again if the last compilation failed and its files haven't changed
               bool error = false;
               std::string preprocessed_code;
					std::string* preprocessed_code_ref = &preprocessed_code;
#if DEVELOPMENT
               preprocessed_code_ref = &custom_shader->preprocessed_code;
#endif
               const bool needs_compilation = Shader::PreprocessShaderFromFile(entry_path.c_str(), compile_from_current_path ? entry_path.filename().c_str() : entry_path.c_str(), shader_target.c_str(), *preprocessed_code_ref, custom_shader->preprocessed_hash, uncompiled_code_blob, local_shader_defines, error, &compilation_errors);

               // Only overwrite the previous compilation error if we have any preprocessor errors
               if (!compilation_errors.empty() || error)
               {
                  custom_shader->compilation_errors = compilation_errors;
#if DEVELOPMENT || TEST
                  custom_shader->compilation_error = error;
#endif
#if !DEVELOPMENT && !TEST // Ignore warnings for public builds
                  if (error)
#endif
                  {
                     shaders_compilation_errors.append(filename_no_extension_string);
                     shaders_compilation_errors.append(": ");
                     shaders_compilation_errors.append(compilation_errors);
                  }
               }
               // Print out the same (last) compilation errors again if the shader still needs to be compiled but hasn't changed.
               // We might want to ignore this case for public builds (we can't know whether this was an error or a warning atm),
               // but it seems like this can only trigger after a shader had previous failed to build, so these should be guaranteed to be errors,
               // and thus we should be able to print them to all users (we don't want warnings in public builds).
               else if (!needs_compilation && custom_shader->code.size() == 0 && !custom_shader->compilation_errors.empty())
               {
                  shaders_compilation_errors.append(filename_no_extension_string);
                  shaders_compilation_errors.append(": ");
                  shaders_compilation_errors.append(custom_shader->compilation_errors);
               }

               if (compile_from_current_path)
               {
                  // Restore it to avoid unknown consequences
                  std::filesystem::current_path(previous_path);
               }

               if (!needs_compilation)
               {
                  continue;
               }
            }

            // If we reached this place, we can consider this shader as "changed" even if it will fail compiling.
            // We don't care to avoid adding duplicate elements to this list.
            changed_shaders_hashes.emplace(shader_hash);

            // For extra safety, just clear everything that will be re-assigned below if this custom shader already existed
            if (has_custom_shader)
            {
               auto preprocessed_hash = custom_shader->preprocessed_hash;
#if DEVELOPMENT || TEST
               auto preprocessed_code = custom_shader->preprocessed_code;
#endif
               ClearCustomShader(shader_hash);
               // Keep the data we just filled up
               custom_shader->preprocessed_hash = preprocessed_hash;
#if DEVELOPMENT || TEST
               custom_shader->preprocessed_code = preprocessed_code;
#endif
            }
            custom_shader->file_path = entry_path;
            custom_shader->is_hlsl = is_hlsl;
            // Clear these in case the compiler didn't overwrite them
            custom_shader->code.clear();
            custom_shader->compilation_errors.clear();
#if DEVELOPMENT || TEST
            custom_shader->compilation_error = false;
#endif

            if (is_hlsl)
            {
#if _DEBUG && LOG_VERBOSE
               {
                  std::stringstream s;
                  s << "LoadCustomShaders(Compiling file: ";
                  s << entry_path.string();
                  s << ", hash: " << PRINT_CRC32(shader_hash);
                  s << ", target: " << shader_target;
                  s << ")";
                  reshade::log::message(reshade::log::level::debug, s.str().c_str());
               }
#endif

               bool error = false;
               // TODO: specify the name of the function to compile (e.g. "main" or HDRTonemapPS) so we could unify more shaders into a single file with multiple techniques?
               Shader::CompileShaderFromFile(
                  custom_shader->code,
                  uncompiled_code_blob,
                  entry_path.c_str(),
                  shader_target.c_str(),
                  local_shader_defines,
                  // Save to disk for faster loading after the first compilation
                  !prevent_shader_cache_saving,
                  error,
                  &custom_shader->compilation_errors,
                  trimmed_file_path_cso.c_str());
               ASSERT_ONCE(!trimmed_file_path_cso.empty()); // If we got here, this string should always be valid, as it means the shader read from disk was an hlsl

               // Ugly workaround to avoid providing the shader compiler a custom name for CSO files, given we trim their name from multiple hashes that the HLSL original path might have
               if (!prevent_shader_cache_saving && !original_file_path_cso.empty() && original_file_path_cso != trimmed_file_path_cso)
               {
                  if (std::filesystem::is_regular_file(original_file_path_cso))
                  {
                     ASSERT_ONCE(false); // This shouldn't happen anymore unless the shader was manually created or named
                     std::filesystem::remove(trimmed_file_path_cso);
                     std::filesystem::rename(original_file_path_cso, trimmed_file_path_cso);
                  }
               }

               if (!custom_shader->compilation_errors.empty())
               {
#if DEVELOPMENT || TEST
                  custom_shader->compilation_error = error;
#endif
#if !DEVELOPMENT && !TEST // Ignore warnings for public builds
                  if (error)
#endif
                  {
                     shaders_compilation_errors.append(filename_no_extension_string);
                     shaders_compilation_errors.append(": ");
                     shaders_compilation_errors.append(custom_shader->compilation_errors);
                  }
               }

               if (custom_shader->code.empty())
               {
                  std::stringstream s;
                  s << "LoadCustomShaders(Compilation failed: ";
                  s << entry_path.string();
                  s << ")";
                  reshade::log::message(reshade::log::level::warning, s.str().c_str());

                  continue;
               }
               // Save the matching the pre-compiled shader hash in the config, so we can skip re-compilation on the next boot
               else if (!prevent_shader_cache_saving)
               {
                  reshade::set_config_value(nullptr, NAME_ADVANCED_SETTINGS.c_str(), &config_name[0], custom_shader->preprocessed_hash);
               }

#if _DEBUG && LOG_VERBOSE
               {
                  std::stringstream s;
                  s << "LoadCustomShaders(Shader built with size: " << custom_shader->code.size() << ")";
                  reshade::log::message(reshade::log::level::debug, s.str().c_str());
               }
#endif
            }
            else if (is_cso)
            {
               try
               {
                  std::ifstream file;
                  file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
                  file.open(entry_path, std::ios::binary);
                  file.seekg(0, std::ios::end);
                  custom_shader->code.resize(file.tellg());
#if _DEBUG && LOG_VERBOSE
                  {
                     std::stringstream s;
                     s << "LoadCustomShaders(Reading " << custom_shader->code.size() << " from " << filename_no_extension_string << ")";
                     reshade::log::message(reshade::log::level::debug, s.str().c_str());
                  }
#endif
                  if (!custom_shader->code.empty())
                  {
                     file.seekg(0, std::ios::beg);
                     file.read(reinterpret_cast<char*>(custom_shader->code.data()), custom_shader->code.size());
                  }
               }
               catch (const std::exception& e)
               {
               }
            }
         }
      }

      // TODO: theoretically if "prevent_shader_cache_saving" is true, we should clean all the shader hashes and defines from the config, though hopefully it's fine without
      if (pipelines_filter.empty() && !prevent_shader_cache_saving)
      {
         const std::shared_lock lock(s_mutex_shader_defines);
         // Only save after compiling, to make sure the config data aligns with the serialized compiled shaders data (blobs)
         ShaderDefineData::Save(shader_defines_data, NAME_ADVANCED_SETTINGS);
      }

      // Refresh the persistent custom shaders we have.
      if (optional_device_data)
      {
         CreateCustomDeviceShaders(*optional_device_data, changed_shaders_hashes);
      }
   }

   // Optionally compiles all the shaders we have in our data folder and links them with the game rendering pipelines
   void LoadCustomShaders(DeviceData& device_data, const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>(), bool recompile_shaders = true, bool immediate_unload = false)
   {
#if _DEBUG && LOG_VERBOSE
      reshade::log::message(reshade::log::level::info, "LoadCustomShaders()");
#endif

      if (recompile_shaders)
      {
         CompileCustomShaders(&device_data, false, pipelines_filter);
      }

      // We can, and should, only lock this after compiling new shaders (above)
      const std::unique_lock lock(s_mutex_generic);

      // Clear all previously loaded custom shaders
      UnloadCustomShaders(device_data, pipelines_filter, immediate_unload, false);

      std::unordered_set<uint64_t> cloned_pipelines;

      const std::unique_lock lock_loading(s_mutex_loading);
      for (const auto& custom_shader_pair : custom_shaders_cache)
      {
         uint32_t shader_hash = custom_shader_pair.first;
         const auto custom_shader = custom_shaders_cache[shader_hash];

         // Skip shaders that don't have code binaries at the moment
         if (custom_shader == nullptr || custom_shader->code.empty()) continue;

         auto pipelines_pair = device_data.pipeline_caches_by_shader_hash.find(shader_hash);
         if (pipelines_pair == device_data.pipeline_caches_by_shader_hash.end())
         {
            std::stringstream s;
            s << "LoadCustomShaders(Unknown hash: ";
            s << PRINT_CRC32(shader_hash);
            s << ")";
            reshade::log::message(reshade::log::level::warning, s.str().c_str());
            continue;
         }

         // Re-clone all the pipelines that used this shader hash (except the ones that are filtered out)
         for (CachedPipeline* cached_pipeline : pipelines_pair->second)
         {
            if (cached_pipeline == nullptr) continue;
            if (!pipelines_filter.empty() && !pipelines_filter.contains(cached_pipeline->pipeline.handle)) continue;
            if (cloned_pipelines.contains(cached_pipeline->pipeline.handle)) { assert(false); continue; }
            cloned_pipelines.emplace(cached_pipeline->pipeline.handle);
            // Force destroy this pipeline in case it was already cloned
            UnloadCustomShaders(device_data, { cached_pipeline->pipeline.handle }, immediate_unload, false);

#if _DEBUG && LOG_VERBOSE
            {
               std::stringstream s;
               s << "LoadCustomShaders(Read ";
               s << custom_shader->code.size() << " bytes ";
               s << " from " << custom_shader->file_path.string();
               s << ")";
               reshade::log::message(reshade::log::level::debug, s.str().c_str());
            }
#endif

            // DX12 can use PSO objects that need to be cloned
            const uint32_t subobject_count = cached_pipeline->subobject_count;
            reshade::api::pipeline_subobject* subobjects = cached_pipeline->subobjects_cache;
            reshade::api::pipeline_subobject* new_subobjects = Shader::ClonePipelineSubobjects(subobject_count, subobjects);

#if _DEBUG && LOG_VERBOSE
            {
               std::stringstream s;
               s << "LoadCustomShaders(Cloning pipeline ";
               s << reinterpret_cast<void*>(cached_pipeline->pipeline.handle);
               s << " with " << subobject_count << " object(s)";
               s << ")";
               reshade::log::message(reshade::log::level::debug, s.str().c_str());
            }
            reshade::log::message(reshade::log::level::debug, "Iterating pipeline...");
#endif

            for (uint32_t i = 0; i < subobject_count; ++i)
            {
               const auto& subobject = subobjects[i];
               switch (subobject.type)
               {
               case reshade::api::pipeline_subobject_type::geometry_shader:
               [[fallthrough]];
               case reshade::api::pipeline_subobject_type::vertex_shader:
               [[fallthrough]];
               case reshade::api::pipeline_subobject_type::compute_shader:
               [[fallthrough]];
               case reshade::api::pipeline_subobject_type::pixel_shader:
               break;
               default:
               continue;
               }

               auto& clone_subject = new_subobjects[i];

               auto* new_desc = static_cast<reshade::api::shader_desc*>(clone_subject.data);

               new_desc->code_size = custom_shader->code.size();
               new_desc->code = malloc(custom_shader->code.size());
               std::memcpy(const_cast<void*>(new_desc->code), custom_shader->code.data(), custom_shader->code.size());

               const auto new_hash = compute_crc32(static_cast<const uint8_t*>(new_desc->code), new_desc->code_size);

#if _DEBUG && LOG_VERBOSE
               {
                  std::stringstream s;
                  s << "LoadCustomShaders(Injected pipeline data";
                  s << " with " << PRINT_CRC32(new_hash);
                  s << " (" << custom_shader->code.size() << " bytes)";
                  s << ")";
                  reshade::log::message(reshade::log::level::debug, s.str().c_str());
               }
#endif
            }

#if _DEBUG && LOG_VERBOSE
            {
               std::stringstream s;
               s << "Creating pipeline clone (";
               s << "hash: " << PRINT_CRC32(shader_hash);
               s << ", layout: " << reinterpret_cast<void*>(cached_pipeline->layout.handle);
               s << ", subobject_count: " << subobject_count;
               s << ")";
               reshade::log::message(reshade::log::level::debug, s.str().c_str());
            }
#endif

            reshade::api::pipeline pipeline_clone;
            const bool built_pipeline_ok = cached_pipeline->device->create_pipeline(
               cached_pipeline->layout,
               subobject_count,
               new_subobjects,
               &pipeline_clone);
#if !_DEBUG || !LOG_VERBOSE
            if (!built_pipeline_ok)
#endif
            {
               std::stringstream s;
               s << "LoadCustomShaders(cloned ";
               s << reinterpret_cast<void*>(cached_pipeline->pipeline.handle);
               s << " => " << reinterpret_cast<void*>(pipeline_clone.handle);
               s << ", layout: " << reinterpret_cast<void*>(cached_pipeline->layout.handle);
               s << ", size: " << subobject_count;
               s << ", " << (built_pipeline_ok ? "OK" : "FAILED!");
               s << ")";
               reshade::log::message(built_pipeline_ok ? reshade::log::level::info : reshade::log::level::error, s.str().c_str());
            }

            if (built_pipeline_ok)
            {
               assert(!cached_pipeline->cloned && cached_pipeline->pipeline_clone.handle == 0);
               cached_pipeline->pipeline_clone = pipeline_clone;
               cached_pipeline->cloned = true;
               device_data.pipeline_cache_by_pipeline_clone_handle[pipeline_clone.handle] = cached_pipeline;
               device_data.cloned_pipeline_count++;
               device_data.cloned_pipelines_changed = true;
            }
            // Clean up unused cloned subobjects
            else
            {
               DestroyPipelineSubojects(new_subobjects, subobject_count);
               new_subobjects = nullptr;
            }
         }
      }
   }

   void OnDisplayModeChanged()
   {
      // s_mutex_reshade should already be locked here, it's not necessary anyway
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).editable = cb_luma_frame_settings.DisplayMode != 0; //TODOFT4: necessary to disable this in SDR?

      game->OnDisplayModeChanged();
   }

   void OnInitDevice(reshade::api::device* device)
   {
      ASSERT_ONCE(device->get_api() == reshade::api::device_api::d3d11);

      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      DeviceData& device_data = *device->create_private_data<DeviceData>();
      device_data.native_device = native_device;

#if DEVELOPMENT
      native_device->GetImmediateContext(&device_data.primary_command_list);
      device_data.primary_command_list->Release(); // No need keep a strong reference in memory, this object will be kept alive anyway
#endif // DEVELOPMENT

      {
         const std::unique_lock lock(s_mutex_device);
         // Inherit a minimal set of states from the possible previous device. Any other state wouldn't be relevant or could be outdated, so we might as well reset all to default.
         if (!global_devices_data.empty())
         {
            device_data.output_resolution = global_devices_data[0]->output_resolution;
            device_data.render_resolution = global_devices_data[0]->render_resolution;
            device_data.previous_render_resolution = global_devices_data[0]->render_resolution;
         }
         // In case there already was a device, we could copy some states from it, but given that the previous device might still be rendering a frame and is in a "random" state, let's keep them completely independent
         global_native_devices.push_back(native_device);
         global_devices_data.push_back(&device_data);
      }

      game->OnCreateDevice(native_device, device_data);

      HRESULT hr;

      D3D11_BUFFER_DESC buffer_desc = {};
      // From MS docs: you must set the ByteWidth value of D3D11_BUFFER_DESC in multiples of 16, and less than or equal to D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT.
      buffer_desc.ByteWidth = sizeof(LumaFrameSettings);
      buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
      buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      D3D11_SUBRESOURCE_DATA data = {};
      {
         const std::unique_lock lock_reshade(s_mutex_reshade);
         data.pSysMem = &cb_luma_frame_settings;
         hr = native_device->CreateBuffer(&buffer_desc, &data, &device_data.luma_frame_settings);
         device_data.cb_luma_frame_settings_dirty = false;
      }
      assert(SUCCEEDED(hr));
      if (luma_data_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
      {
         buffer_desc.ByteWidth = sizeof(LumaInstanceData);
         data.pSysMem = &device_data.cb_luma_instance_data;
         hr = native_device->CreateBuffer(&buffer_desc, &data, &device_data.luma_instance_data);
         assert(SUCCEEDED(hr));
      }
      if (luma_ui_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
      {
         buffer_desc.ByteWidth = sizeof(LumaUIData);
         data.pSysMem = &device_data.cb_luma_ui_data;
         hr = native_device->CreateBuffer(&buffer_desc, &data, &device_data.luma_ui_data);
         assert(SUCCEEDED(hr));
      }

      D3D11_BLEND_DESC blend_desc = {};
      blend_desc.AlphaToCoverageEnable = FALSE;
      blend_desc.IndependentBlendEnable = FALSE;
      // We only need RT 0
      blend_desc.RenderTarget[0].BlendEnable = FALSE;
      blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
      blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
      blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
      blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
      blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
      blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
      blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
      hr = native_device->CreateBlendState(&blend_desc, &device_data.default_blend_state);
      assert(SUCCEEDED(hr));

      D3D11_DEPTH_STENCIL_DESC depth_stencil_desc = {};
      depth_stencil_desc.DepthEnable = false;
      depth_stencil_desc.StencilEnable = false;
      hr = native_device->CreateDepthStencilState(&depth_stencil_desc, &device_data.default_depth_stencil_state);
      assert(SUCCEEDED(hr));

      game->OnInitDevice(native_device, device_data);

#if ENABLE_NGX
      com_ptr<IDXGIDevice> native_dxgi_device;
      hr = native_device->QueryInterface(&native_dxgi_device);
      com_ptr<IDXGIAdapter> native_adapter;
      if (SUCCEEDED(hr))
      {
         hr = native_dxgi_device->GetAdapter(&native_adapter);
      }
      assert(SUCCEEDED(hr));

      // Inherit the default from the global user setting
      {
         const std::unique_lock lock_reshade(s_mutex_reshade);
         device_data.dlss_sr = dlss_sr;
      }
      // We always do this, which will force the DLSS dll to load, but it should be fast enough to not bother users from other vendors
      device_data.dlss_sr_supported = NGX::DLSS::Init(device_data.dlss_sr_handle, native_device, native_adapter.get());
      if (!device_data.dlss_sr_supported)
      {
         NGX::DLSS::Deinit(device_data.dlss_sr_handle, native_device); // No need to keep it initialized if it's not supported
         device_data.dlss_sr = false; // No need to serialize this to config really
         const std::unique_lock lock_reshade(s_mutex_reshade);
         dlss_sr = false; // Disable the global user setting if it's not supported (it's ok even if we pollute device and global data), we want to grey it out in the UI (there's no need to serialize the new value for it though!)
      }
#else
      device_data.dlss_sr = false;
      dlss_sr = false;
#endif // ENABLE_NGX

      // If all custom shaders from boot already loaded/compiled, but the custom device shaders weren't created, create them
      if (precompile_custom_shaders && block_draw_until_device_custom_shaders_creation)
      {
         const std::unique_lock lock_loading(s_mutex_loading);
         const std::unique_lock lock_shader_objects(s_mutex_shader_objects);
         if (!thread_auto_compiling_running && !device_data.created_custom_shaders)
         {
            CreateCustomDeviceShaders(device_data, std::nullopt, false);
         }
      }
   }

   void OnDestroyDevice(reshade::api::device* device)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      DeviceData& device_data = *device->get_private_data<DeviceData>(); // No need to lock the data mutex here, it could be concurrently used at this point

      game->OnDestroyDeviceData(device_data);

      {
         const std::unique_lock lock(s_mutex_device);
         if (std::vector<ID3D11Device*>::iterator position = std::find(global_native_devices.begin(), global_native_devices.end(), native_device); position != global_native_devices.end())
         {
            global_native_devices.erase(position);
         }
         else
         {
            ASSERT_ONCE(false);
         }
         if (std::vector<DeviceData*>::iterator position = std::find(global_devices_data.begin(), global_devices_data.end(), &device_data); position != global_devices_data.end())
         {
            global_devices_data.erase(position);
         }
         else
         {
            ASSERT_ONCE(false);
         }
      }

      ASSERT_ONCE(device_data.swapchains.empty()); // Hopefully this is forcefully garbage collected when the device is destroyed (it is!)

      if (device_data.thread_auto_loading.joinable())
      {
         device_data.thread_auto_loading.join();
         device_data.thread_auto_loading_running = false;
      }

      assert(device_data.cb_per_view_global_buffer_map_data == nullptr); // It's fine (but not great) if we map wasn't unmapped before destruction (not our fault anyway)

      {
         const std::unique_lock lock_samplers(s_mutex_samplers);
         ASSERT_ONCE(device_data.custom_sampler_by_original_sampler.empty()); // These should be guaranteed to have been cleared already ("OnDestroySampler()")
         device_data.custom_sampler_by_original_sampler.clear();
      }

#if ENABLE_NGX
      NGX::DLSS::Deinit(device_data.dlss_sr_handle, native_device); // NOTE: this could stutter the game on closure as it forces unloading the DLSS DLL (if it's the last device instance?), but we can't avoid it
#endif // ENABLE_NGX

      device->destroy_private_data<DeviceData>();
   }

   bool OnCreateSwapchain(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd)
   {
      // There's only one swapchain so it's fine if this is global ("OnInitSwapchain()" will always be called later anyway)
      bool changed = false;

      // sRGB formats don't support flip modes, if we previously upgraded the swapchain, select a flip mode compatible format when the swapchain resizes, as we can't change it anymore after creation
      if (!enable_swapchain_upgrade && swapchain_upgrade_type > 0 && (desc.back_buffer.texture.format == reshade::api::format::r8g8b8a8_unorm_srgb || desc.back_buffer.texture.format == reshade::api::format::b8g8r8a8_unorm_srgb))
      {
         if (desc.back_buffer.texture.format == reshade::api::format::r8g8b8a8_unorm_srgb)
            desc.back_buffer.texture.format = reshade::api::format::r8g8b8a8_unorm;
         else
            desc.back_buffer.texture.format = reshade::api::format::b8g8r8a8_unorm;
      }
      
      // Generally we want to add these flags in all cases, they seem to work in all games
      {
         desc.back_buffer_count = max(desc.back_buffer_count, 2); // Needed by flip models, which is mandatory for HDR (for some reason DX11 might still create one buffer)
         if ((enable_swapchain_upgrade && swapchain_upgrade_type > 0) || (desc.back_buffer.texture.format != reshade::api::format::r8g8b8a8_unorm_srgb && desc.back_buffer.texture.format != reshade::api::format::b8g8r8a8_unorm_srgb)) // sRGB formats don't support flip modes
         {
            desc.present_mode = DXGI_SWAP_EFFECT_FLIP_DISCARD;
         }
         desc.present_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // Games will still need to call "Present()" with the tearing flag enabled for this to do anything
         desc.present_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
         desc.fullscreen_refresh_rate = 0.f; // This fixes games forcing a specific refresh rate (e.g. Mafia III forces 60Hz for no reason)
         desc.fullscreen_state = false; // Force disable FSE (see "OnSetFullscreenState()")
         changed = true;
      }

#if GAME_THIEF && DEVELOPMENT && 0 // Force it back to the original format, given that this game keeps the last swapchain format when resizing the swapchain //TODOFT: Disabled as this crashes and anyway the game was already linear so it works nonetheless, swapchain resource creation fails
      if (!enable_swapchain_upgrade && swapchain_upgrade_type > 0)
      {
         desc.back_buffer.texture.format = reshade::api::format::r8g8b8a8_unorm_srgb;
         changed = true;
      }
#endif

      // Note that occasionally this breaks after resizing the swapchain, because some games resize the swapchain maintaining whatever format it had before
      last_swapchain_linear_space = desc.back_buffer.texture.format == reshade::api::format::r8g8b8a8_unorm_srgb || desc.back_buffer.texture.format == reshade::api::format::b8g8r8a8_unorm_srgb || desc.back_buffer.texture.format == reshade::api::format::r16g16b16a16_float;

      if (enable_swapchain_upgrade && swapchain_upgrade_type > 0)
      {
         ASSERT_ONCE(desc.back_buffer.texture.format == reshade::api::format::r10g10b10a2_unorm || desc.back_buffer.texture.format == reshade::api::format::r8g8b8a8_unorm || desc.back_buffer.texture.format == reshade::api::format::r8g8b8a8_unorm_srgb || desc.back_buffer.texture.format == reshade::api::format::r16g16b16a16_float); // Just a bounch of formats we encountered and we are sure we can upgrade (or that have already been upgraded)
         // DXGI_FORMAT_R16G16B16A16_FLOAT will automatically pick DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 on first creation
         desc.back_buffer.texture.format = reshade::api::format::r16g16b16a16_float;
         changed = true;
      }

      return changed;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize)
   {
      IDXGISwapChain* native_swapchain = (IDXGISwapChain*)(swapchain->get_native());
      const size_t back_buffer_count = swapchain->get_back_buffer_count();
      auto* device = swapchain->get_device();
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      SwapchainData& swapchain_data = *swapchain->create_private_data<SwapchainData>();
      ASSERT_ONCE(&device_data != nullptr); // Hacky nullptr check (should ever be able to happen)

      swapchain_data.vanilla_was_linear_space = last_swapchain_linear_space || game->ForceVanillaSwapchainLinear();
      // We expect this define to be set to linear if the swapchain was already linear in Vanilla SDR (there might be code that makes such assumption)
      ASSERT_ONCE(!swapchain_data.vanilla_was_linear_space || (GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) == 1));

      {
         const std::unique_lock lock(swapchain_data.mutex); // Not much need to lock this on its own creation, but let's do it anyway...
         for (uint32_t index = 0; index < back_buffer_count; index++)
         {
            auto buffer = swapchain->get_back_buffer(index);
            swapchain_data.back_buffers.emplace(buffer.handle);
            swapchain_data.display_composition_rtvs.push_back(nullptr);
         }
      }

      {
         const std::unique_lock lock(device_data.mutex);
         device_data.swapchains.emplace(swapchain);
         ASSERT_ONCE(SUCCEEDED(device_data.swapchains.size() == 1)); // Having more than one swapchain per device is probably supported but unexpected

         for (uint32_t index = 0; index < back_buffer_count; index++)
         {
            auto buffer = swapchain->get_back_buffer(index);
            device_data.back_buffers.emplace(buffer.handle);
         }
      }

      // We assume there's only one swapchain (there is!), given that the resolution would theoretically be by swapchain and not device
      DXGI_SWAP_CHAIN_DESC swapchain_desc;
      HRESULT hr = native_swapchain->GetDesc(&swapchain_desc);
      ASSERT_ONCE(SUCCEEDED(hr));
      if (SUCCEEDED(hr))
      {
         device_data.output_resolution.x = swapchain_desc.BufferDesc.Width;
         device_data.output_resolution.y = swapchain_desc.BufferDesc.Height;
         device_data.render_resolution.x = device_data.output_resolution.x;
         device_data.render_resolution.y = device_data.output_resolution.y;
      }

		device_data.ui_texture = nullptr;
      device_data.ui_texture_rtv = nullptr;
      device_data.ui_texture_srv = nullptr;
      // At the moment this is just created when the swapchain changes anything about it,
      // so we don't support changing this shader define live, but if needed, we could always move these allocations
      if (GetShaderDefineCompiledNumericalValue(UI_DRAW_TYPE_HASH) >= 3)
      {
         device_data.ui_texture = CloneTexture2D(native_device, (ID3D11Texture2D*)swapchain->get_back_buffer(0).handle, game->GetSeparateUITextureFormat(swapchain_data.vanilla_was_linear_space), true, false, nullptr);
         native_device->CreateRenderTargetView(device_data.ui_texture.get(), nullptr, &device_data.ui_texture_rtv);
         native_device->CreateShaderResourceView(device_data.ui_texture.get(), nullptr, &device_data.ui_texture_srv);
      }

      IDXGISwapChain3* native_swapchain3;
      // The cast pointer is actually the same, we are just making sure the type is right.
      hr = native_swapchain->QueryInterface(&native_swapchain3);
      ASSERT_ONCE(SUCCEEDED(hr)); // This is required by LUMA, but all systems should have this swapchain by now

      // This is basically where we verify and update the user display settings
      if (native_swapchain3 != nullptr)
      {
         const std::unique_lock lock_reshade(s_mutex_reshade);
         Display::GetHDRMaxLuminance(native_swapchain3, device_data.default_user_peak_white, srgb_white_level);
         Display::IsHDRSupportedAndEnabled(swapchain_desc.OutputWindow, hdr_supported_display, hdr_enabled_display, native_swapchain3);
         game_window = swapchain_desc.OutputWindow; // This shouldn't really need any thread safety protection

#if GAME_BIOSHOCK_2 //TODOFT6 (does this make the game stutter like crazy?)
         // Force borderless window:
         int screen_width = GetSystemMetrics(SM_CXSCREEN);
         int screen_height = GetSystemMetrics(SM_CYSCREEN);
         // Calculate centered position
         int x_pos = (screen_width - swapchain_desc.BufferDesc.Width) / 2;
         int y_yos = (screen_height - swapchain_desc.BufferDesc.Height) / 2;

         auto style = GetWindowLong(game_window, GWL_STYLE);
         style |= WS_POPUP;
         SetWindowLong(game_window, GWL_STYLE, style);

         SetWindowPos(game_window, NULL, x_pos, y_yos, swapchain_desc.BufferDesc.Width, swapchain_desc.BufferDesc.Height, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOZORDER);
#endif

#if GAME_BIOSHOCK_2 && 0 //TODOFT6: probably not necessary
         com_ptr<IDXGIOutput> output;
         native_swapchain->GetContainingOutput(&output);
         output->SetGammaControl(nullptr);
#endif

         if (!hdr_enabled_display)
         {
            // Force the display mode to SDR if HDR is not engaged
            cb_luma_frame_settings.DisplayMode = 0;
            OnDisplayModeChanged();
            cb_luma_frame_settings.ScenePeakWhite = srgb_white_level;
            cb_luma_frame_settings.ScenePaperWhite = srgb_white_level;
            cb_luma_frame_settings.UIPaperWhite = srgb_white_level;
         }
         // Avoid increasing the peak if the user has SDR mode set, SDR mode might still rely on the peak white value
         else if (cb_luma_frame_settings.DisplayMode > 0 && cb_luma_frame_settings.DisplayMode < 2)
         {
            cb_luma_frame_settings.ScenePeakWhite = device_data.default_user_peak_white;
         }
         device_data.cb_luma_frame_settings_dirty = true;

#if 0 // Not needed until proven otherwise (we already upgrade in "OnCreateSwapchain()", which should always be called when resizing the swapchain too)
         if (enable_swapchain_upgrade && swapchain_upgrade_type > 0)
         {
            UINT flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            hr = native_swapchain3->ResizeBuffers(0, 0, 0, format, flags); // Pass in zero to not change any values if not the format
            ASSERT_ONCE(SUCCEEDED(hr));

            DXGI_COLOR_SPACE_TYPE colorSpace;
            colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            hr = native_swapchain3->SetColorSpace1(colorSpace);
            ASSERT_ONCE(SUCCEEDED(hr));
         }
#endif

         // We release the resource because the swapchain lifespan is, and should be, controlled by the game.
         // We already have "OnDestroySwapchain()" to handle its destruction.
         native_swapchain3->Release();
      }

      game->OnInitSwapchain(swapchain);
   }

   void OnDestroySwapchain(reshade::api::swapchain* swapchain, bool resize)
   {
      auto* device = swapchain->get_device();
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      ASSERT_ONCE(&device_data != nullptr); // Hacky nullptr check (should ever be able to happen)
      SwapchainData& swapchain_data = *swapchain->get_private_data<SwapchainData>();
      {
         const std::unique_lock lock(device_data.mutex);
         device_data.swapchains.erase(swapchain);
         for (const uint64_t handle : swapchain_data.back_buffers)
         {
            device_data.back_buffers.erase(handle);
         }
      }

      swapchain->destroy_private_data<SwapchainData>();
   }

   bool OnSetFullscreenState(reshade::api::swapchain* swapchain, bool fullscreen, void* hmonitor)
   {
#if 1 //TODOFT: make per game features
      // For now we always prevent fullscreen on boot and later, given that it's pointless.
      // If there were issues, we could exclusively do it when the swapchain resolution matched the monitor resolution
      return true;
#else
      return false;
#endif
   }

   void OnInitCommandList(reshade::api::command_list* cmd_list)
   {
      CommandListData& cmd_list_data = *cmd_list->create_private_data<CommandListData>();

#if DEVELOPMENT
      com_ptr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(&native_device_context);
      if (SUCCEEDED(hr) && native_device_context)
      {
         if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE)
         {
            DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
            ASSERT_ONCE(!device_data.primary_command_list_data); // There should never be more than one of these?
            device_data.primary_command_list_data = &cmd_list_data;
         }
      }
#endif
   }

   void OnDestroyCommandList(reshade::api::command_list* cmd_list)
   {
      cmd_list->destroy_private_data<CommandListData>();
   }

   void OnInitPipeline(
      reshade::api::device* device,
      reshade::api::pipeline_layout layout,
      uint32_t subobject_count,
      const reshade::api::pipeline_subobject* subobjects,
      reshade::api::pipeline pipeline)
   {
      // In DX11 each pipeline should only have one subobject (e.g. a shader)
      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const auto& subobject = subobjects[i];
         for (uint32_t j = 0; j < subobject.count; ++j)
         {
            switch (subobject.type)
            {
#if GEOMETRY_SHADER_SUPPORT // Simply skipping cloning geom shaders pipelines is enough to stop the whole functionality (and the only place that is performance relevant)
            case reshade::api::pipeline_subobject_type::geometry_shader:
#endif // GEOMETRY_SHADER_SUPPORT
            case reshade::api::pipeline_subobject_type::vertex_shader:
            case reshade::api::pipeline_subobject_type::compute_shader:
            case reshade::api::pipeline_subobject_type::pixel_shader:
            {
               ASSERT_ONCE(subobject_count == 1);
               break;
            }
            default:
            {
               return; // Nothing to do here, we don't want to clone the pipeline
            }
            }
         }
      }
      reshade::api::pipeline_subobject* subobjects_cache = Shader::ClonePipelineSubobjects(subobject_count, subobjects);

      auto* cached_pipeline = new CachedPipeline{
          pipeline,
          device,
          layout,
          subobjects_cache,
          subobject_count };

      bool found_replaceable_shader = false;
      bool found_custom_shader_file = false;

      DeviceData& device_data = *device->get_private_data<DeviceData>();

      const std::unique_lock lock(s_mutex_generic);
      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const auto& subobject = subobjects[i];
         for (uint32_t j = 0; j < subobject.count; ++j)
         {
            switch (subobject.type)
            {
#if GEOMETRY_SHADER_SUPPORT
            case reshade::api::pipeline_subobject_type::geometry_shader:
#endif // GEOMETRY_SHADER_SUPPORT
            case reshade::api::pipeline_subobject_type::vertex_shader:
            case reshade::api::pipeline_subobject_type::compute_shader:
            case reshade::api::pipeline_subobject_type::pixel_shader:
            {
               auto* new_desc = static_cast<reshade::api::shader_desc*>(subobjects_cache[i].data);
               ASSERT_ONCE(new_desc->code_size > 0);
               if (new_desc->code_size == 0) break;
               found_replaceable_shader = true;
               auto shader_hash = compute_crc32(static_cast<const uint8_t*>(new_desc->code), new_desc->code_size);

#if ALLOW_SHADERS_DUMPING
               {
                  const std::unique_lock lock_dumping(s_mutex_dumping);

                  // Delete any previous shader with the same hash (unlikely to happen, but safer nonetheless)
                  if (auto previous_shader_pair = shader_cache.find(shader_hash); previous_shader_pair != shader_cache.end() && previous_shader_pair->second != nullptr)
                  {
                     auto& previous_shader = previous_shader_pair->second;
                     // Make sure that two shaders have the same hash, their code size also matches (theoretically we could check even more, but the chances hashes overlapping is extremely small)
                     assert(previous_shader->size == new_desc->code_size);
#if DEVELOPMENT
                     shader_cache_count--;
#endif
                     delete previous_shader->data;
                     delete previous_shader;
                  }

                  // Cache shader
                  auto* cache = new CachedShader{
                      malloc(new_desc->code_size),
                      new_desc->code_size,
                      subobject.type };
                  std::memcpy(cache->data, new_desc->code, cache->size);
#if DEVELOPMENT
                  shader_cache_count++;
#endif
                  shader_cache[shader_hash] = cache;
                  shaders_to_dump.emplace(shader_hash);
               }
#endif // ALLOW_SHADERS_DUMPING

               // Indexes
               assert(std::find(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end(), shader_hash) == cached_pipeline->shader_hashes.end());
               cached_pipeline->shader_hashes.emplace_back(shader_hash);
               ASSERT_ONCE(cached_pipeline->shader_hashes.size() == 1); // Just to make sure if this actually happens

               // Make sure we didn't already have a valid pipeline in there (this should never happen)
               auto pipelines_pair = device_data.pipeline_caches_by_shader_hash.find(shader_hash);
               if (pipelines_pair != device_data.pipeline_caches_by_shader_hash.end())
               {
                  pipelines_pair->second.emplace(cached_pipeline);
               }
               else
               {
                  device_data.pipeline_caches_by_shader_hash[shader_hash] = { cached_pipeline };
               }
               {
                  const std::shared_lock lock(s_mutex_loading);
                  found_custom_shader_file |= custom_shaders_cache.contains(shader_hash);
               }

#if _DEBUG && LOG_VERBOSE
               // Metrics
               {
                  std::stringstream s2;
                  s2 << "caching shader(";
                  s2 << "hash: " << PRINT_CRC32(shader_hash);
                  s2 << ", type: " << subobject.type;
                  s2 << ", pipeline: " << reinterpret_cast<void*>(pipeline.handle);
                  s2 << ")";
                  reshade::log::message(reshade::log::level::info, s2.str().c_str());
               }
#endif // DEVELOPMENT
               break;
            }
            }
         }
      }
      if (!found_replaceable_shader)
      {
         delete cached_pipeline;
         cached_pipeline = nullptr;
         DestroyPipelineSubojects(subobjects_cache, subobject_count);
         subobjects_cache = nullptr;
         return;
      }
      device_data.pipeline_cache_by_pipeline_handle[pipeline.handle] = cached_pipeline;

      // Automatically load any custom shaders that might have been bound to this pipeline.
      // To avoid this slowing down everything, we only do it if we detect the user already had a matching shader in its custom shaders folder.
      if (auto_load && !last_pressed_unload && found_custom_shader_file)
      {
         const std::unique_lock lock_loading(s_mutex_loading);
         // Immediately cloning and replacing the pipeline might be unsafe, we might need to delay it to the next frame.
         // NOTE: this is totally fine to be done immediately (inline) in DX11, it's only unsafe in DX12.
         device_data.pipelines_to_reload.emplace(pipeline.handle);
         if (precompile_custom_shaders) // Re-use this value to symbolize that we don't want to wait until shaders async compilation is done to use the new shaders
         {
            LoadCustomShaders(device_data, device_data.pipelines_to_reload, !precompile_custom_shaders);
            device_data.pipelines_to_reload.clear();
         }
      }
   }

   void OnDestroyPipeline(
      reshade::api::device* device,
      reshade::api::pipeline pipeline)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      {
         const std::unique_lock lock_loading(s_mutex_loading);
         device_data.pipelines_to_reload.erase(pipeline.handle);
      }

      const std::unique_lock lock(s_mutex_generic);
      if (auto pipeline_cache_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline.handle); pipeline_cache_pair != device_data.pipeline_cache_by_pipeline_handle.end())
      {
         auto& cached_pipeline = pipeline_cache_pair->second;

         if (cached_pipeline != nullptr)
         {
            // Clean other references to the pipeline
            for (auto& pipelines_cache_pair : device_data.pipeline_caches_by_shader_hash)
            {
               auto& cached_pipelines = pipelines_cache_pair.second;
               cached_pipelines.erase(cached_pipeline);
            }

            // Destroy our cloned subojects
            DestroyPipelineSubojects(cached_pipeline->subobjects_cache, cached_pipeline->subobject_count);
            cached_pipeline->subobjects_cache = nullptr;

            // Destroy our cloned version of the pipeline (and leave the original intact)
            if (cached_pipeline->cloned)
            {
               cached_pipeline->cloned = false;
               cached_pipeline->device->destroy_pipeline(cached_pipeline->pipeline_clone);
               device_data.pipeline_cache_by_pipeline_clone_handle.erase(cached_pipeline->pipeline_clone.handle);
               device_data.cloned_pipeline_count--;
               device_data.cloned_pipelines_changed = true;
            }
            free(cached_pipeline);
            cached_pipeline = nullptr;
         }

         device_data.pipeline_cache_by_pipeline_handle.erase(pipeline.handle);
      }
   }

   void OnBindPipeline(
      reshade::api::command_list* cmd_list,
      reshade::api::pipeline_stage stages,
      reshade::api::pipeline pipeline)
   {
      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();

      if ((stages & reshade::api::pipeline_stage::compute_shader) != 0)
      {
         ASSERT_ONCE(stages == reshade::api::pipeline_stage::compute_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time (it does in DX11)
         cmd_list_data.pipeline_state_original_compute_shader = pipeline;
      }
      if ((stages & reshade::api::pipeline_stage::vertex_shader) != 0)
      {
         ASSERT_ONCE(stages == reshade::api::pipeline_stage::vertex_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time (it does in DX11)
         cmd_list_data.pipeline_state_original_vertex_shader = pipeline;
      }
      if ((stages & reshade::api::pipeline_stage::pixel_shader) != 0)
      {
         ASSERT_ONCE(stages == reshade::api::pipeline_stage::pixel_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time (it does in DX11)
         cmd_list_data.pipeline_state_original_pixel_shader = pipeline;
      }

      const std::shared_lock lock(s_mutex_generic);
      auto pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline.handle);
      if (pair == device_data.pipeline_cache_by_pipeline_handle.end() || pair->second == nullptr) return;

      auto* cached_pipeline = pair->second;

#if DEVELOPMENT
      if (cached_pipeline->skip)
      {
         // This will make the shader output black, or skip drawing, so we can easily detect it. This might not be very safe but seems to work in DX11.
         // TODO: replace the pipeline with a shader that outputs all "SV_Target" as purple for more visibility,
         // or return false in "reshade::addon_event::draw_or_dispatch_indirect" and similar draw calls to prevent them from being drawn.
         cmd_list->bind_pipeline(stages, reshade::api::pipeline{ 0 });
      }
      else
#endif
      if (cached_pipeline->cloned)
      {
         cmd_list->bind_pipeline(stages, cached_pipeline->pipeline_clone);
      }
   }

   enum class LumaConstantBufferType
   {
      LumaSettings,
      LumaData,
      LumaUIData
   };

   void SetLumaConstantBuffers(ID3D11DeviceContext* native_device_context, DeviceData& device_data, reshade::api::shader_stage stages, LumaConstantBufferType type, uint32_t custom_data_1 = 0, uint32_t custom_data_2 = 0)
   {
      constexpr bool force_update = false;

      // Most games (e.g. Prey, Dishonored 2) doesn't ever use these buffers, so it's fine to re-apply them once per frame if they didn't change
      switch (type)
      {
      case LumaConstantBufferType::LumaSettings:
      {
         if (luma_settings_cbuffer_index >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) break;

         {
            const std::shared_lock lock_reshade(s_mutex_reshade);
            if (force_update || device_data.cb_luma_frame_settings_dirty)
            {
               device_data.cb_luma_frame_settings_dirty = false;
               // My understanding is that "Map" doesn't immediately copy the memory to the GPU, but simply stores it on the side (in the command list),
               // and then copies it to the GPU when the command list is executed, so the resource is updated with deterministic order.
               // From our point of view, we don't really know what command list is currently running and what it is doing,
               // so we could still end up first updating the resource in a command list that will be executed with a delay,
               // and then updating the resource again in a command list that executes first, leaving the GPU buffer with whatever latest data it got (which might not be based on our latest version).
               // Fortunately we rarely use our cbuffers in any of the non main/primary command lists, and if we do, we generally don't change them within the frame,
               // so we can consider it all single threaded and deterministic. If ever needed, we could force a map to happen every time if we are in a new (non main) command list,
               // but then again, that would cross pollute the buffer across thread (plus, a command list is not guaranteed to ever be executed, it could be cleared before executing),
               // so the best solution would be to have these cbuffers per thread or per pass, or to not change them within a frame (or to always write the buffer again!)!
               if (D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                  SUCCEEDED(native_device_context->Map(device_data.luma_frame_settings.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer)))
               {
                  std::memcpy(mapped_buffer.pData, &cb_luma_frame_settings, sizeof(cb_luma_frame_settings));
                  native_device_context->Unmap(device_data.luma_frame_settings.get(), 0);
               }
            }
         }

         ID3D11Buffer* const buffer = device_data.luma_frame_settings.get();
         if ((stages & reshade::api::shader_stage::vertex) == reshade::api::shader_stage::vertex)
            native_device_context->VSSetConstantBuffers(luma_settings_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::geometry) == reshade::api::shader_stage::geometry)
            native_device_context->GSSetConstantBuffers(luma_settings_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::pixel) == reshade::api::shader_stage::pixel)
            native_device_context->PSSetConstantBuffers(luma_settings_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::compute) == reshade::api::shader_stage::compute)
            native_device_context->CSSetConstantBuffers(luma_settings_cbuffer_index, 1, &buffer);
         break;
      }
      case LumaConstantBufferType::LumaData:
      {
         if (luma_data_cbuffer_index >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) break;

         LumaInstanceData cb_luma_instance_data;
         cb_luma_instance_data.CustomData1 = custom_data_1;
         cb_luma_instance_data.CustomData2 = custom_data_2;
         cb_luma_instance_data.CustomData3 = 0; // Used as padding for now (unused)
         cb_luma_instance_data.FrameIndex = frame_index; // TODO: move this to "LumaConstantBufferType::LumaSettings" and rename it to "Globals"?

         cb_luma_instance_data.RenderResolutionScale.x = device_data.render_resolution.x / device_data.output_resolution.x;
         cb_luma_instance_data.RenderResolutionScale.y = device_data.render_resolution.y / device_data.output_resolution.y;
         // Always do this relative to the current output resolution
         cb_luma_instance_data.PreviousRenderResolutionScale.x = device_data.previous_render_resolution.x / device_data.output_resolution.x;
         cb_luma_instance_data.PreviousRenderResolutionScale.y = device_data.previous_render_resolution.y / device_data.output_resolution.y;

         game->UpdateLumaInstanceDataCB(cb_luma_instance_data);

         if (force_update || memcmp(&device_data.cb_luma_instance_data, &cb_luma_instance_data, sizeof(cb_luma_instance_data)) != 0)
         {
            device_data.cb_luma_instance_data = cb_luma_instance_data;
            if (D3D11_MAPPED_SUBRESOURCE mapped_buffer;
               SUCCEEDED(native_device_context->Map(device_data.luma_instance_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer)))
            {
               std::memcpy(mapped_buffer.pData, &device_data.cb_luma_instance_data, sizeof(device_data.cb_luma_instance_data));
               native_device_context->Unmap(device_data.luma_instance_data.get(), 0);
            }
         }

         ID3D11Buffer* const buffer = device_data.luma_instance_data.get();
         if ((stages & reshade::api::shader_stage::vertex) == reshade::api::shader_stage::vertex)
            native_device_context->VSSetConstantBuffers(luma_data_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::geometry) == reshade::api::shader_stage::geometry)
            native_device_context->GSSetConstantBuffers(luma_data_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::pixel) == reshade::api::shader_stage::pixel)
            native_device_context->PSSetConstantBuffers(luma_data_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::compute) == reshade::api::shader_stage::compute)
            native_device_context->CSSetConstantBuffers(luma_data_cbuffer_index, 1, &buffer);
         break;
      }
      case LumaConstantBufferType::LumaUIData:
      {
         ASSERT_ONCE(false); // Not implemented (yet?)
         break;
      }
      }
   }

#if DEVELOPMENT
   void OnExecuteSecondaryCommandList(reshade::api::command_list* cmd_list, reshade::api::command_list* secondary_cmd_list)
   {
      const std::shared_lock lock_trace(s_mutex_trace);
      if (trace_running)
      {
         CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
         CommandListData& secondary_cmd_list_data = *secondary_cmd_list->get_private_data<CommandListData>();
         const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
         const std::unique_lock lock_trace_3(secondary_cmd_list_data.mutex_trace);
         cmd_list_data.trace_draw_calls_data.append_range(secondary_cmd_list_data.trace_draw_calls_data);
         secondary_cmd_list_data.trace_draw_calls_data.clear();
      }
   }
#endif

   void OnPresent(
      reshade::api::command_queue* queue,
      reshade::api::swapchain* swapchain,
      const reshade::api::rect* source_rect,
      const reshade::api::rect* dest_rect,
      uint32_t dirty_rect_count,
      const reshade::api::rect* dirty_rects)
   {
      ID3D11Device* native_device = (ID3D11Device*)(queue->get_device()->get_native());
      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(queue->get_immediate_command_list()->get_native());
      DeviceData& device_data = *queue->get_device()->get_private_data<DeviceData>();
      SwapchainData& swapchain_data = *swapchain->get_private_data<SwapchainData>();

#if DEVELOPMENT
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *queue->get_immediate_command_list()->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data = {};
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Present;
            trace_draw_call_data.command_list = native_device_context;
            trace_draw_call_data.thread_id = std::this_thread::get_id();
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }

#if GAME_BIOSHOCK_2 && 0 //TODOFT6: probably not necessary
      if (auto native_swapchain = device_data.GetMainNativeSwapchain().get())
      {
         com_ptr<IDXGIOutput> output;
         native_swapchain->GetContainingOutput(&output);
         if (output)
         {
            DXGI_GAMMA_CONTROL gamma_control;
            ASSERT_ONCE(output->GetGammaControl(&gamma_control));

            // Set scale to default (1.0, 1.0, 1.0)
            gamma_control.Scale.Red = 1.0f;
            gamma_control.Scale.Green = 1.0f;
            gamma_control.Scale.Blue = 1.0f;
            // Set offset to default (0.0, 0.0, 0.0)
            gamma_control.Offset.Red = 0.0f;
            gamma_control.Offset.Green = 0.0f;
            gamma_control.Offset.Blue = 0.0f;

            // Create a simple gamma curve (linear in this example)
            for (int i = 0; i < 1025; ++i)
            {
               float value = i / 1024.0f;
               gamma_control.GammaCurve[i].Red = value;
               gamma_control.GammaCurve[i].Green = value;
               gamma_control.GammaCurve[i].Blue = value;
            }

            ASSERT_ONCE(output->SetGammaControl(nullptr));
            ASSERT_ONCE(output->SetGammaControl(&gamma_control));
         }
      }
#endif // GAME_BIOSHOCK_2
#endif

#if DEVELOPMENT // Allow to tank performance to test auto rendering resolution scaling etc
      if (frame_sleep_ms > 0 && frame_index % frame_sleep_interval == 0)
         Sleep(frame_sleep_ms);
#endif

      // If there are no shaders being currently replaced in the game ("cloned_pipeline_count"),
      // we can assume that we either missed replacing some shaders, or that we have unloaded all of our shaders.
      bool mod_active = device_data.cloned_pipeline_count != 0;
      // Theoretically we should simply check the current swapchain buffer format, but this also works
      const bool output_linear = (enable_swapchain_upgrade && swapchain_upgrade_type >= 1) || swapchain_data.vanilla_was_linear_space;
      bool input_linear = swapchain_data.vanilla_was_linear_space;
#if GAME_PREY // Prey's native code hooks already make the swapchain linear, but don't change the shaders
      input_linear = false;
#endif
      if (mod_active)
      {
      // "POST_PROCESS_SPACE_TYPE" 1 means that the final image was stored in textures in linear space (e.g. float or sRGB texture formats),
      // any other type would have been in gamma space, so it needs to be linearized for scRGB HDR (linear) output.
      // "GAMMA_CORRECTION_TYPE" 2 is always re-corrected (e.g. from sRGB) in the final shader.
         input_linear = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) == 1;
      }
      // Note that not all these combinations might be handled by the shader
      bool needs_reencoding = output_linear != input_linear;
      bool early_display_encoding = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) == 1 && GetShaderDefineCompiledNumericalValue(EARLY_DISPLAY_ENCODING_HASH) >= 1;
      bool needs_scaling = mod_active ? !early_display_encoding : (cb_luma_frame_settings.DisplayMode >= 1);
      bool early_gamma_correction = early_display_encoding && GetShaderDefineCompiledNumericalValue(GAMMA_CORRECTION_TYPE_HASH) < 2;
      // If the vanilla game was already doing post processing in linear space, it would have used sRGB buffers, hence it needs a sRGB<->2.2 gamma mismatch fix (we assume the vanilla game was running in SDR, not scRGB HDR).
      bool in_out_gamma_different = GetShaderDefineCompiledNumericalValue(VANILLA_ENCODING_TYPE_HASH) != GetShaderDefineCompiledNumericalValue(GAMMA_CORRECTION_TYPE_HASH);
      // If we are outputting SDR on SDR Display on a scRGB HDR swapchain, we might need Gamma 2.2/sRGB mismatch correction, because Windows would encode the scRGB buffer with sRGB (instead of Gamma 2.2, which the game would likely have expected)
      bool display_mode_needs_gamma_correction = swapchain_data.vanilla_was_linear_space ? false : (cb_luma_frame_settings.DisplayMode == 0);
      bool needs_gamma_correction = (mod_active ? (!early_gamma_correction && in_out_gamma_different) : in_out_gamma_different) || display_mode_needs_gamma_correction;
      // If this is true, the UI and Scene were both drawn with a brightness that is relative to each other, so we need to normalize it back to the scene brightness range
      bool ui_needs_scaling = mod_active && GetShaderDefineCompiledNumericalValue(UI_DRAW_TYPE_HASH) == 2;
      // If this is true, the UI was drawn on a separate buffer and needs to be composed onto the scene (which allows for UI background tonemapping, for increased visibility in HDR)
      bool ui_needs_composition = mod_active && GetShaderDefineCompiledNumericalValue(UI_DRAW_TYPE_HASH) >= 3 && device_data.ui_texture.get();
      bool needs_gamut_mapping = mod_active && GetShaderDefineCompiledNumericalValue(GAMUT_MAPPING_TYPE_HASH) != 0;

#if DEVELOPMENT
      bool needs_debug_draw_texture = device_data.debug_draw_texture.get() != nullptr; // Note that this might look wrong if "output_linear" is false
#else
      constexpr bool needs_debug_draw_texture = false;
#endif
      if (needs_debug_draw_texture || needs_reencoding || needs_gamma_correction || ui_needs_scaling || ui_needs_composition || needs_gamut_mapping)
      {
         const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
         if (device_data.copy_vertex_shader && device_data.display_composition_pixel_shader)
         {
            IDXGISwapChain* native_swapchain = (IDXGISwapChain*)(swapchain->get_native());

            UINT back_buffer_index = 0;
            com_ptr<IDXGISwapChain3> native_swapchain3;
            // The cast pointer is actually the same, we are just making sure the type is right (it should always be).
            if (SUCCEEDED(native_swapchain->QueryInterface(&native_swapchain3)))
            {
               back_buffer_index = native_swapchain3->GetCurrentBackBufferIndex();
            }
            com_ptr<ID3D11Texture2D> back_buffer;
            native_swapchain->GetBuffer(back_buffer_index, IID_PPV_ARGS(&back_buffer));
            assert(back_buffer != nullptr && swapchain_data.back_buffers.size() >= back_buffer_index + 1);
            assert(swapchain_data.display_composition_rtvs.size() >= back_buffer_index + 1);

            D3D11_TEXTURE2D_DESC target_desc;
            back_buffer->GetDesc(&target_desc);
            ASSERT_ONCE((target_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0);
            // For now we only support this format, nothing else wouldn't really make sense
            ASSERT_ONCE(target_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);

            uint32_t custom_const_buffer_data_1 = 0;
            uint32_t custom_const_buffer_data_2 = 0;

            DrawStateStack<DrawStateStackType::SimpleGraphics> draw_state_stack;
            draw_state_stack.Cache(native_device_context);

#if DEVELOPMENT
            if (device_data.debug_draw_texture.get())
            {
               D3D11_SHADER_RESOURCE_VIEW_DESC debug_srv_desc;
               debug_srv_desc.Format = device_data.debug_draw_texture_format;
               debug_srv_desc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
               debug_srv_desc.Texture2D.MipLevels = 1;
               debug_srv_desc.Texture2D.MostDetailedMip = 0;
               com_ptr<ID3D11ShaderResourceView> debug_srv;
               // We recreate this every frame, it doesn't really matter (and this is allowed to fail in case of quirky formats)
               HRESULT hr = native_device->CreateShaderResourceView(device_data.debug_draw_texture.get(), &debug_srv_desc, &debug_srv);
               ASSERT_ONCE(SUCCEEDED(hr));

               ID3D11ShaderResourceView* const debug_srv_const = debug_srv.get();
               native_device_context->PSSetShaderResources(2, 1, &debug_srv_const); // Use index 1 (0 is already used)

               custom_const_buffer_data_2 = debug_draw_options;
            }
            // Empty the shader resource so the shader can tell there isn't one
            else
            {
               ID3D11ShaderResourceView* const debug_srv_const = nullptr;
               native_device_context->PSSetShaderResources(2, 1, &debug_srv_const);
            }
#endif

            D3D11_TEXTURE2D_DESC proxy_target_desc;
            if (device_data.display_composition_texture.get() != nullptr)
            {
               device_data.display_composition_texture->GetDesc(&proxy_target_desc);
            }
            if (device_data.display_composition_texture.get() == nullptr || proxy_target_desc.Width != target_desc.Width || proxy_target_desc.Height != target_desc.Height || proxy_target_desc.Format != target_desc.Format)
            {
               proxy_target_desc = target_desc;
               proxy_target_desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
               proxy_target_desc.BindFlags &= ~D3D11_BIND_RENDER_TARGET;
               proxy_target_desc.BindFlags &= ~D3D11_BIND_UNORDERED_ACCESS;
               proxy_target_desc.CPUAccessFlags = 0;
               proxy_target_desc.Usage = D3D11_USAGE_DEFAULT;
               device_data.display_composition_texture = nullptr;
               device_data.display_composition_srv = nullptr;
               // Don't change the allocation number
					for (size_t i = 0; i < swapchain_data.display_composition_rtvs.size(); ++i)
					{
						swapchain_data.display_composition_rtvs[i] = nullptr;
					}
               HRESULT hr = native_device->CreateTexture2D(&proxy_target_desc, nullptr, &device_data.display_composition_texture);
               assert(SUCCEEDED(hr));

               hr = native_device->CreateShaderResourceView(device_data.display_composition_texture.get(), nullptr, &device_data.display_composition_srv);
               assert(SUCCEEDED(hr));
            }

            // We need to copy the texture to read back from it, even if we only exclusively write to the same pixel we read and thus there couldn't be any race condition. Unfortunately DX works like that.
            native_device_context->CopyResource(device_data.display_composition_texture.get(), back_buffer.get());

            com_ptr<ID3D11RenderTargetView> target_resource_texture_view = swapchain_data.display_composition_rtvs[back_buffer_index];
            // If we already had a render target, we can assume it was already set to the swapchain,
            // but it's good to make sure of it nonetheless.
            if (draw_state_stack.render_target_views[0] != nullptr) //TODOFT: disable for Vertigo?
            {
               swapchain_data.display_composition_rtvs[back_buffer_index] = nullptr;
               com_ptr<ID3D11Resource> render_target_resource;
               draw_state_stack.render_target_views[0]->GetResource(&render_target_resource);
               if (render_target_resource.get() == back_buffer.get())
               {
                  target_resource_texture_view = draw_state_stack.render_target_views[0];
               }
            }
            if (!target_resource_texture_view)
            {
               swapchain_data.display_composition_rtvs[back_buffer_index] = nullptr;
               HRESULT hr = native_device->CreateRenderTargetView(back_buffer.get(), nullptr, &swapchain_data.display_composition_rtvs[back_buffer_index]);
               ASSERT_ONCE(SUCCEEDED(hr));
               target_resource_texture_view = swapchain_data.display_composition_rtvs[back_buffer_index];
            }

            // Push our settings cbuffer in case where no other custom shader run this frame
            {
               DeviceData& device_data = *queue->get_device()->get_private_data<DeviceData>();
               const std::shared_lock lock(device_data.mutex); //TODOFT: is this needed right here?
               const auto cb_luma_frame_settings_copy = cb_luma_frame_settings;
               // Force a custom display mode in case we have no game custom shaders loaded, so the custom linearization shader can linearize anyway, independently of "POST_PROCESS_SPACE_TYPE"
               bool force_reencoding_or_gamma_correction = !mod_active; // We ignore "s_mutex_generic", it doesn't matter
               if (force_reencoding_or_gamma_correction)
               {
                  // No need for "s_mutex_reshade" here or above, given that they are generally only also changed by the user manually changing the settings in ImGUI, which runs at the very end of the frame
                  custom_const_buffer_data_1 = input_linear ? 2 : 1;
               }
               SetLumaConstantBuffers(native_device_context, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
               SetLumaConstantBuffers(native_device_context, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, custom_const_buffer_data_1, custom_const_buffer_data_2);
            }

            // Set UI texture
            ID3D11ShaderResourceView* const ui_texture_srv_const = ui_needs_composition ? device_data.ui_texture_srv.get() : nullptr;
            native_device_context->PSSetShaderResources(1, 1, &ui_texture_srv_const);

            // Note: we don't need to re-apply our custom cbuffers in most games (e.g. Prey), they are on indexes that are never used by the game's code
            DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), device_data.copy_vertex_shader.get(), device_data.display_composition_pixel_shader.get(), device_data.display_composition_srv.get(), target_resource_texture_view.get(), target_desc.Width, target_desc.Height, false);

            if (ui_needs_composition)
            {
               const FLOAT ColorRGBA[4] = { 0.f, 0.f, 0.f, 0.f };
               native_device_context->ClearRenderTargetView(device_data.ui_texture_rtv.get(), ColorRGBA);
            }

            draw_state_stack.Restore(native_device_context);
         }
         else
         {
#if DEVELOPMENT
            ASSERT_ONCE(false); // The custom shaders failed to be found (they have either been unloaded or failed to compile, or simply missing in the files)
#else
            static bool warning_sent = false;
            if (!warning_sent)
            {
               warning_sent = true;
               const std::string warn_message = "Some of the shader files are missing from the \"" + std::string(NAME) + "\" folder, or failed to compile for some unknown reason, please re-install the mod.";
               MessageBoxA(game_window, warn_message.c_str(), NAME, MB_SETFOREGROUND);
            }
#endif
         }
      }
      else
      {
         device_data.display_composition_texture = nullptr;
         device_data.display_composition_srv = nullptr;
         // Don't change the allocation number
         for (size_t i = 0; i < swapchain_data.display_composition_rtvs.size(); ++i)
         {
            swapchain_data.display_composition_rtvs[i] = nullptr;
         }
      }

#if DEVELOPMENT
      // Clear at the end of every frame and re-capture it in the next frame if its still available
      if (debug_draw_auto_clear_texture)
      {
         device_data.debug_draw_texture = nullptr;
         // Leave "debug_draw_texture_format" and "debug_draw_texture_size", we need that in ImGUI (we'd need to clear it after ImGUI if necessary, but we skip drawing it if the texture isn't valid)
      }
      debug_draw_pipeline_instance = 0;
#endif // DEVELOPMENT
      
#if ENABLE_NGX
      // Re-init DLSS if user toggled the settings.
      // We wouldn't really need to do anything other than clearing "dlss_output_color",
      // but to avoid wasting memory allocated by DLSS texture and other resources, clear it up once disabled.
      // Note that we keep these textures in memory if the user temporarily changed away from an AA method that supports DLSS, or if users unloaded shaders (there's no reason to, and it'd cause stutters).
      if (device_data.dlss_sr != NGX::DLSS::HasInit(device_data.dlss_sr_handle))
      {
         if (device_data.dlss_sr)
         {
            com_ptr<IDXGIDevice> native_dxgi_device;
            HRESULT hr = native_device->QueryInterface(&native_dxgi_device);
            com_ptr<IDXGIAdapter> native_adapter;
            if (SUCCEEDED(hr))
            {
               hr = native_dxgi_device->GetAdapter(&native_adapter);
            }
            assert(SUCCEEDED(hr));

            device_data.dlss_sr = NGX::DLSS::Init(device_data.dlss_sr_handle, native_device, native_adapter.get()); // No need to update "dlss_sr_supported", it wouldn't have changed.
            if (!device_data.dlss_sr)
            {
               const std::unique_lock lock_reshade(s_mutex_reshade);
               dlss_sr = false; // Disable the global user setting if it's not supported (it's ok even if we pollute device and global data), we want to grey it out in the UI (there's no need to serialize the new value for it though!)
            }
         }
         else
         {
            device_data.dlss_output_color = nullptr;
            device_data.dlss_exposure = nullptr;
            device_data.dlss_render_resolution_scale = 1.f; // Reset this to 1 when DLSS is toggled, even if dynamic resolution scaling is active (e.g. in Prey), we'll set it back to a low value if DRS is used again.
            device_data.dlss_scene_exposure = 1.f;
            device_data.dlss_scene_pre_exposure = 1.f;
            game->CleanExtraDLSSResources(device_data);
#if 0 // This would actually unload the DLSS DLL and all, making the game hitch, so it's better to just keep it in memory
            NGX::DLSS::Deinit(device_data.dlss_sr_handle);
#endif
         }
      }
#endif // ENABLE_NGX

      game->OnPresent(native_device, device_data);

#if 0 // We do this only when ImGUI is on screen now, restore this if ever needed
      device_data.cb_luma_frame_settings_dirty = true; // Force re-upload the frame settings buffer at least once per frame, just to make sure to catch any user (or dev) settings changes, it should be fast enough
#endif

      frame_index++;
   }

   //TODOFT3: merge all the shader permutations that use the same code (and then move shader binaries to bin folder? Add shader files to VS project?)

   // Return false to prevent the original draw call from running (e.g. if you replaced it or just want to skip it)
   // Most games (e.g. Prey, Dishonored 2) always draw in direct mode (as opposed to indirect), but uses different command lists on different threads (e.g. on Prey, that's almost only used for the shadow projection maps, in Dishonored 2, for almost every separate pass).
   // Usually there's a few compute shaders but most passes are "classic" pixel shaders.
   // If we ever wanted to still run the game's original draw call (first) and then ours (second), we'd need to pass more arguments in this function (to replicate the draw call identically).
   bool OnDraw_Custom(reshade::api::command_list* cmd_list, bool is_dispatch /*= false*/, ShaderHashesList& original_shader_hashes)
   {
      const auto* device = cmd_list->get_device();
      auto device_api = device->get_api();
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
      DeviceData& device_data = *device->get_private_data<DeviceData>();

      reshade::api::shader_stage stages = reshade::api::shader_stage::all_graphics | reshade::api::shader_stage::all_compute;

      bool is_custom_pass = false;
      bool updated_cbuffers = false;

      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();

#if DEVELOPMENT
      last_drawn_shader = "";
      global_cmd_list = cmd_list;
#endif //DEVELOPMENT

      // We check the last shader pointers ("pipeline_state_original_compute_shader") we had cached in the pipeline set state functions.
      // Alternatively we could check "PSGetShader()" against "pipeline_cache_by_pipeline_clone_handle" but that'd probably have uglier and slower code.
      if (is_dispatch)
      {
         if (cmd_list_data.pipeline_state_original_compute_shader.handle != 0)
         {
            const std::shared_lock lock(s_mutex_generic);
            const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(cmd_list_data.pipeline_state_original_compute_shader.handle);
            if (pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
            {
               original_shader_hashes.compute_shaders = std::unordered_set<uint32_t>(pipeline_pair->second->shader_hashes.begin(), pipeline_pair->second->shader_hashes.end());
#if DEVELOPMENT
               last_drawn_shader = original_shader_hashes.compute_shaders.empty() ? "" : Shader::Hash_NumToStr(*original_shader_hashes.compute_shaders.begin()); // String hash to int
#endif //DEVELOPMENT
               is_custom_pass = pipeline_pair->second->cloned;
               stages = reshade::api::shader_stage::compute;
            }
         }
      }
      else
      {
         if (cmd_list_data.pipeline_state_original_vertex_shader.handle != 0)
         {
            const std::shared_lock lock(s_mutex_generic);
            const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(cmd_list_data.pipeline_state_original_vertex_shader.handle);
            if (pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
            {
               original_shader_hashes.vertex_shaders = std::unordered_set<uint32_t>(pipeline_pair->second->shader_hashes.begin(), pipeline_pair->second->shader_hashes.end());
               is_custom_pass = pipeline_pair->second->cloned;
               stages = reshade::api::shader_stage::vertex;
            }
         }

         if (cmd_list_data.pipeline_state_original_pixel_shader.handle != 0)
         {
            const std::shared_lock lock(s_mutex_generic);
            const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(cmd_list_data.pipeline_state_original_pixel_shader.handle);
            if (pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
            {
               original_shader_hashes.pixel_shaders = std::unordered_set<uint32_t>(pipeline_pair->second->shader_hashes.begin(), pipeline_pair->second->shader_hashes.end());
#if DEVELOPMENT
               last_drawn_shader = original_shader_hashes.pixel_shaders.empty() ? "" : Shader::Hash_NumToStr(*original_shader_hashes.pixel_shaders.begin()); // String hash to int
#endif //DEVELOPMENT
               is_custom_pass |= pipeline_pair->second->cloned;
               stages |= reshade::api::shader_stage::pixel;
            }
         }
      }

#if DEVELOPMENT
      {
         // TODO: add custom Luma passes to this list
         // Do this before any custom code runs as the state might change
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            const std::shared_lock lock_generic(s_mutex_generic);
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            ASSERT_ONCE(native_device_context);
            if (is_dispatch)
            {
               AddTraceDrawCallData(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_compute_shader.handle);
            }
            else
            {
               AddTraceDrawCallData(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_vertex_shader.handle);
               if (cmd_list_data.pipeline_state_original_pixel_shader.handle != 0) // Somehow this can happen (e.g. query tests don't require pixel shaders)
               {
                  AddTraceDrawCallData(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_pixel_shader.handle);
               }
            }
         }
      }
#endif

      const bool mod_active = device_data.cloned_pipeline_count != 0;
      if (enable_separate_ui_drawing && mod_active && ((device_data.has_drawn_main_post_processing && native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && !original_shader_hashes.Contains(shader_hashes_UI_excluded)) || original_shader_hashes.Contains(shader_hashes_UI)))
      {
         ID3D11RenderTargetView* const ui_texture_rtv_const = device_data.ui_texture_rtv.get();
         native_device_context->OMSetRenderTargets(1, &ui_texture_rtv_const, nullptr); // Note: for now we don't restore this back to the original value, as we haven't found any game that reads back the RT, or doesn't set it every frame or draw call
      }

      const bool had_drawn_main_post_processing = device_data.has_drawn_main_post_processing;

      if (!original_shader_hashes.Empty())
      {
         //TODOFT: optimize these shader searches by simply marking "CachedPipeline" with a tag on what they are (and whether they have a particular role) (also we can restrict the search to pixel shaders) upfront. And move these into their own functions. Update: we optimized this enough.

         if (game->OnDrawCustom(native_device, native_device_context, device_data, stages, original_shader_hashes, is_custom_pass, updated_cbuffers))
         {
            return true;
         }
      }

      // We have a way to track whether this data changed to avoid sending them again when not necessary, we could further optimize it by adding a flag to the shader hashes that need the cbuffers, but it really wouldn't help much
      if (is_custom_pass && !updated_cbuffers)
      {
         SetLumaConstantBuffers(native_device_context, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, device_data, stages, LumaConstantBufferType::LumaData);
         updated_cbuffers = true;
      }

#if !DEVELOPMENT || !GAME_PREY //TODOFT2: re-enable once we are sure we replaced all the post tonemap shaders and we are done debugging the blend states
      if (!is_custom_pass) return false;
#else // ("GAME_PREY") We can't do any further checks in this case because some UI draws at the beginning of the frame (in world computers, in Prey), and sometimes the scene doesn't draw, but we still need to update the cbuffers (though maybe we could clear it up on present, to avoid problems)
      //if (device_data.has_drawn_main_post_processing_previous && !device_data.has_drawn_main_post_processing) return false;
#endif // !DEVELOPMENT

      // Skip the rest in cases where the UI isn't passing through our custom linear blends that emulate SDR gamma->gamma blends.
      if (GetShaderDefineCompiledNumericalValue(UI_DRAW_TYPE_HASH) != 1) return false;

      LumaUIData ui_data = {};

      com_ptr<ID3D11RenderTargetView> render_target_view;
      // Theoretically we should also retrieve the UAVs and all other RTs but in reality it doesn't ever really matter
      native_device_context->OMGetRenderTargets(1, &render_target_view, nullptr);
      if (render_target_view)
      {
         com_ptr<ID3D11Resource> render_target_resource;
         render_target_view->GetResource(&render_target_resource);
         if (render_target_resource != nullptr)
         {
            bool targeting_swapchain;
            {
               const std::shared_lock lock(device_data.mutex);
               targeting_swapchain = device_data.back_buffers.contains((uint64_t)render_target_resource.get());
            }
            // We check across all the swap chain back buffers, not just the one that will be presented this frame (we have no 100% of what swapchain will draw this frame).
            // Note that some games (e.g. Prey) compose the UI in separate render targets to then be drawn into the world (e.g. in game interactive computer screens), but these usually don't draw on any of the swapchain buffers.
            if (targeting_swapchain)
            {
               ui_data.targeting_swapchain = 1;

               const bool paused = game->IsGamePaused(device_data);
               // Highlight that the game is paused or that we are in a menu with no scene rendering (e.g. allows us to fully skip lens distortion on the UI, as sometimes it'd apply in loading screen menus).
               if (paused || !device_data.has_drawn_main_post_processing)
               {
                  ui_data.fullscreen_menu = 1;
               }
            }
            render_target_resource = nullptr;
         }
         render_target_view = nullptr;
      }

      // No need to lock "s_mutex_reshade" for "cb_luma_frame_settings" here, it's not relevant
      // We could use "has_drawn_composed_gbuffers" here instead of "has_drawn_main_post_processing", but then again, they should always match (pp should always be run)
      ui_data.background_tonemapping_amount = (cb_luma_frame_settings.DisplayMode == 1 && device_data.has_drawn_main_post_processing_previous && ui_data.targeting_swapchain) ? game->GetTonemapUIBackgroundAmount(device_data) : 0.0;

      com_ptr<ID3D11BlendState> blend_state;
      native_device_context->OMGetBlendState(&blend_state, nullptr, nullptr);
      if (blend_state)
      {
         D3D11_BLEND_DESC blend_desc;
         blend_state->GetDesc(&blend_desc);
         // Mirrored from UI shaders:
         // 0 No alpha blend (or other unknown blend types that we can ignore)
         // 1 Straight alpha blend: "result = (source.RGB * source.A) + (dest.RGB * (1 - source.A))" or "result = lerp(dest.RGB, source.RGB, source.A)"
         // 2 Pre-multiplied alpha blend (alpha is also pre-multiplied, not just rgb): "result = source.RGB + (dest.RGB * (1 - source.A))"
         // 3 Additive alpha blend (source is "Straight alpha" while destination is retained at 100%): "result = (source.RGB * source.A) + dest.RGB"
         // 4 Additive blend (source and destination are simply summed up, ignoring the alpha): result = source.RGB + dest.RGB
         // 
         // We don't care for the alpha blend operation (source alpha * dest alpha) as alpha is never read back from destination
         if (blend_desc.RenderTarget[0].BlendEnable
            && blend_desc.RenderTarget[0].BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD)
         {
            // Do both the "straight alpha" and "pre-multiplied alpha" cases
            if ((blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA || blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE)
               && (blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA || blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE))
            {
               if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
               {
                  ui_data.blend_mode = 4;
               }
               else if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
               {
                  ui_data.blend_mode = 3;
               }
               else if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
               {
                  ui_data.blend_mode = 2;
               }
               else /*if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)*/
               {
                  ui_data.blend_mode = 1;
#if GAME_PREY
                  assert(!had_drawn_main_post_processing || !ui_data.targeting_swapchain || (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA));
#endif // GAME_PREY
               }
#if GAME_PREY
               //if (ui_data.blend_mode == 1 || ui_data.blend_mode == 3) // Old check
               {
                  // In "blend_mode == 1", Prey seems to erroneously use "D3D11_BLEND::D3D11_BLEND_SRC_ALPHA" as source blend alpha, thus it multiplies alpha by itself when using pre-multiplied alpha passes,
                  // which doesn't seem to make much sense, at least not for the first write on a separate new texture (it means that the next blend with the final target background could end up going beyond 1 because the background darkening intensity is lower than it should be).
                  ASSERT_ONCE(!had_drawn_main_post_processing || (ui_data.targeting_swapchain ?
                     // Make sure we never read back from the swap chain texture (which means we can ignore all the alpha blend ops on previous to it)
                     (blend_desc.RenderTarget[0].SrcBlend != D3D11_BLEND::D3D11_BLEND_DEST_ALPHA
                        && blend_desc.RenderTarget[0].DestBlend != D3D11_BLEND::D3D11_BLEND_DEST_ALPHA
                        && blend_desc.RenderTarget[0].SrcBlend != D3D11_BLEND::D3D11_BLEND_DEST_COLOR
                        && blend_desc.RenderTarget[0].DestBlend != D3D11_BLEND::D3D11_BLEND_DEST_COLOR)
                     // Make sure that writes to separate textures always use known alpha blends modes, because we'll be reading back that alpha for later (possibly)
                     : (blend_desc.RenderTarget[0].BlendOpAlpha == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD
                        && (blend_desc.RenderTarget[0].SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA
                           || blend_desc.RenderTarget[0].SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE))));
               }
#endif // GAME_PREY
            }
            else
            {
               ASSERT_ONCE(!had_drawn_main_post_processing || !ui_data.targeting_swapchain);
            }
         }
         assert(!had_drawn_main_post_processing || !ui_data.targeting_swapchain || !blend_desc.RenderTarget[0].BlendEnable || blend_desc.RenderTarget[0].BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD);
         blend_state = nullptr;
      }

      if (is_custom_pass && luma_ui_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
      {
         if (memcmp(&device_data.cb_luma_ui_data, &ui_data, sizeof(ui_data)) != 0)
         {
            device_data.cb_luma_ui_data = ui_data;
            if (D3D11_MAPPED_SUBRESOURCE mapped_buffer;
               SUCCEEDED(native_device_context->Map(device_data.luma_ui_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer)))
            {
               std::memcpy(mapped_buffer.pData, &device_data.cb_luma_ui_data, sizeof(device_data.cb_luma_ui_data));
               native_device_context->Unmap(device_data.luma_ui_data.get(), 0);
            }
         }

         ID3D11Buffer* const buffer = device_data.luma_ui_data.get();
         if ((stages & reshade::api::shader_stage::vertex) == reshade::api::shader_stage::vertex)
            native_device_context->VSSetConstantBuffers(luma_ui_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::geometry) == reshade::api::shader_stage::geometry)
            native_device_context->GSSetConstantBuffers(luma_ui_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::pixel) == reshade::api::shader_stage::pixel)
            native_device_context->PSSetConstantBuffers(luma_ui_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::compute) == reshade::api::shader_stage::compute)
            native_device_context->CSSetConstantBuffers(luma_ui_cbuffer_index, 1, &buffer);
      }

      return false; // Return true to cancel this draw call
   }

#if DEVELOPMENT
   bool HandlePipelineRedirections(ID3D11DeviceContext* native_device_context, const DeviceData& device_data, const CommandListData& cmd_list_data, bool is_dispatch, std::function<void()>& draw_func)
   {
      CachedPipeline::RedirectData redirect_data;
      if (is_dispatch)
      {
         const std::shared_lock lock(s_mutex_generic);
         const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(cmd_list_data.pipeline_state_original_compute_shader.handle);
         if (pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
         {
            redirect_data = pipeline_pair->second->redirect_data;
         }
      }
      else
      {
         const std::shared_lock lock(s_mutex_generic);
         const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(cmd_list_data.pipeline_state_original_pixel_shader.handle);
         if (pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
         {
            redirect_data = pipeline_pair->second->redirect_data;
         }
      }

      if (redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::None && redirect_data.target_type != CachedPipeline::RedirectData::RedirectTargetType::None)
      {
			com_ptr<ID3D11Resource> source_resource;
         com_ptr<ID3D11Resource> target_resource;

         switch (redirect_data.source_type)
         {
         case CachedPipeline::RedirectData::RedirectSourceType::SRV:
         {
            if (redirect_data.source_index >= 0 && redirect_data.source_index < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
            {
               com_ptr<ID3D11ShaderResourceView> srv;
               if (is_dispatch)
                  native_device_context->CSGetShaderResources(redirect_data.source_index, 1, &srv);
               else
                  native_device_context->PSGetShaderResources(redirect_data.source_index, 1, &srv);
               if (srv)
                  srv->GetResource(&source_resource);
            }
         }
			break;
         case CachedPipeline::RedirectData::RedirectSourceType::UAV:
         {
            com_ptr<ID3D11UnorderedAccessView> uav;
            if (is_dispatch)
            {
               if (redirect_data.source_index >= 0 && redirect_data.source_index < D3D11_1_UAV_SLOT_COUNT)
                  native_device_context->CSGetUnorderedAccessViews(redirect_data.source_index, 1, &uav);
            }
            else
            {
               if (redirect_data.source_index >= 0 && redirect_data.source_index < D3D11_PS_CS_UAV_REGISTER_COUNT)
                  native_device_context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, redirect_data.source_index, 1, &uav);
            }
            if (uav)
               uav->GetResource(&source_resource);
         }
         break;
         }

         switch (redirect_data.target_type)
         {
         case CachedPipeline::RedirectData::RedirectTargetType::RTV:
         {
            if (redirect_data.target_index >= 0 && redirect_data.target_index < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && !is_dispatch)
            {
               com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
               native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], nullptr);
               if (rtvs[redirect_data.target_index])
						rtvs[redirect_data.target_index]->GetResource(&target_resource);
            }
         }
         break;
         case CachedPipeline::RedirectData::RedirectTargetType::UAV:
         {
            com_ptr<ID3D11UnorderedAccessView> uav;
            if (is_dispatch)
            {
               if (redirect_data.target_index >= 0 && redirect_data.target_index < D3D11_1_UAV_SLOT_COUNT)
                  native_device_context->CSGetUnorderedAccessViews(redirect_data.target_index, 1, &uav);
            }
            else
            {
               if (redirect_data.target_index >= 0 && redirect_data.target_index < D3D11_PS_CS_UAV_REGISTER_COUNT)
                  native_device_context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, redirect_data.target_index, 1, &uav);
            }
            if (uav)
               uav->GetResource(&target_resource);
         }
         break;
         }

         // TODO: fall back to pixel shader if the formats/sizes are not compatible? Otherwise add a safety check to avoid crashing (it doesn't seem to)
         if (source_resource.get() && target_resource.get() && source_resource.get() != target_resource.get())
         {
#if 0 // We don't actually need to force run the original draw call
            draw_func();
#endif
            native_device_context->CopyResource(target_resource.get(), source_resource.get());
            return true; // Make sure the original draw call is cancelled, otherwise the target resource would get overwritten
         }
      }

      return false;
   }
#endif //DEVELOPMENT

   bool OnDraw(
      reshade::api::command_list* cmd_list,
      uint32_t vertex_count,
      uint32_t instance_count,
      uint32_t first_vertex,
      uint32_t first_instance)
   {
#if DEVELOPMENT
      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());

      bool wants_debug_draw = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0;
		wants_debug_draw &= (debug_draw_pipeline == 0 || debug_draw_pipeline == cmd_list_data.pipeline_state_original_pixel_shader.handle);

      DrawStateStack pre_draw_state_stack;
      if (wants_debug_draw)
         {
         pre_draw_state_stack.Cache(native_device_context);
      }

      std::function<void()> drawLambda = [&]()
            {
               if (instance_count > 1)
               {
                  native_device_context->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
               }
               else
               {
                  ASSERT_ONCE(first_instance == 0);
                  native_device_context->Draw(vertex_count, first_vertex);
               }
         };
#endif
      ShaderHashesList original_shader_hashes;
      bool cancelled_or_replaced = OnDraw_Custom(cmd_list, false, original_shader_hashes);
#if DEVELOPMENT
#if 0 // We should do this manually when replacing each draw call, we don't know if it was replaced or cancelled here
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (cancelled_or_replaced && trace_running)
         {
            const std::shared_lock lock_generic(s_mutex_generic);
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            if (cmd_list_data.pipeline_state_original_pixel_shader.handle != 0)
            {
               cmd_list_data.trace_draw_calls_data[cmd_list_data.trace_draw_calls_data.size() - 1].skipped = true;
            }
         }
      }
#endif

      // First run the draw call (don't delegate it to ReShade) and then copy its output
      if (wants_debug_draw && (debug_draw_shader_hash == 0 || original_shader_hashes.Contains(debug_draw_shader_hash, reshade::api::shader_stage::pixel)))
      {
         auto local_debug_draw_pipeline_instance = debug_draw_pipeline_instance.fetch_add(1);
         // TODO: make the "debug_draw_pipeline_target_instance" by thread (and command list) too, though it's rarely useful
         if (debug_draw_pipeline_target_instance == -1 || local_debug_draw_pipeline_instance == debug_draw_pipeline_target_instance)
         {
            DrawStateStack post_draw_state_stack;

            if (!cancelled_or_replaced)
            {
               drawLambda();
            }
            else if (!debug_draw_replaced_pass)
            {
               post_draw_state_stack.Cache(native_device_context);
               pre_draw_state_stack.Restore(native_device_context);
            }

            CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, cmd_list, false);

            if (cancelled_or_replaced && !debug_draw_replaced_pass)
            {
               post_draw_state_stack.Restore(native_device_context);
         }
            cancelled_or_replaced = true;
      }
      }
      if (cancelled_or_replaced)
      {
         // Cancel the lambda as we've already drawn once, we don't want to do it further below
         drawLambda = []() {};
      }

      const DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      cancelled_or_replaced |= HandlePipelineRedirections(native_device_context, device_data, cmd_list_data, false, drawLambda);
#endif
      return cancelled_or_replaced;
   }

   bool OnDrawIndexed(
      reshade::api::command_list* cmd_list,
      uint32_t index_count,
      uint32_t instance_count,
      uint32_t first_index,
      int32_t vertex_offset,
      uint32_t first_instance)
   {
#if DEVELOPMENT
      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());

      bool wants_debug_draw = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0;
      wants_debug_draw &= (debug_draw_pipeline == 0 || debug_draw_pipeline == cmd_list_data.pipeline_state_original_pixel_shader.handle);

      DrawStateStack pre_draw_state_stack;
      if (wants_debug_draw)
         {
         pre_draw_state_stack.Cache(native_device_context);
      }

      std::function<void()> drawLambda = [&]()
            {
               if (instance_count > 1)
               {
                  native_device_context->DrawIndexedInstanced(index_count, instance_count, first_index, vertex_offset, first_instance);
               }
               else
               {
                  ASSERT_ONCE(first_instance == 0);
                  native_device_context->DrawIndexed(index_count, first_index, vertex_offset);
               }
         };
#endif
      ShaderHashesList original_shader_hashes;
      bool cancelled_or_replaced = OnDraw_Custom(cmd_list, false, original_shader_hashes);
#if DEVELOPMENT
      // First run the draw call (don't delegate it to ReShade) and then copy its output
      if (wants_debug_draw && (debug_draw_shader_hash == 0 || original_shader_hashes.Contains(debug_draw_shader_hash, reshade::api::shader_stage::pixel)))
      {
         auto local_debug_draw_pipeline_instance = debug_draw_pipeline_instance.fetch_add(1);
         if (debug_draw_pipeline_target_instance == -1 || local_debug_draw_pipeline_instance == debug_draw_pipeline_target_instance)
         {
            DrawStateStack post_draw_state_stack;

            if (!cancelled_or_replaced)
            {
               drawLambda();
            }
            else if (!debug_draw_replaced_pass)
            {
               post_draw_state_stack.Cache(native_device_context);
               pre_draw_state_stack.Restore(native_device_context);
            }

            CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, cmd_list, false);

            if (cancelled_or_replaced && !debug_draw_replaced_pass)
            {
               post_draw_state_stack.Restore(native_device_context);
            }
            cancelled_or_replaced = true;
         }
      }
      if (cancelled_or_replaced)
      {
         // Cancel the lambda as we've already drawn once, we don't want to do it further below
         drawLambda = []() {};
      }

      const DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      cancelled_or_replaced |= HandlePipelineRedirections(native_device_context, device_data, cmd_list_data, false, drawLambda);
#endif
      return cancelled_or_replaced;
   }

   bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
   {
#if DEVELOPMENT
      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());

      bool wants_debug_draw = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0;
      wants_debug_draw &= (debug_draw_pipeline == 0 || debug_draw_pipeline == cmd_list_data.pipeline_state_original_compute_shader.handle);

      DrawStateStack<DrawStateStackType::Compute> pre_draw_state_stack;
      if (wants_debug_draw)
      {
         pre_draw_state_stack.Cache(native_device_context);
      }

      std::function<void()> drawLambda = [&]()
         {
            native_device_context->Dispatch(group_count_x, group_count_y, group_count_z);
         };
#endif
      ShaderHashesList original_shader_hashes;
      bool cancelled_or_replaced = OnDraw_Custom(cmd_list, true, original_shader_hashes);
#if DEVELOPMENT
      // First run the draw call (don't delegate it to ReShade) and then copy its output
      if (wants_debug_draw && (debug_draw_shader_hash == 0 || original_shader_hashes.Contains(debug_draw_shader_hash, reshade::api::shader_stage::compute)))
      {
         auto local_debug_draw_pipeline_instance = debug_draw_pipeline_instance.fetch_add(1);
         if (debug_draw_pipeline_target_instance == -1 || local_debug_draw_pipeline_instance == debug_draw_pipeline_target_instance)
         {
            DrawStateStack<DrawStateStackType::Compute> post_draw_state_stack;

            if (!cancelled_or_replaced)
            {
               drawLambda();
            }
            else if (!debug_draw_replaced_pass)
            {
               post_draw_state_stack.Cache(native_device_context);
               pre_draw_state_stack.Restore(native_device_context);
            }

            CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, cmd_list, true);

            if (cancelled_or_replaced && !debug_draw_replaced_pass)
            {
               post_draw_state_stack.Restore(native_device_context);
            }
            cancelled_or_replaced = true;
         }
      }
      if (cancelled_or_replaced)
      {
         // Cancel the lambda as we've already drawn once, we don't want to do it further below
         drawLambda = []() {};
      }

      const DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      cancelled_or_replaced |= HandlePipelineRedirections(native_device_context, device_data, cmd_list_data, true, drawLambda);
#endif
      return cancelled_or_replaced;
   }

   bool OnDrawOrDispatchIndirect(
      reshade::api::command_list* cmd_list,
      reshade::api::indirect_command type,
      reshade::api::resource buffer,
      uint64_t offset,
      uint32_t draw_count,
      uint32_t stride)
   {
      // Not used by Dishonored 2 (DrawIndexedInstancedIndirect() and DrawInstancedIndirect() weren't used in Void Engine). Happens in Vertigo (Unity).
      const bool is_dispatch = type == reshade::api::indirect_command::dispatch;
      // Unsupported types (not used in DX11)
      ASSERT_ONCE(type != reshade::api::indirect_command::dispatch_mesh && type != reshade::api::indirect_command::dispatch_rays);
      // NOTE: according to ShortFuse, this can be "reshade::api::indirect_command::unknown" too, so we'd need to fall back on checking what shader is bound to know if this is a compute shader draw
      ASSERT_ONCE(type != reshade::api::indirect_command::unknown);

#if DEVELOPMENT
      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());

      bool wants_debug_draw = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0;
      wants_debug_draw &= debug_draw_pipeline == 0 || debug_draw_pipeline == (is_dispatch ? cmd_list_data.pipeline_state_original_compute_shader.handle : cmd_list_data.pipeline_state_original_pixel_shader.handle);

      DrawStateStack<DrawStateStackType::FullGraphics> pre_draw_state_stack_graphics;
      DrawStateStack<DrawStateStackType::Compute> pre_draw_state_stack_compute;
      if (wants_debug_draw)
         {
         if (is_dispatch)
            pre_draw_state_stack_compute.Cache(native_device_context);
         else
            pre_draw_state_stack_graphics.Cache(native_device_context);
      }

      std::function<void()> drawLambda = [&]()
            {
               // We only support one draw for now (it couldn't be otherwise in DX11)
               ASSERT_ONCE(draw_count == 1);
               uint32_t i = 0;

               if (is_dispatch)
               {
                  native_device_context->DispatchIndirect(reinterpret_cast<ID3D11Buffer*>(buffer.handle), static_cast<UINT>(offset) + i * stride);
               }
               else
               {
                  if (type == reshade::api::indirect_command::draw_indexed)
                  {
                     native_device_context->DrawIndexedInstancedIndirect(reinterpret_cast<ID3D11Buffer*>(buffer.handle), static_cast<UINT>(offset) + i * stride);
                  }
                  else
                  {
                     native_device_context->DrawInstancedIndirect(reinterpret_cast<ID3D11Buffer*>(buffer.handle), static_cast<UINT>(offset) + i * stride);
                  }
               }
         };
#endif
      ShaderHashesList original_shader_hashes;
      bool cancelled_or_replaced = OnDraw_Custom(cmd_list, is_dispatch, original_shader_hashes);
#if DEVELOPMENT
      if (wants_debug_draw && (debug_draw_shader_hash == 0 || original_shader_hashes.Contains(debug_draw_shader_hash, is_dispatch ? reshade::api::shader_stage::compute : reshade::api::shader_stage::pixel)))
      {
         auto local_debug_draw_pipeline_instance = debug_draw_pipeline_instance.fetch_add(1);
         if (debug_draw_pipeline_target_instance == -1 || local_debug_draw_pipeline_instance == debug_draw_pipeline_target_instance)
         {
            DrawStateStack<DrawStateStackType::FullGraphics> post_draw_state_stack_graphics;
            DrawStateStack<DrawStateStackType::Compute> post_draw_state_stack_compute;

            if (!cancelled_or_replaced)
            {
               drawLambda();
            }
            else if (!debug_draw_replaced_pass)
            {
               if (is_dispatch)
               {
                  post_draw_state_stack_compute.Cache(native_device_context);
                  pre_draw_state_stack_compute.Restore(native_device_context);
               }
               else
               {
                  post_draw_state_stack_graphics.Cache(native_device_context);
                  pre_draw_state_stack_graphics.Restore(native_device_context);
               }
            }

            CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, cmd_list, is_dispatch);

            if (cancelled_or_replaced && !debug_draw_replaced_pass)
            {
               if (is_dispatch)
                  post_draw_state_stack_compute.Restore(native_device_context);
               else
                  post_draw_state_stack_graphics.Restore(native_device_context);
         }
            cancelled_or_replaced = true;
      }
      }
      if (cancelled_or_replaced)
      {
         // Cancel the lambda as we've already drawn once, we don't want to do it further below
         drawLambda = []() {};
      }

      const DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      cancelled_or_replaced |= HandlePipelineRedirections(native_device_context, device_data, cmd_list_data, is_dispatch, drawLambda);
#endif
      return cancelled_or_replaced;
   }

   // TODO: use the native ReShade sampler desc instead? It's not really necessary
   // Expects "s_mutex_samplers" to already be locked
   com_ptr<ID3D11SamplerState> CreateCustomSampler(const DeviceData& device_data, ID3D11Device* device, D3D11_SAMPLER_DESC desc)
   {
#if !DEVELOPMENT
      if (desc.Filter == D3D11_FILTER_ANISOTROPIC || desc.Filter == D3D11_FILTER_COMPARISON_ANISOTROPIC)
      {
         desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
#if 1 // Without bruteforcing the offset, many textures (e.g. decals) stay blurry. Based on "samplers_upgrade_mode" 5.
         desc.MipLODBias = std::clamp(device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX); // Setting this out of range (~ +/- 16) will make DX11 crash
#else
         desc.MipLODBias = std::clamp(desc.MipLODBias + device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX); // Setting this out of range (~ +/- 16) will make DX11 crash
#endif
      }
      else
      {
         return nullptr;
      }
#else
      if (samplers_upgrade_mode <= 0)
         return nullptr;

      // Prey's CryEngine (and most games) only uses:
      // D3D11_FILTER_ANISOTROPIC
      // D3D11_FILTER_COMPARISON_ANISOTROPIC
      // D3D11_FILTER_MIN_MAG_MIP_POINT
      // D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT
      // D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
      // D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
      // D3D11_FILTER_MIN_MAG_MIP_LINEAR
      // D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR
      //TODOFT: check the cases for other games!

      // This could theoretically make some textures that have moire patters, or were purposely blurry, "worse", but the positives of upgrading still outweight the negatives.
      // Note that this might not fix all cases because there's still "ID3D11DeviceContext::SetResourceMinLOD()" and textures that are blurry for other reasons
      // because they use other types of samplers (unfortunately it seems like some decals use "D3D11_FILTER_MIN_MAG_MIP_LINEAR").
      // Note that the AF on different textures in the game seems is possibly linked with other graphics settings than just AF (maybe textures or objects quality).
      if (desc.Filter == D3D11_FILTER_ANISOTROPIC || desc.Filter == D3D11_FILTER_COMPARISON_ANISOTROPIC)
      {
         // Note: this doesn't seem to affect much
         if (samplers_upgrade_mode == 1)
         {
            desc.MaxAnisotropy = min(desc.MaxAnisotropy * 2, D3D11_REQ_MAXANISOTROPY);
         }
         else if (samplers_upgrade_mode == 2)
         {
            desc.MaxAnisotropy = min(desc.MaxAnisotropy * 4, D3D11_REQ_MAXANISOTROPY);
         }
         else if (samplers_upgrade_mode >= 3)
         {
            desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
         }
         // Note: this is the main ingredient in making textures less blurry
         if (samplers_upgrade_mode == 4 && desc.MipLODBias <= 0.f)
         {
            desc.MipLODBias = std::clamp(desc.MipLODBias + device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
         }
         else if (samplers_upgrade_mode >= 5)
         {
            desc.MipLODBias = std::clamp(device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
         }
         // Note: this never seems to affect anything in Prey, probably also doesn't in most games
         if (samplers_upgrade_mode >= 6)
         {
            desc.MinLOD = min(desc.MinLOD, 0.f);
         }
      }
      else if ((desc.Filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR && samplers_upgrade_mode_2 >= 1) // This is the most common (main/only) format being used other than AF
         || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR && samplers_upgrade_mode_2 >= 2)
         || (desc.Filter == D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT && samplers_upgrade_mode_2 >= 3)
         || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT && samplers_upgrade_mode_2 >= 4)
         || (desc.Filter == D3D11_FILTER_MIN_MAG_MIP_POINT && samplers_upgrade_mode_2 >= 5)
         || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT && samplers_upgrade_mode_2 >= 6))
      {
         //TODOFT: research. Force this on to see how it behaves. Doesn't work, it doesn't really help any further with (e.g.) blurry decal textures
         // Note: this doesn't seem to do anything really, it doesn't help with the occasional blurry texture (probably because all samplers that needed anisotropic already had it set)
         if (samplers_upgrade_mode >= 7)
         {
            desc.Filter == (desc.ComparisonFunc != D3D11_COMPARISON_NEVER && samplers_upgrade_mode == 7) ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
            desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
         }
         // Note: changing the lod bias of non anisotropic filters makes reflections (cubemap samples?) a lot more specular (shiny) in Prey (and probably does in other games too), so it's best avoided (it can look better is some screenshots, but it's likely not intended).
         // Even if we only fix up textures that didn't have a positive bias, we run into the same problem.
         if (samplers_upgrade_mode == 4 && desc.MipLODBias <= 0.f)
         {
            desc.MipLODBias = std::clamp(desc.MipLODBias + device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
         }
         else if (samplers_upgrade_mode >= 5)
         {
            desc.MipLODBias = std::clamp(device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
         }
         if (samplers_upgrade_mode >= 6)
         {
            desc.MinLOD = min(desc.MinLOD, 0.f);
         }
      }
#endif // !DEVELOPMENT

      com_ptr<ID3D11SamplerState> sampler;
      device->CreateSamplerState(&desc, &sampler);
      ASSERT_ONCE(sampler != nullptr);
      return sampler;
   }

   void OnInitSampler(reshade::api::device* device, const reshade::api::sampler_desc& desc, reshade::api::sampler sampler)
   {
      if (sampler == 0)
         return;
#if DEVELOPMENT
      if (samplers_upgrade_mode == 0)
         return;
#endif

      DeviceData& device_data = *device->get_private_data<DeviceData>();

#if DEVELOPMENT && 0 // Assert in case we got unexpected samplers
      if (desc.filter == reshade::api::filter_mode::anisotropic || desc.filter == reshade::api::filter_mode::compare_anisotropic)
      {
         assert(desc.max_anisotropy >= 2); // Doesn't seem to happen
         assert(desc.min_lod == 0); // Doesn't seem to happen
         assert(desc.mip_lod_bias == 0.f); // This seems to happen when enabling TAA (but not with SMAA 2TX), some new samplers are created with bias -1 and then persist, it's unclear if they are used though.
      }
      else
      {
         assert(desc.max_anisotropy <= 1); // This can happen (like once) in Prey. AF is probably ignored for these anyway so it's innocuous
      }
      assert(desc.filter != reshade::api::filter_mode::min_mag_anisotropic_mip_point && desc.filter != reshade::api::filter_mode::compare_min_mag_anisotropic_mip_point); // Doesn't seem to happen

      ASSERT_ONCE(desc.filter == reshade::api::filter_mode::anisotropic
         || desc.filter == reshade::api::filter_mode::compare_anisotropic
         || desc.filter == reshade::api::filter_mode::min_mag_mip_linear
         || desc.filter == reshade::api::filter_mode::compare_min_mag_mip_linear
         || desc.filter == reshade::api::filter_mode::min_mag_linear_mip_point
         || desc.filter == reshade::api::filter_mode::compare_min_mag_linear_mip_point); // Doesn't seem to happen
#endif // DEVELOPMENT

      ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);

      std::shared_lock shared_lock_samplers(s_mutex_samplers);

      // Custom samplers lifetime should never be tracked by ReShade, otherwise we'd recursively create custom samplers out of custom samplers
      // (it's unclear if engines (e.g. CryEngine) somehow do anything with these samplers or if ReShade captures our own samplers creation events (it probably does as we create them directly through the DX native funcs))
      for (const auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
      {
         for (const auto& custom_sampler_handle : samplers_handle.second)
         {
            ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
            if (custom_sampler_handle.second.get() == native_sampler)
            {
               return;
            }
         }
      }

      D3D11_SAMPLER_DESC native_desc;
      native_sampler->GetDesc(&native_desc);
      shared_lock_samplers.unlock(); // This is fine!
      std::unique_lock unique_lock_samplers(s_mutex_samplers);
      device_data.custom_sampler_by_original_sampler[sampler.handle][device_data.texture_mip_lod_bias_offset] = CreateCustomSampler(device_data, (ID3D11Device*)device->get_native(), native_desc);
   }

   void OnDestroySampler(reshade::api::device* device, reshade::api::sampler sampler)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      // This only seems to happen when the game shuts down in Prey (as any destroy callback, it can be called from an arbitrary thread, but that's fine)
      const std::unique_lock lock_samplers(s_mutex_samplers);

#if DEVELOPMENT //TODOFT: delete, already in "OnInitSampler()", so this shouldn't be able to ever happen
      ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
      // Custom samplers lifetime should never be tracked by ReShade (is this innoucuous? remove it from the list in case it happened)
      for (const auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
      {
         for (const auto& custom_sampler_handle : samplers_handle.second)
         {
            ASSERT_ONCE(custom_sampler_handle.second.get() != native_sampler);
         }
      }
#endif

      device_data.custom_sampler_by_original_sampler.erase(sampler.handle);
   }

   // TODO: delete? do we need "upgraded_resources"? It's good to have it to handle edge cases (e.g. shared resources?)
   void OnInitResource(
      reshade::api::device* device,
      const reshade::api::resource_desc& desc,
      const reshade::api::subresource_data* initial_data,
      reshade::api::resource_usage initial_state,
      reshade::api::resource resource)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      const std::unique_lock lock(device_data.mutex);
      if (waiting_on_upgraded_resource_init)
      {
         // If this happened, some resource creation failed an "OnInitResource()" was never called after "OnCreateResource()".
         // It might have to do with the fact that the resource was shared.
         // Either way we could never catch all cases as we don't know when that happens, and this might have false positives as a desc isn't a unique pointer (two resources might use the same desc).
         // Then again, if the description was identical and the first would have failed, so would have the second one.
         ASSERT_ONCE(std::memcmp(&upgraded_resource_init_desc, &desc, sizeof(upgraded_resource_init_desc)) == 0);
         if (std::memcmp(&upgraded_resource_init_desc, &desc, sizeof(upgraded_resource_init_desc)) == 0)
         {
            ASSERT_ONCE(upgraded_resource_init_data == nullptr || initial_data->data == upgraded_resource_init_data);
         }
         else
         {
            ASSERT_ONCE(upgraded_resource_init_data == nullptr || initial_data->data != upgraded_resource_init_data);
         }
         // Delete the converted data we created (there's a chance of memory leaks if creation failed, though unlikely)
         delete upgraded_resource_init_data;

         device_data.upgraded_resources.emplace(resource.handle);
         waiting_on_upgraded_resource_init = false;
      }
      else
      {
         // Code mirrored from "OnCreateResource()" (though some filter cases are still missing)
         if (desc.type == reshade::api::resource_type::texture_2d && desc.texture.depth_or_layers == 1)
         {
            if ((desc.usage & (reshade::api::resource_usage::render_target | reshade::api::resource_usage::unordered_access)) != 0 && enable_texture_format_upgrades && texture_upgrade_formats.contains(desc.texture.format))
            {
               // Shared resources can call "OnInitResource()" without a "OnCreateResource()" (see "D3D11Device::OpenSharedResource"), as they've been created on a different device (and possibly API), so we can't always upgrade them,
               // though Luma only allows DX11 devices so it's probably guaranteed!
               ASSERT_ONCE((desc.flags & reshade::api::resource_flags::shared) == 0);
            }
         }
      }
   }

   // Define source pixel structure (8-bit per channel)
   struct R8G8B8A8_UNORM
   {
      uint8_t r, g, b, a;
   };
   struct B8G8R8A8_UNORM
   {
      uint8_t b, g, r, a;
   };
   struct R16G16B16A16_FLOAT
   {
      uint16_t r, g, b, a;
   };

   inline uint16_t XMConvertFloatToHalf(float Value)
   {
      __m128 V1 = _mm_set_ss(Value);
      __m128i V2 = _mm_cvtps_ph(V1, 0);
      return _mm_cvtsi128_si32(V2);
   }

   inline uint16_t ConvertFloatToHalf(float value)
   {
      // XMConvertFloatToHalf converts a float to a half, returning the 16-bit unsigned short representation.
      return XMConvertFloatToHalf(value);
   }
   
   template<typename T>
   void ConvertR8G8B8A8toR16G16B16A16(
      const T* src_data,
      R16G16B16A16_FLOAT* dst_data,
      size_t width,
      size_t height,
      size_t depth = 1)
   {
      size_t slice_size = width * height;

      for (size_t z = 0; z < depth; ++z)
      {
         size_t slice_offset = slice_size * z;

         for (size_t i = 0; i < slice_size; i++)
         {
            const T& pixel = src_data[slice_offset + i];

            // Read each channel and normalize from 0-255 to 0.0-1.0.
            float r = pixel.r / 255.0f;
            float g = pixel.g / 255.0f;
            float b = pixel.b / 255.0f;
            float a = pixel.a / 255.0f;

            // Convert normalized floats to half-floats.
            dst_data[i].r = ConvertFloatToHalf(r);
            dst_data[i].g = ConvertFloatToHalf(g);
            dst_data[i].b = ConvertFloatToHalf(b);
            dst_data[i].a = ConvertFloatToHalf(a);
         }
      }
   }

   bool IsMipOf(uint32_t base_w, uint32_t base_h, uint32_t w, uint32_t h)
   {
      if (w == 0 || h == 0 || base_w == 0 || base_h == 0)
         return false;

      // Check if w and h are powers-of-two divisions of base
      if (base_w < w || base_h < h)
         return false;

      // Check that downscaling factor is exact power of two
      bool valid_w = (base_w >> (std::countr_zero(base_w) - std::countr_zero(w))) == w;
      bool valid_h = (base_h >> (std::countr_zero(base_h) - std::countr_zero(h))) == h;

      return valid_w && valid_h;
   }

   bool ShouldUpgradeResource(const reshade::api::resource_desc& desc, const DeviceData& device_data)
   {
      if ((desc.usage & (reshade::api::resource_usage::render_target | reshade::api::resource_usage::unordered_access)) == 0 || !enable_texture_format_upgrades || !texture_upgrade_formats.contains(desc.texture.format))
      {
         return false;
      }

      // At least in DX11, any resource that isn't exclusively accessible by the GPU, can't be set as output (render target/unordered access).
		// These probably wouldn't have the RT/UA usage flags set anyway, or they'd fail on creation if they did.
      if (desc.heap != reshade::api::memory_heap::gpu_only)
      {
         ASSERT_ONCE(desc.heap != reshade::api::memory_heap::unknown && desc.heap != reshade::api::memory_heap::custom); // Unexpected heap types
         return false;
      }

      // Note: we can't fully exclude texture 2D arrays here, because they might still have 1 layer
      bool type_and_size_filter = desc.type == reshade::api::resource_type::texture_2d && desc.texture.depth_or_layers == 1;
      
      if (texture_format_upgrades_2d_size_filters != (uint32_t)TextureFormatUpgrades2DSizeFilters::All)
      {
         bool size_filter = false;
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution) != 0)
         {
            size_filter |= desc.texture.width == device_data.output_resolution.x && desc.texture.height == device_data.output_resolution.y;
         }
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::RenderResolution) != 0)
         {
            size_filter |= desc.texture.width == device_data.render_resolution.x && desc.texture.height == device_data.render_resolution.y;
         }
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::AspectRatio) != 0)
         {
            // Always scale from the smallest dimension, as that gives up more threshold, depending on how the devs scaled down textures (they can use multiple rounding models)
            float min_aspect_ratio = desc.texture.width <= desc.texture.height ? ((float)(desc.texture.width - texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)desc.texture.height) : ((float)desc.texture.width / (float)(desc.texture.height + texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
            float max_aspect_ratio = desc.texture.width <= desc.texture.height ? ((float)desc.texture.width / (float)(desc.texture.height - texture_format_upgrades_2d_aspect_ratio_pixel_threshold)) : ((float)(desc.texture.width + texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)desc.texture.height);
            float target_aspect_ratio = texture_format_upgrades_2d_target_aspect_ratio > 0.f ? texture_format_upgrades_2d_target_aspect_ratio : ((float)device_data.output_resolution.x / (float)device_data.output_resolution.y);
            size_filter |= target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON);
         }
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::Mips) != 0)
         {
            float2 max_resolution = device_data.output_resolution.y >= device_data.render_resolution.y ? device_data.output_resolution : device_data.render_resolution;
            size_filter |= IsMipOf(max_resolution.x, max_resolution.y, desc.texture.width, desc.texture.height);
         }
         type_and_size_filter &= size_filter;
      }

      switch (texture_format_upgrades_lut_dimensions)
      {
      case LUTDimensions::_1D:
      {
         // For 1D, "texture_format_upgrades_lut_size" is the whole width (usually they extend in width)
         type_and_size_filter |= desc.type == reshade::api::resource_type::texture_1d && desc.texture.width == texture_format_upgrades_lut_size && desc.texture.height == 1 && desc.texture.depth_or_layers == 1 && desc.texture.levels == 1;
         break;
      }
      default:
      [[fallthrough]];
      case LUTDimensions::_2D:
      {
         // For 2D, "texture_format_upgrades_lut_size" is the height, usually they extend in width and that's squared
         type_and_size_filter |= desc.type == reshade::api::resource_type::texture_2d && desc.texture.width == (texture_format_upgrades_lut_size * texture_format_upgrades_lut_size) && desc.texture.height == texture_format_upgrades_lut_size && desc.texture.depth_or_layers == 1 && desc.texture.levels == 1;
         break;
      }
      case LUTDimensions::_3D:
      {
         // For 3D, all the dimensions usually match
         type_and_size_filter |= desc.type == reshade::api::resource_type::texture_3d && desc.texture.width == texture_format_upgrades_lut_size && desc.texture.height == texture_format_upgrades_lut_size && desc.texture.depth_or_layers == texture_format_upgrades_lut_size && desc.texture.levels == 1;
         break;
      }
      }

      return type_and_size_filter;
   }

   // Note that swapchain textures (backbuffer) don't pass through here!
   bool OnCreateResource(
      reshade::api::device* device,
      reshade::api::resource_desc& desc,
      reshade::api::subresource_data* initial_data,
      reshade::api::resource_usage initial_state)
   {
      ASSERT_ONCE(device->get_api() == reshade::api::device_api::d3d11);

      // No need to clear "upgraded_resource_init_desc"/"upgraded_resource_init_data" from its last value
      waiting_on_upgraded_resource_init = false; // If the same thread called another "OnCreateResource()" before we got a "OnInitResource()", it implies the resource creation has failed and we didn't get that event (we have to do it by thread as device objects creation is thread safe and can be done by multiple threads concurrently)

      DeviceData& device_data = *device->get_private_data<DeviceData>();
      const std::shared_lock lock(device_data.mutex);
      
      if (ShouldUpgradeResource(desc, device_data))
      {
         // Note that upgrading typeless texture could have unforeseen consequences in some games, especially when the textures are then used as unsigned int or signed int etc (e.g. Trine 5)
         desc.texture.format = reshade::api::format::r16g16b16a16_float;
         waiting_on_upgraded_resource_init = true;
         upgraded_resource_init_desc = desc;
         bool converted_initial_data = false;
         // We need to convert the initial data to the new format
         if (initial_data != nullptr)
         {
            void* prev_data = initial_data->data;
            ASSERT_ONCE(initial_data->data != nullptr);

            constexpr size_t bytes_per_pixel = 8; // 4 for 8bpc, 8 for 16bpc
            const size_t buffer_size = desc.texture.width * desc.texture.height * desc.texture.depth_or_layers * bytes_per_pixel;

            switch (desc.texture.format)
            {
            case reshade::api::format::r8g8b8a8_unorm:
            case reshade::api::format::r8g8b8a8_unorm_srgb:
            case reshade::api::format::r8g8b8x8_unorm:
            case reshade::api::format::r8g8b8x8_unorm_srgb:
            {
               initial_data->data = new uint8_t[buffer_size];
               initial_data->row_pitch = desc.texture.width * bytes_per_pixel;
               initial_data->slice_pitch = initial_data->row_pitch * desc.texture.height;
               ConvertR8G8B8A8toR16G16B16A16((R8G8B8A8_UNORM*)prev_data, (R16G16B16A16_FLOAT*)initial_data->data, desc.texture.width, desc.texture.height, desc.texture.depth_or_layers);
               converted_initial_data = true;
               break;
            }
            case reshade::api::format::b8g8r8a8_unorm:
            case reshade::api::format::b8g8r8a8_unorm_srgb:
            case reshade::api::format::b8g8r8x8_unorm:
            case reshade::api::format::b8g8r8x8_unorm_srgb:
            {
               initial_data->data = new uint8_t[buffer_size];
               initial_data->row_pitch = desc.texture.width * bytes_per_pixel;
               initial_data->slice_pitch = initial_data->row_pitch * desc.texture.height;
               ConvertR8G8B8A8toR16G16B16A16((B8G8R8A8_UNORM*)prev_data, (R16G16B16A16_FLOAT*)initial_data->data, desc.texture.width, desc.texture.height, desc.texture.depth_or_layers);
               converted_initial_data = true;
               break;
            }
            case reshade::api::format::r16g16b16a16_float:
            {
               break;
            }
            default:
            {
               ASSERT_ONCE(false); // TODO: add support
               break;
            }
            }
         }
         upgraded_resource_init_data = converted_initial_data ? initial_data->data : nullptr;

         return true;
      }
      return false;
   }

   void OnDestroyResource(reshade::api::device* device, reshade::api::resource resource)
   {
      if (!device || device->get_private_data<DeviceData>() == nullptr)
      {
#if 0
         ASSERT_ONCE(false); // Happens when BioShock Infinite closes down (due to it using shared resources!), though it seems to be almost safe (could it be that the (now stale) device pointer has already been re-allocated? Probably not as this call comes from within the device object itself?)
#endif
         return;
      }
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      const std::unique_lock lock(device_data.mutex);
      device_data.upgraded_resources.erase(resource.handle);
   }

   bool OnCreateResourceView(
      reshade::api::device* device,
      reshade::api::resource resource,
      reshade::api::resource_usage usage_type,
      reshade::api::resource_view_desc& desc)
   {
		reshade::api::resource_view_type lut_dimensions = reshade::api::resource_view_type::unknown;
      switch (texture_format_upgrades_lut_dimensions)
      {
      case LUTDimensions::_1D:
      {
         lut_dimensions = reshade::api::resource_view_type::texture_1d;
         break;
      }
      default:
      [[fallthrough]];
      case LUTDimensions::_2D:
      {
         lut_dimensions = reshade::api::resource_view_type::texture_2d;
         break;
      }
      case LUTDimensions::_3D:
      {
         lut_dimensions = reshade::api::resource_view_type::texture_3d;
         break;
      }
      }

      // In DX11 apps can not pass a "DESC" when creating resource views, and DX11 will automatically generate the default one from it, we handle it through "reshade::api::resource_view_type::unknown".
      if (resource.handle != 0 && (desc.type == reshade::api::resource_view_type::unknown || desc.type == lut_dimensions || desc.type == reshade::api::resource_view_type::texture_2d || desc.type == reshade::api::resource_view_type::texture_2d_array || desc.type == reshade::api::resource_view_type::texture_2d_multisample || desc.type == reshade::api::resource_view_type::texture_2d_multisample_array))
      {
         const reshade::api::resource_desc resource_desc = device->get_resource_desc(resource);

         DeviceData& device_data = *device->get_private_data<DeviceData>();
         const std::shared_lock lock(device_data.mutex);

         // Needed because these were not in the upgraded resources list, but we upgraded the swapchain's textures,
         // some games randomly pick a view format when they can't pick a proper one (due to the format upgrades).
         if (swapchain_upgrade_type >= 1 && device_data.back_buffers.contains(resource.handle))
         {
            if (desc.type == reshade::api::resource_view_type::unknown)
            {
               desc.type = resource_desc.texture.samples <= 1 ? (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d : reshade::api::resource_view_type::texture_2d_array) : (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d_multisample : reshade::api::resource_view_type::texture_2d_multisample_array);
            }
            ASSERT_ONCE(desc.texture.level_count != 0);
            // Redirect typeless formats (not even sure they are supported, but it won't hurt to check)
            switch (resource_desc.texture.format)
            {
            case reshade::api::format::r16g16b16a16_typeless:
            {
               desc.format = reshade::api::format::r16g16b16a16_float;
               break;
            }
            case reshade::api::format::r8g8b8a8_typeless:
            {
               if (desc.format != reshade::api::format::r8g8b8a8_unorm_srgb)
                  desc.format = reshade::api::format::r8g8b8a8_unorm;
               break;
            }
            case reshade::api::format::b8g8r8a8_typeless:
            {
               if (desc.format != reshade::api::format::b8g8r8a8_unorm_srgb)
                  desc.format = reshade::api::format::b8g8r8a8_unorm;
               break;
            }
            case reshade::api::format::r10g10b10a2_typeless:
            {
               desc.format = reshade::api::format::r10g10b10a2_unorm;
               break;
            }
            default:
            {
               bool formats_compatible = false;
               bool rgba8_1 = desc.format == reshade::api::format::r8g8b8a8_unorm || desc.format == reshade::api::format::r8g8b8a8_unorm_srgb;
               bool rgba8_2 = resource_desc.texture.format == reshade::api::format::r8g8b8a8_unorm || resource_desc.texture.format == reshade::api::format::r8g8b8a8_unorm_srgb;
               bool bgra8_1 = desc.format == reshade::api::format::b8g8r8a8_unorm || desc.format == reshade::api::format::b8g8r8a8_unorm_srgb;
               bool bgra8_2 = resource_desc.texture.format == reshade::api::format::b8g8r8a8_unorm || resource_desc.texture.format == reshade::api::format::b8g8r8a8_unorm_srgb;
               formats_compatible |= rgba8_1 && rgba8_2;
               formats_compatible |= bgra8_1 && bgra8_2;
               // No need to force replace the view format if formats are compatible
               if (!formats_compatible)
                 desc.format = resource_desc.texture.format; // Should be R16G16B16A16F if "enable_swapchain_upgrade" is on (depending on "swapchain_upgrade_type")
               break;
            }
            }
            return true;
         }

#if DEVELOPMENT
         bool usage_filter = usage_type == reshade::api::resource_usage::render_target || usage_type == reshade::api::resource_usage::unordered_access || usage_type == reshade::api::resource_usage::shader_resource; // This is all of the possible types anyway...
         if (usage_filter && ShouldUpgradeResource(resource_desc, device_data) && resource_desc.texture.format == reshade::api::format::r16g16b16a16_float)
         {
            switch (desc.format)
            {
            default:
            {
               // Nobody should be creating a typeless view, though it might be a bug with the game's code in case of unexpected format upgrades
               ASSERT_ONCE(desc.format != reshade::api::format::r16g16b16a16_typeless && desc.format != reshade::api::format::r8g8b8a8_typeless && desc.format != reshade::api::format::b8g8r8a8_typeless && desc.format != reshade::api::format::b8g8r8x8_typeless);
               break;
            }
            case reshade::api::format::unknown:
            {
               // Happens when the call didn't provide a "DESC", creating a default view (because if we reached here, the texture is 16bpc float)
               ASSERT_ONCE(device_data.upgraded_resources.contains(resource.handle));
               break;
            }
            }
         }
#endif

         if (device_data.upgraded_resources.contains(resource.handle))
         {
            if (desc.type == reshade::api::resource_view_type::unknown)
            {
               if (resource_desc.type == reshade::api::resource_type::texture_3d)
               {
                  desc.type = reshade::api::resource_view_type::texture_3d;
               }
               else
               {
                  desc.type = resource_desc.texture.samples <= 1 ? (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d : reshade::api::resource_view_type::texture_2d_array) : (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d_multisample : reshade::api::resource_view_type::texture_2d_multisample_array); // We need to set it in case it was "reshade::api::resource_view_type::unknown", otherwise the format would also need to be unknown
               }
               desc.texture.first_level = 0;
               desc.texture.level_count = -1; // All levels (e.g. Dishonored 2 sets this to invalid values if the resource format was upgraded)
               desc.texture.first_layer = 0;
               desc.texture.layer_count = resource_desc.texture.depth_or_layers;
            }
               desc.format = reshade::api::format::r16g16b16a16_float;
            return true;
         }
      }

#if DEVELOPMENT
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      const std::shared_lock lock(device_data.mutex);
      if (desc.format != reshade::api::format::r16g16b16a16_float)
      {
         ASSERT_ONCE(!device_data.upgraded_resources.contains(resource.handle)); // Why did we get here in this case?
      }
      D3D11_TEXTURE2D_DESC texture_2d_desc;
      D3D11_TEXTURE3D_DESC texture_3d_desc;
      D3D11_TEXTURE1D_DESC texture_1d_desc;
      if (device_data.upgraded_resources.contains(resource.handle))
      {
         ID3D11Resource* native_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
         ID3D11Texture2D* texture_2d = nullptr;
         ID3D11Texture3D* texture_3d = nullptr;
         ID3D11Texture1D* texture_1d = nullptr;
         HRESULT hr_2d = native_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&texture_2d);
         HRESULT hr_3d = native_resource->QueryInterface(__uuidof(ID3D11Texture3D), (void**)&texture_3d);
         HRESULT hr_1d = native_resource->QueryInterface(__uuidof(ID3D11Texture1D), (void**)&texture_1d);
         if (SUCCEEDED(hr_2d) && texture_2d != nullptr)
         {
            texture_2d->GetDesc(&texture_2d_desc);
            ASSERT_ONCE(desc.type == reshade::api::resource_view_type::texture_2d || desc.type == reshade::api::resource_view_type::texture_2d_array || desc.type == reshade::api::resource_view_type::texture_2d_multisample || desc.type == reshade::api::resource_view_type::texture_2d_multisample_array);
            ASSERT_ONCE(desc.type != reshade::api::resource_view_type::unknown);
         }
         else if (SUCCEEDED(hr_3d) && texture_3d != nullptr)
         {
            texture_3d->GetDesc(&texture_3d_desc);
            ASSERT_ONCE(desc.type == reshade::api::resource_view_type::texture_3d);
            ASSERT_ONCE(desc.type != reshade::api::resource_view_type::unknown);
         }
         else if (SUCCEEDED(hr_1d) && texture_1d != nullptr)
         {
            texture_1d->GetDesc(&texture_1d_desc);
            ASSERT_ONCE(desc.type == reshade::api::resource_view_type::texture_1d);
            ASSERT_ONCE(desc.type != reshade::api::resource_view_type::unknown);
         }

         if (desc.type == reshade::api::resource_view_type::unknown)
         {
            const reshade::api::resource_desc resource_desc = device->get_resource_desc(resource);
            if (resource_desc.type == reshade::api::resource_type::texture_3d)
            {
               desc.type = reshade::api::resource_view_type::texture_3d;
            }
            else if (resource_desc.type == reshade::api::resource_type::texture_1d)
            {
               desc.type = reshade::api::resource_view_type::texture_1d;
            }
            else
            {
               desc.type = resource_desc.texture.samples <= 1 ? (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d : reshade::api::resource_view_type::texture_2d_array) : (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d_multisample : reshade::api::resource_view_type::texture_2d_multisample_array); // We need to set it in case it was "reshade::api::resource_view_type::unknown", otherwise the format would also need to be unknown
            }
         }
         desc.format = reshade::api::format::r16g16b16a16_float;
         return true;
      }
#endif // DEVELOPMENT
      return false;
   }

   void OnPushDescriptors(
      reshade::api::command_list* cmd_list,
      reshade::api::shader_stage stages,
      reshade::api::pipeline_layout layout,
      uint32_t param_index,
      const reshade::api::descriptor_table_update& update)
   {
      auto* device = cmd_list->get_device();
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
      DeviceData& device_data = *device->get_private_data<DeviceData>();

      switch (update.type)
      {
      default:
      break;
      case reshade::api::descriptor_type::sampler:
      {
         reshade::api::descriptor_table_update custom_update = update;
         bool any_modified = false;
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         for (uint32_t i = 0; i < update.count; i++)
         {
            const reshade::api::sampler& sampler = static_cast<const reshade::api::sampler*>(update.descriptors)[i];
            if (device_data.custom_sampler_by_original_sampler.contains(sampler.handle))
            {
               ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
               // Create the version of this sampler to match the current mip lod bias
               if (!device_data.custom_sampler_by_original_sampler[sampler.handle].contains(device_data.texture_mip_lod_bias_offset))
               {
                  D3D11_SAMPLER_DESC native_desc;
                  native_sampler->GetDesc(&native_desc);
                  const auto last_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;
                  shared_lock_samplers.unlock();
                  {
                     std::unique_lock unique_lock_samplers(s_mutex_samplers); // Only lock for reading if necessary. It doesn't matter if we released the shared lock above for a tiny amount of time, it's safe anyway
                     device_data.custom_sampler_by_original_sampler[sampler.handle][last_texture_mip_lod_bias_offset] = CreateCustomSampler(device_data, (ID3D11Device*)device->get_native(), native_desc);
                  }
                  shared_lock_samplers.lock();
               }
               // Update the customized descriptor data
               uint64_t custom_sampler_handle = (uint64_t)(device_data.custom_sampler_by_original_sampler[sampler.handle][device_data.texture_mip_lod_bias_offset].get());
               if (custom_sampler_handle != 0)
               {
                  reshade::api::sampler& custom_sampler = ((reshade::api::sampler*)(custom_update.descriptors))[i];
                  custom_sampler.handle = custom_sampler_handle;
                  any_modified |= true;
               }
            }
            else
            {
#if DEVELOPMENT
               // If recursive (already cloned) sampler ptrs are set, it's because the game somehow got the pointers and is re-using them (?),
               // this seems to happen when we change the ImGui settings for samplers a lot and quickly.
               bool recursive_or_null = sampler.handle == 0;
               ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
               for (const auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
               {
                  for (const auto& custom_sampler_handle : samplers_handle.second)
                  {
                     recursive_or_null |= custom_sampler_handle.second.get() == native_sampler;
                  }
               }
               ASSERT_ONCE(recursive_or_null || samplers_upgrade_mode == 0); // Shouldn't happen! (if we know the sampler set is "recursive", then we are good and don't need to replace this sampler again)
#if 0 // TODO: delete or restore in case the "recursive_or_null" assert above ever triggered (seems like it won't)
               if (sampler.handle != 0)
               {
                  ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
                  D3D11_SAMPLER_DESC native_desc;
                  native_sampler->GetDesc(&native_desc);
                  custom_sampler_by_original_sampler[sampler.handle] = CreateCustomSampler(device_data, (ID3D11Device*)device->get_native(), native_desc);
               }
#endif
#endif // DEVELOPMENT
            }
         }

         if (any_modified)
         {
            cmd_list->push_descriptors(stages, layout, param_index, custom_update);
         }
         break;
      }
      }
   }

#if DEVELOPMENT
   void OnMapTextureRegion(reshade::api::device* device, reshade::api::resource resource, uint32_t subresource, const reshade::api::subresource_box* box, reshade::api::map_access access, reshade::api::subresource_data* data)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();

      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            auto& cmd_list_data = *device_data.primary_command_list_data;
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data = {};
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::CPUWrite;
            trace_draw_call_data.command_list = device_data.primary_command_list;
            trace_draw_call_data.thread_id = std::this_thread::get_id();
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
            // Re-use the RTV data for simplicity
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }

      {
         const std::shared_lock lock(device_data.mutex);
         ASSERT_ONCE(!device_data.upgraded_resources.contains(resource.handle)); // This would probably fail!
      }

#if GAME_PREY // For Prey only (given we manually upgrade resources through native hooks)
      ID3D11Resource* native_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
      com_ptr<ID3D11Texture2D> resource_texture;
      HRESULT hr = native_resource->QueryInterface(&resource_texture);
      if (SUCCEEDED(hr))
      {
         D3D11_TEXTURE2D_DESC texture_2d_desc;
         resource_texture->GetDesc(&texture_2d_desc);
         if ((texture_2d_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0)
         {
            // Probably not supported, at least if it's an RT and we upgraded the format
            ASSERT_ONCE(texture_2d_desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS && texture_2d_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT);
         }
      }
#endif
   }

   bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      // Verify that we didn't miss any changes to the global g-buffer
      ASSERT_ONCE(!device_data.has_drawn_main_post_processing_previous || !device_data.cb_per_view_global_buffers.contains(buffer));
      return false;
   }

   bool OnUpdateTextureRegion(reshade::api::device* device, const reshade::api::subresource_data& data, reshade::api::resource resource, uint32_t subresource, const reshade::api::subresource_box* box)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();

      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            auto& cmd_list_data = *device_data.primary_command_list_data;
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data = {};
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::CPUWrite;
            trace_draw_call_data.command_list = device_data.primary_command_list;
            trace_draw_call_data.thread_id = std::this_thread::get_id();
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
            // Re-use the RTV data for simplicity
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }

      {
         const std::shared_lock lock(device_data.mutex);
         ASSERT_ONCE(!device_data.upgraded_resources.contains(resource.handle)); // This would probably fail!
      }

#if GAME_PREY // For Prey only (given we manually upgrade resources through native hooks)
      ID3D11Resource* native_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
      com_ptr<ID3D11Texture2D> resource_texture;
      HRESULT hr = native_resource->QueryInterface(&resource_texture);
      if (SUCCEEDED(hr))
      {
         D3D11_TEXTURE2D_DESC texture_2d_desc;
         resource_texture->GetDesc(&texture_2d_desc);
         if ((texture_2d_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0)
         {
            // Probably not supported, at least if it's an RT and we upgraded the format
            ASSERT_ONCE(texture_2d_desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS && texture_2d_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT);
         }
      }
#endif

      return false;
   }

   bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data = {};
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::ClearResource;
            trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
            trace_draw_call_data.thread_id = std::this_thread::get_id();
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(cmd_list->get_device()->get_resource_from_view(rtv).handle);
            // Re-use the RTV data for simplicity
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
      return false;
   }

   bool OnClearUnorderedAccessViewUInt(reshade::api::command_list* cmd_list, reshade::api::resource_view uav, const uint32_t values[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data = {};
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::ClearResource;
            trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
            trace_draw_call_data.thread_id = std::this_thread::get_id();
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(cmd_list->get_device()->get_resource_from_view(uav).handle);
            // Re-use the RTV data for simplicity
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
      return false;
   }

   bool OnClearUnorderedAccessViewFloat(reshade::api::command_list* cmd_list, reshade::api::resource_view uav, const float values[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data = {};
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::ClearResource;
            trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
            trace_draw_call_data.thread_id = std::this_thread::get_id();
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(cmd_list->get_device()->get_resource_from_view(uav).handle);
            // Re-use the RTV data for simplicity
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
      return false;
   }
#endif // DEVELOPMENT

   bool OnCopyResource(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest)
   {
#if DEVELOPMENT
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data = {};
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::CopyResource;
            trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
            trace_draw_call_data.thread_id = std::this_thread::get_id();
            ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(source.handle);
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
            // Re-use the SRV and RTV data for simplicity
            GetResourceInfo(source_resource, trace_draw_call_data.sr_size[0], trace_draw_call_data.sr_format[0], &trace_draw_call_data.sr_hash[0]);
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
#endif

      ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(source.handle);
      com_ptr<ID3D11Texture2D> source_resource_texture;
      HRESULT hr = source_resource->QueryInterface(&source_resource_texture);
      if (SUCCEEDED(hr))
      {
         ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
         com_ptr<ID3D11Texture2D> target_resource_texture;
         hr = target_resource->QueryInterface(&target_resource_texture);
         if (SUCCEEDED(hr))
         {
            D3D11_TEXTURE2D_DESC source_desc;
            D3D11_TEXTURE2D_DESC target_desc;
            source_resource_texture->GetDesc(&source_desc);
            target_resource_texture->GetDesc(&target_desc);

            if (source_desc.Width != target_desc.Width || source_desc.Height != target_desc.Height)
               return false;

            auto isUnorm8 = [](DXGI_FORMAT format)
               {
                  switch (format)
                  {
                  case DXGI_FORMAT_R8G8B8A8_TYPELESS:
                  case DXGI_FORMAT_R8G8B8A8_UNORM:
                  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                  case DXGI_FORMAT_B8G8R8A8_UNORM:
                  case DXGI_FORMAT_B8G8R8X8_UNORM:
                  case DXGI_FORMAT_B8G8R8A8_TYPELESS:
                  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                  case DXGI_FORMAT_B8G8R8X8_TYPELESS:
                  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                  // Hacky case (just as a shortcut)
                  case DXGI_FORMAT_R10G10B10A2_TYPELESS:
                  case DXGI_FORMAT_R10G10B10A2_UNORM:
                  return true;
                  }
                  return false;
               };
            auto isUnorm16 = [](DXGI_FORMAT format)
               {
                  switch (format)
                  {
                  case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                  case DXGI_FORMAT_R16G16B16A16_UNORM:
                  return true;
                  }
                  return false;
               };
            auto isFloat16 = [](DXGI_FORMAT format)
               {
                  switch (format)
                  {
                  case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                  case DXGI_FORMAT_R16G16B16A16_FLOAT:
                  return true;
                  }
                  return false;
               };
            auto isFloat11 = [](DXGI_FORMAT format)
               {
                  switch (format)
                  {
                  case DXGI_FORMAT_R11G11B10_FLOAT:
                  return true;
                  }
                  return false;
               };

            DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();

            // If we detected incompatible formats that were likely caused by Luma upgrading texture formats (of render targets only...),
            // do the copy in shader
            // TODO: add gamma to linear support (e.g. non sRGB views into sRGB views)?
            //TODOFT3: this doesn't fully work, this triggers once when a level loads in Prey? So we should make sure it works. Is it fixed now?
            if (((isUnorm8(target_desc.Format) || isUnorm16(target_desc.Format) || isFloat11(target_desc.Format)) && isFloat16(source_desc.Format))
               || ((isUnorm8(source_desc.Format) || isUnorm16(source_desc.Format) || isFloat11(source_desc.Format)) && isFloat16(target_desc.Format)))
            {
               const std::shared_lock lock(s_mutex_shader_objects);
               if (device_data.copy_vertex_shader == nullptr || device_data.copy_pixel_shader == nullptr)
               {
                  ASSERT_ONCE(false); // The custom shaders failed to be found (they have either been unloaded or failed to compile, or simply missing in the files)
                  // We can't continue, drawing with emtpy shaders would crash or skip the call
                  return false;
               }

               const auto* device = cmd_list->get_device();
               ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
               ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());

               //
               // Prepare resources:
               //
               ASSERT_ONCE((source_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0);
               com_ptr<ID3D11ShaderResourceView> source_resource_texture_view;
               D3D11_SHADER_RESOURCE_VIEW_DESC source_srv_desc;
               source_srv_desc.Format = source_desc.Format;
               // Redirect typeless and sRGB formats to classic UNORM, the "copy resource" functions wouldn't distinguish between these, as they copy by byte.
               switch (source_srv_desc.Format)
               {
               case DXGI_FORMAT_R10G10B10A2_TYPELESS:
               source_srv_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
               break;
               case DXGI_FORMAT_R8G8B8A8_TYPELESS:
               case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
               source_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
               break;
               case DXGI_FORMAT_B8G8R8A8_TYPELESS:
               case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
               source_srv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
               break;
               case DXGI_FORMAT_B8G8R8X8_TYPELESS:
               case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
               source_srv_desc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
               break;
               case DXGI_FORMAT_R16G16B16A16_TYPELESS:
               source_srv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
               break;
               }
               source_srv_desc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
               source_srv_desc.Texture2D.MipLevels = 1;
               source_srv_desc.Texture2D.MostDetailedMip = 0;
               hr = native_device->CreateShaderResourceView(source_resource_texture.get(), &source_srv_desc, &source_resource_texture_view);
               ASSERT_ONCE(SUCCEEDED(hr));

               com_ptr<ID3D11Texture2D> proxy_target_resource_texture;
               // We need to make a double copy if the target texture isn't a render target, unfortunately (we could intercept its creation and add the flag, or replace any further usage in this frame by redirecting all pointers
               // to the new copy we made, but for now this works)
               // TODO: we could also check if the target texture supports UAV writes (unlikely) and fall back on a Copy Compute Shader instead of a Pixel Shader, to avoid two further texture copies.
               if ((target_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0)
               {
                  // Create the persisting texture copy if necessary (if anything changed from the last copy).
                  // Theoretically all these textures have the same resolution as the screen so having one persisten texture should be ok.
                  // TODO: create more than one texture (one per format and one per resolution?) if ever needed
                  //TODOFT3: verify the above assumption, testing whether this texture is actually constantly re-created (it'd depend on the case)
                  D3D11_TEXTURE2D_DESC proxy_target_desc;
                  if (device_data.copy_texture.get() != nullptr)
                  {
                     device_data.copy_texture->GetDesc(&proxy_target_desc);
                  }
                  if (device_data.copy_texture.get() == nullptr || proxy_target_desc.Width != target_desc.Width || proxy_target_desc.Height != target_desc.Height || proxy_target_desc.Format != target_desc.Format)
                  {
                     proxy_target_desc = target_desc;
                     proxy_target_desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
                     proxy_target_desc.BindFlags &= ~D3D11_BIND_SHADER_RESOURCE;
                     proxy_target_desc.BindFlags &= ~D3D11_BIND_UNORDERED_ACCESS;
                     proxy_target_desc.CPUAccessFlags = 0;
                     proxy_target_desc.Usage = D3D11_USAGE_DEFAULT;
                     device_data.copy_texture = nullptr;
                     hr = native_device->CreateTexture2D(&proxy_target_desc, nullptr, &device_data.copy_texture);
                     ASSERT_ONCE(SUCCEEDED(hr));
                  }
                  proxy_target_resource_texture = device_data.copy_texture;
               }
               else
               {
                  proxy_target_resource_texture = target_resource_texture;
               }

               com_ptr<ID3D11RenderTargetView> target_resource_texture_view;
               D3D11_RENDER_TARGET_VIEW_DESC target_rtv_desc;
               target_rtv_desc.Format = target_desc.Format;
               switch (target_rtv_desc.Format)
               {
               case DXGI_FORMAT_R10G10B10A2_TYPELESS:
               target_rtv_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
               break;
               case DXGI_FORMAT_R8G8B8A8_TYPELESS:
               case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
               target_rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
               break;
               case DXGI_FORMAT_B8G8R8A8_TYPELESS:
               case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
               target_rtv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
               break;
               case DXGI_FORMAT_B8G8R8X8_TYPELESS:
               case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
               target_rtv_desc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
               break;
               case DXGI_FORMAT_R16G16B16A16_TYPELESS:
               target_rtv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
               break;
               }
               target_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2D;
               target_rtv_desc.Texture2D.MipSlice = 0;
               hr = native_device->CreateRenderTargetView(proxy_target_resource_texture.get(), &target_rtv_desc, &target_resource_texture_view);
               ASSERT_ONCE(SUCCEEDED(hr));

               DrawStateStack<DrawStateStackType::SimpleGraphics> draw_state_stack;
               draw_state_stack.Cache(native_device_context);

               DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), device_data.copy_vertex_shader.get(), device_data.copy_pixel_shader.get(), source_resource_texture_view.get(), target_resource_texture_view.get(), target_desc.Width, target_desc.Height, true);

               //
               // Copy our render target target resource into the non render target target resource if necessary:
               //
               if ((target_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0)
               {
                  native_device_context->CopyResource(target_resource_texture.get(), proxy_target_resource_texture.get());
               }

               draw_state_stack.Restore(native_device_context);
               return true;
            }
         }
      }

      return false;
   }

   bool OnCopyTextureRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box, reshade::api::resource dest, uint32_t dest_subresource, const reshade::api::subresource_box* dest_box, reshade::api::filter_mode filter /*Unused in DX11*/)
   {
      if (source_subresource == 0 && dest_subresource == 0 && (!source_box || (source_box->left == 0 && source_box->top == 0)) && (!dest_box || (dest_box->left == 0 && dest_box->top == 0)) && (!dest_box || !source_box || (source_box->width() == dest_box->width() && source_box->height() == dest_box->height())))
      {
         return OnCopyResource(cmd_list, source, dest);
      }
#if DEVELOPMENT
      else
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data = {};
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::CopyResource;
            trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
            trace_draw_call_data.thread_id = std::this_thread::get_id();
            ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(source.handle);
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
            // Re-use the SRV and RTV data for simplicity
            GetResourceInfo(source_resource, trace_draw_call_data.sr_size[0], trace_draw_call_data.sr_format[0], &trace_draw_call_data.sr_hash[0]);
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
#endif
      return false;
   }

   bool OnResolveTextureRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box, reshade::api::resource dest, uint32_t dest_subresource, uint32_t dest_x, uint32_t dest_y, uint32_t dest_z, reshade::api::format format)
   {
#if DEVELOPMENT
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data = {};
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::CopyResource;
            trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
            trace_draw_call_data.thread_id = std::this_thread::get_id();
            ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(source.handle);
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
            // Re-use the SRV and RTV data for simplicity
            GetResourceInfo(source_resource, trace_draw_call_data.sr_size[0], trace_draw_call_data.sr_format[0], &trace_draw_call_data.sr_hash[0]);
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
#endif // DEVELOPMENT

      if (source_subresource == 0 && dest_subresource == 0 && (!source_box || (source_box->left == 0 && source_box->top == 0)) && (dest_x == 0 && dest_y == 0 && dest_z == 0))
      {
         return OnCopyResource(cmd_list, source, dest);
      }

      ASSERT_ONCE(false);
      return false;
   }

   void OnReShadePresent(reshade::api::effect_runtime* runtime)
   {
      DeviceData& device_data = *runtime->get_device()->get_private_data<DeviceData>();
#if DEVELOPMENT
      {
         const std::unique_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
#if _DEBUG && LOG_VERBOSE
            reshade::log::message(reshade::log::level::info, "present()");
            reshade::log::message(reshade::log::level::info, "--- End Frame ---");
#endif
            trace_running = false;
            CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
            const std::shared_lock lock_trace_2(cmd_list_data.mutex_trace);
            trace_count = cmd_list_data.trace_draw_calls_data.size();
         }
         else if (trace_scheduled)
         {
#if _DEBUG && LOG_VERBOSE
            reshade::log::message(reshade::log::level::info, "--- Frame ---");
#endif
            // Split the trace logic over "two" frames
            trace_scheduled = false;
            {
               CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               cmd_list_data.trace_draw_calls_data.clear();
            }
            trace_count = 0;
            trace_running = true;
         }
      }
#endif // DEVELOPMENT

      // Dump new shaders (checking the "shaders_to_dump" count is theoretically not thread safe but it should work nonetheless as this is run every frame)
      if (auto_dump && !thread_auto_dumping_running && !shaders_to_dump.empty())
      {
         if (thread_auto_dumping.joinable())
         {
            thread_auto_dumping.join();
         }
         thread_auto_dumping_running = true;
         thread_auto_dumping = std::thread(AutoDumpShaders);
      }

      s_mutex_loading.lock_shared();
      // Load new shaders
      // We avoid running this if "thread_auto_compiling" is still running from boot.
      // Note that this thread doesn't really need to be by "device", but we did so to make it simpler, to automatically handle the "CreateCustomDeviceShaders()" shaders.
      if (auto_load && !last_pressed_unload && !thread_auto_compiling_running && !device_data.thread_auto_loading_running && !device_data.pipelines_to_reload.empty())
      {
         s_mutex_loading.unlock_shared();
         if (device_data.thread_auto_loading.joinable())
         {
            device_data.thread_auto_loading.join();
         }
         device_data.thread_auto_loading_running = true;
         device_data.thread_auto_loading = std::thread(AutoLoadShaders, &device_data);
      }
      else
      {
         s_mutex_loading.unlock_shared();
      }

      // Destroy the cloned pipelines in the following frame to avoid crashes
      bool has_pipelines_to_destroy = false;
      {
         // This is fine because we can afford delaying it even further in case they were changed in between the read and write locks here
         const std::shared_lock lock(s_mutex_generic);
         has_pipelines_to_destroy = !device_data.pipelines_to_destroy.empty();

      }
      if (has_pipelines_to_destroy)
      {
         const std::unique_lock lock(s_mutex_generic);
         for (auto pair : device_data.pipelines_to_destroy)
         {
            pair.second->destroy_pipeline(reshade::api::pipeline{ pair.first });
         }
         device_data.pipelines_to_destroy.clear();
      }

      if (needs_unload_shaders)
      {
         {
            const std::unique_lock lock_loading(s_mutex_loading);
            shaders_compilation_errors.clear();
         }
         UnloadCustomShaders(device_data);
#if 1 // Optionally unload all custom shaders data
         {
            const std::unique_lock lock_loading(s_mutex_loading);
            custom_shaders_cache.clear();
         }
#endif
         needs_unload_shaders = false;

#if !FORCE_KEEP_CUSTOM_SHADERS_LOADED
         // Unload customly created shader objects (from the shader code/binaries above),
         // to make sure they will re-create
         {
            const std::unique_lock lock(s_mutex_shader_objects);
            device_data.copy_vertex_shader = nullptr;
            device_data.copy_pixel_shader = nullptr;
            device_data.display_composition_pixel_shader = nullptr;
            static_assert(false, "Please add a function to clean up custom shader objects in game's implementations")
         }
#endif
      }

      if (!block_draw_until_device_custom_shaders_creation) s_mutex_shader_objects.lock_shared();
      // Force re-load shaders (which will also end up re-creating the custom device shaders) if async shaders loading on boot finished without being able to create custom shaders
      if (needs_load_shaders || (!block_draw_until_device_custom_shaders_creation && !thread_auto_compiling_running && !device_data.created_custom_shaders))
      {
         if (!block_draw_until_device_custom_shaders_creation) s_mutex_shader_objects.unlock_shared();
         // Cache the defines at compilation time
         {
            const std::unique_lock lock(s_mutex_shader_defines);
            ShaderDefineData::OnCompilation(shader_defines_data);
            shader_defines_data_index.clear();
            for (int i = 0; i < shader_defines_data.size(); i++)
            {
               shader_defines_data_index[string_view_crc32(std::string_view(shader_defines_data[i].compiled_data.GetName()))] = i;
            }
         }
         LoadCustomShaders(device_data);
         needs_load_shaders = false;
      }
      else
      {
         if (!block_draw_until_device_custom_shaders_creation) s_mutex_shader_objects.unlock_shared();
      }
   }

   bool OnReShadeSetEffectsState(reshade::api::effect_runtime* runtime, bool enabled)
   {
      DeviceData& device_data = *runtime->get_device()->get_private_data<DeviceData>();
      // Note that this is not called on startup (even if the ReShade effects are enabled by default)
      // We were going to read custom keyboard events like this "GetAsyncKeyState(VK_ESCAPE) & 0x8000", but this seems like a better design
      needs_unload_shaders = !enabled;
      last_pressed_unload = !enabled;
      needs_load_shaders = enabled; // This also re-compile shaders possibly
      const std::unique_lock lock(s_mutex_loading);
      device_data.pipelines_to_reload.clear();
      return false; // You can return true to deny the change
   }

   // Note that this can also happen when entering or exiting "FSE" games given that often they change resolutions (e.g. old DICE games, Unreal Engine games, ...)
   void OnReShadeReloadedEffects(reshade::api::effect_runtime* runtime)
   {
      if (!last_pressed_unload)
      {
         OnReShadeSetEffectsState(runtime, true); // This will load and recompile all shaders (there's no need to delete the previous pre-compiled cache)
      }
   }

   // Expects "s_mutex_dumping"
   void DumpShader(uint32_t shader_hash, bool auto_detect_type = true)
   {
      auto dump_path = GetShadersRootPath();
      if (!std::filesystem::exists(dump_path))
      {
         std::filesystem::create_directories(dump_path);
      }
      dump_path = dump_path / Globals::GAME_NAME / "Dump"; // We dump in the game specific folder
      if (!std::filesystem::exists(dump_path))
      {
         std::filesystem::create_directories(dump_path);
      }
      else if (!std::filesystem::is_directory(dump_path))
      {
         ASSERT_ONCE(false); // The target path is already taken by a file
         return;
      }

      dump_path /= Shader::Hash_NumToStr(shader_hash, true);

      auto* cached_shader = shader_cache.find(shader_hash)->second;

      // Automatically find the shader type and append it to the name (a bit hacky). This can make dumping relevantly slower.
      if (auto_detect_type)
      {
         if (cached_shader->disasm.empty())
         {
            auto disasm_code = Shader::DisassembleShader(cached_shader->data, cached_shader->size);
            if (disasm_code.has_value())
            {
               cached_shader->disasm.assign(disasm_code.value());
            }
            else
            {
               cached_shader->disasm.assign("DECOMPILATION FAILED");
            }
         }

         if (cached_shader->type == reshade::api::pipeline_subobject_type::geometry_shader
            || cached_shader->type == reshade::api::pipeline_subobject_type::vertex_shader
            || cached_shader->type == reshade::api::pipeline_subobject_type::pixel_shader
            || cached_shader->type == reshade::api::pipeline_subobject_type::compute_shader)
         {
            static const std::string template_geometry_shader_name = "gs_";
            static const std::string template_vertex_shader_name = "vs_";
            static const std::string template_pixel_shader_name = "ps_";
            static const std::string template_compute_shader_name = "cs_";
            static const std::string template_shader_model_version_name = "x_x";

            std::string_view template_shader_name;
            switch (cached_shader->type)
            {
            case reshade::api::pipeline_subobject_type::geometry_shader:
            {
               template_shader_name = template_geometry_shader_name;
               break;
            }
            case reshade::api::pipeline_subobject_type::vertex_shader:
            {
               template_shader_name = template_vertex_shader_name;
               break;
            }
            case reshade::api::pipeline_subobject_type::pixel_shader:
            {
               template_shader_name = template_pixel_shader_name;
               break;
            }
            case reshade::api::pipeline_subobject_type::compute_shader:
            {
               template_shader_name = template_compute_shader_name;
               break;
            }
            default:
            {
               template_shader_name = "xx_"; // Unknown
               break;
            }
            }
            for (char i = '0'; i <= '9'; i++)
            {
               std::string type_wildcard = std::string(template_shader_name) + i + '_';
               const auto type_index = cached_shader->disasm.find(type_wildcard);
               if (type_index != std::string::npos)
               {
                  const std::string type = cached_shader->disasm.substr(type_index, template_shader_name.length() + template_shader_model_version_name.length());
                  dump_path += ".";
                  dump_path += type;
                  break;
               }
            }
         }
      }

      dump_path += L".cso";

      // If the shader was already serialized, make sure the new one is of the same size, to catch the near impossible case
      // of two different shaders having the same hash
      if (std::filesystem::is_regular_file(dump_path))
      {
         ASSERT_ONCE(std::filesystem::file_size(dump_path) == cached_shader->size);
      }

      try
      {
         std::ofstream file(dump_path, std::ios::binary);

         file.write(static_cast<const char*>(cached_shader->data), cached_shader->size);

         if (!dumped_shaders.contains(shader_hash))
         {
            dumped_shaders.emplace(shader_hash);
         }
      }
      catch (const std::exception& e)
      {
      }
   }

   void AutoDumpShaders()
   {
      // Copy the "shaders_to_dump" so we don't have to lock "s_mutex_dumping" all the times
      std::unordered_set<uint32_t> shaders_to_dump_copy;
      {
         const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
         if (shaders_to_dump.empty())
         {
            thread_auto_dumping_running = false;
            return;
         }
         shaders_to_dump_copy = shaders_to_dump;
         shaders_to_dump.clear();
      }
      for (auto shader_to_dump : shaders_to_dump_copy)
      {
         const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
         // Set this to true in case your old dumped shaders have bad naming (e.g. missing the "ps_5_0" appendix) and you want to replace them (on the next boot, the duplicate shaders with the shorter name will be deleted)
         constexpr bool force_redump_shaders = false;
         if (force_redump_shaders || !dumped_shaders.contains(shader_to_dump))
         {
            DumpShader(shader_to_dump, true);
         }
#if DEVELOPMENT && 0 // Disabled as it's extremely slow // TODO: speed up (also, check ALLOW_SHADERS_DUMPING instead?)
         else
         {
            // Make sure two different shaders didn't have the same hash (we only check if they start by the same name/hash)
            auto dump_path = GetShadersRootPath() / Globals::GAME_NAME / "Dump";
            std::string shader_name = Shader::Hash_NumToStr(shader_to_dump, true);
            for (const auto& entry : std::filesystem::directory_iterator(dump_path))
            {
               if (entry.is_regular_file() && entry.path().filename().string().rfind(shader_name, 0) == 0)
               {
                  auto* cached_shader = shader_cache.find(shader_to_dump)->second;
                  ASSERT_ONCE(std::filesystem::file_size(entry) == cached_shader->size);
                  // Don't break as there might be more than one by the same hash (but of a different shader type)
               }
            }
         }
#endif
      }
      thread_auto_dumping_running = false;
   }

   void AutoLoadShaders(DeviceData* device_data)
   {
      // Copy the "pipelines_to_reload_copy" so we don't have to lock "s_mutex_loading" all the times
      std::unordered_set<uint64_t> pipelines_to_reload_copy;
      {
         const std::unique_lock lock_loading(s_mutex_loading);
         if (device_data->pipelines_to_reload.empty())
         {
            device_data->thread_auto_loading_running = false;
            return;
         }
         pipelines_to_reload_copy = device_data->pipelines_to_reload;
         device_data->pipelines_to_reload.clear();
      }
      if (pipelines_to_reload_copy.size() > 0)
      {
         LoadCustomShaders(*device_data, pipelines_to_reload_copy, !precompile_custom_shaders);
      }
      device_data->thread_auto_loading_running = false;
   }

   // @see https://pthom.github.io/imgui_manual_online/manual/imgui_manual.html
   // This runs within the swapchain "Present()" function, and thus it's thread safe
   void OnRegisterOverlay(reshade::api::effect_runtime* runtime)
   {
      DeviceData& device_data = *runtime->get_device()->get_private_data<DeviceData>();

      // Always do this in case a user changed the settings through ImGUI
      device_data.cb_luma_frame_settings_dirty = true;

#if DEVELOPMENT
      const bool refresh_cloned_pipelines = device_data.cloned_pipelines_changed.exchange(false);

      if (ImGui::Button("Trace"))
      {
         trace_scheduled = true;
      }
#if 0 // Currently not necessary
      ImGui::SameLine();
      ImGui::Checkbox("List Unique Shaders Only", &trace_list_unique_shaders_only);
#endif

      ImGui::SameLine();
      ImGui::Checkbox("Ignore Vertex Shaders", &trace_ignore_vertex_shaders);

      ImGui::SameLine();
      ImGui::PushID("##DumpShaders");
      // "ALLOW_SHADERS_DUMPING" is expected to be on here
      if (ImGui::Button(std::format("Dump Shaders ({})", shader_cache_count).c_str()))
      {
         const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
         // Force dump everything here
         for (auto shader : shader_cache)
         {
            DumpShader(shader.first, true);
         }
         shaders_to_dump.clear();
      }
      ImGui::PopID();

      ImGui::SameLine();
      ImGui::PushID("##AutoDumpCheckBox");
      if (ImGui::Checkbox("Auto Dump", &auto_dump))
      {
         if (!auto_dump && thread_auto_dumping.joinable())
         {
            thread_auto_dumping.join();
         }
      }
      ImGui::PopID();
#endif // DEVELOPMENT

#if DEVELOPMENT || TEST
      if (ImGui::Button(std::format("Unload Shaders ({})", device_data.cloned_pipeline_count).c_str()))
      {
         needs_unload_shaders = true;
         last_pressed_unload = true;
#if 0  // Not necessary anymore with "last_pressed_unload"
         // For consistency, disable auto load, it makes no sense for them to be on if we have unloaded shaders
         if (auto_load)
         {
            auto_load = false;
            if (device_data.thread_auto_loading.joinable())
            {
               device_data.thread_auto_loading.join();
            }
         }
#endif
         const std::unique_lock lock(s_mutex_loading);
         device_data.pipelines_to_reload.clear();
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Unload all compiled and replaced shaders. The numbers shows how many shaders are being replaced at this moment in the game, from the custom loaded/compiled ones.\nYou can use ReShade's Global Effects Toggle Shortcut to toggle these on and off.");
      }
      ImGui::SameLine();
#endif // DEVELOPMENT || TEST

      bool needs_compilation = false;
      {
         const std::shared_lock lock(s_mutex_shader_defines);
         needs_compilation = defines_need_recompilation;
         for (uint32_t i = 0; i < shader_defines_data.size() && !needs_compilation; i++)
         {
            needs_compilation |= shader_defines_data[i].NeedsCompilation();
         }
      }
#if !DEVELOPMENT && !TEST
      ImGui::BeginDisabled(!needs_compilation);
#endif
      static const std::string reload_shaders_button_title_error = std::string("Reload Shaders ") + std::string(ICON_FK_WARNING);
      static const std::string reload_shaders_button_title_outdated = std::string("Reload Shaders ") + std::string(ICON_FK_REFRESH);
      // We skip locking "s_mutex_loading" just to read the size of "shaders_compilation_errors".
      // We could maybe check "last_pressed_unload" instead of "cloned_pipeline_count", but that wouldn't work in case unloading shaders somehow failed.
      if (ImGui::Button(shaders_compilation_errors.empty() ? (device_data.cloned_pipeline_count ? (needs_compilation ? reload_shaders_button_title_outdated.c_str() : "Reload Shaders") : "Load Shaders") : reload_shaders_button_title_error.c_str()))
      {
         needs_unload_shaders = false;
         last_pressed_unload = false;
         needs_load_shaders = true;
         const std::unique_lock lock(s_mutex_loading);
         device_data.pipelines_to_reload.clear();
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         const std::shared_lock lock(s_mutex_loading);
#if !DEVELOPMENT
         if (shaders_compilation_errors.empty())
         {
#if TEST
            ImGui::SetTooltip((device_data.cloned_pipeline_count && needs_compilation) ? "Shaders recompilation is needed for the changed settings to apply\nYou can use ReShade's Recompile Effects Shortcut to recompile these." : "(Re)Compiles shaders");
#else
            ImGui::SetTooltip((device_data.cloned_pipeline_count && needs_compilation) ? "Shaders recompilation is needed for the changed settings to apply" : "(Re)Compiles shaders");
#endif
         }
         else
#endif
         {
            if (!shaders_compilation_errors.empty())
            {
               ImGui::SetTooltip(shaders_compilation_errors.c_str());
            }
         }
      }
#if !DEVELOPMENT && !TEST
      ImGui::EndDisabled();
#endif
#if DEVELOPMENT || TEST
      ImGui::SameLine();
      if (ImGui::Button("Clean Shaders Cache"))
      {
         const std::unique_lock lock_loading(s_mutex_loading);
         CleanShadersCache();
         // Force recompile all shaders the next time
         for (const auto& custom_shader_pair : custom_shaders_cache)
         {
            if (custom_shader_pair.second)
            {
               custom_shader_pair.second->preprocessed_hash = 0;
            }
         }
      }
#endif

#if DEVELOPMENT
      ImGui::SameLine();
      ImGui::PushID("##AutoLoadCheckBox");
      if (ImGui::Checkbox("Auto Load", &auto_load))
      {
         if (!auto_load && device_data.thread_auto_loading.joinable())
         {
            device_data.thread_auto_loading.join();
         }
         const std::unique_lock lock(s_mutex_loading);
         device_data.pipelines_to_reload.clear();
      }
      ImGui::PopID();
#endif // DEVELOPMENT

      if (ImGui::BeginTabBar("##TabBar", ImGuiTabBarFlags_None))
      {
#if DEVELOPMENT
         static int32_t selected_index = -1;
         static std::string highlighted_resource = {};
         bool changed_selected = false;
         ImGui::PushID("##ShadersTab");
         bool handle_shader_tab = trace_count > 0 && ImGui::BeginTabItem(std::format("Traced Shaders ({})", trace_count).c_str()); // No need for "s_mutex_trace" here
         ImGui::PopID();
         if (handle_shader_tab)
         {
            if (ImGui::BeginChild("HashList", ImVec2(100, -FLT_MIN), ImGuiChildFlags_ResizeX))
            {
               if (ImGui::BeginListBox("##HashesListbox", ImVec2(-FLT_MIN, -FLT_MIN)))
               {
                  const std::shared_lock lock_trace(s_mutex_trace); // We don't really need "s_mutex_trace" here as when that data is being written ImGUI isn't running, but...
                  if (!trace_running)
                  {
                     const std::shared_lock lock_generic(s_mutex_generic);
                     CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                     const std::shared_lock lock_trace_2(cmd_list_data.mutex_trace);
                     for (auto index = 0; index < trace_count; index++)
                     {
                        auto pipeline_handle = cmd_list_data.trace_draw_calls_data.at(index).pipeline_handle;
                        auto thread_id = cmd_list_data.trace_draw_calls_data.at(index).thread_id._Get_underlying_id(); // Possibly compiler dependent but whatever, cast to int alternatively
                        const bool is_selected = selected_index == index;
                        // Note that the pipelines can be run more than once so this will return the first one matching (there's only one actually, we don't have separate settings for their running instance, as that's runtime stuff)
                        const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
                        const bool is_valid = pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr;
                        std::stringstream name;
                        auto text_color = IM_COL32(255, 255, 255, 255); // White

                        bool found_highlighted_resource_write = false;
                        bool found_highlighted_resource_read = false;
                        if (!highlighted_resource.empty())
                        {
                           for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
                           {
                              if (found_highlighted_resource_read || found_highlighted_resource_write) break;
                              found_highlighted_resource_read |= cmd_list_data.trace_draw_calls_data.at(index).sr_hash[i] == highlighted_resource;
                           }
                           for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
                           {
                              if (found_highlighted_resource_write) break;
                              found_highlighted_resource_write |= cmd_list_data.trace_draw_calls_data.at(index).uar_hash[i] == highlighted_resource; // We consider UAV as write even if it's not necessarily one
                           }
                           for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
                           {
                              if (found_highlighted_resource_write) break;
                              found_highlighted_resource_write |= cmd_list_data.trace_draw_calls_data.at(index).rt_hash[i] == highlighted_resource;
                           }
                        }

                        if (is_valid && cmd_list_data.trace_draw_calls_data.at(index).type == TraceDrawCallData::TraceDrawCallType::Shader)
                        {
                           const auto pipeline = pipeline_pair->second;

                           // Highlight other draw calls with the same shader
                           bool same_as_selected = false;
                           if (/*!is_selected &&*/ selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= selected_index + 1 && cmd_list_data.trace_draw_calls_data.at(selected_index).type == TraceDrawCallData::TraceDrawCallType::Shader)
                           {
                              auto pipeline_handle_2 = cmd_list_data.trace_draw_calls_data.at(selected_index).pipeline_handle;
                              const auto pipeline_pair_2 = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle_2);
                              const bool is_valid_2 = pipeline_pair_2 != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair_2->second != nullptr;
                              if (is_valid_2)
                              {
                                 const auto pipeline_2 = pipeline_pair_2->second;
                                 if (pipeline_2->shader_hashes == pipeline->shader_hashes)
                                 {
#if 1 // Add a space before it
                                    name << "   ";
#endif
                                    same_as_selected = true;
                                 }
                              }
                           }

                           // Index - Thread ID (command list) - Shader Hash(es) - Shader Name
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0); // Fill up 3 slots for the index so the text is aligned

                           // Deferred
                           if (cmd_list_data.trace_draw_calls_data.at(index).command_list->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
                           {
                              name << " - " << thread_id << "*";
                           }
                           // Immediate
                           else
                           {
                              name << " - " << thread_id;
                           }

                           for (auto shader_hash : pipeline->shader_hashes)
                           {
                              name << " - " << PRINT_CRC32(shader_hash);
                           }

                           // Pick the default color by shader type
                           if (pipeline->HasVertexShader())
                           {
                              if (trace_ignore_vertex_shaders)
                              {
                                 continue;
                              }
                              text_color = IM_COL32(192, 192, 0, 255); // Yellow
                           }
                           else if (pipeline->HasComputeShader())
                           {
                              text_color = IM_COL32(192, 0, 192, 255); // Purple
                           }

                           const std::shared_lock lock_loading(s_mutex_loading);
                           const auto custom_shader = !pipeline->shader_hashes.empty() ? custom_shaders_cache[pipeline->shader_hashes[0]] : nullptr;

                           // Find if the shader has been modified
                           if (pipeline->cloned)
                           {
                              name << "*";

                              // For now just force picking the first shader linked to the pipeline, there should always only be one (?)
                              if (custom_shader != nullptr && custom_shader->is_hlsl && !custom_shader->file_path.empty())
                              {
                                 auto filename_string = custom_shader->file_path.filename().string();
                                 if (const auto hash_begin_index = filename_string.find("0x"); hash_begin_index != std::string::npos)
                                 {
                                    filename_string.erase(hash_begin_index); // Start deleting from where the shader hash(es) begin (e.g. "0x12345678.xx_x_x.hlsl")
                                 }
                                 if (filename_string.ends_with("_") || filename_string.ends_with("."))
                                 {
                                    filename_string.erase(filename_string.length() - 1);
                                 }
                                 if (!filename_string.empty())
                                 {
                                    name << " - " << filename_string;
                                 }
                              }

                              if (pipeline->HasVertexShader())
                              {
                                 text_color = IM_COL32(128, 255, 0, 255); // Yellow + Green
                              }
                              else if (pipeline->HasComputeShader())
                              {
                                 text_color = IM_COL32(128, 255, 128, 255); // Purple + Green
                              }
                              else
                              {
                                 text_color = IM_COL32(0, 255, 0, 255); // Green
                              }
                           }
                           else
                           {
                              std::optional<std::string> optional_name = GetD3DNameW(reinterpret_cast<ID3D11DeviceChild*>(pipeline->pipeline.handle));
                              if (optional_name.has_value())
                              {
                                 name << " - " << optional_name.value().c_str();
                              }
                           }
                           // Highlight loading error
                           if (custom_shader != nullptr && !custom_shader->compilation_errors.empty())
                           {
                              text_color = custom_shader->compilation_error ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 165, 0, 255); // Red for Error, Orange for Warning
                           }

                           if (same_as_selected)
                           {
#if 0 // We already do this better above
                              name << " - (Selected) ";
#endif
                              if (!is_selected)
                              {
                                 text_color = IM_COL32(192, 192, 192, 255); // Grey
                              }
                           }
                        }
                        else if (cmd_list_data.trace_draw_calls_data.at(index).type == TraceDrawCallData::TraceDrawCallType::CopyResource)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);

                           // Deferred
                           if (cmd_list_data.trace_draw_calls_data.at(index).command_list->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
                           {
                              name << " - " << thread_id << "*";
                           }
                           // Immediate
                           else
                           {
                              name << " - " << thread_id;
                           }

                           name << " - Copy Resource";
                           
                           text_color = IM_COL32(255, 105, 0, 255); // Orange
                        }
                        else if (cmd_list_data.trace_draw_calls_data.at(index).type == TraceDrawCallData::TraceDrawCallType::CPUWrite)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);

                           // Immediate context
                           {
                              name << " - " << thread_id;
                           }

                           name << " - Resource CPU Write";

                           text_color = IM_COL32(255, 105, 0, 255); // Orange
                        }
                        else if (cmd_list_data.trace_draw_calls_data.at(index).type == TraceDrawCallData::TraceDrawCallType::ClearResource)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);

                           // Immediate context
                           {
                              name << " - " << thread_id;
                           }

                           name << " - Clear Resource";

                           text_color = IM_COL32(255, 105, 0, 255); // Orange
                        }
                        else if (cmd_list_data.trace_draw_calls_data.at(index).type == TraceDrawCallData::TraceDrawCallType::Present)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << " - " << thread_id;
                           name << " - Present";

                           // Highlight the resource on the swapchain too, given that "Present" traces aren't tracked the same way
                           if (!highlighted_resource.empty() && !device_data.swapchains.empty())
                           {
                              // Not fully safe, but it should do for almost all cases
                              reshade::api::swapchain* swapchain = *device_data.swapchains.begin();
                              IDXGISwapChain* native_swapchain = (IDXGISwapChain*)(swapchain->get_native());
                              UINT back_buffer_index = swapchain->get_current_back_buffer_index();
                              com_ptr<ID3D11Texture2D> back_buffer;
                              native_swapchain->GetBuffer(back_buffer_index, IID_PPV_ARGS(&back_buffer));

                              std::string backbuffer_hash = std::to_string(std::hash<void*>{}(back_buffer.get()));
                              if (highlighted_resource == backbuffer_hash)
                              {
                                 found_highlighted_resource_read = true;
                              }
                           }
                        }
                        else if (cmd_list_data.trace_draw_calls_data.at(index).type == TraceDrawCallData::TraceDrawCallType::Custom)
                        {
                           text_color = IM_COL32(0, 0, 255, 255); // Blue

                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << " - " << cmd_list_data.trace_draw_calls_data.at(index).custom_name;
                        }
                        else
                        {
                           text_color = IM_COL32(255, 0, 0, 255); // Red
                           name << "ERROR: Trace data not found"; // The draw call either had an empty (e.g. pixel) shader set, or the game has since unloaded them
                        }

                        if (found_highlighted_resource_write || found_highlighted_resource_read)
                        {
                           text_color = IM_COL32(255, 192, 203, 255); // Pink
                           name << (found_highlighted_resource_write ? " - (Highlighted Resource Write)" : " - (Highlighted Resource Read)");
                        }

                        ImGui::PushStyleColor(ImGuiCol_Text, text_color);
                        if (ImGui::Selectable(name.str().c_str(), is_selected))
                        {
                           selected_index = index;
                           changed_selected = true;
                        }
                        ImGui::PopStyleColor();

                        if (is_selected)
                        {
                           ImGui::SetItemDefaultFocus();
                        }
                     }
                  }
                  else
                  {
                     selected_index = -1;
                  }
                  selected_index = min(selected_index, trace_count - 1); // Extra safety
                  ImGui::EndListBox();
               }
            }
            ImGui::EndChild(); // HashList

            ImGui::SameLine();
            if (ImGui::BeginChild("##ShaderDetails", ImVec2(0, 0)))
            {
               ImGui::BeginDisabled(selected_index == -1);
               if (ImGui::BeginTabBar("##ShadersCodeTab", ImGuiTabBarFlags_None))
               {
                  const bool open_disassembly_tab_item = ImGui::BeginTabItem("Disassembly");
                  static bool opened_disassembly_tab_item = false;
                  if (open_disassembly_tab_item)
                  {
                     static std::string disasm_string;
                     CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                     const std::shared_lock lock_trace(cmd_list_data.mutex_trace);
                     if (selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= selected_index + 1 && (changed_selected || opened_disassembly_tab_item != open_disassembly_tab_item))
                     {
                        const auto pipeline_handle = cmd_list_data.trace_draw_calls_data.at(selected_index).pipeline_handle;
                        const std::unique_lock lock(s_mutex_generic);
                        if (auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle); pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
                        {
                           const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
                           auto* cache = (!pipeline_pair->second->shader_hashes.empty() && shader_cache.contains(pipeline_pair->second->shader_hashes[0])) ? shader_cache[pipeline_pair->second->shader_hashes[0]] : nullptr;
                           if (cache && cache->disasm.empty())
                           {
                              auto disasm_code = Shader::DisassembleShader(cache->data, cache->size);
                              if (disasm_code.has_value())
                              {
                                 cache->disasm.assign(disasm_code.value());
                              }
                              else
                              {
                                 cache->disasm.assign("DECOMPILATION FAILED");
                              }
                           }
                           disasm_string.assign(cache ? cache->disasm : "");
                        }
                     }

                     if (ImGui::BeginChild("DisassemblyCode"))
                     {
                        ImGui::InputTextMultiline(
                           "##disassemblyCode",
                           const_cast<char*>(disasm_string.c_str()),
                           disasm_string.length(),
                           ImVec2(-FLT_MIN, -FLT_MIN),
                           ImGuiInputTextFlags_ReadOnly);
                     }
                     ImGui::EndChild(); // DisassemblyCode
                     ImGui::EndTabItem(); // Disassembly
                  }
                  opened_disassembly_tab_item = open_disassembly_tab_item;

                  ImGui::PushID("##LiveCodeTabItem");
                  const bool open_live_tab_item = ImGui::BeginTabItem("Live Code");
                  ImGui::PopID();
                  static bool opened_live_tab_item = false;
                  if (open_live_tab_item)
                  {
                     static std::string hlsl_string;
                     static bool hlsl_error = false;
                     static bool hlsl_warning = false;
                     CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                     const std::shared_lock lock_trace(cmd_list_data.mutex_trace);
                     if (selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= selected_index + 1 && (changed_selected || opened_live_tab_item != open_live_tab_item || refresh_cloned_pipelines))
                     {
                        bool hlsl_set = false;
                        auto pipeline_handle = cmd_list_data.trace_draw_calls_data.at(selected_index).pipeline_handle;

                        const std::shared_lock lock(s_mutex_generic);
                        if (auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
                           pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
                        {
                           const auto pipeline = pipeline_pair->second;
                           const std::shared_lock lock_loading(s_mutex_loading);
                           const auto custom_shader = !pipeline->shader_hashes.empty() ? custom_shaders_cache[pipeline->shader_hashes[0]] : nullptr;
                           // If the custom shader has a compilation error, print that, otherwise read the file text
                           if (custom_shader != nullptr && !custom_shader->compilation_errors.empty())
                           {
                              hlsl_string = custom_shader->compilation_errors;
                              hlsl_error = custom_shader->compilation_error;
                              hlsl_warning = !custom_shader->compilation_error;
                              hlsl_set = true;
                           }
                           else if (custom_shader != nullptr && custom_shader->is_hlsl && !custom_shader->file_path.empty())
                           {
                              if (!custom_shader->preprocessed_code.empty())
                              {
                                 // Remove line breaks as there's a billion of them in the preprocessed code
                                 std::istringstream in_stream(custom_shader->preprocessed_code);
                                 std::ostringstream out_stream;
                                 std::string line;
                                 unsigned int empty_lines_count = 0;
                                 while (std::getline(in_stream, line))
                                 {
                                    // Skip lines that begin line "#line n" (unless they also include the name of a header, wrapped around "), for some reason the concatenated source file includes many of these.
                                    // Also skip pragmas as they aren't particularly relevant when viewing the source in a tiny box (this is arguable!).
                                    // Also skip any empty line beyond 2 ones, as usually it's empty spaces (or tabs) generated by the source concatenation.
                                    bool is_meta_line = line.rfind("#line ", 0) == 0 && !line.contains("\"");
                                    bool is_pragma_line = line.rfind("#pragma ", 0) == 0;
                                    if (is_meta_line || is_pragma_line || line.empty() || std::all_of(line.begin(), line.end(), [](unsigned char c) { return std::isspace(c); }))
                                    {
                                       empty_lines_count++;
                                    }
                                    else
                                    {
                                       empty_lines_count = 0;
                                    }

                                    if (!is_meta_line && !is_pragma_line && empty_lines_count <= 2)
                                    {
                                       out_stream << line << '\n';
                                    }
                                 }
                                 hlsl_string = out_stream.str();
                                 hlsl_error = false;
                                 hlsl_warning = false;
                                 hlsl_set = true;
                              }
                              else
                              {
                                 auto result = ReadTextFile(custom_shader->file_path);
                                 if (result.has_value())
                                 {
                                    hlsl_string.assign(result.value());
                                    hlsl_error = false;
                                    hlsl_warning = false;
                                    hlsl_set = true;
                                 }
                              }
                              if (!hlsl_set)
                              {
                                 hlsl_string.assign("FAILED TO READ FILE");
                                 hlsl_error = true;
                                 hlsl_warning = false;
                                 hlsl_set = true;
                              }
                           }
                        }

                        if (!hlsl_set)
                        {
                           hlsl_string.clear();
                        }
                     }
                     opened_live_tab_item = open_live_tab_item;

                     if (ImGui::BeginChild("LiveCode"))
                     {
                        ImGui::PushStyleColor(ImGuiCol_Text, hlsl_error ? IM_COL32(255, 0, 0, 255) : (hlsl_warning ? IM_COL32(255, 165, 0, 255) : IM_COL32(255, 255, 255, 255))); // Red for Error, Orange for Warning, White for the rest
                        ImGui::InputTextMultiline(
                           "##liveCode",
                           const_cast<char*>(hlsl_string.c_str()),
                           hlsl_string.length(),
                           ImVec2(-FLT_MIN, -FLT_MIN),
                           ImGuiInputTextFlags_ReadOnly);
                        ImGui::PopStyleColor();
                     }
                     ImGui::EndChild(); // LiveCode
                     ImGui::EndTabItem(); // Live Code
                  }

                  ImGui::PushID("##SettingsTabItem");
                  const bool open_settings_tab_item = ImGui::BeginTabItem("Info & Settings");
                  ImGui::PopID();
                  if (open_settings_tab_item)
                  {
                     CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                     const std::shared_lock lock_trace(cmd_list_data.mutex_trace);
                     if (selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= selected_index + 1)
                     {
                        auto pipeline_handle = cmd_list_data.trace_draw_calls_data.at(selected_index).pipeline_handle;
                        bool reload = false;
                        bool recompile = false;

                        {
                           const std::unique_lock lock(s_mutex_generic);
                           if (auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle); pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
                           {
                              bool skip_pipeline = pipeline_pair->second->skip;
                              if (ImGui::BeginChild("Settings and Info"))
                              {
                                 if (!pipeline_pair->second->HasVertexShader())
                                 {
                                    ImGui::Checkbox("Skip Shader", &skip_pipeline);
                                 }
                                 if (pipeline_pair->second->cloned && ImGui::Button("Unload"))
                                 {
                                    UnloadCustomShaders(device_data, { pipeline_handle }, false, false);
                                 }
                                 if (ImGui::Button(pipeline_pair->second->cloned ? "Recompile" : "Load"))
                                 {
                                    reload = true;
                                    recompile = true; // If this shader wasn't cloned, we'd need to compile it probably as it might not have already been compiled. If it was cloned, then our intent is to re-compile it anyway
                                 }

                                 bool debug_draw_shader_enabled = false; // Whether this shader/pipeline instance is the one we are draw debugging

                                 if (pipeline_pair->second->HasPixelShader() || pipeline_pair->second->HasComputeShader())
                                 {
                                    debug_draw_shader_enabled = debug_draw_shader_hash == pipeline_pair->second->shader_hashes[0];

                                    int32_t target_instance = -1;
                                    // Automatically calculate the index of the instance of this pipeline run, to directly select it on selection (this works as long as the current scene draw calls match the ones in the trace)
                                    {
                                       target_instance = 0;
                                       for (int32_t i = 0; i < selected_index; i++)
                                       {
                                          if (pipeline_handle == cmd_list_data.trace_draw_calls_data.at(i).pipeline_handle)
                                             target_instance++;
                                       }

                                       debug_draw_shader_enabled &= debug_draw_pipeline_target_instance < 0 || debug_draw_pipeline_target_instance == target_instance;
                                    }

                                    // Note that this might not work properly if the render target textures are 3D or 1D etc
                                    if (debug_draw_shader_enabled ? ImGui::Button("Disable Debug Draw Shader Instance") : ImGui::Button("Debug Draw Shader Instance"))
                                    {
                                       ASSERT_ONCE(GetShaderDefineCompiledNumericalValue(DEVELOPMENT_HASH) >= 1); // Development flag is needed in shaders for this to output correctly
                                       ASSERT_ONCE(device_data.display_composition_pixel_shader); // This shader is necessary to draw this debug stuff

                                       if (debug_draw_shader_enabled)
                                       {
                                          debug_draw_pipeline = 0;
                                          debug_draw_shader_hash = 0;
                                          debug_draw_shader_hash_string[0] = 0;
                                       }
                                       else
                                       {
                                          debug_draw_pipeline = pipeline_pair->first; // Note: this is probably completely useless at the moment as we don't store the index of the pipeline instance the user had selected (e.g. "debug_draw_pipeline_target_instance")
                                          debug_draw_shader_hash = pipeline_pair->second->shader_hashes[0];
                                          std::string new_debug_draw_shader_hash_string = Shader::Hash_NumToStr(debug_draw_shader_hash);
                                          if (new_debug_draw_shader_hash_string.size() <= HASH_CHARACTERS_LENGTH)
                                             strcpy(&debug_draw_shader_hash_string[0], new_debug_draw_shader_hash_string.c_str());
                                          else
                                             debug_draw_shader_hash_string[0] = 0;
                                       }
                                       device_data.debug_draw_texture = nullptr;
                                       device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
                                       device_data.debug_draw_texture_size = {};
                                       debug_draw_pipeline_instance = 0;
#if 1 // We could also let the user settings persist if we wished so, but automatically setting them is usually better
                                       debug_draw_pipeline_target_instance = debug_draw_shader_enabled ? -1 : target_instance;
                                       debug_draw_mode = pipeline_pair->second->HasPixelShader() ? DebugDrawMode::RenderTarget : (pipeline_pair->second->HasComputeShader() ? DebugDrawMode::UnorderedAccessView : DebugDrawMode::ShaderResource); // Do it regardless of "debug_draw_shader_enabled"
                                       debug_draw_view_index = 0;
                                       //debug_draw_replaced_pass = false;
#endif
                                    }
                                 }

                                 if (pipeline_pair->second->HasVertexShader() || pipeline_pair->second->HasPixelShader() || pipeline_pair->second->HasComputeShader())
                                 {
                                    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
                                    {
                                       auto srv_format = cmd_list_data.trace_draw_calls_data.at(selected_index).srv_format[i];
                                       if (srv_format == DXGI_FORMAT_UNKNOWN) // Resource was not valid
                                       {
                                          continue;
                                       }
                                       auto sr_format = cmd_list_data.trace_draw_calls_data.at(selected_index).sr_format[i];
                                       auto sr_size = cmd_list_data.trace_draw_calls_data.at(selected_index).sr_size[i];
                                       auto sr_hash = cmd_list_data.trace_draw_calls_data.at(selected_index).sr_hash[i];

                                       ImGui::PushID(i);

                                       ImGui::Text("");
                                       ImGui::Text("SRV Index: %u", i);
                                       ImGui::Text("R Hash: %s", sr_hash.c_str());
                                       if (GetFormatName(sr_format) != nullptr)
                                       {
                                          ImGui::Text("R Format: %s", GetFormatName(sr_format));
                                       }
                                       else
                                       {
                                          ImGui::Text("R Format: %u", sr_format);
                                       }
                                       if (GetFormatName(srv_format) != nullptr)
                                       {
                                          ImGui::Text("RV Format: %s", GetFormatName(srv_format));
                                       }
                                       else
                                       {
                                          ImGui::Text("RV Format: %u", srv_format);
                                       }
                                       ImGui::Text("R Size: %ux%ux%u", sr_size.x, sr_size.y, sr_size.z);
                                       for (uint64_t upgraded_resource : device_data.upgraded_resources)
                                       {
                                          void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource);
                                          if (sr_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                          {
                                             ImGui::Text("R: Upgraded");
                                             break;
                                          }
                                       }

                                       const bool is_highlighted_resource = highlighted_resource == sr_hash;
                                       if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                       {
                                          highlighted_resource = is_highlighted_resource ? "" : sr_hash;
                                       }

                                       if (debug_draw_shader_enabled && (debug_draw_mode != DebugDrawMode::ShaderResource || debug_draw_view_index != i) && ImGui::Button("Debug Draw Resource"))
                                       {
                                          debug_draw_mode = DebugDrawMode::ShaderResource;
                                          debug_draw_view_index = i;
                                       }

                                       ImGui::PopID();
                                    }
                                 }

                                 if (pipeline_pair->second->HasPixelShader() || pipeline_pair->second->HasComputeShader())
                                 {
                                    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
                                    {
                                       auto uarv_format = cmd_list_data.trace_draw_calls_data.at(selected_index).uarv_format[i];
                                       if (uarv_format == DXGI_FORMAT_UNKNOWN) // Resource was not valid
                                       {
                                          continue;
                                       }
                                       auto uar_format = cmd_list_data.trace_draw_calls_data.at(selected_index).uar_format[i];
                                       auto uar_size = cmd_list_data.trace_draw_calls_data.at(selected_index).uar_size[i];
                                       auto uar_hash = cmd_list_data.trace_draw_calls_data.at(selected_index).uar_hash[i];

                                       ImGui::PushID(i + D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT); // Offset by the max amount of previous iterations from above

                                       ImGui::Text("");
                                       ImGui::Text("UAV Index: %u", i);
                                       ImGui::Text("R Hash: %s", uar_hash.c_str());
                                       if (GetFormatName(uar_format) != nullptr)
                                       {
                                          ImGui::Text("R Format: %s", GetFormatName(uar_format));
                                       }
                                       else
                                       {
                                          ImGui::Text("R Format: %u", uar_format);
                                       }
                                       if (GetFormatName(uarv_format) != nullptr)
                                       {
                                          ImGui::Text("RV Format: %s", GetFormatName(uarv_format));
                                       }
                                       else
                                       {
                                          ImGui::Text("RV Format: %u", uarv_format);
                                       }
                                       ImGui::Text("R Size: %ux%ux%u", uar_size.x, uar_size.y, uar_size.z);
                                       for (uint64_t upgraded_resource : device_data.upgraded_resources)
                                       {
                                          void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource);
                                          if (uar_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                          {
                                             ImGui::Text("R: Upgraded");
                                             break;
                                          }
                                       }

                                       const bool is_highlighted_resource = highlighted_resource == uar_hash;
                                       if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                       {
                                          highlighted_resource = is_highlighted_resource ? "" : uar_hash;
                                       }

                                       if (debug_draw_shader_enabled && (debug_draw_mode != DebugDrawMode::UnorderedAccessView || debug_draw_view_index != i) && ImGui::Button("Debug Draw Resource"))
                                       {
                                          debug_draw_mode = DebugDrawMode::UnorderedAccessView;
                                          debug_draw_view_index = i;
                                       }

                                       bool is_redirection_target = pipeline_pair->second->redirect_data.target_type == CachedPipeline::RedirectData::RedirectTargetType::UAV && pipeline_pair->second->redirect_data.target_index == i;
                                       if (pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::None && is_redirection_target && ImGui::Button("Disable Copy"))
                                       {
                                          pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::None;
                                          pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::None;
                                          pipeline_pair->second->redirect_data.source_index = 0;
                                          pipeline_pair->second->redirect_data.target_index = 0;
                                          is_redirection_target = false;
                                       }
                                       if ((pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::SRV || !is_redirection_target) && ImGui::Button("Copy from SRV"))
                                       {
                                          pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::SRV;
                                          pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::UAV;
                                          pipeline_pair->second->redirect_data.source_index = 0;
                                          pipeline_pair->second->redirect_data.target_index = i;
                                          is_redirection_target = true;
                                       }
                                       if ((pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::UAV || !is_redirection_target) && ImGui::Button("Copy from UAV"))
                                       {
                                          pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::UAV;
                                          pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::UAV;
                                          pipeline_pair->second->redirect_data.source_index = 0;
                                          pipeline_pair->second->redirect_data.target_index = i;
                                          is_redirection_target = true;
                                       }
                                       if (is_redirection_target)
                                       {
                                          ImGui::SliderInt("Copy from View Index", &pipeline_pair->second->redirect_data.source_index, 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT /*The largest allowed view count by type*/);
                                       }

                                       ImGui::PopID();
                                    }
                                 }

                                 if (pipeline_pair->second->HasPixelShader())
                                 {
                                    auto blend_desc = cmd_list_data.trace_draw_calls_data.at(selected_index).blend_desc;

                                    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
                                    {
                                       auto rtv_format = cmd_list_data.trace_draw_calls_data.at(selected_index).rtv_format[i];
                                       if (rtv_format == DXGI_FORMAT_UNKNOWN) // Resource was not valid
                                       {
                                          continue;
                                       }
                                       auto rt_format = cmd_list_data.trace_draw_calls_data.at(selected_index).rt_format[i];
                                       auto rt_size = cmd_list_data.trace_draw_calls_data.at(selected_index).rt_size[i];
                                       auto rt_hash = cmd_list_data.trace_draw_calls_data.at(selected_index).rt_hash[i];

                                       ImGui::PushID(i + D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT + D3D11_1_UAV_SLOT_COUNT); // Offset by the max amount of previous iterations from above

                                       ImGui::Text("");
                                       ImGui::Text("RT Index: %u", i);
                                       ImGui::Text("R Hash: %s", rt_hash.c_str());
                                       if (GetFormatName(rt_format) != nullptr)
                                       {
                                          ImGui::Text("R Format: %s", GetFormatName(rt_format));
                                       }
                                       else
                                       {
                                          ImGui::Text("R Format: %u", rt_format);
                                       }
                                       if (GetFormatName(rtv_format) != nullptr)
                                       {
                                          ImGui::Text("RV Format: %s", GetFormatName(rtv_format));
                                       }
                                       else
                                       {
                                          ImGui::Text("RV Format: %u", rtv_format);
                                       }
                                       ImGui::Text("R Size: %ux%ux%u", rt_size.x, rt_size.y, rt_size.z);
                                       //TODOFT: test these more
                                       for (uint64_t upgraded_resource : device_data.upgraded_resources)
                                       {
                                          void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource);
                                          if (rt_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                          {
                                             ImGui::Text("R: Upgraded");
                                             break;
                                          }
                                       }
                                       ImGui::Text("R Swapchain: %s", cmd_list_data.trace_draw_calls_data.at(selected_index).rt_is_swapchain[i] ? "True" : "False"); // TODO: add this for computer shaders / UAVs toos

                                       // See "ui_data.blend_mode" for details on usage
                                       if (blend_desc.RenderTarget[i].BlendEnable)
                                       {
                                          bool has_drawn_blend_rgb_text = false;
                                          if (blend_desc.RenderTarget[i].BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD)
                                          {
                                             if (blend_desc.RenderTarget[i].SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && blend_desc.RenderTarget[i].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                                             {
                                                ImGui::Text("Blend RGB Mode: Additive Color");
                                                has_drawn_blend_rgb_text = true;
                                             }
                                             else if (blend_desc.RenderTarget[i].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && blend_desc.RenderTarget[i].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                                             {
                                                ImGui::Text("Blend RGB Mode: Additive Alpha");
                                                has_drawn_blend_rgb_text = true;
                                             }
                                             else if (blend_desc.RenderTarget[i].SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && blend_desc.RenderTarget[i].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                                             {
                                                ImGui::Text("Blend RGB Mode: Premultiplied Alpha");
                                                has_drawn_blend_rgb_text = true;
                                             }
                                             else if (blend_desc.RenderTarget[i].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && blend_desc.RenderTarget[i].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                                             {
                                                ImGui::Text("Blend RGB Mode: Straight Alpha");
                                                has_drawn_blend_rgb_text = true;
                                             }
                                             // Often used for lighting, glow, or compositing effects where the destination alpha controls how much of the source contributes
                                             else if (blend_desc.RenderTarget[i].SrcBlend == D3D11_BLEND::D3D11_BLEND_DEST_ALPHA && blend_desc.RenderTarget[i].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                                             {
                                                ImGui::Text("Blend RGB Mode: Reverse Premultiplied Alpha");
                                                has_drawn_blend_rgb_text = true;
                                             }
                                          }
                                          if (!has_drawn_blend_rgb_text)
                                          {
                                             ImGui::Text("Blend RGB Mode: Unknown");
                                             has_drawn_blend_rgb_text = true;
                                          }

                                          bool has_drawn_blend_a_text = false;
                                          if (blend_desc.RenderTarget[i].BlendOpAlpha == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD)
                                          {
                                             if (blend_desc.RenderTarget[i].SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE && blend_desc.RenderTarget[i].DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE)
                                             {
                                                ImGui::Text("Blend A Mode: Additive");
                                                has_drawn_blend_a_text = true;
                                             }
                                             else if (blend_desc.RenderTarget[i].SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE && blend_desc.RenderTarget[i].DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO)
                                             {
                                                ImGui::Text("Blend A Mode: Source Alpha");
                                                has_drawn_blend_a_text = true;
                                             }
                                             else if (blend_desc.RenderTarget[i].SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO && blend_desc.RenderTarget[i].DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE)
                                             {
                                                ImGui::Text("Blend A Mode: Destination Alpha");
                                                has_drawn_blend_a_text = true;
                                             }
                                          }
                                          if (!has_drawn_blend_a_text)
                                          {
                                             ImGui::Text("Blend A Mode: Unknown");
                                             has_drawn_blend_a_text = true;
                                          }
                                       }
                                       else
                                       {
                                          ImGui::Text("Blend Mode: Disabled");
                                       }

                                       const bool is_highlighted_resource = highlighted_resource == rt_hash;
                                       if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                       {
                                          highlighted_resource = is_highlighted_resource ? "" : rt_hash;
                                       }

                                       if (debug_draw_shader_enabled && (debug_draw_mode != DebugDrawMode::RenderTarget || debug_draw_view_index != i) && ImGui::Button("Debug Draw Resource"))
                                       {
                                          debug_draw_mode = DebugDrawMode::RenderTarget;
                                          debug_draw_view_index = i;
                                       }

                                       bool is_redirection_target = pipeline_pair->second->redirect_data.target_type == CachedPipeline::RedirectData::RedirectTargetType::RTV && pipeline_pair->second->redirect_data.target_index == i;
                                       if (pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::None && is_redirection_target && ImGui::Button("Disable Copy"))
                                       {
                                          pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::None;
                                          pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::None;
                                          pipeline_pair->second->redirect_data.source_index = 0;
                                          pipeline_pair->second->redirect_data.target_index = 0;
                                          is_redirection_target = false;
                                       }
                                       if ((pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::SRV || !is_redirection_target) && ImGui::Button("Copy from SRV"))
                                       {
                                          pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::SRV;
                                          pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::RTV;
                                          pipeline_pair->second->redirect_data.source_index = 0;
                                          pipeline_pair->second->redirect_data.target_index = i;
                                          is_redirection_target = true;
                                       }
                                       if ((pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::UAV || !is_redirection_target) && ImGui::Button("Copy from UAV"))
                                       {
                                          pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::UAV;
                                          pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::RTV;
                                          pipeline_pair->second->redirect_data.source_index = 0;
                                          pipeline_pair->second->redirect_data.target_index = i;
                                          is_redirection_target = true;
                                       }
                                       if (is_redirection_target)
                                       {
                                          ImGui::SliderInt("Copy from View Index", &pipeline_pair->second->redirect_data.source_index, 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT /*The largest allowed view count by type*/);
                                       }

                                       ImGui::PopID();
                                    }

                                    ImGui::Text("");
                                    ImGui::Text("Depth Enabled: %s", cmd_list_data.trace_draw_calls_data.at(selected_index).depth_enabled ? "True" : "False");
                                    ImGui::Text("Strencil Enabled: %s", cmd_list_data.trace_draw_calls_data.at(selected_index).stencil_enabled ? "True" : "False");
                                 }

                                 if (pipeline_pair->second->HasPixelShader() || pipeline_pair->second->HasVertexShader())
                                 {
                                    ImGui::Text("");
                                    ImGui::Text("Scissors Enabled: %s", cmd_list_data.trace_draw_calls_data.at(selected_index).scissors ? "True" : "False");
                                    //TODOFT: do it with float print
                                    ImGui::Text("Viewport 0: x: %s y:%s w: %s h: %s",
                                       std::to_string(cmd_list_data.trace_draw_calls_data.at(selected_index).viewport_0.x).c_str(),
                                       std::to_string(cmd_list_data.trace_draw_calls_data.at(selected_index).viewport_0.y).c_str(),
                                       std::to_string(cmd_list_data.trace_draw_calls_data.at(selected_index).viewport_0.z).c_str(),
                                       std::to_string(cmd_list_data.trace_draw_calls_data.at(selected_index).viewport_0.w).c_str());
                                    
                                 }
                              }
                              ImGui::EndChild(); // Settings and Info
                              pipeline_pair->second->skip = skip_pipeline;
                           }
                        }

                        if (cmd_list_data.trace_draw_calls_data.at(selected_index).type == TraceDrawCallData::TraceDrawCallType::CopyResource
                           || cmd_list_data.trace_draw_calls_data.at(selected_index).type == TraceDrawCallData::TraceDrawCallType::CPUWrite
                           || cmd_list_data.trace_draw_calls_data.at(selected_index).type == TraceDrawCallData::TraceDrawCallType::ClearResource)
                        {
                           if (ImGui::BeginChild("Settings and Info"))
                           {
                              if (cmd_list_data.trace_draw_calls_data.at(selected_index).type == TraceDrawCallData::TraceDrawCallType::CopyResource)
                              {
                                 ImGui::PushID(0);
                                 auto sr_format = cmd_list_data.trace_draw_calls_data.at(selected_index).sr_format[0];
                                 auto sr_size = cmd_list_data.trace_draw_calls_data.at(selected_index).sr_size[0];
                                 auto sr_hash = cmd_list_data.trace_draw_calls_data.at(selected_index).sr_hash[0];
                                 ImGui::Text("Source R Hash: %s", sr_hash.c_str());
                                 if (GetFormatName(sr_format) != nullptr)
                                 {
                                    ImGui::Text("Source R Format: %s", GetFormatName(sr_format));
                                 }
                                 ImGui::Text("Source R Size: %ux%ux%u", sr_size.x, sr_size.y, sr_size.z);

                                 const bool is_highlighted_resource = highlighted_resource == sr_hash;
                                 if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                 {
                                    highlighted_resource = is_highlighted_resource ? "" : sr_hash;
                                 }

                                 ImGui::Text(""); // Empty line for spacing
                                 ImGui::PopID();
                              }

                              auto rt_format = cmd_list_data.trace_draw_calls_data.at(selected_index).rt_format[0];
                              auto rt_size = cmd_list_data.trace_draw_calls_data.at(selected_index).rt_size[0];
                              auto rt_hash = cmd_list_data.trace_draw_calls_data.at(selected_index).rt_hash[0];

                              ImGui::PushID(1);
                              ImGui::Text("Target R Hash: %s", rt_hash.c_str());
                              if (GetFormatName(rt_format) != nullptr)
                              {
                                 ImGui::Text("Target R Format: %s", GetFormatName(rt_format));
                              }
                              else
                              {
                                 ImGui::Text("Target R Format: %u", rt_format);
                              }
                              ImGui::Text("Target R Size: %ux%ux%u", rt_size.x, rt_size.y, rt_size.z);

#if 0 // TODO: implement for this case
                              ImGui::Text("Target R Swapchain: %s", cmd_list_data.trace_draw_calls_data.at(selected_index).rt_is_swapchain[0] ? "True" : "False");
#endif

                              const bool is_highlighted_resource = highlighted_resource == rt_hash;
                              if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                              {
                                 highlighted_resource = is_highlighted_resource ? "" : rt_hash;
                              }
                              
                              ImGui::PopID();
                           }
                           ImGui::EndChild(); // Settings and Info
                        }
                        
                        // We need to do this here or it'd deadlock due to "s_mutex_generic" trying to be locked in shared mod again
                        if (reload && pipeline_handle != 0)
                        {
                           LoadCustomShaders(device_data, { pipeline_handle }, recompile);
                        }
                     }

                     ImGui::EndTabItem(); // Settings
                  }

                  ImGui::EndTabBar(); // ShadersCodeTab
               }
               ImGui::EndDisabled();
            }
            ImGui::EndChild(); // ShaderDetails
            ImGui::EndTabItem(); // Traced Shaders
         }
#endif // DEVELOPMENT

         if (ImGui::BeginTabItem("Settings"))
         {
            const std::unique_lock lock_reshade(s_mutex_reshade); // Lock the entire scope for extra safety, though we are mainly only interested in keeping "cb_luma_frame_settings" safe

#if ENABLE_NGX
            ImGui::BeginDisabled(!device_data.dlss_sr_supported);
            bool optiscaler_detected = GetModuleHandle(TEXT("amd_fidelityfx_dx12.dll")) != NULL; // Make a guess on the presence of FSR DLL, just to inform the user it might be working
            bool fake_dlss_sr_enabled = false; // Force show that DLSS is disabled when it's not supported, to avoid confusion in case the user setting had stayed true
            if (ImGui::Checkbox(optiscaler_detected ? "DLSS/FSR Super Resolution" : "DLSS Super Resolution", device_data.dlss_sr_supported ? &dlss_sr : &fake_dlss_sr_enabled))
            {
               device_data.dlss_sr = dlss_sr;
               if (device_data.dlss_sr) device_data.dlss_sr_suppressed = false;
               reshade::set_config_value(runtime, NAME, "DLSSSuperResolution", dlss_sr);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("This replaces the game's native AA and dynamic resolution scaling implementations.\nSelect \"SMAA 2TX\" or \"TAA\" in the game's AA settings for DLSS/DLAA to engage.\nA tick will appear here when it's engaged and a warning will appear if it failed.\n\nRequires compatible Nvidia GPUs (or OptiScaler for FSR).");
            }

            ImGui::SameLine();
            if (dlss_sr != true && device_data.dlss_sr_supported)
            {
               ImGui::PushID("DLSS Super Resolution Enabled");
               if (ImGui::SmallButton(ICON_FK_UNDO))
               {
                  dlss_sr = true;
                  device_data.dlss_sr = true;
                  reshade::set_config_value(runtime, NAME, "DLSSSuperResolution", dlss_sr);
               }
               ImGui::PopID();
            }
            else
            {
               // Show that DLSS is engaged. Ignored if the game scene isn't rendering.
               // If DLSS currently can't run due to the user settings/state, or failed, show a warning.
               if (device_data.has_drawn_main_post_processing && device_data.dlss_sr /*&& device_data.cloned_pipeline_count != 0*/)
               {
                  ImGui::PushID("DLSS Super Resolution Active");
                  ImGui::BeginDisabled();
                  ImGui::SmallButton((device_data.taa_detected && device_data.has_drawn_dlss_sr_imgui && !device_data.dlss_sr_suppressed) ? ICON_FK_OK : ICON_FK_WARNING);
                  ImGui::EndDisabled();
                  ImGui::PopID();
               }
               else
               {
                  const auto& style = ImGui::GetStyle();
                  ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                  size.x += style.FramePadding.x;
                  size.y += style.FramePadding.y;
                  ImGui::InvisibleButton("", ImVec2(size.x, size.y));
               }
            }
            ImGui::EndDisabled();
#endif

            auto ChangeDisplayMode = [&](int display_mode, bool enable_hdr_on_display = true, IDXGISwapChain3* swapchain = nullptr)
               {
                  reshade::set_config_value(runtime, NAME, "DisplayMode", display_mode);
                  cb_luma_frame_settings.DisplayMode = display_mode;
                  OnDisplayModeChanged();
                  if (display_mode >= 1)
                  {
                     if (enable_hdr_on_display)
                     {
                        Display::SetHDREnabled(game_window);
                        bool dummy_bool;
                        Display::IsHDRSupportedAndEnabled(game_window, dummy_bool, hdr_enabled_display, swapchain); // This should always succeed, so we don't fallback to SDR in case it didn't
                     }
                     if (!reshade::get_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_frame_settings.ScenePeakWhite) || cb_luma_frame_settings.ScenePeakWhite <= 0.f)
                     {
                        cb_luma_frame_settings.ScenePeakWhite = device_data.default_user_peak_white;
                     }
                     if (!reshade::get_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_frame_settings.ScenePaperWhite))
                     {
                        cb_luma_frame_settings.ScenePaperWhite = default_paper_white;
                     }
                     if (!reshade::get_config_value(runtime, NAME, "UIPaperWhite", cb_luma_frame_settings.UIPaperWhite))
                     {
                        cb_luma_frame_settings.UIPaperWhite = default_paper_white;
                     }
                     // Align all the parameters for the SDR on HDR mode (the game paper white can still be changed)
                     if (display_mode >= 2)
                     {
                        // For now we don't default to 203 nits game paper white when changing to this mode
                        cb_luma_frame_settings.UIPaperWhite = cb_luma_frame_settings.ScenePaperWhite;
                        cb_luma_frame_settings.ScenePeakWhite = cb_luma_frame_settings.ScenePaperWhite; // No, we don't want "default_peak_white" here
                     }
                  }
                  else
                  {
                     cb_luma_frame_settings.ScenePeakWhite = display_mode == 0 ? srgb_white_level : (display_mode >= 2 ? default_paper_white : default_peak_white);
                     cb_luma_frame_settings.ScenePaperWhite = display_mode == 0 ? srgb_white_level : default_paper_white;
                     cb_luma_frame_settings.UIPaperWhite = display_mode == 0 ? srgb_white_level : default_paper_white;
                  }
               };

            auto DrawScenePaperWhite = [&](bool has_separate_ui_paper_white = true)
               {
                  static const char* scene_paper_white_name = "Scene Paper White";
                  static const char* paper_white_name = "Paper White";
                  if (ImGui::SliderFloat(has_separate_ui_paper_white ? scene_paper_white_name : paper_white_name, &cb_luma_frame_settings.ScenePaperWhite, srgb_white_level, 500.f, "%.f"))
                  {
                     cb_luma_frame_settings.ScenePaperWhite = max(cb_luma_frame_settings.ScenePaperWhite, 0.0);
                     reshade::set_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_frame_settings.ScenePaperWhite);
                  }
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("The \"average\" brightness of the game scene.\nChange this to your liking, just don't get too close to the peak white.\nHigher does not mean better (especially if you struggle to read UI text), the brighter the image is, the lower the dynamic range (contrast) is.\nThe in game settings brightness is best left at default.");
                  }
                  ImGui::SameLine();
                  if (cb_luma_frame_settings.ScenePaperWhite != default_paper_white)
                  {
                     ImGui::PushID(has_separate_ui_paper_white ? scene_paper_white_name : paper_white_name);
                     if (ImGui::SmallButton(ICON_FK_UNDO))
                     {
                        cb_luma_frame_settings.ScenePaperWhite = default_paper_white;
                        reshade::set_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_frame_settings.ScenePaperWhite);
                     }
                     ImGui::PopID();
                  }
                  else
                  {
                     const auto& style = ImGui::GetStyle();
                     ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                     size.x += style.FramePadding.x;
                     size.y += style.FramePadding.y;
                     ImGui::InvisibleButton("", ImVec2(size.x, size.y));
                  }
               };

            {
               // Note: this is fast enough that we can check it every frame.
               Display::IsHDRSupportedAndEnabled(game_window, hdr_supported_display, hdr_enabled_display, device_data.GetMainNativeSwapchain().get());
            }

            int display_mode = cb_luma_frame_settings.DisplayMode;
            int display_mode_max = 1;
            if (hdr_supported_display)
            {
#if DEVELOPMENT || TEST
               display_mode_max++; // Add "SDR in HDR for HDR" mode
#endif
            }
            const char* preset_strings[3] = {
                "SDR", // SDR (80 nits) on scRGB HDR for SDR (gamma sRGB, because Windows interprets scRGB as sRGB)
                "HDR",
                "SDR on HDR", // (Fake) SDR (baseline to 203 nits) on scRGB HDR for HDR (gamma 2.2) - Dev only, for quick comparisons
            };
            ImGui::BeginDisabled(!hdr_supported_display);
            if (ImGui::SliderInt("Display Mode", &display_mode, 0, display_mode_max, preset_strings[display_mode], ImGuiSliderFlags_NoInput))
            {
               ChangeDisplayMode(display_mode, true, device_data.GetMainNativeSwapchain().get());
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("Display Mode. Greyed out if HDR is not supported.\nThe HDR display calibration (peak white brightness) is retrieved from the OS (Windows 11 HDR user calibration or display EDID),\nonly adjust it if necessary.\nIt's suggested to only play the game in SDR while the display is in SDR mode (with gamma 2.2, not sRGB) (avoid SDR mode in HDR).");
            }
            ImGui::SameLine();
            // Show a reset button to enable HDR in the game if we are playing SDR in HDR
            if ((display_mode == 0 && hdr_enabled_display) || (display_mode >= 1 && !hdr_enabled_display))
            {
               ImGui::PushID("Display Mode");
               if (ImGui::SmallButton(ICON_FK_UNDO))
               {
                  display_mode = hdr_enabled_display ? 1 : 0;
                  ChangeDisplayMode(display_mode, false, device_data.GetMainNativeSwapchain().get());
               }
               ImGui::PopID();
            }
            else
            {
               const auto& style = ImGui::GetStyle();
               ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
               size.x += style.FramePadding.x;
               size.y += style.FramePadding.y;
               ImGui::InvisibleButton("", ImVec2(size.x, size.y));
            }

            const bool mod_active = device_data.cloned_pipeline_count != 0;
            const bool has_separate_ui_paper_white = GetShaderDefineCompiledNumericalValue(UI_DRAW_TYPE_HASH) >= 1;
            if (display_mode == 1)
            {
               ImGui::BeginDisabled(!mod_active);
               // We should this even if "device_data.cloned_pipeline_count" is 0
               if (ImGui::SliderFloat("Scene Peak White", &cb_luma_frame_settings.ScenePeakWhite, 400.0, 10000.f, "%.f"))
               {
                  if (cb_luma_frame_settings.ScenePeakWhite == device_data.default_user_peak_white)
                  {
                     reshade::set_config_value(runtime, NAME, "ScenePeakWhite", 0.f); // Store it as 0 to highlight that it's default (whatever the current or next display peak white is)
                  }
                  else
                  {
                     reshade::set_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_frame_settings.ScenePeakWhite);
                  }
               }
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               {
                  ImGui::SetTooltip("Set this to the brightest nits value your display (TV/Monitor) can emit.\nDirectly calibrating in Windows is suggested.");
               }
               ImGui::SameLine();
               if (cb_luma_frame_settings.ScenePeakWhite != device_data.default_user_peak_white)
               {
                  ImGui::PushID("Scene Peak White");
                  if (ImGui::SmallButton(ICON_FK_UNDO))
                  {
                     cb_luma_frame_settings.ScenePeakWhite = device_data.default_user_peak_white;
                     reshade::set_config_value(runtime, NAME, "ScenePeakWhite", 0.f);
                  }
                  ImGui::PopID();
               }
               else
               {
                  const auto& style = ImGui::GetStyle();
                  ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                  size.x += style.FramePadding.x;
                  size.y += style.FramePadding.y;
                  ImGui::InvisibleButton("", ImVec2(size.x, size.y));
               }
               ImGui::EndDisabled();
               DrawScenePaperWhite(has_separate_ui_paper_white && mod_active);
               constexpr bool supports_custom_ui_paper_white_scaling = true; // Currently all "post_process_space_define_index" modes support it (modify the tooltip otherwise)
               if (has_separate_ui_paper_white)
               {
                  ImGui::BeginDisabled(!supports_custom_ui_paper_white_scaling || !mod_active);
                  if (ImGui::SliderFloat("UI Paper White", supports_custom_ui_paper_white_scaling ? &cb_luma_frame_settings.UIPaperWhite : &cb_luma_frame_settings.ScenePaperWhite, srgb_white_level, 500.f, "%.f"))
                  {
                     cb_luma_frame_settings.UIPaperWhite = max(cb_luma_frame_settings.UIPaperWhite, 0.0);
                     reshade::set_config_value(runtime, NAME, "UIPaperWhite", cb_luma_frame_settings.UIPaperWhite);

                     // This is not safe to do, so let's rely on users manually setting this instead.
                     // Also note that this is a test implementation, it doesn't react to all places that change "cb_luma_frame_settings.UIPaperWhite", and does not restore the user original value on exit.
#if 0
                     // This makes the game cursor have the same brightness as the game's UI
                     SetSDRWhiteLevel(game_window, std::clamp(cb_luma_frame_settings.UIPaperWhite, 80.f, 480.f));
#endif
                  }
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("The peak brightness of the User Interface (with the exception of the 2D cursor, which is driven by the Windows SDR White Level).\nHigher does not mean better, change this to your liking.");
                  }
                  ImGui::SameLine();
                  if (cb_luma_frame_settings.UIPaperWhite != default_paper_white)
                  {
                     ImGui::PushID("UI Paper White");
                     if (ImGui::SmallButton(ICON_FK_UNDO))
                     {
                        cb_luma_frame_settings.UIPaperWhite = default_paper_white;
                        reshade::set_config_value(runtime, NAME, "UIPaperWhite", cb_luma_frame_settings.UIPaperWhite);
                     }
                     ImGui::PopID();
                  }
                  else
                  {
                     const auto& style = ImGui::GetStyle();
                     ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                     size.x += style.FramePadding.x;
                     size.y += style.FramePadding.y;
                     ImGui::InvisibleButton("", ImVec2(size.x, size.y));
                  }
                  ImGui::EndDisabled();
               }
            }
            else if (display_mode >= 2)
            {
               DrawScenePaperWhite(has_separate_ui_paper_white);
               cb_luma_frame_settings.UIPaperWhite = cb_luma_frame_settings.ScenePaperWhite;
               cb_luma_frame_settings.ScenePeakWhite = cb_luma_frame_settings.ScenePaperWhite;
            }

            game->DrawImGuiSettings(device_data);

#if DEVELOPMENT
				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Developer Settings"))
            {
               static std::string DevSettingsNames[LumaFrameDevSettings::SettingsNum];
               for (size_t i = 0; i < LumaFrameDevSettings::SettingsNum; i++)
               {
                  // These strings need to persist
                  if (DevSettingsNames[i].empty())
                  {
                     DevSettingsNames[i] = "Developer Setting " + std::to_string(i + 1);
                  }
                  float& value = cb_luma_frame_settings.DevSettings[i];
                  float& min_value = cb_luma_frame_dev_settings_min_value[i];
                  float& max_value = cb_luma_frame_dev_settings_max_value[i];
                  float& default_value = cb_luma_frame_dev_settings_default_value[i];
                  // Note: this will "fail" if we named two devs settings with the same name!
                  ImGui::SliderFloat(cb_luma_frame_dev_settings_names[i].empty() ? DevSettingsNames[i].c_str() : cb_luma_frame_dev_settings_names[i].c_str(), &value, min_value, max_value);
                  ImGui::SameLine();
                  if (value != default_value)
                  {
                     ImGui::PushID(DevSettingsNames[i].c_str());
                     if (ImGui::SmallButton(ICON_FK_UNDO))
                     {
                        value = default_value;
                     }
                     ImGui::PopID();
                  }
                  else
                  {
                     const auto& style = ImGui::GetStyle();
                     ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                     size.x += style.FramePadding.x;
                     size.y += style.FramePadding.y;
                     ImGui::InvisibleButton("", ImVec2(size.x, size.y));
                  }
               }

               ImGui::NewLine();
               ImGui::SliderInt("Tank Performance (Frame Sleep MS)", &frame_sleep_ms, 0, 100);
               ImGui::SliderInt("Tank Performance (Frame Sleep Interval)", &frame_sleep_interval, 1, 30);

               ImGui::NewLine();
               ImGuiInputTextFlags text_flags = ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AlwaysOverwrite | ImGuiInputTextFlags_NoUndoRedo;
               if (ImGui::InputTextWithHint("Debug Draw Shader Hash", "12345678", debug_draw_shader_hash_string, HASH_CHARACTERS_LENGTH + 1, text_flags))
               {
                  try
                  {
                     if (strlen(debug_draw_shader_hash_string) != HASH_CHARACTERS_LENGTH)
                     {
                        throw std::invalid_argument("Shader Hash has invalid length");
                     }
                     debug_draw_shader_hash = Shader::Hash_StrToNum(&debug_draw_shader_hash_string[0]);
                  }
                  catch (const std::exception& e)
                  {
                     debug_draw_shader_hash = 0;
                  }
                  // Keep the pipeline ptr if we are simply clearing the hash // TODO: why???
                  if (debug_draw_shader_hash != 0)
                  {
                     debug_draw_pipeline = 0;
                  }

                  device_data.debug_draw_texture = nullptr;
                  device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
                  device_data.debug_draw_texture_size = {};
               }
               ImGui::SameLine();
               bool debug_draw_enabled = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0; // It wants to be shown (if it manages!)
               // Show the reset button for both conditions or they could get stuck
               if (debug_draw_enabled)
               {
                  ImGui::PushID("Debug Draw");
                  if (ImGui::SmallButton(ICON_FK_UNDO))
                  {
                     debug_draw_shader_hash_string[0] = 0;
                     debug_draw_shader_hash = 0;
                     debug_draw_pipeline = 0;

                     device_data.debug_draw_texture = nullptr;
                     device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
                     device_data.debug_draw_texture_size = {};
                  }
                  ImGui::PopID();
               }
               else
               {
                  const auto& style = ImGui::GetStyle();
                  ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                  size.x += style.FramePadding.x;
                  size.y += style.FramePadding.y;
                  ImGui::InvisibleButton("", ImVec2(size.x, size.y));
               }
               if (debug_draw_enabled)
               {
                  // See "DebugDrawMode"
                  const char* debug_draw_mode_strings[3] = {
                      "Render Target",
                      "Unordered Access View",
                      "Shader Resource",
                  };
                  if (ImGui::SliderInt("Debug Draw Mode", &(int&)debug_draw_mode, (int)DebugDrawMode::RenderTarget, (int)DebugDrawMode::ShaderResource, debug_draw_mode_strings[(uint32_t)debug_draw_mode], ImGuiSliderFlags_NoInput))
                  {
                     debug_draw_view_index = 0;
                  }
                  if (debug_draw_mode == DebugDrawMode::RenderTarget)
                  {
                     ImGui::SliderInt("Debug Draw: Render Target Index", &debug_draw_view_index, 0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT - 1);
                  }
                  else if (debug_draw_mode == DebugDrawMode::UnorderedAccessView)
                  {
                     ImGui::SliderInt("Debug Draw: Unordered Access View", &debug_draw_view_index, 0, D3D11_1_UAV_SLOT_COUNT - 1); // "D3D11_PS_CS_UAV_REGISTER_COUNT" is smaller (we should theoretically use that unless we are a compute shader)
                  }
                  else /*if (debug_draw_mode == DebugDrawMode::ShaderResource)*/
                  {
                     ImGui::SliderInt("Debug Draw: Pixel Shader Resource Index", &debug_draw_view_index, 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1);
                  }
                  ImGui::Checkbox("Debug Draw: Allow Drawing Replaced Pass", &debug_draw_replaced_pass);
                  ImGui::SliderInt("Debug Draw: Pipeline Instance", &debug_draw_pipeline_target_instance, -1, 100); // In case the same pipeline was run more than once by the game, we can pick one to print
                  bool debug_draw_fullscreen = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::Fullscreen) != 0;
                  bool debug_draw_rend_res_scale = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::RenderResolutionScale) != 0;
                  bool debug_draw_show_alpha = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::ShowAlpha) != 0;
                  bool debug_draw_premultiply_alpha = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::PreMultiplyAlpha) != 0;
                  bool debug_draw_invert_colors = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::InvertColors) != 0;
                  bool debug_draw_linear_to_gamma = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::LinearToGamma) != 0;
                  bool debug_draw_gamma_to_linear = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::GammaToLinear) != 0;
                  bool debug_draw_flip_y = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::FlipY) != 0;
                  bool debug_draw_saturate = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::Saturate) != 0;
                  if (ImGui::Checkbox("Debug Draw Options: Fullscreen", &debug_draw_fullscreen))
                  {
                     if (debug_draw_fullscreen)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Fullscreen;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Fullscreen;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Render Resolution Scale", &debug_draw_rend_res_scale))
                  {
                     if (debug_draw_rend_res_scale)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::RenderResolutionScale;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RenderResolutionScale;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Show Alpha", &debug_draw_show_alpha))
                  {
                     if (debug_draw_show_alpha)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::ShowAlpha;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::ShowAlpha;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Premultiply Alpha", &debug_draw_premultiply_alpha))
                  {
                     if (debug_draw_premultiply_alpha)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::PreMultiplyAlpha;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::PreMultiplyAlpha;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Invert Colors", &debug_draw_invert_colors))
                  {
                     if (debug_draw_invert_colors)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::InvertColors;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::InvertColors;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Linear to Gamma", &debug_draw_linear_to_gamma))
                  {
                     if (debug_draw_linear_to_gamma)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::LinearToGamma;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::LinearToGamma;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Gamma to Linear", &debug_draw_gamma_to_linear))
                  {
                     if (debug_draw_gamma_to_linear)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::GammaToLinear;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::GammaToLinear;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Flip Y", &debug_draw_flip_y))
                  {
                     if (debug_draw_flip_y)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::FlipY;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::FlipY;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Saturate", &debug_draw_saturate))
                  {
                     if (debug_draw_saturate)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Saturate;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Saturate;
                     }
                  }
                  if (device_data.debug_draw_texture || debug_draw_auto_clear_texture) // Expected to be 2D
                  {
                     if (GetFormatName(device_data.debug_draw_texture_format) != nullptr)
                     {
                        ImGui::Text("Debug Draw Info: Texture (View) Format: %s", GetFormatName(device_data.debug_draw_texture_format));
                     }
                     else
                     {
                        ImGui::Text("Debug Draw Info: Texture (View) Format: %u", device_data.debug_draw_texture_format);
                     }
                     ImGui::Text("Debug Draw Info: Texture Size: %ux%ux%u", device_data.debug_draw_texture_size.x, device_data.debug_draw_texture_size.y, device_data.debug_draw_texture_size.z);
                  }
                  if (ImGui::Checkbox("Debug Draw: Auto Clear Texture", &debug_draw_auto_clear_texture)) // Is it persistent or not (in case the target texture stopped being found on newer frames). We could also "freeze" it and stop updating it, but we don't need that for now.
                  {
                     device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
                     device_data.debug_draw_texture_size = {};
                  }
               }

               ImGui::NewLine();
               // Requires a change in resolution to (~fully) apply
               if (enable_texture_format_upgrades ? ImGui::Button("Disable Texture Format Upgrades") : ImGui::Button("Enable Texture Format Upgrades"))
               {
                  enable_texture_format_upgrades = !enable_texture_format_upgrades;
               }
               if (enable_swapchain_upgrade ? ImGui::Button("Disable Swapchain Upgrade") : ImGui::Button("Enable Swapchain Upgrade"))
               {
                  enable_swapchain_upgrade = !enable_swapchain_upgrade;
               }

               ImGui::NewLine();
               if (enable_separate_ui_drawing ? ImGui::Button("Disable Separate UI Drawing") : ImGui::Button("Enable Separate UI Drawing"))
               {
                  enable_separate_ui_drawing = !enable_separate_ui_drawing;
               }

               game->DrawImGuiDevSettings(device_data);

#if UPGRADE_SAMPLERS
               ImGui::NewLine();
               bool samplers_changed = ImGui::SliderInt("Texture Samplers Upgrade Mode", &samplers_upgrade_mode, 0, 7);
               samplers_changed |= ImGui::SliderInt("Texture Samplers Upgrade Mode - 2", &samplers_upgrade_mode_2, 0, 6);
               ImGui::Checkbox("Custom Texture Samplers Mip LOD Bias", &custom_texture_mip_lod_bias_offset);
               if (samplers_upgrade_mode > 0 && custom_texture_mip_lod_bias_offset)
               {
                  const std::unique_lock lock_samplers(s_mutex_samplers);
                  samplers_changed |= ImGui::SliderFloat("Texture Samplers Mip LOD Bias", &device_data.texture_mip_lod_bias_offset, -8.f, +8.f);
               }
               if (samplers_changed)
               {
                  const std::unique_lock lock_samplers(s_mutex_samplers);
                  if (samplers_upgrade_mode <= 0)
                  {
                     device_data.custom_sampler_by_original_sampler.clear();
                  }
                  else
                  {
                     for (auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
                     {
                        ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(samplers_handle.first);
                        D3D11_SAMPLER_DESC native_desc;
                        native_sampler->GetDesc(&native_desc);
                        samplers_handle.second[device_data.texture_mip_lod_bias_offset] = CreateCustomSampler(device_data, (ID3D11Device*)runtime->get_device()->get_native(), native_desc);
                     }
                  }
               }
#endif // UPGRADE_SAMPLERS

               ImGui::TreePop();
            }
#endif // DEVELOPMENT

				ImGui::EndTabItem(); // Settings
         }

         if (ImGui::BeginTabItem("Advanced Settings"))
         {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("Shader Defines: reload shaders after changing these for the changes to apply (and save).\nSome settings are only editable in debug modes, and only apply if the \"DEVELOPMENT\" flag is turned on.\nDo not change unless you know what you are doing.");
            }

            const std::unique_lock lock_shader_defines(s_mutex_shader_defines);

            bool shader_defines_changed = false;

            // Show reset button
            {
               bool is_default = true;
               for (uint32_t i = 0; i < shader_defines_data.size() && is_default; i++)
               {
                  is_default = shader_defines_data[i].IsDefault() && !shader_defines_data[i].IsCustom();
               }
               ImGui::BeginDisabled(is_default);
               ImGui::PushID("Advanced Settings: Reset Defines");
               static const std::string reset_button_title = std::string(ICON_FK_UNDO) + std::string(" Reset");
               if (ImGui::Button(reset_button_title.c_str()))
               {
                  // Remove all newly added settings
                  ShaderDefineData::RemoveCustomData(shader_defines_data);

                  // Reset the rest to default
                  ShaderDefineData::Reset(shader_defines_data);

                  shader_defines_changed = true;
               }
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               {
                  ImGui::SetTooltip("Resets the defines to their default value");
               }
               ImGui::PopID();
               ImGui::EndDisabled();
            }
            // Show restore button (basically "undo")
            {
               bool needs_compilation = defines_need_recompilation;
               for (uint32_t i = 0; i < shader_defines_data.size() && !needs_compilation; i++)
               {
                  needs_compilation |= shader_defines_data[i].NeedsCompilation();
               }
               ImGui::BeginDisabled(!needs_compilation);
               ImGui::SameLine();
               ImGui::PushID("Advanced Settings: Restore Defines");
               static const std::string restore_button_title = std::string(ICON_FK_UNDO) + std::string(" Restore");
               if (ImGui::Button(restore_button_title.c_str()))
               {
                  ShaderDefineData::Restore(shader_defines_data);
                  shader_defines_changed = true;
               }
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               {
                  ImGui::SetTooltip("Restores the defines to the last compiled values, undoing any changes that haven't been applied");
               }
               ImGui::PopID();
               ImGui::EndDisabled();
            }

#if DEVELOPMENT || TEST
            ImGui::BeginDisabled(shader_defines_data.empty() || !shader_defines_data[shader_defines_data.size() - 1].IsCustom());
            ImGui::SameLine();
            ImGui::PushID("Advanced Settings: Remove Define");
            static const std::string remove_button_title = std::string(ICON_FK_MINUS) + std::string(" Remove");
            if (ImGui::Button(remove_button_title.c_str()))
            {
               shader_defines_data.pop_back();
               defines_count--;
               shader_defines_changed = true;
            }
            ImGui::PopID();
            ImGui::EndDisabled();

            ImGui::BeginDisabled(shader_defines_data.size() >= MAX_SHADER_DEFINES);
            ImGui::SameLine();
            ImGui::PushID("Advanced Settings: Add Define");
            static const std::string add_button_title = std::string(ICON_FK_PLUS) + std::string(" Add");
            if (ImGui::Button(add_button_title.c_str()))
            {
               // We don't default the value to 0 here, we leave it blank
               shader_defines_data.emplace_back();
               shader_defines_changed = true; // Probably not necessary in this case but ...
            }
            ImGui::PopID();
            ImGui::EndDisabled();
#endif

#if 0 // We simply add a "*" next to the reload shaders button now instead
            // Show when the defines are "dirty" (shaders need recompile)
            {
               bool needs_compilation = defines_need_recompilation;
               for (uint32_t i = 0; i < shader_defines_data.size() && !needs_compilation; i++)
               {
                  needs_compilation |= shader_defines_data[i].NeedsCompilation();
               }
               if (needs_compilation)
               {
                  ImGui::SameLine();
                  ImGui::PushID("Advanced Settings: Defines Dirty");
                  ImGui::BeginDisabled();
                  ImGui::SmallButton(ICON_FK_REFRESH); // Note: we don't want to modify "needs_load_shaders" here, there's another button for that
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("Recompile shaders needed to apply the changed settings");
                  }
                  ImGui::EndDisabled();
                  ImGui::PopID();
               }
            }
#endif

            uint8_t longest_shader_define_name_length = 0;
#if 1 // Enables automatic sizing
            for (uint32_t i = 0; i < shader_defines_data.size(); i++)
            {
               longest_shader_define_name_length = max(longest_shader_define_name_length, strlen(shader_defines_data[i].editable_data.GetName()));
            }
            longest_shader_define_name_length += 1; // Add an extra space to avoid it looking too crammed and lagging by one frame
#else
            uint8_t longest_shader_define_name_length = SHADER_DEFINES_MAX_NAME_LENGTH - 1; // Remove the null termination
#endif
            for (uint32_t i = 0; i < shader_defines_data.size(); i++)
            {
               // Don't render empty text fields that couldn't be filled due to them not being editable
               bool disabled = false;
               if (!shader_defines_data[i].IsNameEditable() && !shader_defines_data[i].IsValueEditable())
               {
#if !DEVELOPMENT && !TEST
                  if (shader_defines_data[i].IsCustom())
                  {
                     continue;
                  }
#endif
                  disabled = true;
                  ImGui::BeginDisabled();
               }

               bool show_tooltip = false;

               ImGui::PushID(shader_defines_data[i].name_hint.data());
               ImGuiInputTextFlags flags = ImGuiInputTextFlags_CharsNoBlank;
               if (!shader_defines_data[i].IsNameEditable())
               {
                  flags |= ImGuiInputTextFlags_ReadOnly;
               }
               // All characters should (roughly) have the same length
               ImGui::SetNextItemWidth(ImGui::CalcTextSize("0").x * longest_shader_define_name_length);
               // ImGUI doesn't work with std::string data, it seems to need c style char arrays.
               bool name_edited = ImGui::InputTextWithHint("", shader_defines_data[i].name_hint.data(), shader_defines_data[i].editable_data.GetName(), std::size(shader_defines_data[i].editable_data.name) /*SHADER_DEFINES_MAX_NAME_LENGTH*/, flags);
               show_tooltip |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
               ImGui::PopID();

               // TODO: fix this, it doesn't seem to work
               auto ModulateValueText = [](ImGuiInputTextCallbackData* data) -> int
                  {
#if 0
                     if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
                     {
                        if (data->Buf[0] == '\0')
                        {
                           // SHADER_DEFINES_MAX_VALUE_LENGTH
#if 0 // Better implementation (actually resets to default when the text was cleaned (invalid value)) (space and - can also be currently written to in the value text field)
                           data->Buf[0] = shader_defines_data[i].default_data.value[0];
                           data->Buf[1] = shader_defines_data[i].default_data.value[1];
#else
                           data->Buf[0] == '0';
                           data->Buf[1] == '\0';
#endif
                           data->BufDirty = true;
                        };
                     };
#endif
                     return 0;
                  };

               ImGui::SameLine();
               ImGui::PushID(shader_defines_data[i].value_hint.data());
               flags = ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AlwaysOverwrite | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_CallbackEdit;
               if (!shader_defines_data[i].IsValueEditable())
               {
                  flags |= ImGuiInputTextFlags_ReadOnly;
               }
               ImGui::SetNextItemWidth(ImGui::CalcTextSize("00").x);
               bool value_edited = ImGui::InputTextWithHint("", shader_defines_data[i].value_hint.data(), shader_defines_data[i].editable_data.GetValue(), std::size(shader_defines_data[i].editable_data.value) /*SHADER_DEFINES_MAX_VALUE_LENGTH*/, flags, ModulateValueText);
               show_tooltip |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
               // Avoid having empty values unless the default value also was empty. This is a worse implementation of the "ImGuiInputTextFlags_CallbackEdit" above, which we can't get to work.
               // If the value was empty to begin with, we leave it, to avoid confusion.
               if (value_edited && shader_defines_data[i].IsValueEmpty())
               {
                  // SHADER_DEFINES_MAX_VALUE_LENGTH
                  shader_defines_data[i].editable_data.value[0] = shader_defines_data[i].default_data.value[0];
                  shader_defines_data[i].editable_data.value[1] = shader_defines_data[i].default_data.value[1];
#if 0 // This would only appear for 1 frame at the moment
                  if (show_tooltip)
                  {
                     ImGui::SetTooltip(shader_defines_data[i].value_hint.c_str());
                     show_tooltip = false;
                  }
#endif
               }
#if 0 // Disabled for now as this is not very user friendly and could accidentally happen if two defines start with the same name.
               // Reset the define name if it matches another one
               if (name_edited && ShaderDefineData::ContainsName(shader_defines_data, shader_defines_data[i].editable_data.GetName(), i))
               {
                  shader_defines_data[i].Clear();
               }
#endif
               ImGui::PopID();

               if (disabled)
               {
                  ImGui::EndDisabled();
               }

               if (name_edited || value_edited)
               {
                  shader_defines_changed = true;
               }

               if (show_tooltip && shader_defines_data[i].IsNameDefault() && shader_defines_data[i].HasTooltip())
               {
                  ImGui::SetTooltip(shader_defines_data[i].GetTooltip());
               }
            }

            if (shader_defines_changed)
            {
               game->OnShaderDefinesChanged();
            }

            ImGui::EndTabItem();
         }

#if DEVELOPMENT || TEST
         if (ImGui::BeginTabItem("Info"))
         {
            std::string text;

            const std::shared_lock lock(device_data.mutex);
            {
               for (auto back_buffer : device_data.back_buffers)
               {
                  ImGui::Text("Swapchain Format: ", "");

                  reshade::api::resource resource;
                  resource.handle = back_buffer;
                  const reshade::api::resource_desc resource_desc = runtime->get_device()->get_resource_desc(resource);
                  std::ostringstream oss;
                  oss << resource_desc.texture.format;
                  text = oss.str();
                  ImGui::Text(text.c_str(), "");
                  ImGui::NewLine();
               }

               ImGui::Text("Upgraded Textures: ", "");
               text = std::to_string((int)device_data.upgraded_resources.size());
               ImGui::Text(text.c_str(), "");
            }

            ImGui::NewLine();
            ImGui::Text("Render Resolution: ", "");
            text = std::to_string((int)device_data.render_resolution.x) + " " + std::to_string((int)device_data.render_resolution.y);
            ImGui::Text(text.c_str(), "");

            ImGui::NewLine();
            ImGui::Text("Output Resolution: ", "");
            text = std::to_string((int)device_data.output_resolution.x) + " " + std::to_string((int)device_data.output_resolution.y);
            ImGui::Text(text.c_str(), "");

            if (device_data.dlss_sr)
            {
               ImGui::NewLine();
               ImGui::Text("DLSS Target Resolution Scale: ", "");
               text = std::to_string(device_data.dlss_render_resolution_scale);
               ImGui::Text(text.c_str(), "");
            }

            if (device_data.dlss_sr && device_data.cloned_pipeline_count != 0)
            {
               ImGui::NewLine();
               ImGui::Text("DLSS Scene Pre Exposure: ", "");
               text = std::to_string(device_data.dlss_scene_pre_exposure);
               ImGui::Text(text.c_str(), "");
            }

            game->PrintImGuiInfo(device_data);

            ImGui::EndTabItem(); // Info
         }
#endif // DEVELOPMENT || TEST

         if (ImGui::BeginTabItem("About"))
         {
            game->PrintImGuiAbout();

            ImGui::NewLine();
            static const std::string version = "Version: " + std::to_string(Globals::VERSION);
            ImGui::Text(version.c_str());

            ImGui::EndTabItem(); // About
         }

         ImGui::EndTabBar(); // TabBar
      }
   }
} // namespace

void Init(bool async)
{
   has_init = true;

#if ALLOW_SHADERS_DUMPING
   // Add all the shaders we have already dumped to the dumped list to avoid live re-dumping them
   dumped_shaders.clear();
   std::set<std::filesystem::path> dumped_shaders_paths;
   auto dump_path = GetShadersRootPath() / Globals::GAME_NAME / "Dump";
   // No need to create the directory here if it didn't already exist
   if (std::filesystem::is_directory(dump_path))
   {
      const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
      for (const auto& entry : std::filesystem::directory_iterator(dump_path))
      {
         if (!entry.is_regular_file()) continue;
         const auto& entry_path = entry.path();
         if (entry_path.extension() != ".cso") continue;
         const auto& entry_strem_string = entry_path.stem().string();
         if (entry_strem_string.starts_with("0x") && entry_strem_string.length() >= 2 + HASH_CHARACTERS_LENGTH)
         {
            const std::string shader_hash_string = entry_strem_string.substr(2, HASH_CHARACTERS_LENGTH);
            try
            {
               uint32_t shader_hash = Shader::Hash_StrToNum(shader_hash_string);
               bool duplicate = dumped_shaders.contains(shader_hash);
#if DEVELOPMENT
               ASSERT_ONCE(!duplicate); // We have a duplicate shader dumped, cancel here to avoid deleting it
#endif
               if (duplicate)
               {
                  for (const auto& prev_entry_path : dumped_shaders_paths)
                  {
                     if (prev_entry_path.string().contains(shader_hash_string))
                     {
                        // Delete the old version if it's shorter in name (e.g. it might have missed the "ps_5_0" appendix, or simply missing a name we manually appended to it)
                        if (prev_entry_path.string().length() < entry_path.string().length())
                        {
                           if (std::filesystem::remove(prev_entry_path))
                           {
                              duplicate = false;
                              break;
                           }
                        }
                        // Delete the new version
                        else
                        {
                           if (std::filesystem::remove(entry_path))
                           {
                              break;
                           }
                        }
                     }
                  }
               }
               if (!duplicate)
               {
                  dumped_shaders.emplace(shader_hash);
                  dumped_shaders_paths.emplace(entry_path);
               }
            }
            catch (const std::exception& e)
            {
               continue;
            }
         }
      }
   }
#endif // ALLOW_SHADERS_DUMPING

   for (int i = 0; i < shader_defines_data.size(); i++)
   {
      shader_defines_data_index[string_view_crc32(std::string_view(shader_defines_data[i].default_data.GetName()))] = i;
   }

   game->OnInit(async);

   assert(luma_settings_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
   assert(luma_data_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);

   cb_luma_frame_settings.DisplayMode = 1; // Default to HDR in case we had no prior config, it will be automatically disabled if the current display doesn't support it (when the swapchain is created, which should be guaranteed to be after)
   cb_luma_frame_settings.ScenePeakWhite = default_peak_white;
   cb_luma_frame_settings.ScenePaperWhite = default_paper_white;
   cb_luma_frame_settings.UIPaperWhite = default_paper_white;
   cb_luma_frame_settings.DLSS = 0; // We can't set this to 1 until we verified DLSS engaged correctly and is running

   // Load settings
   {
      const std::unique_lock lock_reshade(s_mutex_reshade);

      reshade::api::effect_runtime* runtime = nullptr;
      uint32_t config_version = Globals::VERSION;
      reshade::get_config_value(runtime, NAME, "Version", config_version);
      if (config_version != Globals::VERSION)
      {
         if (config_version < Globals::VERSION)
         {
            const std::unique_lock lock_loading(s_mutex_loading);
            // NOTE: put behaviour to load previous versions into new ones here
            CleanShadersCache(); // Force recompile shaders, just for extra safety (theoretically changes are auto detected through the preprocessor, but we can't be certain). We don't need to change the last config serialized shader defines.
         }
         else if (config_version > Globals::VERSION)
         {
            reshade::log::message(reshade::log::level::warning, "Luma: trying to load a config from a newer version of the mod, loading might have unexpected results");
         }
         reshade::set_config_value(runtime, NAME, "Version", Globals::VERSION);
      }

#if ENABLE_NGX
      reshade::get_config_value(runtime, NAME, "DLSSSuperResolution", dlss_sr);
#endif
      reshade::get_config_value(runtime, NAME, "DisplayMode", cb_luma_frame_settings.DisplayMode);
#if !DEVELOPMENT && !TEST // Don't allow "SDR in HDR for HDR" mode (there's no strong reason not to, but it avoids permutations exposed to users)
      if (cb_luma_frame_settings.DisplayMode >= 2)
      {
         cb_luma_frame_settings.DisplayMode = 0;
      }
#endif

      // If we read an invalid value from the config, reset it
      if (reshade::get_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_frame_settings.ScenePeakWhite) && cb_luma_frame_settings.ScenePeakWhite <= 0.f)
      {
         const std::shared_lock lock(s_mutex_device); // This is not completely safe as the write to "default_user_peak_white" isn't protected by this mutex but it's fine, it shouldn't have been written yet when we get here
         cb_luma_frame_settings.ScenePeakWhite = global_devices_data.empty() ? default_peak_white : global_devices_data[0]->default_user_peak_white;
      }
      reshade::get_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_frame_settings.ScenePaperWhite);
      reshade::get_config_value(runtime, NAME, "UIPaperWhite", cb_luma_frame_settings.UIPaperWhite);
      if (cb_luma_frame_settings.DisplayMode == 0)
      {
         cb_luma_frame_settings.ScenePeakWhite = srgb_white_level;
         cb_luma_frame_settings.ScenePaperWhite = srgb_white_level;
         cb_luma_frame_settings.UIPaperWhite = srgb_white_level;
      }
      else if (cb_luma_frame_settings.DisplayMode >= 2)
      {
         cb_luma_frame_settings.UIPaperWhite = cb_luma_frame_settings.ScenePaperWhite;
         cb_luma_frame_settings.ScenePeakWhite = cb_luma_frame_settings.ScenePaperWhite;
      }

      const std::unique_lock lock_shader_defines(s_mutex_shader_defines);
      ShaderDefineData::Load(shader_defines_data, NAME_ADVANCED_SETTINGS, runtime);

      game->LoadConfigs();

      OnDisplayModeChanged();

      game->OnShaderDefinesChanged();
   }

   {
      // Assume that the shader defines loaded from config match the ones the current pre-compiled shaders have (or, simply use the defaults otherwise)
      const std::unique_lock lock_shader_defines(s_mutex_shader_defines);
      ShaderDefineData::OnCompilation(shader_defines_data);
      shader_defines_data_index.clear();
      for (uint32_t i = 0; i < shader_defines_data.size(); i++)
      {
         shader_defines_data_index[string_view_crc32(std::string_view(shader_defines_data[i].compiled_data.GetName()))] = i;
      }
   }

   // Pre-load all shaders to minimize the wait before replacing them after they are found in game ("auto_load"),
   // and to fill the list of shaders we customized, so we can know which ones we need replace on the spot.
   if (async && precompile_custom_shaders)
   {
      thread_auto_compiling_running = true;
      static std::binary_semaphore async_shader_compilation_semaphore{ 0 };
      thread_auto_compiling = std::thread([]
         {
            // We need to lock this mutex for the whole async shader loading, so that if the game starts loading shaders (from another thread), we can already see if we have a custom version and live load it ("live_load"), otherwise the "custom_shaders_cache" list would be incomplete
            const std::unique_lock lock_loading(s_mutex_loading);
            // This is needed to make sure this thread locks "s_mutex_loading" before any other function could
            async_shader_compilation_semaphore.release();
            CompileCustomShaders(nullptr, true);
            const std::shared_lock lock_device(s_mutex_device);
            // Create custom device shaders if the device has already been created before custom shaders were loaded on boot, independently of "block_draw_until_device_custom_shaders_creation".
            // Note that this might be unsafe if "global_devices_data" was already being destroyed in "OnDestroyDevice()" (I'm not sure you can create device resources anymore at that point).
            for (auto global_device_data : global_devices_data)
            {
               const std::unique_lock lock_shader_objects(s_mutex_shader_objects);
               if (!global_device_data->created_custom_shaders)
               {
                  CreateCustomDeviceShaders(*global_device_data, std::nullopt, false);
               }
            }
            thread_auto_compiling_running = false;
         });
      async_shader_compilation_semaphore.acquire();
   }
}

// This can't be called on "DLL_PROCESS_DETACH" as it needs a multi threaded enviroment
void Uninit()
{
   if (thread_auto_dumping.joinable())
   {
      thread_auto_dumping.join();
   }
   if (thread_auto_compiling.joinable())
   {
      thread_auto_compiling.join();
   }
   {
      const std::shared_lock lock(s_mutex_device);
      for (auto global_device_data : global_devices_data)
      {
         if (global_device_data->thread_auto_loading.joinable())
         {
            global_device_data->thread_auto_loading.join();
         }
      }
   }

   has_init = false;
}

#ifndef RESHADE_EXTERNS
// This is called immediately after the main function ("DllMain") gets "DLL_PROCESS_ATTACH" if this dll/addon is loaded directly by ReShade
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

// This is a static library so this "main" function won't ever be automatically called, it needs to be manually hooked.
BOOL APIENTRY CoreMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved)
{
   switch (fdw_reason)
   {
   // Note: this dll should support being loaded more than once (included being unloaded in the middle of execution).
   // ReShade loads addons when the game creates a DirectX device, this usually only happens once on boot under normal circumstances (e.g. Prey), but can happen multiple times (e.g. Dishonored 2, Unity games, ...).
   case DLL_PROCESS_ATTACH:
   {
// Some games like to crash or have input issues if a debugger is present on boot, so make it optional
#if (DEVELOPMENT || _DEBUG) && !defined(DISABLE_AUTO_DEBUGGER)
      LaunchDebugger(NAME);
#endif // DEVELOPMENT

      wchar_t file_path_char[MAX_PATH] = L"";
      GetModuleFileNameW(h_module, file_path_char, ARRAYSIZE(file_path_char));
      std::filesystem::path file_path = file_path_char;
      if (file_path.extension() == ".addon" || file_path.extension() == ".addon64")
      {
         asi_loaded = false;
      }
      else
      {
         // Just to make sure, if we got loaded then it's probably fine either way
         assert(file_path.extension() == ".dll" || file_path.extension() == ".asi");
      }

      bool load_failed = false;

#if !DISABLE_RESHADE
      // Register the ReShade addon.
      // We simply cancel everything else if reshade is not present or failed to register,
      // we could still load the native plugin,
      const bool reshade_addon_register_succeeded = reshade::register_addon(h_module);
      if (!reshade_addon_register_succeeded) load_failed = true;
#endif // !DISABLE_RESHADE

      // We give the game code the opportunity to do something before rejecting the dll load
      game->OnLoad(file_path, load_failed);

#if DISABLE_RESHADE
      if (!asi_loaded) return FALSE;
#endif // DISABLE_RESHADE

      if (load_failed)
      {
         return FALSE;
      }

#if DISABLE_RESHADE
      if (asi_loaded) return TRUE;
#endif // DISABLE_RESHADE

      reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
      reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
      reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
      reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
      reshade::register_event<reshade::addon_event::set_fullscreen_state>(OnSetFullscreenState);

      reshade::register_event<reshade::addon_event::init_pipeline>(OnInitPipeline);
      reshade::register_event<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);

      reshade::register_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);

      reshade::register_event<reshade::addon_event::init_command_list>(OnInitCommandList);
      reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);

      if (enable_texture_format_upgrades)
      {
         reshade::register_event<reshade::addon_event::init_resource>(OnInitResource);
         reshade::register_event<reshade::addon_event::create_resource>(OnCreateResource);
         reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      }
      if (enable_texture_format_upgrades || enable_swapchain_upgrade)
      {
         reshade::register_event<reshade::addon_event::create_resource_view>(OnCreateResourceView);
      }

      reshade::register_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
#if DEVELOPMENT
      reshade::register_event<reshade::addon_event::map_texture_region>(OnMapTextureRegion);
      reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
      reshade::register_event<reshade::addon_event::update_texture_region>(OnUpdateTextureRegion);
      reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
      reshade::register_event<reshade::addon_event::clear_unordered_access_view_uint>(OnClearUnorderedAccessViewUInt);
      reshade::register_event<reshade::addon_event::clear_unordered_access_view_float>(OnClearUnorderedAccessViewFloat);
#endif // DEVELOPMENT
      reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
      reshade::register_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
      reshade::register_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);

      reshade::register_event<reshade::addon_event::draw>(OnDraw);
      reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
      reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
      reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);

#if UPGRADE_SAMPLERS
      reshade::register_event<reshade::addon_event::init_sampler>(OnInitSampler);
      reshade::register_event<reshade::addon_event::destroy_sampler>(OnDestroySampler);
#endif

#if DEVELOPMENT
      reshade::register_event<reshade::addon_event::execute_secondary_command_list>(OnExecuteSecondaryCommandList);
#endif // DEVELOPMENT

      reshade::register_event<reshade::addon_event::present>(OnPresent);

      reshade::register_event<reshade::addon_event::reshade_present>(OnReShadePresent);

#if DEVELOPMENT || TEST // Currently Dev only as we don't need the average user to compare the mod on/off
      reshade::register_event<reshade::addon_event::reshade_set_effects_state>(OnReShadeSetEffectsState);
      reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(OnReShadeReloadedEffects);
#endif // DEVELOPMENT

      reshade::register_overlay(NAME, OnRegisterOverlay);

      break;
   }
   case DLL_PROCESS_DETACH:
   {
      // Automatically destroy this if it was instanced by a game implementation
      if (game != &default_game)
      {
         delete game;
         game = nullptr;
      }

      reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
      reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      reshade::unregister_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
      reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
      reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
      reshade::unregister_event<reshade::addon_event::set_fullscreen_state>(OnSetFullscreenState);

      reshade::unregister_event<reshade::addon_event::init_pipeline>(OnInitPipeline);
      reshade::unregister_event<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);

      reshade::unregister_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);

      reshade::unregister_event<reshade::addon_event::init_command_list>(OnInitCommandList);
      reshade::unregister_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);

      if (enable_texture_format_upgrades)
      {
         reshade::unregister_event<reshade::addon_event::init_resource>(OnInitResource);
         reshade::unregister_event<reshade::addon_event::create_resource>(OnCreateResource);
         reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      }
      if (enable_texture_format_upgrades || enable_swapchain_upgrade)
      {
         reshade::unregister_event<reshade::addon_event::create_resource_view>(OnCreateResourceView);
      }

      reshade::unregister_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
#if DEVELOPMENT
      reshade::unregister_event<reshade::addon_event::map_texture_region>(OnMapTextureRegion);
      reshade::unregister_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
      reshade::unregister_event<reshade::addon_event::update_texture_region>(OnUpdateTextureRegion);
      reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
      reshade::unregister_event<reshade::addon_event::clear_unordered_access_view_uint>(OnClearUnorderedAccessViewUInt);
      reshade::unregister_event<reshade::addon_event::clear_unordered_access_view_float>(OnClearUnorderedAccessViewFloat);
#endif // DEVELOPMENT
      reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);
      reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
      reshade::unregister_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);

      reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
      reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
      reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
      reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);

#if UPGRADE_SAMPLERS
      reshade::unregister_event<reshade::addon_event::init_sampler>(OnInitSampler);
      reshade::unregister_event<reshade::addon_event::destroy_sampler>(OnDestroySampler);
#endif

#if DEVELOPMENT
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(OnExecuteSecondaryCommandList);
#endif // DEVELOPMENT

      reshade::unregister_event<reshade::addon_event::present>(OnPresent);

      reshade::unregister_event<reshade::addon_event::reshade_present>(OnReShadePresent);

#if DEVELOPMENT || TEST
      reshade::unregister_event<reshade::addon_event::reshade_set_effects_state>(OnReShadeSetEffectsState);
      reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(OnReShadeReloadedEffects);
#endif // DEVELOPMENT

      reshade::unregister_overlay(NAME, OnRegisterOverlay);

      reshade::unregister_addon(h_module);

      // In case our threads are still not joined, detach them and safely do a busy loop
      // until they finished running, so we don't risk them reading/writing to stale memory.
      // This could cause a bit of wait, especially if we just booted the game and shaders are still compiling,
      // but there's no nice and clear alternatively really.
      // This is needed because DLL loading/unloading is completely single threaded and isn't
      // able to join threads (though "thread.detach()" somehow seems to work).
      // Note that there's no need to call "Uninit()" here, independently on whether we are asi or ReShade loaded.
      if (thread_auto_dumping.joinable())
      {
         thread_auto_dumping.detach();
         while (thread_auto_dumping_running) {}
      }
      if (thread_auto_compiling.joinable())
      {
         thread_auto_compiling.detach();
         while (thread_auto_compiling_running) {}
      }
      // We can't lock "s_mutex_device" here, but we also know that if this ptr is valid, then there's no other thread able to run now and change it.
      // ReShade is unloaded when the last device is destroyed so we should have already received an event to clear this thread anyway.
      for (auto global_device_data : global_devices_data)
      {
         if (global_device_data->thread_auto_loading.joinable())
         {
            global_device_data->thread_auto_loading.detach();
            while (global_device_data->thread_auto_loading_running) {}
         }
      }

      break;
   }
   }

   return TRUE;
}