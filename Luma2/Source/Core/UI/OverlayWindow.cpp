#include "UI/OverlayWindow.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "Game/GameRegistry.hpp"
#include "Hooks/HookManager.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Shaders/ShaderDefines.hpp"
#include "Shaders/Shaders.hpp"
#include "System/JobQueue.hpp"
#include "Trace/RuntimeStatus.hpp"
#if DEVELOPMENT
#include "Trace/D3D12/D3D12Stats.hpp"
#endif
#include "UI/Icons.hpp"
#include "UI/Overlay.hpp"
#include "UI/OverlayInput.hpp"
#include "UI/Panels/PatchesPanel.hpp"
#include "UI/Panels/ResourcesPanel.hpp"
#include "UI/Panels/ShadersPanel.hpp"
#include "UI/Panels/TracePanel.hpp"
#include "UI/Style.hpp"
#include "UI/Widgets.hpp"

namespace Luma {
namespace UI {
namespace {
using Widgets::Tone;

enum class Tab : uint8_t
{
   Settings,
#if DEVELOPMENT
   Framework,
#endif
   Overlay,
#if DEVELOPMENT
   Development,
   Shaders,
   Patches,
   Resources,
   Trace,
#endif
#if DEVELOPMENT || TEST
   Info,
#else
   About,
#endif
};

Tab current_tab = Tab::Settings;
std::atomic<bool> reset_window_layout = false;
bool capturing_enabled_key = false;

// Shared project links appended after each game's donation links in the footer
constexpr std::array<Game::Link, 2> CORE_LINKS = {{
   {"Join our \"HDR Den\" Discord", "https://discord.gg/J9fM3EVuEZ"},
   {"Contribute on Github", "https://github.com/Filoppi/Luma-Framework"},
}};

std::vector<Game::Link> CollectFooterLinks(const Game::Info& info)
{
   std::vector<Game::Link> links(info.links.begin(), info.links.end());
   for (const Game::Link& core_link : CORE_LINKS)
   {
      if (std::ranges::none_of(links, [&](const Game::Link& existing) { return existing.url == core_link.url; }))
         links.push_back(core_link);
   }
   return links;
}

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

void DrawHeader(const Trace::OverlaySummary& summary)
{
   const Game::Info& info = Game::Registry::GetInfo();
   ImGui::AlignTextToFramePadding();
   ImGui::TextUnformatted(info.name.empty() ? "Luma" : Terminated(info.name).c_str());
   ImGui::SameLine();
   Widgets::TextDim(info.version);

   // Update captured on/off hotkey if the header keybind button requested one
   uint32_t enabled_key_code = Settings::Get(Settings::Core::ENABLED_KEY).code;
   if (capturing_enabled_key && !Input::IsCapturingKey())
   {
      capturing_enabled_key = false;
      if (const uint32_t captured = Input::TakeCapturedKey(); captured && captured != enabled_key_code)
      {
         enabled_key_code = captured;
         Settings::Set(Settings::Core::ENABLED_KEY, Settings::Key{captured});
      }
   }
   const std::string key_name = capturing_enabled_key ? "..." : Input::GetKeyName(enabled_key_code);
   const std::string key_label = std::format("{} {}", Icons::KEYBOARD, key_name);

   // Right aligned: the API, compact on/off hotkey button, then the global switch
   const std::string_view api = summary.primary_swapchain ? std::string_view(summary.primary_swapchain->api) : std::string_view();
   const float unit = Style::Scale(Style::Sizes::UNIT);
   const ImGuiStyle& style = ImGui::GetStyle();
   const float key_width = ImGui::CalcTextSize(key_label.c_str()).x + style.FramePadding.x * 2.f;
   const float toggle_width = ImGui::GetFrameHeight() * 1.8f + ImGui::CalcTextSize(Icons::POWER).x + style.ItemInnerSpacing.x;
   const float api_width = api.empty() ? 0.f : ImGui::CalcTextSize(api.data(), api.data() + api.size()).x + unit * 2.5f;
   const float right_width = api_width + key_width + style.ItemSpacing.x + toggle_width;
   ImGui::SameLine(std::max(ImGui::GetCursorPosX() + unit * 2.f, ImGui::GetContentRegionMax().x - right_width));
   if (!api.empty())
   {
      Widgets::TextDim(api);
      ImGui::SameLine(0.f, unit * 2.5f);
   }
   ImGui::PushStyleColor(ImGuiCol_Text, capturing_enabled_key ? Style::Colors::ACCENT : Style::Colors::TEXT_DIM);
   if (ImGui::Button(std::format("{}###HeaderEnabledKey", key_label).c_str()))
   {
      capturing_enabled_key = true;
      Input::CaptureKey();
   }
   ImGui::PopStyleColor();
   ImGui::SetItemTooltip("%s", capturing_enabled_key ? "Press a key for the Luma on/off hotkey (Escape cancels)" : "Luma on/off hotkey (click to rebind)");
   ImGui::SameLine();
   bool enabled = Settings::IsEnabled();
   ImGui::PushStyleColor(ImGuiCol_Text, enabled ? Style::Colors::ACCENT : Style::Colors::ICON_DISABLED);
   ImGui::TextUnformatted(Icons::POWER);
   ImGui::PopStyleColor();
   ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
   if (Widgets::Toggle("Enabled", enabled))
      Settings::Set(Settings::Core::ENABLED, enabled);
   ImGui::SetItemTooltip("%s", std::format("Luma {} ({} switches it)", enabled ? "on" : "off, the game's own image", Input::GetKeyName(enabled_key_code)).c_str());
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

void DrawSection(const SettingsSection& section, std::optional<bool> force_open)
{
   if (section.settings.empty())
      return;
   if (force_open)
      Widgets::SetNextSectionOpen(*force_open);
   if (!Widgets::Section(section.title, section.default_open))
      return;
   if (section.role == SettingsSection::Role::Defines)
      DrawPendingCompile(section);
   for (const Settings::Setting* setting : section.settings)
      Widgets::SettingRow(*setting);
   Widgets::EndSection();
}

void DrawSettingsTab(SettingsTab tab)
{
   const std::vector<SettingsSection> sections = CollectSettingsSections(tab);
   std::optional<bool> force_open;
   if (sections.size() > 1)
   {
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y * 0.65f));
      if (Widgets::Button("Expand All", Widgets::ButtonKind::Secondary, "Expands every section on this tab"))
         force_open = true;
      ImGui::SameLine();
      if (Widgets::Button("Collapse All", Widgets::ButtonKind::Secondary, "Collapses every section on this tab"))
         force_open = false;
      ImGui::PopStyleVar();
   }
   for (const SettingsSection& section : sections)
      DrawSection(section, force_open);
   if (tab == SettingsTab::Settings)
      CallPanel(Game::Registry::GetPanels().settings);
   else if (tab == SettingsTab::Overlay)
   {
      if (Widgets::Button("Reset Layout", Widgets::ButtonKind::Secondary, "Resets overlay window size, position, and column widths saved in Luma.toml"))
         ResetLayout();
   }
#if DEVELOPMENT
   else if (tab == SettingsTab::Development && Game::Registry::GetPanels().development)
   {
      if (force_open)
         Widgets::SetNextSectionOpen(*force_open);
      if (Widgets::Section("Game Development", true))
      {
         Game::Registry::GetPanels().development();
         Widgets::EndSection();
      }
   }
#endif
}

// Headings in Info::credits end with ":" ("Main:", "Third Party:"), the rest are the names under them
void DrawCredits(std::span<const std::string_view> credits)
{
   for (const std::string_view line : credits)
   {
      if (line.ends_with(':'))
         Widgets::Caption(line);
      else
         Widgets::ListItem(line);
   }
}

void DrawAbout(bool default_open)
{
   const Game::Info& info = Game::Registry::GetInfo();
   const Game::Panels& panels = Game::Registry::GetPanels();
   if (!Widgets::Section("About", default_open))
      return;
   if (!info.description.empty())
      Widgets::Paragraph(info.description);
   CallPanel(panels.about);
   if (!info.credits.empty())
      DrawCredits(info.credits);
#if DEVELOPMENT
   Widgets::Property("Build", "Development");
#elif TEST
   Widgets::Property("Build", "Test");
#else
   Widgets::Property("Build", "Publishing");
#endif
   Widgets::EndSection();
}

// Compact single-row link padding so donation/project links stay unobtrusive on every tab
float GetCompactLinkHeight()
{
   return ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y;
}

int CountLinkRows(std::span<const Game::Link> links, float width)
{
   int rows = 0;
   float used = 0.f;
   const float spacing = ImGui::GetStyle().ItemSpacing.x;
   for (const Game::Link& link : links)
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

// The links and the status bar, pinned under the scrolling content across all tabs
float GetFooterHeight(std::span<const Game::Link> links)
{
   const float spacing = ImGui::GetStyle().ItemSpacing.y;
   float height = GetStatusBarHeight() + spacing;
   if (const int rows = CountLinkRows(links, ImGui::GetContentRegionAvail().x); rows > 0)
      height += float(rows) * (GetCompactLinkHeight() + spacing) + spacing;
   return height;
}

void DrawLinks(std::span<const Game::Link> links)
{
   if (links.empty())
      return;
   ImGui::Separator();
   ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y * 0.5f));
   float line_used = 0.f;
   for (const Game::Link& link : links)
   {
      line_used = Widgets::WrapSameLine(line_used, Widgets::GetLinkWidth(link.label, link.url));
      Widgets::Link(link.label, link.url);
   }
   ImGui::PopStyleVar();
}

#if DEVELOPMENT || TEST
void DrawInfo(const Trace::Status& status)
{
   // What's at each hooked target, read again while this is shown
   static uint64_t last_verify = 0;
   if (const uint64_t now = GetTickCount64(); now - last_verify > 1000)
   {
      last_verify = now;
      Hooks::Verify();
   }
   CallPanel(Game::Registry::GetPanels().info);
   if (Widgets::Section("Status"))
   {
      const auto row = [](std::string_view name, const std::string& value) { Widgets::Property(name, value); };
      row("Process", std::format("{} ({}-bit, pid {})", status.exe_path, status.bits, status.pid));
      row("Luma", status.luma_path);
      row("Frames", std::to_string(status.frame_count));
      row("Devices", std::to_string(status.counts.proxies));
      row("Hooks", std::format("{} installed of {} declared, {} failed", status.counts.hooks_installed, status.counts.hooks_declared, status.counts.hooks_failed));
      row("Hook chains", std::format("{} first, {} under another hooker, {} on top of one, {} unhooked by someone else", status.hooks.intact, status.hooks.chained, status.hooks.detoured_before, status.hooks.restored));
      row("Present", status.hooks.present_overlay.empty() ? status.hooks.present_order : std::format("{} ({})", status.hooks.present_order, status.hooks.present_overlay));
      if (!status.hooks.overlays.empty())
      {
         std::string overlays;
         for (const std::string& overlay : status.hooks.overlays)
            overlays += (overlays.empty() ? "" : ", ") + overlay;
         row("Overlays", overlays);
      }
      row("ReShade load", status.reshade.load);
      row("ReShade bridge", status.reshade.bridge);
      row("ReShade swapchain view", status.reshade.mirror_swapchain_view);
      for (const Trace::SwapchainInfo& swapchain : status.swapchains)
         row("Swapchain", std::format("{} {}x{}, format {}, {} buffers", swapchain.api, swapchain.width, swapchain.height, swapchain.format, swapchain.buffers));
      row("Validation", status.validation.debug_layer ? std::format("{} errors, {} warnings, {} corruptions", status.validation.errors, status.validation.warnings, status.validation.corruptions) : std::string("off"));
      Widgets::EndSection();
   }
#if DEVELOPMENT
   if (Trace::Stats::D3D12::HasData() &&
      Widgets::Button("Write D3D12 Stats", Widgets::ButtonKind::Secondary, "Writes Luma/Dump/D3D12Stats.json (also written with each frame capture): what the game does on D3D12, for porting research"))
      Trace::Stats::D3D12::Write();
#endif
   DrawAbout(true);
}
#endif

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

std::vector<Widgets::StatusItem> GetStatusItems(const Trace::OverlaySummary& summary)
{
   std::vector<Widgets::StatusItem> items;
   if (summary.primary_swapchain)
   {
      const Trace::SwapchainInfo& swapchain = *summary.primary_swapchain;
      items.push_back({Icons::DISPLAY, std::format("{} {}x{}", swapchain.api, swapchain.width, swapchain.height), Tone::Info, "The game's swapchain"});
   }
   items.push_back({Icons::TOOLS, std::format("{} hooks", summary.hooks_installed), summary.hooks_failed ? Tone::Warning : Tone::Info,
      std::format("{} failed", summary.hooks_failed)});
   if (const System::Jobs::Status job = System::Jobs::GetStatus(); !job.name.empty())
   {
      const std::string others = job.running > 1 || job.queued ? std::format(", {} more running, {} queued", job.running - 1, job.queued) : std::string();
      items.push_back({Icons::RELOAD, std::format("{} {}/{}", job.name, job.done, job.total), Tone::Info, std::format("On Luma's worker threads (up to {}){}", job.threads, others),
         job.total ? float(job.done) / float(job.total) : -1.f});
   }
   if (const Shaders::BuildInfo& build = Shaders::GetLastBuild(); build.failed)
      items.push_back({Icons::ERROR_, std::format("{} shader{} failed", build.failed, build.failed == 1 ? "" : "s"), Tone::Error, build.errors});
   if (summary.broken_recipes)
      items.push_back({Icons::PATCH, std::format("{} recipe{} broken", summary.broken_recipes, summary.broken_recipes == 1 ? "" : "s"), Tone::Error,
         "Didn't parse: their shaders run as the game made them. See the Patches tab."});
   if (summary.failed_patches)
      items.push_back({Icons::PATCH, std::format("{} patch{} failed", summary.failed_patches, summary.failed_patches == 1 ? "" : "es"), Tone::Warning,
         "The shaders run as the game made them: see the Patches tab"});
   if (summary.validation_errors || summary.validation_corruptions)
      items.push_back({Icons::WARNING, std::format("{} validation errors", summary.validation_errors + summary.validation_corruptions), Tone::Error, "See Luma.log"});
   return items;
}

// Wrapping tab bar so narrow windows flow tabs onto a second row instead of squeezing labels to "..."
void DrawTabBar()
{
   struct TabItem
   {
      Tab id;
      std::string_view label;
      const char* icon;
   };

   const std::array tabs = {
      TabItem{Tab::Settings, "Settings", Icons::SLIDERS},
#if DEVELOPMENT
      TabItem{Tab::Framework, "Framework", Icons::TOOLS},
#endif
      TabItem{Tab::Overlay, "Overlay", Icons::OVERLAY},
#if DEVELOPMENT
      TabItem{Tab::Development, "Development", Icons::CONSOLE},
      TabItem{Tab::Shaders, "Shaders", Icons::CODE},
      TabItem{Tab::Patches, "Patches", Icons::PATCH},
      TabItem{Tab::Resources, "Resources", Icons::LAYERS},
      TabItem{Tab::Trace, "Trace", Icons::LIST},
#endif
#if DEVELOPMENT || TEST
      TabItem{Tab::Info, "Info", Icons::INFO},
#else
      TabItem{Tab::About, "About", Icons::INFO},
#endif
   };
   const ImGuiStyle& style = ImGui::GetStyle();
   const float unit = Style::Scale(Style::Sizes::UNIT);
   const ImVec2 padding(unit * 2.f, style.FramePadding.y);
   ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
   ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(unit, unit));
   float line_used = 0.f;
   for (const TabItem& item : tabs)
   {
      const bool active = current_tab == item.id;
      const std::string text = std::format("{} {}", item.icon, item.label);
      const float width = ImGui::CalcTextSize(text.c_str()).x + padding.x * 2.f;
      line_used = Widgets::WrapSameLine(line_used, width);
      ImGui::PushStyleColor(ImGuiCol_Button, active ? Style::Colors::CONTROL_ACTIVE : Style::WithAlpha(Style::Colors::PANEL, 0.45f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? Style::Colors::CONTROL_ACTIVE : Style::Colors::CONTROL_HOVERED);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, Style::Colors::CONTROL_ACTIVE);
      ImGui::PushStyleColor(ImGuiCol_Text, active ? Style::Colors::TEXT : Style::Colors::TEXT_DIM);
      if (ImGui::Button(std::format("{}###Tab_{}", text, item.label).c_str()))
         current_tab = item.id;
      if (active)
      {
         const ImVec2 min = ImGui::GetItemRectMin();
         const ImVec2 max = ImGui::GetItemRectMax();
         const float bar = Style::Scale(Style::Sizes::ACCENT_BAR);
         ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x, max.y - bar), max, ImGui::GetColorU32(Style::Colors::ACCENT));
      }
      ImGui::PopStyleColor(4);
   }
   ImGui::PopStyleVar(2);
   ImGui::Separator();
}

