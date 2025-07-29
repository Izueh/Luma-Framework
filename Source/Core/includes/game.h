#pragma once

#include "shaders.h"

using namespace Shader;

// "Sub" device data per game, subclassable.
// Make sure all variables are default initialized.
struct GameDeviceData
{
   // Empty by default
};

// Per game implementation
class Game
{
public:
   // The mod dll loaded (before init)
   virtual void OnLoad(std::filesystem::path& file_path, bool failed = false) {}
   // The mod initialized
   virtual void OnInit(bool async) {}
   virtual void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) {}
   virtual void OnInitSwapchain(reshade::api::swapchain* swapchain) {}
   // You can create and "append" your custom device data here
   virtual void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data)
   {
      // Example data
      device_data.game = new GameDeviceData;
   }
   // You can destroy your custom device data here
   virtual void OnDestroyDeviceData(DeviceData& device_data)
   {
      delete device_data.game;
      device_data.game = nullptr;
   }
   virtual void CreateShaderObjects(DeviceData& native_device, const std::optional<std::unordered_set<uint32_t>>& shader_hashes_filter) {}
   // Called for every game's valid draw call (any type),
   // this is where you can override passes, add new ones, cancel other ones etc.
   // Return true to cancel the call.
   virtual bool OnDrawCustom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers) { return false; }
   // This is called every frame just before sending out the final image to the display (the swapchain).
   // You can reliable reset any per frame setting here.
   virtual void OnPresent(ID3D11Device* native_device, DeviceData& device_data) {}
   virtual void UpdateLumaInstanceDataCB(LumaInstanceData& data) {}
   // Retrieves the game's "global" (main, per view, ...) cbuffer
   virtual bool UpdateGlobalCB(const void* global_buffer_data_ptr, reshade::api::device* device) { return false; }

   // Load ReShade configs on boot
   virtual void LoadConfigs() {}
   // Triggered when the user swaps between SDR and HDR output (in the game settings, not the display state)
   virtual void OnDisplayModeChanged() {}
   // Called when users change the "advanced" settings shader defines (e.g. you can grey out certain settings based on other settings here)
   virtual void OnShaderDefinesChanged() {}
   // Draw and save your settings here
   virtual void DrawImGuiSettings(DeviceData& device_data) {}
#if DEVELOPMENT
   // Same as "DrawImGuiSettings()" but for development settings
   virtual void DrawImGuiDevSettings(DeviceData& device_data) {}
#endif // DEVELOPMENT
#if DEVELOPMENT || TEST
   // You can print game specific information here (e.g. the weapon FOV, once you got access to the projection matrix)
   virtual void PrintImGuiInfo(const DeviceData& device_data) {}
#endif
   // About and Credits section
   virtual void PrintImGuiAbout() {}

   // In case you knew, you can specify whether the game was paused here
   virtual bool IsGamePaused(const DeviceData& device_data) const { return false; }
   // Values between 0 and 0.25 are usually good
   virtual float GetTonemapUIBackgroundAmount(const DeviceData& device_data) const { return 0.f; }
   // In case your DLSS implementation had any extra resources, you can clean them up here
   virtual void CleanExtraDLSSResources(DeviceData& device_data) {}
   // Some games draw UI through sRGB views on non sRGB textures (thus in linear space), or through non sRGB views on sRGB textures (thus in gamma space), hence the check below is not necessarily right, override per game if necessary
   virtual DXGI_FORMAT GetSeparateUITextureFormat(bool vanilla_swapchain_was_linear_space) const
   {
#if 1 // Use "DXGI_FORMAT_R16G16B16A16_UNORM" for UI to have the highest quality possible (pre-multiplying the UI on a separate render target leads to minor quantization)
      return vanilla_swapchain_was_linear_space ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R16G16B16A16_UNORM;
#else
      return vanilla_swapchain_was_linear_space ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
#endif
   }
	// Some games use a non linear swapchain, but always write to it through sRGB view, so we should essentially treat it as linear
   virtual bool ForceVanillaSwapchainLinear() const { return false; }
};