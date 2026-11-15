#include "UI/Shell.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Game/Registry.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Shaders/Defines.hpp"
#include "System/Jobs.hpp"
#include "Shaders/Shaders.hpp"
#include "Trace/Info.hpp"
#include "UI/Icons.hpp"
#include "UI/Input.hpp"
#include "UI/Panels/Patches.hpp"
#include "UI/Panels/Resources.hpp"
#include "UI/Panels/Shaders.hpp"
#include "UI/Panels/Trace.hpp"
#include "UI/Style.hpp"
#include "UI/Widgets.hpp"

namespace Luma {
namespace UI {
namespace {
using Widgets::Tone;

#if DEVELOPMENT
enum class Tab : uint8_t
{
   Settings,
   Shaders,
   Patches,
   Resources,
   Trace,
   Info,
};

Tab current_tab = Tab::Settings;
#endif

// Luma's categories at the top of the settings view, in this order
constexpr std::array<std::string_view, 2> TOP_CATEGORIES = {"Luma", "Overlay"};

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
   const Game::Info& info = Game::Registry::GetInfo();
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
      Settings::Set(Settings::Core::ENABLED, enabled);
   const std::string key = Input::GetKeyName(Settings::Get(Settings::Core::ENABLED_KEY).code);
   ImGui::SetItemTooltip("%s", std::format("Luma {} ({} switches it)", enabled ? "on" : "off, the game's own image", key).c_str());
   ImGui::Separator();
}

// Defines changed while Auto Compile is off apply once compiled: one banner says so, the rows are tagged "pending"
void DrawPendingCompile(const SettingsSection& section)
{
   const size_t pending = std::ranges::count_if(section.settings, [](const Settings::Setting* setting) { return Shaders::Defines::IsPending(*setting); });
   if (pending == 0)
      return;
   const std::string text = std::format("{} define{} changed since the shaders were built, applied once they compile", pending, pending == 1 ? "" : "s");
   if (Widgets::Banner(Tone::Warning, text, "Compile Shaders", "Compiles the shaders with the defines as they are now"))
      Shaders::Reload();
}

void DrawSection(const SettingsSection& section)
{
   void (*const development_panel)() = section.role == SettingsSection::Role::Development ? Game::Registry::GetPanels().development : nullptr;
   if (section.settings.empty() && !development_panel)
      return;
   if (!Widgets::Section(section.title))
      return;
   if (section.role == SettingsSection::Role::Defines)
      DrawPendingCompile(section);
   // Rows may change the registry: drawn from the collected pointers, which stay valid (settings are never removed while drawing)
   for (const Settings::Setting* setting : section.settings)
      Widgets::SettingRow(*setting);
   CallPanel(development_panel);
   Widgets::EndSection();
}

// The sections, with the game's own settings panel after its categories
void DrawSettings()
{
   const std::vector<SettingsSection> sections = CollectSettingsSections();
   const auto is_top = [](const SettingsSection& section) { return std::ranges::contains(TOP_CATEGORIES, section.title) && section.role == SettingsSection::Role::Settings; };
   size_t game_end = 0;
   for (size_t i = 0; i < sections.size(); i++)
   {
      if (is_top(sections[i]) || sections[i].role == SettingsSection::Role::Game)
         game_end = i + 1;
      else
         break;
   }
   for (size_t i = 0; i < sections.size(); i++)
   {
      if (i == game_end)
         CallPanel(Game::Registry::GetPanels().settings);
      DrawSection(sections[i]);
   }
   if (game_end == sections.size())
      CallPanel(Game::Registry::GetPanels().settings);
}

// 1.0's About tab: what the mod is, the game's own about panel, the credits, the version and build
void DrawAbout()
{
   const Game::Info& info = Game::Registry::GetInfo();
   if (!Widgets::Section("About", false))
      return;
   if (!info.description.empty())
      Widgets::Paragraph(info.description);
   CallPanel(Game::Registry::GetPanels().about);
   if (!info.credits.empty())
   {
      for (const std::string_view credit : info.credits)
      {
         // "Main:", "Acknowledgments:", ... head the names after them
         if (credit.ends_with(':'))
            Widgets::Caption(credit);
         else
            Widgets::ListItem(credit);
      }
   }
   // Luma's own, Font Awesome's license asks for the attribution
   Widgets::Caption("Luma uses:");
   Widgets::ListItem("Dear ImGui");
   Widgets::ListItem("Font Awesome Free (icons CC BY 4.0, fonts SIL OFL 1.1)");
   ImGui::Spacing();
   if (!info.version.empty())
      Widgets::Property("Version", info.version);
#if DEVELOPMENT
   Widgets::Property("Build", "Development");
#elif TEST
   Widgets::Property("Build", "Test");
#else
   Widgets::Property("Build", "Publishing");
#endif
   Widgets::EndSection();
}

// The rows the game's links wrap to at `width`, as Widgets::WrapSameLine lays them out
int CountLinkRows(const Game::Info& info, float width)
{
   int rows = 0;
   float used = 0.f;
   const float spacing = ImGui::GetStyle().ItemSpacing.x;
   for (const Game::Link& link : info.links)
   {
      const float link_width = Widgets::GetLinkWidth(link.label, link.url);
      if (used > 0.f && used + spacing + link_width <= width)
      {
         used += spacing + link_width;
         continue;
      }
      rows++;
      used = link_width;
   }
   return rows;
}

float GetStatusBarHeight()
{
   return ImGui::GetFrameHeight() + Style::Scale(Style::Sizes::UNIT);
}

// The links and the status bar, under the scrolling content
float GetFooterHeight(const Game::Info& info)
{
   const float spacing = ImGui::GetStyle().ItemSpacing.y;
   float height = GetStatusBarHeight() + spacing;
   if (const int rows = CountLinkRows(info, ImGui::GetContentRegionAvail().x); rows > 0)
      height += float(rows) * (ImGui::GetFrameHeight() + spacing) + spacing;
   return height;
}

void DrawLinks(const Game::Info& info)
{
   if (info.links.empty())
      return;
   ImGui::Separator();
   float line_used = 0.f;
   for (const Game::Link& link : info.links)
   {
      line_used = Widgets::WrapSameLine(line_used, Widgets::GetLinkWidth(link.label, link.url));
      Widgets::Link(link.label, link.url);
   }
}

void DrawInfo(const Trace::Status& status)
{
   CallPanel(Game::Registry::GetPanels().info);
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
   if (const System::Jobs::Status job = System::Jobs::GetStatus(); !job.name.empty())
   {
      const std::string others = job.running > 1 || job.queued ? std::format(", {} more running, {} queued", job.running - 1, job.queued) : std::string();
      items.push_back({Icons::RELOAD, std::format("{} {}/{}", job.name, job.done, job.total), Tone::Info, std::format("On Luma's worker threads (up to {}){}", job.threads, others),
         job.total ? float(job.done) / float(job.total) : -1.f});
   }
   if (const Shaders::BuildInfo& build = Shaders::GetLastBuild(); build.failed)
      items.push_back({Icons::ERROR_, std::format("{} shader{} failed", build.failed, build.failed == 1 ? "" : "s"), Tone::Error, build.errors});
   if (status.patches.broken_recipes)
      items.push_back({Icons::PATCH, std::format("{} recipe{} broken", status.patches.broken_recipes, status.patches.broken_recipes == 1 ? "" : "s"), Tone::Error,
         "Didn't parse: their shaders run as the game made them. See the Patches tab."});
   if (status.patches.failed)
      items.push_back({Icons::PATCH, std::format("{} patch{} failed", status.patches.failed, status.patches.failed == 1 ? "" : "es"), Tone::Warning,
         "The shaders run as the game made them: see the Patches tab"});
   if (status.validation.errors || status.validation.corruptions)
      items.push_back({Icons::WARNING, std::format("{} validation errors", status.validation.errors + status.validation.corruptions), Tone::Error, "See Luma.log"});
   return items;
}

#if DEVELOPMENT
void DrawTabBar()
{
   if (!ImGui::BeginTabBar("Tabs"))
      return;
   const auto tab = [](Tab id, std::string_view label, const char* icon) {
      if (Widgets::Tab(label, icon))
      {
         current_tab = id;
         Widgets::EndTab();
      }
   };
   tab(Tab::Settings, "Settings", Icons::SLIDERS);
   tab(Tab::Shaders, "Shaders", Icons::CODE);
   tab(Tab::Patches, "Patches", Icons::PATCH);
   tab(Tab::Resources, "Resources", Icons::LAYERS);
   tab(Tab::Trace, "Trace", Icons::LIST);
   tab(Tab::Info, "Info", nullptr);
   ImGui::EndTabBar();
}
#endif

void DrawContent(const Trace::Status& status)
{
#if DEVELOPMENT
   switch (current_tab)
   {
   case Tab::Settings:
      DrawSettings();
      DrawAbout();
      break;
   case Tab::Shaders: Panels::DrawShaders(); break;
   case Tab::Patches: Panels::DrawPatches(); break;
   case Tab::Resources: Panels::DrawResources(); break;
   case Tab::Trace: Panels::DrawTrace(); break;
   case Tab::Info: DrawInfo(status); break;
   }
#else
   (void)status;
   DrawSettings();
   if (Game::Registry::GetPanels().info && Widgets::Section("Info", false))
   {
      Game::Registry::GetPanels().info();
      Widgets::EndSection();
   }
   DrawAbout();
#endif
}
} // namespace

std::vector<SettingsSection> CollectSettingsSections()
{
   using Role = SettingsSection::Role;
   std::vector<SettingsSection> top(TOP_CATEGORIES.size());
   for (size_t i = 0; i < TOP_CATEGORIES.size(); i++)
      top[i].title = TOP_CATEGORIES[i];
   std::vector<SettingsSection> game;
   SettingsSection defines = {"Shader Defines", {}, Role::Defines};
   SettingsSection recipes = {"Recipes", {}, Role::Settings};
   std::vector<SettingsSection> rest;
   SettingsSection development = {"Development Settings", {}, Role::Development};
   const auto add = [](std::vector<SettingsSection>& sections, std::string_view title, Role role, const Settings::Setting* setting) {
      auto it = std::ranges::find(sections, title, &SettingsSection::title);
      if (it == sections.end())
         it = sections.insert(it, {title, {}, role});
      it->settings.push_back(setting);
   };
   Settings::ForEach([&](const Settings::Setting& setting, const Settings::Value&) {
      if (!IsShown(setting))
         return;
      const std::string_view category = Settings::GetCategory(setting);
      switch (setting.kind)
      {
      case Settings::Kind::Game: add(game, category, Role::Game, &setting); break;
      case Settings::Kind::Define: defines.settings.push_back(&setting); break;
      case Settings::Kind::Recipe: recipes.settings.push_back(&setting); break;
      case Settings::Kind::Development: development.settings.push_back(&setting); break;
      case Settings::Kind::Luma:
         // Auto Compile is drawn with the defines it compiles (its key and file place stay Luma's "Shaders" category)
         if (setting.key == Settings::Core::AUTO_COMPILE.key)
            defines.settings.insert(defines.settings.begin(), &setting);
         else if (const auto it = std::ranges::find(top, category, &SettingsSection::title); it != top.end())
            it->settings.push_back(&setting);
         else
            add(rest, category, Role::Settings, &setting);
         break;
      }
   });
   // Auto Compile alone has nothing to compile
   if (std::ranges::none_of(defines.settings, [](const Settings::Setting* setting) { return setting->kind == Settings::Kind::Define; }))
      defines.settings.clear();
   std::vector<SettingsSection> sections;
   const auto append = [&](SettingsSection& section) {
      if (!section.settings.empty())
         sections.push_back(std::move(section));
   };
   std::ranges::for_each(top, append);
   std::ranges::for_each(game, append);
   append(defines);
   append(recipes);
   std::ranges::for_each(rest, append);
#if DEVELOPMENT
   // Shown for the game's development panel even without settings
   sections.push_back(std::move(development));
#endif
   return sections;
}

void DrawShell()
{
   const Game::Info& info = Game::Registry::GetInfo();
   const Trace::Status status = Trace::GetStatus();
   const ImGuiViewport* viewport = ImGui::GetMainViewport();
   const float margin = Style::Scale(Style::Sizes::UNIT * 10.f);
   ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + margin, viewport->WorkPos.y + margin), ImGuiCond_FirstUseEver);
   ImGui::SetNextWindowSize(ImVec2(Style::Scale(620.f), Style::Scale(720.f)), ImGuiCond_FirstUseEver);
   const std::string title = std::format("{} Luma{}###Luma", Icons::SETTINGS, info.name.empty() ? "" : std::format(": {}", info.name));
   // Only the content between the header and the footer scrolls
   if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
   {
      DrawTitleBarDepth();
      DrawHeader(status);
#if DEVELOPMENT
      DrawTabBar();
#endif
      if (ImGui::BeginChild("Content", ImVec2(0.f, -GetFooterHeight(info))))
         DrawContent(status);
      ImGui::EndChild();
      DrawLinks(info);
      const std::vector<Widgets::StatusItem> items = GetStatusItems(status);
      Widgets::StatusBar(items);
   }
   ImGui::End();
}
} // namespace UI
} // namespace Luma