void DrawContent()
{
   switch (current_tab)
   {
   case Tab::Settings: DrawSettingsTab(SettingsTab::Settings); break;
#if DEVELOPMENT
   case Tab::Framework: DrawSettingsTab(SettingsTab::Framework); break;
#endif
   case Tab::Overlay: DrawSettingsTab(SettingsTab::Overlay); break;
#if DEVELOPMENT
   case Tab::Development: DrawSettingsTab(SettingsTab::Development); break;
   case Tab::Shaders: Panels::DrawShaders(); break;
   case Tab::Patches: Panels::DrawPatches(); break;
   case Tab::Resources: Panels::DrawResources(); break;
   case Tab::Trace: Panels::DrawTrace(); break;
#endif
#if DEVELOPMENT || TEST
   case Tab::Info: DrawInfo(Trace::GetStatus()); break;
#else
   case Tab::About:
      if (Game::Registry::GetPanels().info && Widgets::Section("Info", true))
      {
         Game::Registry::GetPanels().info();
         Widgets::EndSection();
      }
      DrawAbout(true);
      break;
#endif
   }
}
} // namespace

std::vector<SettingsSection> CollectSettingsSections(SettingsTab tab)
{
   using Role = SettingsSection::Role;
   std::vector<SettingsSection> sections;
   const auto add = [](std::vector<SettingsSection>& list, std::string_view title, Role role, bool default_open, const Settings::Setting* setting) {
      auto it = std::ranges::find(list, title, &SettingsSection::title);
      if (it == list.end())
         it = list.insert(it, {title, {}, role, default_open});
      it->settings.push_back(setting);
   };
   const auto append = [&](SettingsSection& section) {
      if (!section.settings.empty())
         sections.push_back(std::move(section));
   };

   if (tab == SettingsTab::Overlay)
   {
      SettingsSection overlay = {"Overlay", {}, Role::Settings, true};
      Settings::ForEach([&](const Settings::Setting& setting, const Settings::Value&) {
         if (IsShown(setting) && setting.kind == Settings::Kind::Luma && Settings::GetCategory(setting) == "Overlay")
            overlay.settings.push_back(&setting);
      });
      append(overlay);
      return sections;
   }

   if (tab == SettingsTab::Development)
   {
      std::vector<SettingsSection> dev;
      Settings::ForEach([&](const Settings::Setting& setting, const Settings::Value&) {
         if (!IsShown(setting) || setting.kind != Settings::Kind::Development)
            return;
         const std::string_view category = Settings::GetCategory(setting);
         add(dev, category, Role::Development, category == "Display Composition" || category == "Dev Sliders", &setting);
      });
      for (SettingsSection& section : dev)
         append(section);
      return sections;
   }

   if (tab == SettingsTab::Framework)
   {
      std::vector<SettingsSection> framework;
      SettingsSection defines = {"Shader Defines", {}, Role::Defines, false};
      SettingsSection recipes = {"Recipes", {}, Role::Settings, false};
      Settings::ForEach([&](const Settings::Setting& setting, const Settings::Value&) {
         // Recipes remain toggleable in Framework even when not exposed as user/game settings
         if (setting.kind == Settings::Kind::Recipe)
         {
            recipes.settings.push_back(&setting);
            return;
         }
         if (!IsShown(setting))
            return;
         const std::string_view category = Settings::GetCategory(setting);
         if (setting.kind == Settings::Kind::Define)
            defines.settings.push_back(&setting);
         else if (setting.kind == Settings::Kind::Luma)
         {
            if (setting.key == Settings::Core::AUTO_COMPILE.key)
               defines.settings.insert(defines.settings.begin(), &setting);
            else if (category != "Luma" && category != "Display" && category != "Overlay")
               add(framework, category, Role::Settings, false, &setting);
         }
      });
      if (std::ranges::none_of(defines.settings, [](const Settings::Setting* setting) { return setting->kind == Settings::Kind::Define; }))
         defines.settings.clear();
      for (SettingsSection& section : framework)
         append(section);
      append(defines);
      append(recipes);
      return sections;
   }

   // SettingsTab::Settings: Display + Game categories + user/game-exposed Recipes (expanded).
   // Luma.Enabled and Luma.EnabledKey live in the header bar, so no General category is needed.
   // In non-Development builds (no Framework tab), user/test-visible defines and extra framework settings appear under Advanced Settings.
   SettingsSection display = {"Display", {}, Role::Settings, true};
   std::vector<SettingsSection> game;
   SettingsSection recipes = {"Recipes", {}, Role::Game, true};
#if !DEVELOPMENT
   SettingsSection advanced = {"Advanced Settings", {}, Role::Defines, false};
   std::vector<SettingsSection> extra;
#endif
   Settings::ForEach([&](const Settings::Setting& setting, const Settings::Value&) {
      if (!IsShown(setting))
         return;
      const std::string_view category = Settings::GetCategory(setting);
      if (setting.kind == Settings::Kind::Game)
      {
         add(game, category, Role::Game, true, &setting);
      }
      else if (setting.kind == Settings::Kind::Recipe)
      {
         if (setting.visibility != Settings::Visibility::Development)
            recipes.settings.push_back(&setting);
      }
      else if (setting.kind == Settings::Kind::Luma)
      {
         if (category == "Display")
            display.settings.push_back(&setting);
#if !DEVELOPMENT
         else if (setting.key == Settings::Core::AUTO_COMPILE.key)
            advanced.settings.insert(advanced.settings.begin(), &setting);
         else if (category != "Luma" && category != "Overlay")
            add(extra, category, Role::Settings, false, &setting);
#endif
      }
#if !DEVELOPMENT
      else if (setting.kind == Settings::Kind::Define)
         advanced.settings.push_back(&setting);
#endif
   });
   append(display);
   for (SettingsSection& section : game)
      append(section);
   append(recipes);
#if !DEVELOPMENT
   if (std::ranges::none_of(advanced.settings, [](const Settings::Setting* setting) { return setting->kind == Settings::Kind::Define; }))
      advanced.settings.clear();
   append(advanced);
   for (SettingsSection& section : extra)
      append(section);
#endif
   return sections;
}

