#include "UI/Shell.hpp"

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Game/Game.hpp"
#include "Settings/Registry.hpp"
#include "Trace/Info.hpp"
#include "UI/Icons.hpp"
#include "UI/Input.hpp"
#include "UI/Style.hpp"
#include "UI/Widgets.hpp"

namespace Luma {
namespace UI {
namespace {
using Widgets::Tone;

// ImGui wants null terminated strings
std::string Terminated(std::string_view text)
{
   return std::string(text);
}

bool IsShown(const Settings::Setting& setting)
{
   return Settings::IsVisibleInBuild(setting.visibility) && (!setting.is_visible || setting.is_visible());
}

void CallPanel(void (*panel)())
{
   if (panel)
      panel();
}

void DrawHeader(const Trace::Status& status)
{
   const Game::Info& info = Game::GetInfo();
   ImGui::AlignTextToFramePadding();
   ImGui::TextUnformatted(info.name.empty() ? "Luma" : Terminated(info.name).c_str());
   ImGui::SameLine();
   Widgets::TextDim(info.version);

   // Right aligned: the API (the output mode chip arrives with Step 5), then the global switch
   const float unit = Style::Scale(Style::Sizes::UNIT);
   const float toggle_width = ImGui::GetFrameHeight() * 1.8f + ImGui::CalcTextSize(Icons::POWER).x + ImGui::GetStyle().ItemInnerSpacing.x;
   const float api_width = status.api.empty() ? 0.f : ImGui::CalcTextSize(status.api.c_str()).x + unit * 3.f;
   ImGui::SameLine(ImGui::GetContentRegionMax().x - toggle_width - api_width);
   if (!status.api.empty())
   {
      Widgets::TextDim(status.api);
      ImGui::SameLine(0.f, unit * 3.f);
   }
   bool enabled = IsEnabled();
   // The power icon in the accent color while Luma is on
   ImGui::PushStyleColor(ImGuiCol_Text, enabled ? Style::Colors::ACCENT : Style::Colors::ICON_DISABLED);
   ImGui::TextUnformatted(Icons::POWER);
   ImGui::PopStyleColor();
   ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
   if (Widgets::Toggle("Enabled", enabled))
      Settings::Set(ENABLED_SETTING, enabled);
   const std::string key = Input::GetKeyName(Settings::Get<Settings::Key>(Input::ENABLED_KEY_SETTING).code);
   ImGui::SetItemTooltip("%s", std::format("Luma {} ({} switches it)", enabled ? "on" : "off, the game's own image", key).c_str());
   ImGui::Separator();
}

void DrawSettings()
{
   // Categories in the order their first setting was registered
   std::vector<std::pair<std::string_view, std::vector<const Settings::Setting*>>> categories;
   Settings::ForEach([&](const Settings::Setting& setting, const Settings::Value&) {
      if (!IsShown(setting))
         return;
      const std::string_view category = Settings::GetCategory(setting);
      auto it = categories.begin();
      while (it != categories.end() && it->first != category)
         ++it;
      if (it == categories.end())
         it = categories.insert(it, {category, {}});
      it->second.push_back(&setting);
   });
   // Rows may change the registry: draw from the collected pointers, which stay valid (settings are never removed while drawing)
   for (const auto& [category, settings] : categories)
   {
      if (!Widgets::Section(category))
         continue;
      for (const Settings::Setting* setting : settings)
         Widgets::SettingRow(*setting);
      Widgets::EndSection();
   }
   CallPanel(Game::GetPanels().settings);
}

void DrawFooter()
{
   const Game::Info& info = Game::GetInfo();
   ImGui::Spacing();
   ImGui::Separator();
   CallPanel(Game::GetPanels().about);
   if (!info.credits.empty() && Widgets::Section("Credits", false))
   {
      for (const std::string_view credit : info.credits)
      {
         // "Main:", "Acknowledgments:", ... head the names after them
         if (credit.ends_with(':'))
            Widgets::Caption(credit);
         else
            Widgets::ListItem(credit);
      }
      // Luma's own, Font Awesome's license asks for the attribution
      Widgets::Caption("Luma uses:");
      Widgets::ListItem("Dear ImGui");
      Widgets::ListItem("Font Awesome Free (icons CC BY 4.0, fonts SIL OFL 1.1)");
      Widgets::EndSection();
   }
   float line_used = 0.f;
   for (const Game::Link& link : info.links)
   {
      line_used = Widgets::WrapSameLine(line_used, Widgets::GetLinkWidth(link.label, link.url));
      Widgets::Link(link.label, link.url);
   }
}

void DrawInfo(const Trace::Status& status)
{
   CallPanel(Game::GetPanels().info);
   if (Widgets::Section("Status"))
   {
      const auto row = [](std::string_view name, const std::string& value) { Widgets::Property(name, value); };
      row("Process", std::format("{} ({}-bit, pid {})", status.exe_path, status.bits, status.pid));
      row("Luma", status.luma_path);
      row("Frames", std::to_string(status.frame_count));
      row("Devices", std::to_string(status.counts.proxies));
      row("Hooks", std::format("{} installed of {} declared, {} failed", status.counts.hooks_installed, status.counts.hooks_declared, status.counts.hooks_failed));
      for (const Trace::SwapchainInfo& swapchain : status.swapchains)
         row("Swapchain", std::format("{} {}x{}, format {}, {} buffers", swapchain.api, swapchain.width, swapchain.height, swapchain.format, swapchain.buffers));
      row("Validation", status.validation.debug_layer ? std::format("{} errors, {} warnings, {} corruptions", status.validation.errors, status.validation.warnings, status.validation.corruptions) : std::string("off"));
      Widgets::EndSection();
   }
}

// The title bar is drawn by ImGui: a translucent gradient and the top highlight go over it, like the section headers' depth
void DrawTitleBarDepth()
{
   const ImVec2 position = ImGui::GetWindowPos();
   const ImVec2 max(position.x + ImGui::GetWindowWidth(), position.y + ImGui::GetFrameHeight());
   const float rounding = ImGui::GetStyle().WindowRounding;
   ImDrawList* draw_list = ImGui::GetWindowDrawList();
   draw_list->PushClipRect(position, max, false);
   const ImU32 top = ImGui::GetColorU32(Style::Colors::HOVER_OVERLAY);
   const ImU32 bottom = ImGui::GetColorU32(Style::WithAlpha(Style::Colors::HOVER_OVERLAY, 0.f));
   draw_list->AddRectFilledMultiColor(ImVec2(position.x + 1.f, position.y + 1.f), ImVec2(max.x - 1.f, max.y), top, top, bottom, bottom);
   draw_list->AddLine(ImVec2(position.x + rounding, position.y + 1.5f), ImVec2(max.x - rounding, position.y + 1.5f), ImGui::GetColorU32(Style::Colors::HIGHLIGHT));
   draw_list->PopClipRect();
}

std::vector<Widgets::StatusItem> GetStatusItems(const Trace::Status& status)
{
   std::vector<Widgets::StatusItem> items;
   if (!status.swapchains.empty())
   {
      const Trace::SwapchainInfo& swapchain = status.swapchains.front();
      items.push_back({Icons::DISPLAY, std::format("{} {}x{}", swapchain.api, swapchain.width, swapchain.height), Tone::Info, "The game's swapchain"});
   }
   items.push_back({Icons::TOOLS, std::format("{} hooks", status.counts.hooks_installed), status.counts.hooks_failed ? Tone::Warning : Tone::Info,
      std::format("{} failed", status.counts.hooks_failed)});
   if (status.validation.errors || status.validation.corruptions)
      items.push_back({Icons::WARNING, std::format("{} validation errors", status.validation.errors + status.validation.corruptions), Tone::Error, "See Luma.log"});
   return items;
}
} // namespace

void DrawShell()
{
   const Game::Info& info = Game::GetInfo();
   const Trace::Status status = Trace::GetStatus();
   const ImGuiViewport* viewport = ImGui::GetMainViewport();
   const float margin = Style::Scale(Style::Sizes::UNIT * 10.f);
   ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + margin, viewport->WorkPos.y + margin), ImGuiCond_FirstUseEver);
   ImGui::SetNextWindowSize(ImVec2(Style::Scale(620.f), Style::Scale(720.f)), ImGuiCond_FirstUseEver);
   const std::string title = std::format("{} Luma{}###Luma", Icons::SETTINGS, info.name.empty() ? "" : std::format(": {}", info.name));
   if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse))
   {
      DrawTitleBarDepth();
      DrawHeader(status);
      const float bottom = ImGui::GetFrameHeight() + Style::Scale(Style::Sizes::UNIT) + ImGui::GetStyle().ItemSpacing.y;
      if (ImGui::BeginChild("Body", ImVec2(0.f, -bottom)))
      {
#if DEVELOPMENT
         if (ImGui::BeginTabBar("Tabs"))
         {
            if (Widgets::Tab("Settings", Icons::SLIDERS))
            {
               DrawSettings();
               DrawFooter();
               Widgets::EndTab();
            }
            if (Game::GetPanels().development && Widgets::Tab("Development"))
            {
               Game::GetPanels().development();
               Widgets::EndTab();
            }
            if (Widgets::Tab("Info"))
            {
               DrawInfo(status);
               Widgets::EndTab();
            }
            ImGui::EndTabBar();
         }
#else
         DrawSettings();
         if (Game::GetPanels().info && Widgets::Section("Info", false))
         {
            Game::GetPanels().info();
            Widgets::EndSection();
         }
         DrawFooter();
#endif
      }
      ImGui::EndChild();
      const std::vector<Widgets::StatusItem> items = GetStatusItems(status);
      Widgets::StatusBar(items);
   }
   ImGui::End();
}
} // namespace UI
} // namespace Luma
