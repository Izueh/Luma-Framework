#define GAME_MAFIA_III 1

#define UPGRADE_SAMPLERS 0

#include "..\..\Core\core.hpp"

// Hack: we need to include this cpp file here because it's part of the core library but we actually don't include it as a library, due to limitations (see the game template for more)
#include "..\..\Core\dlss\DLSS.cpp"

namespace
{
   ShaderHashesList shader_hashes_3D_UI;
}

struct GameDeviceDataMafiaIII final : public GameDeviceData
{

};

class MafiaIII final : public Game
{
public:
   static const GameDeviceDataMafiaIII& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataMafiaIII*>(device_data.game);
   }
   static GameDeviceDataMafiaIII& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataMafiaIII*>(device_data.game);
   }

   void OnInit(bool async) override
   {
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0');
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataMafiaIII;
   }

   bool OnDrawCustom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      auto game_device_data_prev = game_device_data;
      // TODO: delete
      if (original_shader_hashes.Contains(shader_hashes_3D_UI))
      {
         com_ptr<ID3D11DepthStencilState> depth_stencil_state;
         native_device_context->OMGetDepthStencilState(&depth_stencil_state, nullptr);
         if (depth_stencil_state)
         {
            D3D11_DEPTH_STENCIL_DESC depth_stencil_desc;
            depth_stencil_state->GetDesc(&depth_stencil_desc);
            if (depth_stencil_desc.DepthEnable)
            {
               D3D11_VIEWPORT viewport;
               viewport.TopLeftX = 0.f;
               viewport.TopLeftY = 0.f;
               viewport.MinDepth = 0.f;
               viewport.MaxDepth = 1.f;
               viewport.Width = device_data.output_resolution.x;
               viewport.Height = device_data.output_resolution.y;
               native_device_context->RSSetViewports(1, &viewport);
            }
         }
      }

      return false;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Mafia III\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating.", "");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo.c_str()))
      {
         system("start https://buymeacoffee.com/realfiloppi");
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
         system("start https://github.com/Filoppi/Luma");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::Text("Credits:"
         "\n\nMain:"
         "\nPumbo"

         "\n\nThird Party:"
         "\nReShade"
         "\nImGui"
         "\nRenoDX"
         "\n3Dmigoto"
         "\nOklab"
         "\nDICE (HDR tonemapper)"
         , "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::GAME_NAME = PROJECT_NAME;
      Globals::DESCRIPTION = "Mafia III Luma mod";
      Globals::WEBSITE = "";
      Globals::VERSION = 1;

      shader_hashes_3D_UI.pixel_shaders.emplace(std::stoul("CDB35CB7", nullptr, 16));
      shader_hashes_3D_UI.pixel_shaders.emplace(std::stoul("22EE786B", nullptr, 16));
      shader_hashes_3D_UI.pixel_shaders.emplace(std::stoul("AAA3C7B5", nullptr, 16));

      luma_settings_cbuffer_index = 13;

      enable_swapchain_upgrade = true;
      swapchain_upgrade_type = 1;
      enable_texture_format_upgrades = true;
#if 0 // Not needed really, swapchain is all we need
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
#endif

      game = new MafiaIII();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}