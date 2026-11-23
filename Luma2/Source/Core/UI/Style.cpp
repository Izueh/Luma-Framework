#include "UI/Style.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "UI/Icons.hpp"

namespace Luma {
namespace UI {
namespace Style {
namespace {
float current_scale = 1.f;
std::vector<std::pair<std::string, const char*>> custom_category_icons;

bool EqualsIgnoreCase(std::string_view a, std::string_view b)
{
   return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
}

bool Contains(std::string_view text, std::string_view word)
{
   return std::ranges::search(text, word, [](char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); }).begin() != text.end();
}
} // namespace

void RegisterCategoryIcon(std::string_view category, const char* icon)
{
   if (category.empty() || !icon)
      return;
   for (auto& [name, existing] : custom_category_icons)
   {
      if (EqualsIgnoreCase(name, category))
      {
         existing = icon;
         return;
      }
   }
   custom_category_icons.emplace_back(std::string(category), icon);
}

Category GetCategory(std::string_view name)
{
   for (const auto& [registered, icon] : custom_category_icons)
   {
      if (EqualsIgnoreCase(name, registered))
         return {icon, Colors::TEXT_LABEL};
   }

   struct Entry
   {
      std::string_view word;
      const char* icon;
   };

   const std::array entries = {
      Entry{"overlay", Icons::OVERLAY},
      Entry{"general", Icons::POWER},
      Entry{"luma", Icons::POWER},
      Entry{"swapchain", Icons::DISPLAY},
      Entry{"composition", Icons::LAYERS},
      Entry{"display", Icons::DISPLAY},
      Entry{"hdr", Icons::HDR},
      Entry{"color", Icons::COLOR},
      Entry{"reshade", Icons::LAYERS},
      Entry{"resource", Icons::LAYERS},
      Entry{"recipe", Icons::PATCH},
      Entry{"framework", Icons::TOOLS},
      Entry{"upscal", Icons::UPSCALING},
      Entry{"resolution", Icons::UPSCALING},
      Entry{"post", Icons::POST_PROCESSING},
      Entry{"effect", Icons::POST_PROCESSING},
      Entry{"advanced", Icons::ADVANCED},
      Entry{"develop", Icons::CONSOLE},
      Entry{"validation", Icons::BUG},
      Entry{"debug", Icons::BUG},
      Entry{"credit", Icons::CREDITS},
      Entry{"about", Icons::CREDITS},
      Entry{"define", Icons::CODE},
      Entry{"shader", Icons::CODE},
      Entry{"status", Icons::INFO},
      Entry{"info", Icons::INFO},
   };
   for (const Entry& entry : entries)
   {
      if (Contains(name, entry.word))
         return {entry.icon, Colors::TEXT_LABEL};
   }
   return {Icons::SLIDERS, Colors::TEXT_LABEL};
}

void DrawRaised(ImDrawList* draw_list, ImVec2 min, ImVec2 max, float rounding)
{
   // The rounded top in the top color and the rounded bottom in the bottom one, the gradient between them: the corners
   // match the gradient's ends, with no step where the rounding starts
   const ImU32 top = ImGui::GetColorU32(Colors::RAISED_TOP);
   const ImU32 bottom = ImGui::GetColorU32(Colors::RAISED_BOTTOM);
   const float middle = (min.y + max.y) * 0.5f;
   draw_list->AddRectFilled(min, ImVec2(max.x, middle), top, rounding, ImDrawFlags_RoundCornersTop);
   draw_list->AddRectFilled(ImVec2(min.x, middle), max, bottom, rounding, ImDrawFlags_RoundCornersBottom);
   draw_list->AddRectFilledMultiColor(ImVec2(min.x, min.y + rounding), ImVec2(max.x, max.y - rounding), top, top, bottom, bottom);
   draw_list->AddLine(ImVec2(min.x + rounding, min.y + 0.5f), ImVec2(max.x - rounding, min.y + 0.5f), ImGui::GetColorU32(Colors::HIGHLIGHT));
}

void DrawShadow(ImDrawList* draw_list, ImVec2 min, ImVec2 max, float rounding)
{
   // Rings outside the window, fading out and falling slightly: nothing is drawn over the window itself
   const float size = Scale(Sizes::SHADOW);
   const int rings = std::max(int(size), 1);
   draw_list->PushClipRectFullScreen();
   for (int i = 1; i <= rings; i++)
   {
      const float t = float(i) / float(rings);
      const float fall = float(i) * 0.35f;
      ImVec4 color = Colors::SHADOW;
      color.w *= (1.f - t) * (1.f - t) * 0.6f;
      draw_list->AddRect(ImVec2(min.x - float(i), min.y - float(i) + fall), ImVec2(max.x + float(i), max.y + float(i) + fall), ImGui::GetColorU32(color),
         rounding + float(i), 0, 1.5f);
   }
   draw_list->PopClipRect();
}

float Scale(float pixels)
{
   return pixels * current_scale;
}

ImVec4 WithAlpha(ImVec4 color, float alpha)
{
   color.w = alpha;
   return color;
}

void Apply(float font_size, float dpi_scale, float opacity)
{
   current_scale = font_size / Sizes::DEFAULT_FONT * dpi_scale;
   ImGuiStyle& style = ImGui::GetStyle();
   const float unit = Scale(Sizes::UNIT);

   style.FontSizeBase = font_size;
   style.FontScaleDpi = dpi_scale;
   style.WindowPadding = ImVec2(unit * 2.5f, unit * 2.5f);
   style.FramePadding = ImVec2(unit * 1.75f, unit * 0.75f);
   style.ItemSpacing = ImVec2(unit * 1.75f, unit * 1.25f);
   style.ItemInnerSpacing = ImVec2(unit * 1.25f, unit * 0.75f);
   style.CellPadding = ImVec2(unit * 1.5f, unit * 0.75f);
   style.IndentSpacing = unit * 3.5f;
   style.ScrollbarSize = unit * 3.5f;
   style.GrabMinSize = unit * 2.5f;
   style.WindowRounding = Scale(Sizes::PANEL_ROUNDING);
   style.ChildRounding = Scale(Sizes::PANEL_ROUNDING);
   style.PopupRounding = Scale(Sizes::PANEL_ROUNDING);
   style.FrameRounding = Scale(Sizes::CONTROL_ROUNDING);
   style.GrabRounding = Scale(Sizes::CONTROL_ROUNDING);
   style.TabRounding = Scale(Sizes::CONTROL_ROUNDING);
   style.ScrollbarRounding = Scale(Sizes::CONTROL_ROUNDING);
   style.WindowBorderSize = 1.f;
   style.ChildBorderSize = 1.f;
   style.PopupBorderSize = 1.f;
   style.FrameBorderSize = 0.f;
   style.TabBorderSize = 0.f;
   style.WindowTitleAlign = ImVec2(0.f, 0.5f);
   style.SeparatorTextBorderSize = 1.f;

   ImVec4* colors = style.Colors;
   colors[ImGuiCol_Text] = Colors::TEXT;
   colors[ImGuiCol_TextDisabled] = Colors::TEXT_DIM;
   colors[ImGuiCol_WindowBg] = WithAlpha(Colors::WINDOW, Colors::WINDOW.w * opacity);
   colors[ImGuiCol_ChildBg] = WithAlpha(Colors::PANEL, 0.f);
   colors[ImGuiCol_PopupBg] = Colors::PANEL;
   colors[ImGuiCol_Border] = Colors::BORDER;
   colors[ImGuiCol_BorderShadow] = Colors::SHADOW;
   colors[ImGuiCol_FrameBg] = Colors::CONTROL;
   colors[ImGuiCol_FrameBgHovered] = Colors::CONTROL_HOVERED;
   colors[ImGuiCol_FrameBgActive] = Colors::CONTROL_ACTIVE;
   colors[ImGuiCol_TitleBg] = Colors::PANEL;
   colors[ImGuiCol_TitleBgActive] = Colors::PANEL;
   colors[ImGuiCol_TitleBgCollapsed] = Colors::PANEL;
   colors[ImGuiCol_MenuBarBg] = Colors::PANEL;
   colors[ImGuiCol_ScrollbarBg] = WithAlpha(Colors::WINDOW, 0.f);
   colors[ImGuiCol_ScrollbarGrab] = Colors::CONTROL;
   colors[ImGuiCol_ScrollbarGrabHovered] = Colors::CONTROL_HOVERED;
   colors[ImGuiCol_ScrollbarGrabActive] = Colors::CONTROL_ACTIVE;
   colors[ImGuiCol_CheckMark] = Colors::ACCENT;
   colors[ImGuiCol_SliderGrab] = Colors::ACCENT;
   colors[ImGuiCol_SliderGrabActive] = Colors::ACCENT_ACTIVE;
   colors[ImGuiCol_Button] = Colors::CONTROL;
   colors[ImGuiCol_ButtonHovered] = Colors::CONTROL_HOVERED;
   colors[ImGuiCol_ButtonActive] = Colors::CONTROL_ACTIVE;
   colors[ImGuiCol_Header] = Colors::CONTROL;
   colors[ImGuiCol_HeaderHovered] = Colors::CONTROL_HOVERED;
   colors[ImGuiCol_HeaderActive] = Colors::CONTROL_ACTIVE;
   colors[ImGuiCol_Separator] = Colors::BORDER;
   colors[ImGuiCol_SeparatorHovered] = Colors::ACCENT_HOVERED;
   colors[ImGuiCol_SeparatorActive] = Colors::ACCENT;
   colors[ImGuiCol_ResizeGrip] = WithAlpha(Colors::ACCENT, 0.2f);
   colors[ImGuiCol_ResizeGripHovered] = WithAlpha(Colors::ACCENT, 0.6f);
   colors[ImGuiCol_ResizeGripActive] = Colors::ACCENT;
   colors[ImGuiCol_Tab] = Colors::PANEL;
   colors[ImGuiCol_TabHovered] = Colors::CONTROL_HOVERED;
   colors[ImGuiCol_TabSelected] = Colors::CONTROL;
   // The active tab's accent is an underline, drawn by Widgets::Tab
   colors[ImGuiCol_TabSelectedOverline] = WithAlpha(Colors::ACCENT, 0.f);
   colors[ImGuiCol_TabDimmed] = Colors::PANEL;
   colors[ImGuiCol_TabDimmedSelected] = Colors::CONTROL;
   colors[ImGuiCol_TabDimmedSelectedOverline] = WithAlpha(Colors::ACCENT, 0.f);
   colors[ImGuiCol_DockingPreview] = WithAlpha(Colors::ACCENT, 0.7f);
   colors[ImGuiCol_DockingEmptyBg] = Colors::WINDOW;
   colors[ImGuiCol_PlotHistogram] = Colors::ACCENT;
   colors[ImGuiCol_PlotHistogramHovered] = Colors::ACCENT_HOVERED;
   colors[ImGuiCol_TableHeaderBg] = Colors::PANEL;
   colors[ImGuiCol_TableBorderStrong] = Colors::BORDER;
   colors[ImGuiCol_TableBorderLight] = WithAlpha(Colors::BORDER, 0.5f);
   colors[ImGuiCol_TableRowBg] = WithAlpha(Colors::PANEL, 0.f);
   colors[ImGuiCol_TableRowBgAlt] = WithAlpha(Colors::PANEL, 0.5f);
   colors[ImGuiCol_TextLink] = Colors::ACCENT;
   colors[ImGuiCol_TextSelectedBg] = WithAlpha(Colors::ACCENT, 0.35f);
   colors[ImGuiCol_DragDropTarget] = Colors::ACCENT;
   colors[ImGuiCol_NavCursor] = Colors::ACCENT;
   colors[ImGuiCol_NavWindowingHighlight] = WithAlpha(Colors::ACCENT, 0.7f);
   colors[ImGuiCol_NavWindowingDimBg] = WithAlpha(Colors::SHADOW, 0.2f);
   colors[ImGuiCol_ModalWindowDimBg] = Colors::SHADOW;
}
} // namespace Style
} // namespace UI
} // namespace Luma
