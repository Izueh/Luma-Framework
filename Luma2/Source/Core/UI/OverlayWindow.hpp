#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "Settings/SettingsRegistry.hpp"

namespace Luma {
namespace UI {
enum class SettingsTab : uint8_t
{
   Settings, // General (Luma.Enabled/Hotkey) + Display + Game (+ Release/Test fallback categories)
   Framework, // Development builds: Swapchain, ReShade, Shader Defines, Recipes, Advanced/Resources
   Overlay, // Overlay appearance, keybind, and layout reset
   Development, // Development builds: dev-only settings and game development panel
};

// A section of a settings tab
struct SettingsSection
{
   enum class Role : uint8_t
   {
      Settings, // Core settings
      Game, // A game's category
      Defines, // With the pending compile banner
      Development, // Development settings
   };

   std::string_view title;
   std::vector<const Settings::Setting*> settings;
   Role role = Role::Settings;
   bool default_open = true;
};

// The sections shown on `tab`, in registration order. Empty sections are left out.
std::vector<SettingsSection> CollectSettingsSections(SettingsTab tab = SettingsTab::Settings);

// Requests default window position and size on the next frame (used by UI::ResetLayout)
void RequestResetWindowLayout();

// The overlay's window: pinned header, tabs, scrolling content, pinned footer (links + status bar)
void DrawWindow();
} // namespace UI
} // namespace Luma
