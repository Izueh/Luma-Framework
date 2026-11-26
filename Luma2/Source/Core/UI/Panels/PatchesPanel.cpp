#include "UI/Panels/PatchesPanel.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <format>
#include <string>
#include <tuple>
#include <vector>

#include <imgui.h>

#include "Settings/SettingsRegistry.hpp"
#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/ShaderPatching.hpp"
#include "Shaders/ShaderStore.hpp"
#include "UI/Widgets.hpp"

namespace Luma {
namespace UI {
namespace Panels {
namespace {
using Widgets::Tone;

size_t selected_recipe = SIZE_MAX;
size_t selected_shader = SIZE_MAX;
bool show_unmatched = false;

void DrawToolbar(const Shaders::Patching::Status& status)
{
   if (Widgets::Button("Reload", Widgets::ButtonKind::Primary, "Reads the recipes again and patches every shader the game created (from the cache when it can)"))
   {
      Shaders::Patching::SetLoaded(true);
      Shaders::Patching::Reload();
   }
   ImGui::SameLine();
   if (Widgets::Button(status.loaded ? "Unload" : "Load", Widgets::ButtonKind::Secondary,
          status.loaded ? "The game's shaders where they were patched" : "Patches every shader the game created again"))
      Shaders::Patching::SetLoaded(!status.loaded);
   ImGui::SameLine();
   Widgets::Text(std::format("{} patched, {} unchanged, {} failed{}", status.patched, status.unmatched, status.failed, status.running ? ", patching" : ""),
      status.failed ? Tone::Error : Tone::Info);
}

void DrawRecipe(const Shaders::Patching::RecipeInfo& recipe)
{
   Widgets::Property("Identity", recipe.identity);
   if (!recipe.description.empty())
      Widgets::Paragraph(recipe.description);
   Widgets::Property("Stage", recipe.stage.empty() ? "any" : recipe.stage);
   Widgets::Property("Shaders changed", std::format("{}", recipe.matched));
   if (!recipe.registered)
      Widgets::TextDim("No `meta: name` in the recipe: shown in Development builds only");
   if (!recipe.error.empty())
      Widgets::Code(recipe.error);
   // Its switch and environment values, as in the settings view
   if (const Settings::Setting* setting = Settings::Find(recipe.key))
      Widgets::SettingRow(*setting);
   for (const Shaders::Patching::EnvValue& env : recipe.env)
   {
      if (const Settings::Setting* setting = Settings::Find(env.key))
         Widgets::SettingRow(*setting);
   }
}

void DrawPatch(const Shaders::Patching::PatchInfo& patch)
{
   const Shaders::Database::Shader* shader = Shaders::Database::Find(patch.hash);
   Widgets::HashChip(patch.hash, patch.stage, shader ? shader->name : std::string_view());
   ImGui::SameLine();
   if (patch.patched)
      Widgets::Text(patch.cached ? "patched, from the cache" : "patched", Tone::Success);
   else if (!patch.error.empty())
      Widgets::Text("not patched", Tone::Error);
   else
      Widgets::TextDim("unchanged");
   if (patch.callback)
      Widgets::Property("By", "the game's bytecode patch");
   for (const std::string& recipe : patch.recipes)
      Widgets::ListItem(recipe);
   for (const Shaders::Patching::BindingInfo& binding : patch.bindings)
      Widgets::Text(std::format("{} \"{}\" at {}{}{}", binding.kind, binding.handle, binding.slot, binding.space ? std::format(" space{}", binding.space) : std::string(),
                       binding.resolved ? "" : ": no such resource"),
         binding.resolved ? Tone::Info : Tone::Error);
   if (!patch.error.empty())
      Widgets::Code(patch.error);
   if (!patch.report.empty())
      Widgets::Code(patch.report);
}
} // namespace

void DrawPatches()
{
   const Shaders::Patching::Status status = Shaders::Patching::GetStatus(Shaders::GetShownStore().get());
   if (!status.needed)
   {
      Widgets::TextDim("No recipes in the Recipes folder and no bytecode patch from the game: nothing is patched.");
      return;
   }
   DrawToolbar(status);

   static bool shaders_open = true;
   const std::vector<Shaders::Patching::RecipeInfo> recipes = Shaders::Patching::GetRecipes(Shaders::GetShownStore().get());
   if (Widgets::Section("Recipes"))
   {
      std::vector<Widgets::ListEntry> list;
      for (const Shaders::Patching::RecipeInfo& recipe : recipes)
         list.push_back({std::format("{} ({})", recipe.name, recipe.enabled ? "on" : "off"), !recipe.error.empty() ? Tone::Error : recipe.enabled ? Tone::Success
                                                                                                                                                  : Tone::Info});
      const float header_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.f;
      const float reserved_below = shaders_open ? ImGui::GetTextLineHeightWithSpacing() * 14.f : header_height;
      const float recipes_height = std::max(ImGui::GetTextLineHeightWithSpacing() * 6.f, ImGui::GetContentRegionAvail().y - reserved_below);
      Widgets::ListDetail("Recipes", list, selected_recipe, [&](size_t index) { DrawRecipe(recipes[index]); }, recipes_height);
      Widgets::EndSection();
   }

   const std::shared_ptr<Shaders::Store> store = Shaders::GetShownStore();
   std::vector<Shaders::Patching::PatchInfo> patches = store ? Shaders::Patching::GetPatches(*store) : std::vector<Shaders::Patching::PatchInfo>();
   if (!show_unmatched)
      std::erase_if(patches, [](const Shaders::Patching::PatchInfo& patch) { return !patch.patched && patch.error.empty(); });
   std::ranges::sort(patches, [](const auto& a, const auto& b) { return std::tuple(a.error.empty(), a.stage, a.hash) < std::tuple(b.error.empty(), b.stage, b.hash); });
   shaders_open = Widgets::Section("Shaders");
   if (shaders_open)
   {
      Widgets::FilterChip("Unchanged", show_unmatched, "Also the shaders a recipe ran on and left as they were");
      std::vector<Widgets::ListEntry> list;
      for (const Shaders::Patching::PatchInfo& patch : patches)
      {
         const Shaders::Database::Shader* shader = Shaders::Database::Find(patch.hash);
         const std::string name = shader && !shader->name.empty() ? std::format(" {}", shader->name) : std::string();
         list.push_back({std::format("0x{:08X}{}", patch.hash, name), patch.patched ? Tone::Success : !patch.error.empty() ? Tone::Error
                                                                                                                           : Tone::Info});
      }
      const float shaders_height = std::max(ImGui::GetTextLineHeightWithSpacing() * 8.f, ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y);
      Widgets::ListDetail("Patched", list, selected_shader, [&](size_t index) { DrawPatch(patches[index]); }, shaders_height);
      Widgets::EndSection();
   }
}
} // namespace Panels
} // namespace UI
} // namespace Luma
#endif
