#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "Settings/Registry.hpp"

namespace Luma {
namespace UI {
// A section of the settings view
struct SettingsSection
{
   enum class Role : uint8_t
   {
      Settings, // Luma's
      Game, // A game's category
      Defines, // With the pending compile banner
      Development, // With the game's development panel
   };

   std::string_view title;
   std::vector<const Settings::Setting*> settings;
   Role role = Role::Settings;
};

// The settings view's sections, the shown settings in registration order: Luma's "Luma" and "Overlay" categories, the game's
// categories, "Shader Defines" (Auto Compile first, then Luma's and the game's defines), "Recipes", Luma's other categories, then
// "Development Settings" in Development builds. Empty sections are left out.
std::vector<SettingsSection> CollectSettingsSections();

// The overlay's window: a pinned header (game, the switch, the Development tabs), the scrolling content, a pinned footer (the
// game's links, the status bar)
void DrawShell();
} // namespace UI
} // namespace Luma