void RequestResetWindowLayout()
{
   reset_window_layout.store(true, std::memory_order_relaxed);
}

void DrawWindow()
{
   const Game::Info& info = Game::Registry::GetInfo();
   const std::vector<Game::Link> footer_links = CollectFooterLinks(info);
   const Trace::OverlaySummary summary = Trace::GetOverlaySummary();
   const ImGuiViewport* viewport = ImGui::GetMainViewport();
   const bool reset = reset_window_layout.exchange(false, std::memory_order_relaxed);

   // Passthrough dockspace across the viewport, docked left 1/3 by default (persisted in Luma.toml)
   const ImGuiID root_space_id = ImGui::GetID("LumaViewportDockspace");
   if (reset || !ImGui::DockBuilderGetNode(root_space_id))
   {
      ImGui::DockBuilderRemoveNode(root_space_id);
      ImGui::DockBuilderAddNode(root_space_id, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(root_space_id, viewport->Size);
      ImGuiID main_space_id = 0;
      ImGuiID right_space_id = 0;
      ImGui::DockBuilderSplitNode(root_space_id, ImGuiDir_Left, 1.f / 3.f, &main_space_id, &right_space_id);
      ImGui::DockBuilderDockWindow("###Luma", main_space_id);
      ImGui::DockBuilderFinish(root_space_id);
   }
   ImGui::SetNextWindowPos(viewport->Pos);
   ImGui::SetNextWindowSize(viewport->Size);
   ImGui::SetNextWindowViewport(viewport->ID);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
   ImGui::Begin("LumaViewport", nullptr,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
   ImGui::PopStyleVar(3);
   ImGui::DockSpace(root_space_id, ImVec2(0.f, 0.f), ImGuiDockNodeFlags_PassthruCentralNode);
   ImGui::End();

   ImGui::SetNextWindowSizeConstraints(ImVec2(Style::Scale(420.f), Style::Scale(360.f)), ImVec2(FLT_MAX, FLT_MAX));
   const std::string title = std::format("{} Luma{}###Luma", Icons::SETTINGS, info.name.empty() ? "" : std::format(": {}", info.name));
   // Only the content between the header and the footer scrolls
   if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
   {
      DrawTitleBarDepth();
      DrawHeader(summary);
      DrawTabBar();
      if (ImGui::BeginChild("Content", ImVec2(0.f, -GetFooterHeight(footer_links))))
         DrawContent();
      ImGui::EndChild();
      DrawLinks(footer_links);
      const std::vector<Widgets::StatusItem> items = GetStatusItems(summary);
      Widgets::StatusBar(items);
   }
   ImGui::End();
}
} // namespace UI
} // namespace Luma
