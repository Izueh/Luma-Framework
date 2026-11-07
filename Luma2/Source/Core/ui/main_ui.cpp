#include "ui/main_ui.hpp"

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <imgui.h>

#include "game/game.hpp"
#include "settings/registry.hpp"
#include "ui/input.hpp"

namespace Luma {
namespace UI {
namespace {
// Measured on the previous frame
float footer_height = 0.f;

// ImGui wants null terminated strings
std::string Terminated(std::string_view text)
{
   return std::string(text);
}

bool IsShown(const Settings::Setting& setting)
{
   return Settings::IsVisibleInBuild(setting.visibility) && (!setting.is_visible || setting.is_visible());
}

void DrawToggleKey(const Settings::Setting& setting, const Settings::Value& value)
{
   const std::string label = Input::IsCapturingToggleKey() ? "Press a key (Escape cancels)" : Input::GetKeyName(uint32_t(std::get<int32_t>(value)));
   if (ImGui::Button(std::format("{}###{}", label, setting.key).c_str()))
      Input::CaptureToggleKey();
   ImGui::SameLine();
   ImGui::TextUnformatted(Terminated(setting.label).c_str());
}

// Whether the widget changed `edited`
bool DrawWidget(const Settings::Setting& setting, const Settings::Value& value, Settings::Value& edited)
{
   const std::string label = std::format("{}###{}", setting.label.empty() ? setting.key : setting.label, setting.key);
   edited = value;
   if (bool* typed = std::get_if<bool>(&edited))
      return ImGui::Checkbox(label.c_str(), typed);
   if (int32_t* typed = std::get_if<int32_t>(&edited))
   {
      if (setting.range)
         return ImGui::SliderInt(label.c_str(), typed, std::get<int32_t>(setting.range->min), std::get<int32_t>(setting.range->max), "%d", ImGuiSliderFlags_AlwaysClamp);
      return ImGui::InputInt(label.c_str(), typed);
   }
   if (float* typed = std::get_if<float>(&edited))
   {
      if (setting.range)
         return ImGui::SliderFloat(label.c_str(), typed, std::get<float>(setting.range->min), std::get<float>(setting.range->max), Terminated(setting.format).c_str(), ImGuiSliderFlags_AlwaysClamp);
      return ImGui::DragFloat(label.c_str(), typed, 0.01f);
   }
   if (Settings::EnumIndex* typed = std::get_if<Settings::EnumIndex>(&edited))
   {
      const std::string preview =
         typed->value >= 0 && size_t(typed->value) < setting.enum_labels.size() ? Terminated(setting.enum_labels[typed->value]) : std::to_string(typed->value);
      bool changed = false;
      if (ImGui::BeginCombo(label.c_str(), preview.c_str()))
      {
         for (int32_t i = 0; i < int32_t(setting.enum_labels.size()); i++)
         {
            if (ImGui::Selectable(Terminated(setting.enum_labels[i]).c_str(), i == typed->value))
            {
               typed->value = i;
               changed = true;
            }
         }
         ImGui::EndCombo();
      }
      return changed;
   }
   return false;
}

void DrawSetting(const Settings::Setting& setting, const Settings::Value& value)
{
   ImGui::PushID(Terminated(setting.key).c_str());
   const bool enabled = !setting.is_enabled || setting.is_enabled();
   ImGui::BeginDisabled(!enabled);
   if (setting.key == Input::TOGGLE_KEY_SETTING)
      DrawToggleKey(setting, value);
   else if (Settings::Value edited; DrawWidget(setting, value, edited))
      Settings::Set(setting.key, edited);
   ImGui::EndDisabled();
   if (!setting.tooltip.empty())
      ImGui::SetItemTooltip("%s", Terminated(setting.tooltip).c_str());
   if (ImGui::BeginPopupContextItem("Reset"))
   {
      if (ImGui::MenuItem("Reset to default"))
         Settings::Reset(setting.key);
      ImGui::EndPopup();
   }
   ImGui::PopID();
}

void DrawSettings()
{
   // Categories in the order their first setting was registered
   std::vector<std::pair<std::string_view, std::vector<std::pair<const Settings::Setting*, Settings::Value>>>> categories;
   Settings::ForEach([&](const Settings::Setting& setting, const Settings::Value& value) {
      if (!IsShown(setting))
         return;
      const std::string_view category = Settings::GetCategory(setting);
      auto it = categories.begin();
      while (it != categories.end() && it->first != category)
         ++it;
      if (it == categories.end())
         it = categories.insert(it, {category, {}});
      it->second.emplace_back(&setting, value);
   });
   for (const auto& [category, settings] : categories)
   {
      if (!ImGui::CollapsingHeader(Terminated(category).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
         continue;
      for (const auto& [setting, value] : settings)
         DrawSetting(*setting, value);
   }
}

void DrawFooter()
{
   const Game::Info& info = Game::GetInfo();
   const float start = ImGui::GetCursorPosY();
   ImGui::Separator();
   ImGui::TextUnformatted(std::format("{} {}", info.name.empty() ? "Luma" : info.name, info.version).c_str());
   if (!info.credits.empty() && ImGui::TreeNode("Credits"))
   {
      for (const std::string_view credit : info.credits)
      {
         // "Main:", "Acknowledgments:", ... head the names after them
         if (credit.ends_with(':'))
            ImGui::TextDisabled("%s", Terminated(credit).c_str());
         else
            ImGui::BulletText("%s", Terminated(credit).c_str());
      }
      ImGui::TreePop();
   }
   const float available = ImGui::GetContentRegionAvail().x;
   float line_width = 0.f;
   for (size_t i = 0; i < info.links.size(); i++)
   {
      const std::string label = Terminated(info.links[i].label);
      const float width = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().ItemSpacing.x;
      if (i > 0 && line_width + width <= available)
         ImGui::SameLine();
      else
         line_width = 0.f;
      ImGui::TextLinkOpenURL(label.c_str(), Terminated(info.links[i].url).c_str());
      line_width += width;
   }
   footer_height = ImGui::GetCursorPosY() - start;
}
} // namespace

void DrawMainUI()
{
   const Game::Info& info = Game::GetInfo();
   ImGui::SetNextWindowPos(ImVec2(40.f, 40.f), ImGuiCond_FirstUseEver);
   ImGui::SetNextWindowSize(ImVec2(560.f, 640.f), ImGuiCond_FirstUseEver);
   const std::string title = std::format("Luma: {}###Luma", info.name.empty() ? "settings" : info.name);
   if (ImGui::Begin(title.c_str()))
   {
      if (ImGui::BeginChild("Main", ImVec2(0.f, -footer_height)))
      {
         if (ImGui::BeginTabBar("Tabs"))
         {
            if (ImGui::BeginTabItem("Settings"))
            {
               DrawSettings();
               ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
         }
      }
      ImGui::EndChild();
      DrawFooter();
   }
   ImGui::End();
}
} // namespace UI
} // namespace Luma
