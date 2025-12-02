#define GAME_JUST_CAUSE_3 1

// TODO: this breaks everything? It does... probably due to the gbuffers. I don't know why.
#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 0

#include "..\..\Core\core.hpp"

#include "..\..\Core\includes\shader_patching.h"

namespace
{
   ShaderHashesList shader_hashes_SwapchainCopy;
}

class JustCause3 final : public Game
{
public:
   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"FORCE_VANILLA_FOG", '1', true, false, "In HDR, the fog texture might get upgraded and thus unclipped, causing the fog to be stronger.\nEnable this to re-clamp it to vanilla levels.", 1},
         {"FORCE_VANILLA_AUTO_EXPOSURE", '0', true, false, "The game auto exposure was calculated after tonemapping, hence HDR will affect it.\nTo keep a exposure level as vanilla SDR, turn this on, however, it doesn't actually seem to look better in HDR.", 1},
         {"ENABLE_ANAMORPHIC_BLOOM", '0', true, false, "The game bloom was stretched horizontally to emulate the look of film. It doesn't seem to fit with the rest of the game, so Luma disables that by default.", 1},
         {"ENABLE_FAKE_HDR", '1', true, false, "Enable a \"Fake\" HDR boosting effect", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0'); // The game looks best in sRGB<->sRGB. Doing any kind of 2.2 conversion, whether per channel or by luminance crushes blacks and makes the game look unnatural (especially doing the night). Though just in case, "3" looks second best here. Ideally we'd expose it as a slider, or dynamically pick it based on the LUT and time of day.
      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');
   }

   // Add a saturate on materials/gbuffers
   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   {
      if (type != reshade::api::pipeline_subobject_type::pixel_shader)
         return nullptr;

      std::unique_ptr<std::byte[]> new_code = nullptr;

      bool gbuffers_pattern_found = false;
      bool lighting_pattern_found = false;

      // The game first renders to 4 UNORM GBuffers (which we might upgrade to FLOAT RTs hence we need to add saturate() on their output, to prevent values beyond 0-1 and NaNs)
      const char str_to_find_gbuffers_1[] = "DepthMap";
      const char str_to_find_gbuffers_2[] = "DiffuseMap";
      const char str_to_find_gbuffers_3[] = "DiffuseAlpha";
      const char str_to_find_gbuffers_4[] = "NormalMap";
      const char str_to_find_gbuffers_5[] = "MaterialConsts"; // Sometimes it's "cbMaterialConsts" too

      // The game then composes the gbuffers and renders "lighting" on top (lights, fog, transparency (glass), particles, decals, ...)
      const char str_to_find_lighting[] = "LightingFrameConsts";

      std::vector<std::byte> pattern_safety_check;
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_1), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_1) + strlen(str_to_find_gbuffers_1));
      gbuffers_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_2), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_2) + strlen(str_to_find_gbuffers_2));
      gbuffers_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_3), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_3) + strlen(str_to_find_gbuffers_3));
      gbuffers_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_4), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_4) + strlen(str_to_find_gbuffers_4));
      gbuffers_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_5), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_5) + strlen(str_to_find_gbuffers_5));
      gbuffers_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();

      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_lighting), reinterpret_cast<const std::byte*>(str_to_find_lighting) + strlen(str_to_find_lighting));
      lighting_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      if (lighting_pattern_found)

      if (!gbuffers_pattern_found && !lighting_pattern_found)
         return new_code;

      uint8_t found_dcl_outputs = 0;

      size_t size_u32 = size / sizeof(uint32_t);
      const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
      size_t i = 0;
      bool found_first_dcl = false;
      while (i < size_u32)
      {
         uint32_t opcode_token = code_u32[i];
         D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(opcode_token);

         // Stop scanning the DCL opcodes, they are all declared at the beginning
         if (ShaderPatching::opcodes_dcl.contains(opcode_type))
            found_first_dcl = true;
         else if (found_first_dcl) // Extra safety in case there were instructions before DCL ones
            break;

         uint8_t instruction_size = opcode_type == D3D10_SB_OPCODE_CUSTOMDATA ? code_u32[i + 1] : DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(opcode_token); // Includes itself

         if (opcode_type == D3D10_SB_OPCODE_DCL_OUTPUT)
         {
            uint32_t operand_token = code_u32[i + 1];
            bool four_channels = true;
            uint32_t test_operand_token =
               ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_Y : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_Z : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_W : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
               ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT) |
               ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
               ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(found_dcl_outputs, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
            if (operand_token == test_operand_token)
            {
               found_dcl_outputs++;
            }
         }

         i += instruction_size;
         if (instruction_size == 0)
            break;
      }

      // Make sure they have 4 or 1 RTs
      if (gbuffers_pattern_found)
      {
         gbuffers_pattern_found &= found_dcl_outputs == 4;
      }
      if (lighting_pattern_found)
      {
         lighting_pattern_found &= found_dcl_outputs == 1;
      }

      if (gbuffers_pattern_found || lighting_pattern_found)
      {
         std::vector<uint8_t> appended_patch;

         constexpr bool enable_unorm_emulation = true;
         constexpr bool enable_r11g11b10float_emulation = true;
         if (gbuffers_pattern_found && enable_unorm_emulation)
         {
            // Saturate all 4 outputs
            std::vector<uint32_t> mov_sat_onxyzw_onxyzw = ShaderPatching::GetSatInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()) + mov_sat_onxyzw_onxyzw.size() * sizeof(uint32_t));
            mov_sat_onxyzw_onxyzw = ShaderPatching::GetSatInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 1);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()) + mov_sat_onxyzw_onxyzw.size() * sizeof(uint32_t));
            mov_sat_onxyzw_onxyzw = ShaderPatching::GetSatInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 2);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()) + mov_sat_onxyzw_onxyzw.size() * sizeof(uint32_t));
            mov_sat_onxyzw_onxyzw = ShaderPatching::GetSatInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 3);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()) + mov_sat_onxyzw_onxyzw.size() * sizeof(uint32_t));
         }
         else if (lighting_pattern_found && enable_r11g11b10float_emulation)
         {
            // >= 0 (fixes negative values and NaNs)
            // Note: theoretically we should saturate alpha, but it doesn't seem to ever be a problem
            std::vector<uint32_t> max_o0xyz_o0xyz_0 = ShaderPatching::GetMaxInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, D3D10_SB_OPERAND_TYPE_OUTPUT, 0);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()) + max_o0xyz_o0xyz_0.size() * sizeof(uint32_t));
         }

         new_code = std::make_unique<std::byte[]>(size + appended_patch.size());

         // Pattern to search for: 3E 00 00 01 (the last byte is the size, and the minimum is 1 (the unit is 4 bytes), given it also counts for the opcode and it's own size byte
         const std::vector<std::byte> return_pattern = {std::byte{0x3E}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}};

         // Append before the ret instruction if there's one at the end (there should always be)
         if (!appended_patch.empty() && code[size - return_pattern.size()] == return_pattern[0] && code[size - return_pattern.size() + 1] == return_pattern[1] && code[size - return_pattern.size() + 2] == return_pattern[2] && code[size - return_pattern.size() + 3] == return_pattern[3])
         {
            size_t insert_pos = size - return_pattern.size();
            // Copy everything before pattern
            std::memcpy(new_code.get(), code, insert_pos);
            // Insert the patch
            std::memcpy(new_code.get() + insert_pos, appended_patch.data(), appended_patch.size());
            // Copy the rest (including the return instruction)
            std::memcpy(new_code.get() + insert_pos + appended_patch.size(), code + insert_pos, size - insert_pos);
         }
         // Append patch at the end
         else
         {
            std::memcpy(new_code.get(), code, size);
            std::memcpy(new_code.get() + size, appended_patch.data(), appended_patch.size());
         }

         size += appended_patch.size();
      }

      return new_code;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      // Make sure the swapchain copy shader always and only targets the swapchain RT, otherwise we'd need to branch in it!
      if (is_custom_pass && original_shader_hashes.Contains(shader_hashes_SwapchainCopy))
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         bool is_rt_swapchain = false;
         if (rtv)
         {
            com_ptr<ID3D11Resource> rt_resource;
            rtv->GetResource(&rt_resource);

            const std::shared_lock lock(device_data.mutex);
            is_rt_swapchain = device_data.back_buffers.contains((uint64_t)rt_resource.get());
         }

         // Needed to branch on gamma conversions in the shaders
         uint32_t custom_data_1 = is_rt_swapchain ? 1 : 0;
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1);
         updated_cbuffers = true;
         return DrawOrDispatchOverrideType::None;
      }

      return DrawOrDispatchOverrideType::None; // Don't cancel the original draw call
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "ColorGradingIntensity", cb_luma_global_settings.GameSettings.ColorGradingIntensity);
      reshade::get_config_value(runtime, NAME, "HDRBoostSaturationAmount", cb_luma_global_settings.GameSettings.HDRBoostSaturationAmount);
      // "device_data.cb_luma_global_settings_dirty" should already be true at this point
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
      {
         if (ImGui::SliderFloat("Color Grading Intensity", &cb_luma_global_settings.GameSettings.ColorGradingIntensity, 0.f, 1.f))
         {
            reshade::set_config_value(runtime, NAME, "ColorGradingIntensity", cb_luma_global_settings.GameSettings.ColorGradingIntensity);
         }
         DrawResetButton(cb_luma_global_settings.GameSettings.ColorGradingIntensity, 0.8f, "ColorGradingIntensity", runtime);

         if (GetShaderDefineCompiledNumericalValue(char_ptr_crc32("ENABLE_FAKE_HDR")) > 0)
         {
            if (ImGui::SliderFloat("HDR Saturation Boost", &cb_luma_global_settings.GameSettings.HDRBoostSaturationAmount, 0.f, 1.f))
            {
               reshade::set_config_value(runtime, NAME, "HDRBoostSaturationAmount", cb_luma_global_settings.GameSettings.HDRBoostSaturationAmount);
            }
            DrawResetButton(cb_luma_global_settings.GameSettings.HDRBoostSaturationAmount, 0.2f, "HDRBoostSaturationAmount", runtime);
         }
      }
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Just Cause 3\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee on buymeacoffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo.c_str()))
      {
         system("start https://buymeacoffee.com/realfiloppi");
      }
      static const std::string donation_link_pumbo_2 = std::string("Buy Pumbo a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo_2.c_str()))
      {
         system("start https://ko-fi.com/realpumbo");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      // Restore the previous color, otherwise the state we set would persist even if we popped it
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
#if 0
      static const std::string mod_link = std::string("Nexus Mods Page ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(mod_link.c_str()))
      {
         system("start https://www.nexusmods.com/prey2017/mods/149");
      }
#endif
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma by Pumbo (to track the origin of people joining), do not share for other purposes
         static const std::string obfuscated_link = std::string("start https://discord.gg/J9fM") + std::string("3EVuEZ");
         system(obfuscated_link.c_str());
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         system("start https://github.com/Filoppi/Luma-Framework");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nPumbo"

                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Just Cause 3 Luma mod");
      Globals::VERSION = 1;

      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
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

         reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      enable_indirect_texture_format_upgrades = true; // TODO: is this causing the "low memory" warning on boot?
      enable_automatic_indirect_texture_format_upgrades = true;

      // The game has x16 AA but it doesn't seem to apply to many textures
      //enable_samplers_upgrade = true;
      // TODO: allow upgrading AA samplers... this is currently not working in this game!

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 11; // 12 is used

      // There's many...
      redirected_shader_hashes["Tonemap"] =
         {
            "01F41F2D",
            "0BAC4255",
            "0C35F299",
            "148CD952",
            "15BC0ABC",
            "1C087BA1",
            "2319D5A4",
            "2607E7C0",
            "288B16D3",
            "2FB48E77",
            "371AD4D5",
            "3753CA0A",
            "38ABE9E7",
            "3EC5DBB9",
            "4030BF6E",
            "49704266",
            "4A9BFEC5",
            "6A1C711F",
            "6C0BCB6B",
            "6F6BFEDA",
            "75190444",
            "79193F1D",
            "7CF7827A",
            "7F138E1C",
            "83CC89FB",
            "8610E7F5",
            "87F34BAA",
            "8D59471A",
            "8D8F7072",
            "92550B56",
            "96DA986B",
            "9C62A6F9",
            "9D857B42",
            "A274F081",
            "A91CF149",
            "A91F8AB9",
            "A9CEF67D",
            "ADAFB4CD",
            "BCF2BA69",
            "BF1F1C29",
            "C16B4E6B",
            "D0F9B11B",
            "D4B1C6E9",
            "DC0FE377",
            "DED46AD7",
            "E1ECF661",
            "F21C9CBA",
            "F4E80E62",
            "FA0676EF",
            "FA796E93",
            "FDBDB73F",
         };

      shader_hashes_SwapchainCopy.pixel_shaders = {std::stoul("3DA3DB98", nullptr, 16)};

      // Defaults are hardcoded in ImGUI too
      cb_luma_global_settings.GameSettings.ColorGradingIntensity = 0.8f; // Don't default to 1 (vanilla) because it's too saturated and hue shifted
      cb_luma_global_settings.GameSettings.HDRBoostSaturationAmount = 0.2f;
      // TODO: use "default_luma_global_game_settings"

#if DEVELOPMENT
      forced_shader_names.emplace(Shader::Hash_StrToNum("A1037803"), "Gen Motion Vectors");
      forced_shader_names.emplace(Shader::Hash_StrToNum("60EB1F22"), "SMAA Edge Detection 1");
      forced_shader_names.emplace(Shader::Hash_StrToNum("5DB69E08"), "SMAA Edge Detection 2");
      forced_shader_names.emplace(Shader::Hash_StrToNum("8A824E55"), "SMAA");

      forced_shader_names.emplace(Shader::Hash_StrToNum("D8C32AC0"), "Sky/Stars");

      forced_shader_names.emplace(Shader::Hash_StrToNum("592575B0"), "Clear 4 Textures to Black");
#endif

      game = new JustCause3();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}