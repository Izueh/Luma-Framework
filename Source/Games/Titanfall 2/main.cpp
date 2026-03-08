#define GAME_TITANFALL_2 1

#include "..\..\Core\core.hpp"

namespace
{
   bool first_swapchain_draw_call = true;
}

class Titanfall2 final : public Game
{
public:
   void OnInit(bool async) override
   {
      // From 5 to 9 they don't seem to be used
      luma_settings_cbuffer_index = 7;
      luma_data_cbuffer_index = 8;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      // In TF2, if we upgrade the swapchain format, the game doesn't even try to create a view for it during menus (we'd need to proxy the texture class and make return a desc matching the original format to make it work probably, or create a proxy swapchain alltogether)
      if ((stages & reshade::api::shader_stage::pixel) != 0 && (first_swapchain_draw_call || test_index == 12 || test_index == 13) && test_index != 14) // Only needed on the first draw call seemengly
      {
         D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
         UINT viewports_num = 1;
         native_device_context->RSGetViewports(&viewports_num, nullptr);
         native_device_context->RSGetViewports(&viewports_num, &viewports[0]);
         if (viewports_num == 1 && viewports[0].Width == device_data.output_resolution.x && viewports[0].Height == device_data.output_resolution.y)
         {
            com_ptr<ID3D11RenderTargetView> rtvs[2];
            com_ptr<ID3D11DepthStencilView> depth_stencil_view;
            native_device_context->OMGetRenderTargets(2, &rtvs[0], &depth_stencil_view);
            // Make sure we only have 1 rtv (the first one would be invalid, which is the problem here)
            if (rtvs[0] == nullptr && rtvs[1] == nullptr && !device_data.swapchains.empty())
            {
               SwapchainData& swapchain_data = *(*device_data.swapchains.begin())->get_private_data<SwapchainData>(); // Game only ever has 1 swapchain, so this is safe
               com_ptr<ID3D11RenderTargetView> display_composition_rtv;
               {
                  const std::shared_lock lock_swapchain(swapchain_data.mutex);
                  display_composition_rtv = swapchain_data.display_composition_rtvs.empty() ? nullptr : swapchain_data.display_composition_rtvs[0]; // Always 1 swapchain buffer in DX11, this will never change as it'd break the design of games that rely on it
               }

               ID3D11RenderTargetView* display_composition_rtv_const = display_composition_rtv.get();
               native_device_context->OMSetRenderTargets(1, &display_composition_rtv_const, depth_stencil_view.get());

               first_swapchain_draw_call = false; // TODO: only ever consider the absolute first draw call to optimize this and avoid checking for all the draw calls during gameplay (given it's not necessary there)?
            }
         }
         if (test_index == 13)
            first_swapchain_draw_call = false;
      }
      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      first_swapchain_draw_call = true;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Titanfall 2 Luma mod - about and credits section", ""); // ### Rename this ###
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Titanfall 2 Luma mod");
      Globals::VERSION = 1;
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;

      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedDisabled;
#if 0 // No upgrades are needed, the game directly writes on the swapchain, and does even AA and all other post process before (which is great!). UI is also in linear space.
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
      enable_indirect_texture_format_upgrades = true; // Possibly not needed in this game anyway
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
#endif
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      // Needed to force failed draws on the swapchain
      force_create_swapchain_rtvs = true;
      // Helps given that atm FSE crashes on alt tab
      force_borderless = true;

      game = new Titanfall2();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}