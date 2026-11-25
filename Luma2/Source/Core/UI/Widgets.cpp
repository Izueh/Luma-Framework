#include "UI/Widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "Shaders/ShaderDefines.hpp"
#include "UI/Icons.hpp"
#include "UI/OverlayInput.hpp"
#include "UI/Style.hpp"

namespace Luma {
namespace UI {
namespace Widgets {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::chrono::milliseconds TOAST_DURATION{4000};
constexpr size_t MAX_TOASTS = 5;

struct ToastEntry
{
   Tone tone;
   std::string message;
   Clock::time_point shown;
};

// Never held with another lock
std::mutex toasts_mutex;
std::deque<ToastEntry> toasts;

ImGuiID capturing_key_id = 0;
// The row being dragged or typed in, and its value meanwhile
ImGuiID editing_id = 0;
Settings::Value editing_value;
// ListDetail's filters, by list
std::unordered_map<ImGuiID, std::string> list_filters;

bool ContainsIgnoreCase(std::string_view text, std::string_view part)
{
   return !std::ranges::search(text, part, [](char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); }).empty();
}

ImVec4 GetStageTint(ShaderStage stage)
{
   switch (stage)
   {
   case ShaderStage::Vertex: return Style::Colors::Stages::VERTEX;
   case ShaderStage::Pixel: return Style::Colors::Stages::PIXEL;
   case ShaderStage::Compute: return Style::Colors::Stages::COMPUTE;
   default: return Style::Colors::Stages::OTHER;
   }
}

std::string_view GetStageName(ShaderStage stage)
{
   constexpr std::string_view NAMES[] = {"vertex", "hull", "domain", "geometry", "pixel", "compute"};
   return NAMES[size_t(stage)];
}

// Text inputs and define/recipe sliders apply when the edit ends (avoids recompiling or re-patching per drag step);
// numeric sliders apply live while dragging, and Overlay.cpp writes Luma.toml only once editing finishes (!IsAnyItemActive).
bool IsAppliedOnRelease(const Settings::Setting& setting)
{
   if (std::holds_alternative<std::string>(setting.default_value))
      return true;
   if (std::holds_alternative<Settings::Sources::Define>(setting.source) || std::holds_alternative<Settings::Sources::Recipe>(setting.source))
      return std::holds_alternative<float>(setting.default_value) || (std::holds_alternative<int32_t>(setting.default_value) && setting.range);
   return false;
}

// ImGui wants null terminated strings
std::string Terminated(std::string_view text)
{
   return std::string(text);
}

std::string HiddenLabel(std::string_view id)
{
   return std::format("##{}", id);
}

ImVec4 GetColor(Tone tone)
{
   switch (tone)
   {
   case Tone::Success: return Style::Colors::SUCCESS;
   case Tone::Warning: return Style::Colors::WARNING;
   case Tone::Error: return Style::Colors::DANGER;
   case Tone::Injected: return Style::Colors::INJECTED;
   default: return Style::Colors::TEXT_LABEL;
   }
}

const char* GetIcon(Tone tone)
{
   switch (tone)
   {
   case Tone::Success: return Icons::SUCCESS;
   case Tone::Warning: return Icons::WARNING;
   case Tone::Error: return Icons::ERROR_;
   default: return Icons::INFO;
   }
}

// The shadow of the current popup, tooltip or toast window
void WindowShadow(float rounding)
{
   const ImVec2 position = ImGui::GetWindowPos();
   const ImVec2 size = ImGui::GetWindowSize();
   Style::DrawShadow(ImGui::GetWindowDrawList(), position, ImVec2(position.x + size.x, position.y + size.y), rounding);
}

void Tooltip(std::string_view text)
{
   if (text.empty() || !ImGui::BeginItemTooltip())
      return;
   ImGui::PushTextWrapPos(Style::Scale(Style::Sizes::UNIT * 90.f));
   ImGui::TextUnformatted(text.data(), text.data() + text.size());
   ImGui::PopTextWrapPos();
   WindowShadow(ImGui::GetStyle().PopupRounding);
   ImGui::EndTooltip();
}

// Truncates `text` with "..." so it fits within `max_width` pixels without splitting UTF-8 codepoints
std::string FitWithEllipsis(std::string_view text, float max_width, bool& truncated)
{
   truncated = false;
   if (max_width <= 0.f || ImGui::CalcTextSize(text.data(), text.data() + text.size()).x <= max_width)
      return std::string(text);
   truncated = true;
   const float ellipsis_w = ImGui::CalcTextSize("...").x;
   if (max_width <= ellipsis_w)
      return "...";
   size_t lo = 0;
   size_t hi = text.size();
   while (lo < hi)
   {
      const size_t mid = (lo + hi + 1) / 2;
      if (ImGui::CalcTextSize(text.data(), text.data() + mid).x + ellipsis_w <= max_width)
         lo = mid;
      else
         hi = mid - 1;
   }
   while (lo > 0 && lo < text.size() && (uint8_t(text[lo]) & 0xC0) == 0x80)
      lo--;
   return std::format("{}...", text.substr(0, lo));
}

void CombinedTooltip(std::string_view full_text, bool truncated, std::string_view extra_tooltip)
{
   if (truncated && !extra_tooltip.empty())
      Tooltip(std::format("{}\n\n{}", full_text, extra_tooltip));
   else if (truncated)
      Tooltip(full_text);
   else
      Tooltip(extra_tooltip);
}

void DrawTextEllipsis(std::string_view text, float max_width, std::optional<ImVec4> color, std::string_view extra_tooltip = {})
{
   const float limit = max_width > 0.f ? max_width : ImGui::GetContentRegionAvail().x;
   bool truncated = false;
   const std::string shown = FitWithEllipsis(text, limit, truncated);
   if (color)
      ImGui::PushStyleColor(ImGuiCol_Text, *color);
   ImGui::TextUnformatted(shown.data(), shown.data() + shown.size());
   if (color)
      ImGui::PopStyleColor();
   CombinedTooltip(text, truncated, extra_tooltip);
}

// Spaces as wide as an icon and the gap after it: the icon is drawn over them in its own color (a glyph has one color)
std::string IconRoom(const char* icon)
{
   const float space = ImGui::CalcTextSize(" ").x;
   const float width = ImGui::CalcTextSize(icon).x + ImGui::GetStyle().ItemInnerSpacing.x;
   return std::string(size_t(std::ceil(width / std::max(space, 1.f))), ' ');
}

void DrawIcon(ImVec2 position, const char* icon, ImVec4 tint)
{
   ImGui::GetWindowDrawList()->AddText(position, ImGui::GetColorU32(ImGui::GetStyle().Alpha < 1.f ? Style::Colors::ICON_DISABLED : tint), icon);
}

// A button whose label starts with a tinted icon
bool IconTextButton(const char* icon, ImVec4 tint, std::string_view label)
{
   const std::string text = IconRoom(icon) + std::string(label);
   const bool clicked = ImGui::Button(text.c_str());
   const ImVec2 min = ImGui::GetItemRectMin();
   DrawIcon(ImVec2(min.x + ImGui::GetStyle().FramePadding.x, min.y + ImGui::GetStyle().FramePadding.y), icon, tint);
   return clicked;
}

struct Mark
{
   const char* icon;
   ImVec4 tint;
};

Mark GetMark(std::string_view url)
{
   if (url.find("ko-fi.com") != std::string_view::npos)
      return {Icons::KOFI, Style::Colors::Brands::KOFI};
   if (url.find("buymeacoffee.com") != std::string_view::npos)
      return {Icons::BUY_ME_A_COFFEE, Style::Colors::Brands::BUY_ME_A_COFFEE};
   if (url.find("discord.") != std::string_view::npos)
      return {Icons::DISCORD, Style::Colors::Brands::DISCORD};
   if (url.find("github.com") != std::string_view::npos)
      return {Icons::GITHUB, Style::Colors::Brands::GITHUB};
   return {Icons::LINK, Style::Colors::ACCENT};
}

float CalcTagWidth(std::string_view text)
{
   const ImVec2 size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
   const float padding_x = Style::Scale(Style::Sizes::UNIT * 1.5f);
   return size.x + padding_x * 2.f;
}

void DrawTag(std::string_view text, ImVec4 color)
{
   const ImVec2 size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
   const ImVec2 padding(Style::Scale(Style::Sizes::UNIT * 1.5f), Style::Scale(1.f));
   const ImVec2 position = ImGui::GetCursorScreenPos();
   const float offset = ImGui::GetStyle().FramePadding.y - padding.y;
   ImDrawList* draw_list = ImGui::GetWindowDrawList();
   const ImVec2 min(position.x, position.y + offset);
   const ImVec2 max(min.x + size.x + padding.x * 2.f, min.y + size.y + padding.y * 2.f);
   draw_list->AddRectFilled(min, max, ImGui::GetColorU32(Style::WithAlpha(color, 0.2f)), Style::Scale(Style::Sizes::CONTROL_ROUNDING));
   draw_list->AddText(ImVec2(min.x + padding.x, min.y + padding.y), ImGui::GetColorU32(color), text.data(), text.data() + text.size());
   ImGui::Dummy(ImVec2(max.x - min.x, ImGui::GetFrameHeight()));
}

float GetControlWidth(float right_gutter)
{
   return std::max(ImGui::GetContentRegionAvail().x - right_gutter, Style::Scale(Style::Sizes::UNIT * 24.f));
}

bool DrawControl(const Settings::Setting& setting, Settings::Value& value, float control_width)
{
   const std::string id(setting.key);
   ImGui::SetNextItemWidth(control_width);
   return std::visit([&]<typename T>(T& typed) {
      if constexpr (std::is_same_v<T, bool>)
         return Toggle(id, typed);
      else if constexpr (std::is_same_v<T, int32_t>)
      {
         if (setting.range)
            return Slider(id, typed, std::get<int32_t>(setting.range->min), std::get<int32_t>(setting.range->max));
         return ImGui::InputInt(HiddenLabel(id).c_str(), &typed);
      }
      else if constexpr (std::is_same_v<T, float>)
      {
         if (setting.range)
            return Slider(id, typed, std::get<float>(setting.range->min), std::get<float>(setting.range->max), setting.format);
         return ImGui::DragFloat(HiddenLabel(id).c_str(), &typed, 0.01f, 0.f, 0.f, Terminated(setting.format).c_str());
      }
      else if constexpr (std::is_same_v<T, Settings::EnumIndex>)
      {
         if (setting.enum_labels.size() <= MAX_SEGMENTS)
            return Segmented(id, typed.value, setting.enum_labels, control_width, setting.is_option_enabled);
         return Combo(id, typed.value, setting.enum_labels, setting.is_option_enabled);
      }
      else if constexpr (std::is_same_v<T, std::string>)
         return TextInput(id, typed);
      else
         return KeyBind(id, typed.code);
   },
      value);
}
} // namespace

bool Section(std::string_view label, bool default_open, const char* icon)
{
   const Style::Category category = Style::GetCategory(label);
   if (!icon)
      icon = category.icon;
   const float unit = Style::Scale(Style::Sizes::UNIT);
   const ImVec2 padding(unit * 1.75f, unit);
   const float rounding = Style::Scale(Style::Sizes::CONTROL_ROUNDING);
   ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
   ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
   ImGui::PushStyleColor(ImGuiCol_Header, Style::WithAlpha(Style::Colors::PANEL, 0.f));
   ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Style::Colors::HOVER_OVERLAY);
   ImGui::PushStyleColor(ImGuiCol_HeaderActive, Style::Colors::HIGHLIGHT);

   // The header on top, its raised background and subtle accent bar under it once its rectangle is known
   ImDrawList* draw_list = ImGui::GetWindowDrawList();
   draw_list->ChannelsSplit(2);
   draw_list->ChannelsSetCurrent(1);
   const float text_x = ImGui::GetCursorScreenPos().x + ImGui::GetFontSize() + padding.x * 2.5f;
   const std::string text = std::format("{}{}###{}", IconRoom(icon), label, label);
   const bool open = ImGui::CollapsingHeader(text.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | (default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0));
   const ImVec2 min = ImGui::GetItemRectMin();
   const ImVec2 max = ImGui::GetItemRectMax();
   DrawIcon(ImVec2(text_x, min.y + padding.y), icon, open ? Style::Colors::TEXT : category.tint);
   draw_list->ChannelsSetCurrent(0);
   Style::DrawRaised(draw_list, min, max, rounding);
   const ImVec4 bar_color = open ? Style::WithAlpha(Style::Colors::ACCENT, 0.55f) : Style::Colors::BORDER;
   draw_list->AddRectFilled(min, ImVec2(min.x + Style::Scale(Style::Sizes::ACCENT_BAR), max.y), ImGui::GetColorU32(bar_color), rounding,
      ImDrawFlags_RoundCornersLeft);
   draw_list->ChannelsMerge();
   ImGui::PopStyleColor(3);
   ImGui::PopStyleVar(2);
   if (open)
   {
      ImGui::Indent(unit * 1.5f);
      ImGui::Dummy(ImVec2(0.f, unit * 0.25f));
   }
   return open;
}

void EndSection()
{
   ImGui::Unindent(Style::Scale(Style::Sizes::UNIT) * 1.5f);
   ImGui::Dummy(ImVec2(0.f, Style::Scale(Style::Sizes::UNIT * 0.5f)));
}

void SetNextSectionOpen(bool open)
{
   ImGui::SetNextItemOpen(open, ImGuiCond_Always);
}

void SubHeader(std::string_view label)
{
   ImGui::PushStyleColor(ImGuiCol_Text, Style::Colors::TEXT_LABEL);
   ImGui::SeparatorText(Terminated(label).c_str());
   ImGui::PopStyleColor();
}

void RegisterCategoryIcon(std::string_view category, const char* icon)
{
   Style::RegisterCategoryIcon(category, icon);
}

bool Tab(std::string_view label, const char* icon)
{
   const Style::Category category = Style::GetCategory(label);
   if (!icon)
      icon = category.icon;
   const std::string text = std::format("{}{}###{}", IconRoom(icon), label, label);
   const bool selected = ImGui::BeginTabItem(text.c_str());
   const ImVec2 min = ImGui::GetItemRectMin();
   const ImVec2 max = ImGui::GetItemRectMax();
   const ImVec2 padding = ImGui::GetStyle().FramePadding;
   ImDrawList* draw_list = ImGui::GetWindowDrawList();
   draw_list->PushClipRectFullScreen();
   draw_list->AddText(ImVec2(min.x + padding.x, min.y + padding.y), ImGui::GetColorU32(selected ? Style::Colors::ACCENT : Style::Colors::ICON_DISABLED), icon);
   if (selected)
   {
      const float thickness = Style::Scale(2.f);
      draw_list->AddRectFilled(ImVec2(min.x, max.y - thickness), max, ImGui::GetColorU32(Style::Colors::ACCENT));
   }
   draw_list->PopClipRect();
   return selected;
}

void EndTab()
{
   ImGui::EndTabItem();
}

void Property(std::string_view name, std::string_view value)
{
   RowLabel(name);
   DrawTextEllipsis(value, 0.f, std::nullopt);
}

void Caption(std::string_view text)
{
   ImGui::AlignTextToFramePadding();
   TextDim(text);
}

void Paragraph(std::string_view text)
{
   ImGui::PushTextWrapPos(0.f);
   ImGui::TextUnformatted(text.data(), text.data() + text.size());
   ImGui::PopTextWrapPos();
}

void ListItem(std::string_view text)
{
   ImGui::Bullet();
   DrawTextEllipsis(text, 0.f, std::nullopt);
}

float GetLinkWidth(std::string_view label, std::string_view url)
{
   const std::string text = IconRoom(GetMark(url).icon) + std::string(label);
   return ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.f;
}

bool Link(std::string_view label, std::string_view url)
{
   const Mark mark = GetMark(url);
   ImGui::PushID(url.data(), url.data() + url.size());
   const bool clicked = IconTextButton(mark.icon, mark.tint, label);
   ImGui::PopID();
   const std::string address(url);
   Tooltip(address);
   if (clicked)
   {
      ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
      if (platform.Platform_OpenInShellFn)
         platform.Platform_OpenInShellFn(ImGui::GetCurrentContext(), address.c_str());
   }
   return clicked;
}

float WrapSameLine(float line_used, float width)
{
   const float spacing = ImGui::GetStyle().ItemSpacing.x;
   if (line_used > 0.f && line_used + spacing + width <= ImGui::GetContentRegionAvail().x)
   {
      ImGui::SameLine();
      return line_used + spacing + width;
   }
   return width;
}

void RowLabel(std::string_view label, std::string_view tooltip, bool emphasized)
{
   ImGui::AlignTextToFramePadding();
   const float start = ImGui::GetCursorPosX();
   const float label_width = Style::Scale(Style::Sizes::LABEL_WIDTH);
   const float max_width = std::max(label_width - ImGui::GetStyle().ItemSpacing.x, Style::Scale(Style::Sizes::UNIT * 4.f));
   DrawTextEllipsis(label, max_width, emphasized ? Style::Colors::TEXT_BRIGHT : Style::Colors::TEXT_LABEL, tooltip);
   ImGui::SameLine(start + label_width);
}

bool SettingRow(const Settings::Setting& setting)
{
   const std::optional<Settings::Value> current = Settings::GetValue(setting.key);
   if (!current)
      return false;
   ImGui::PushID(Terminated(setting.key).c_str());
   const ImGuiID row_id = ImGui::GetID("Row");
   const bool is_default = *current == Settings::Clamp(setting, setting.default_value);
   const bool is_pending = Shaders::Defines::IsPending(setting);
   const std::string restart_label = setting.requires_restart ? std::format("{} Restart", Icons::RESTART) : std::string();
   RowLabel(setting.label.empty() ? setting.key : setting.label, setting.tooltip, !is_default);
   const ImGuiStyle& style = ImGui::GetStyle();
   const float reset_button_width = ImGui::CalcTextSize(Icons::RESET).x + style.FramePadding.x * 2.f;
   // Always reserve space for the reset icon so controls stay aligned across rows
   float right_gutter = reset_button_width + style.ItemSpacing.x;
   if (setting.requires_restart)
      right_gutter += CalcTagWidth(restart_label) + style.ItemSpacing.x;
   if (is_pending)
      right_gutter += CalcTagWidth("pending") + style.ItemSpacing.x;
   const bool enabled = !setting.is_enabled || setting.is_enabled();
   ImGui::BeginDisabled(!enabled);
   // A drag or a typed edit shows its own value until it ends, and only then reaches the registry
   Settings::Value edited = editing_id == row_id ? editing_value : *current;
   const bool control_changed = DrawControl(setting, edited, GetControlWidth(right_gutter));
   const bool deferred = IsAppliedOnRelease(setting);
   bool changed = false;
   if (deferred && ImGui::IsItemActive())
   {
      editing_id = row_id;
      editing_value = edited;
   }
   else if (editing_id == row_id)
   {
      editing_id = 0;
   }
   if (deferred ? ImGui::IsItemDeactivatedAfterEdit() || (setting.live && control_changed) : control_changed)
      changed = Settings::Set(setting.key, edited);
   Tooltip(setting.tooltip);
   ImGui::SameLine();
   if (*Settings::GetValue(setting.key) != Settings::Clamp(setting, setting.default_value))
   {
      ImGui::PushStyleColor(ImGuiCol_Text, Style::Colors::ACCENT);
      if (IconButton(Icons::RESET, "Reset to default"))
         changed |= Settings::Reset(setting.key);
      ImGui::PopStyleColor();
   }
   else
   {
      ImGui::Dummy(ImVec2(reset_button_width, ImGui::GetFrameHeight()));
   }
   ImGui::EndDisabled();
   if (setting.requires_restart)
   {
      ImGui::SameLine();
      Tag(restart_label, Tone::Error, "Takes effect on next launch");
   }
   // A define the shaders weren't built with yet
   if (is_pending)
   {
      ImGui::SameLine();
      const std::optional<std::string> compiled = Shaders::Defines::GetCompiledText(std::get<Settings::Sources::Define>(setting.source).name);
      Tag("pending", Tone::Warning,
         std::format("The shaders were built with {}: Compile Shaders (or Auto Compile) applies this value", compiled ? *compiled : std::string("no value")));
   }
   ImGui::PopID();
   return changed;
}

bool Toggle(std::string_view id, bool& value)
{
   // A switch: a pill with a knob, the accent color when on
   const std::string label = HiddenLabel(id);
   const float height = ImGui::GetFrameHeight();
   const float width = height * 1.8f;
   const ImVec2 position = ImGui::GetCursorScreenPos();
   const bool clicked = ImGui::InvisibleButton(label.c_str(), ImVec2(width, height));
   if (clicked)
      value = !value;
   const bool hovered = ImGui::IsItemHovered();
   ImDrawList* draw_list = ImGui::GetWindowDrawList();
   const ImVec4 track = value ? (hovered ? Style::Colors::ACCENT_HOVERED : Style::Colors::ACCENT) : (hovered ? Style::Colors::CONTROL_HOVERED : Style::Colors::CONTROL);
   draw_list->AddRectFilled(position, ImVec2(position.x + width, position.y + height), ImGui::GetColorU32(track), height * 0.5f);
   const float radius = height * 0.5f - Style::Scale(3.f);
   const float knob_x = value ? position.x + width - height * 0.5f : position.x + height * 0.5f;
   draw_list->AddCircleFilled(ImVec2(knob_x, position.y + height * 0.5f), radius, ImGui::GetColorU32(Style::Colors::TEXT));
   return clicked;
}

bool Slider(std::string_view id, float& value, float min, float max, std::string_view format)
{
   return ImGui::SliderFloat(HiddenLabel(id).c_str(), &value, min, max, Terminated(format).c_str(), ImGuiSliderFlags_AlwaysClamp);
}

bool Slider(std::string_view id, int32_t& value, int32_t min, int32_t max)
{
   return ImGui::SliderInt(HiddenLabel(id).c_str(), &value, min, max, "%d", ImGuiSliderFlags_AlwaysClamp);
}

bool Combo(std::string_view id, int32_t& index, std::span<const std::string_view> labels, bool (*is_option_enabled)(int32_t))
{
   const std::string preview = index >= 0 && size_t(index) < labels.size() ? Terminated(labels[index]) : std::to_string(index);
   bool changed = false;
   if (ImGui::BeginCombo(HiddenLabel(id).c_str(), preview.c_str()))
   {
      for (int32_t i = 0; i < int32_t(labels.size()); i++)
      {
         const bool opt_enabled = !is_option_enabled || is_option_enabled(i);
         if (!opt_enabled)
            ImGui::PushStyleColor(ImGuiCol_Text, Style::Colors::ICON_DISABLED);
         const ImGuiSelectableFlags flags = opt_enabled ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled;
         if (ImGui::Selectable(Terminated(labels[i]).c_str(), i == index, flags) && opt_enabled)
         {
            changed = i != index;
            index = i;
         }
         if (!opt_enabled)
            ImGui::PopStyleColor();
      }
      ImGui::EndCombo();
   }
   return changed;
}

bool Segmented(std::string_view id, int32_t& index, std::span<const std::string_view> labels, float width, bool (*is_option_enabled)(int32_t))
{
   if (labels.empty())
      return false;
   const ImGuiStyle& style = ImGui::GetStyle();
   const float height = ImGui::GetFrameHeight();
   std::vector<float> widths;
   float natural = 0.f;
   for (const std::string_view label : labels)
      natural += widths.emplace_back(ImGui::CalcTextSize(label.data(), label.data() + label.size()).x + style.FramePadding.x * 2.f);
   // A given width is shared equally, so segments line up between rows
   if (width > 0.f)
      std::ranges::fill(widths, width / float(labels.size()));
   ImGui::PushID(id.data(), id.data() + id.size());
   const ImVec2 start = ImGui::GetCursorScreenPos();
   const float total = width > 0.f ? width : natural;
   const float rounding = Style::Scale(Style::Sizes::CONTROL_ROUNDING);
   const bool disabled = style.Alpha < 1.f;
   ImDrawList* draw_list = ImGui::GetWindowDrawList();
   draw_list->AddRectFilled(start, ImVec2(start.x + total, start.y + height), ImGui::GetColorU32(Style::Colors::CONTROL), rounding);
   bool changed = false;
   float x = start.x;
   for (int32_t i = 0; i < int32_t(labels.size()); i++)
   {
      const bool opt_enabled = !disabled && (!is_option_enabled || is_option_enabled(i));
      const float segment = widths[size_t(i)];
      ImGui::SetCursorScreenPos(ImVec2(x, start.y));
      ImGui::PushID(i);
      if (ImGui::InvisibleButton("##Segment", ImVec2(segment, height)) && opt_enabled && i != index)
      {
         index = i;
         changed = true;
      }
      const bool hovered = opt_enabled && ImGui::IsItemHovered();
      ImGui::PopID();
      const ImVec2 min(x, start.y);
      const ImVec2 max(x + segment, start.y + height);
      const bool selected = i == index;
      // Inset, so neighbouring fills never touch
      const float inset = Style::Scale(2.f);
      if (selected || hovered)
      {
         const ImVec4 fill = selected ? (hovered ? Style::Colors::ACCENT_HOVERED : Style::Colors::ACCENT) : Style::Colors::CONTROL_HOVERED;
         draw_list->AddRectFilled(ImVec2(min.x + inset, min.y + inset), ImVec2(max.x - inset, max.y - inset), ImGui::GetColorU32(fill), rounding);
      }
      else if (i > 0 && i - 1 != index)
      {
         draw_list->AddLine(ImVec2(min.x, min.y + height * 0.25f), ImVec2(min.x, max.y - height * 0.25f), ImGui::GetColorU32(Style::Colors::BORDER));
      }
      const std::string_view label = labels[size_t(i)];
      bool truncated = false;
      const float inner_pad = Style::Scale(Style::Sizes::UNIT);
      const std::string shown = FitWithEllipsis(label, segment - inner_pad * 2.f, truncated);
      const ImVec2 text_size = ImGui::CalcTextSize(shown.c_str());
      const ImVec4 text_color = !opt_enabled ? Style::Colors::ICON_DISABLED : (selected ? Style::Colors::TEXT_BRIGHT : Style::Colors::TEXT_LABEL);
      draw_list->AddText(ImVec2(min.x + (segment - text_size.x) * 0.5f, min.y + (height - text_size.y) * 0.5f), ImGui::GetColorU32(text_color), shown.c_str());
      if (truncated && hovered)
         Tooltip(label);
      x += segment;
   }
   // One item for the layout and for the caller's tooltip
   ImGui::SetCursorScreenPos(start);
   ImGui::Dummy(ImVec2(total, height));
   ImGui::PopID();
   return changed;
}

bool FilterChip(std::string_view label, bool& value, std::string_view tooltip)
{
   int colors = 1;
   if (value)
   {
      ImGui::PushStyleColor(ImGuiCol_Button, Style::WithAlpha(Style::Colors::ACCENT, 0.35f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Style::WithAlpha(Style::Colors::ACCENT, 0.5f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, Style::WithAlpha(Style::Colors::ACCENT, 0.65f));
      colors = 4;
   }
   ImGui::PushStyleColor(ImGuiCol_Text, value ? Style::Colors::TEXT_BRIGHT : Style::Colors::TEXT_DIM);
   ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
   const bool clicked = ImGui::Button(Terminated(label).c_str());
   ImGui::PopStyleVar();
   ImGui::PopStyleColor(colors);
   Tooltip(tooltip);
   if (clicked)
      value = !value;
   return clicked;
}

void Tag(std::string_view text, Tone tone, std::string_view tooltip)
{
   DrawTag(text, GetColor(tone));
   Tooltip(tooltip);
}

bool Banner(Tone tone, std::string_view text, std::string_view action, std::string_view action_tooltip)
{
   const ImVec4 color = GetColor(tone);
   const float unit = Style::Scale(Style::Sizes::UNIT);
   ImDrawList* draw_list = ImGui::GetWindowDrawList();
   // The tinted background goes under the contents once their height is known
   draw_list->ChannelsSplit(2);
   draw_list->ChannelsSetCurrent(1);
   const ImVec2 start = ImGui::GetCursorScreenPos();
   const float width = ImGui::GetContentRegionAvail().x;
   const float action_width = action.empty() ? 0.f : ImGui::CalcTextSize(action.data(), action.data() + action.size()).x + ImGui::GetStyle().FramePadding.x * 2.f;
   // Window coordinates, as SameLine and the wrap position take them
   const float window_x = ImGui::GetWindowPos().x - ImGui::GetScrollX();
   const float action_x = start.x + width - action_width - unit * 2.f;
   ImGui::SetCursorScreenPos(ImVec2(start.x + unit * 3.f, start.y + unit * 1.5f));
   ImGui::BeginGroup();
   ImGui::AlignTextToFramePadding();
   ImGui::PushStyleColor(ImGuiCol_Text, color);
   ImGui::TextUnformatted(GetIcon(tone));
   ImGui::PopStyleColor();
   ImGui::SameLine();
   ImGui::PushTextWrapPos(action_x - unit * 2.f - window_x);
   ImGui::TextUnformatted(text.data(), text.data() + text.size());
   ImGui::PopTextWrapPos();
   bool clicked = false;
   if (!action.empty())
   {
      ImGui::SameLine();
      ImGui::SetCursorScreenPos(ImVec2(action_x, ImGui::GetCursorScreenPos().y));
      clicked = Button(action, ButtonKind::Primary, action_tooltip);
   }
   ImGui::EndGroup();
   const float bottom = ImGui::GetItemRectMax().y + unit * 1.5f;
   draw_list->ChannelsSetCurrent(0);
   const float rounding = Style::Scale(Style::Sizes::CONTROL_ROUNDING);
   draw_list->AddRectFilled(start, ImVec2(start.x + width, bottom), ImGui::GetColorU32(Style::WithAlpha(color, 0.12f)), rounding);
   draw_list->AddRectFilled(start, ImVec2(start.x + Style::Scale(Style::Sizes::ACCENT_BAR), bottom), ImGui::GetColorU32(color), rounding,
      ImDrawFlags_RoundCornersLeft);
   draw_list->ChannelsMerge();
   ImGui::SetCursorScreenPos(start);
   ImGui::Dummy(ImVec2(width, bottom - start.y + unit));
   return clicked;
}

bool KeyBind(std::string_view id, uint32_t& key)
{
   const ImGuiID item_id = ImGui::GetID(Terminated(id).c_str());
   const bool capturing = capturing_key_id == item_id;
   bool changed = false;
   if (capturing && !Input::IsCapturingKey())
   {
      capturing_key_id = 0;
      if (const uint32_t captured = Input::TakeCapturedKey(); captured && captured != key)
      {
         key = captured;
         changed = true;
      }
   }
   const std::string text = capturing_key_id == item_id ? "Press a key (Escape cancels)" : Input::GetKeyName(key);
   const std::string label = std::format("{}{}###{}", IconRoom(Icons::KEYBOARD), text, id);
   const bool clicked = ImGui::Button(label.c_str(), ImVec2(ImGui::CalcItemWidth(), 0.f));
   const ImVec2 min = ImGui::GetItemRectMin();
   // Centered, like the button's text
   const float text_width = ImGui::CalcTextSize(label.c_str(), nullptr, true).x;
   const float left = min.x + std::max((ImGui::GetItemRectSize().x - text_width) * 0.5f, ImGui::GetStyle().FramePadding.x);
   DrawIcon(ImVec2(left, min.y + ImGui::GetStyle().FramePadding.y), Icons::KEYBOARD, capturing_key_id == item_id ? Style::Colors::ACCENT : Style::Colors::TEXT_DIM);
   if (clicked)
   {
      capturing_key_id = item_id;
      Input::CaptureKey();
   }
   return changed;
}

bool TextInput(std::string_view id, std::string& text)
{
   return ImGui::InputText(HiddenLabel(id).c_str(), &text);
}

bool Button(std::string_view label, ButtonKind kind, std::string_view tooltip)
{
   int colors = 0;
   if (kind == ButtonKind::Primary)
   {
      ImGui::PushStyleColor(ImGuiCol_Button, Style::Colors::ACCENT);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Style::Colors::ACCENT_HOVERED);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, Style::Colors::ACCENT_ACTIVE);
      colors = 3;
   }
   else if (kind == ButtonKind::Danger)
   {
      ImGui::PushStyleColor(ImGuiCol_Button, Style::WithAlpha(Style::Colors::DANGER, 0.25f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Style::Colors::DANGER);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, Style::Colors::DANGER_HOVERED);
      colors = 3;
   }
   const std::string text = Terminated(label);
   bool clicked = ImGui::Button(text.c_str());
   ImGui::PopStyleColor(colors);
   Tooltip(tooltip);
   if (kind != ButtonKind::Danger)
      return clicked;

   const std::string popup = text + "##Confirm";
   if (clicked)
      ImGui::OpenPopup(popup.c_str());
   clicked = false;
   if (ImGui::BeginPopup(popup.c_str()))
   {
      ImGui::PushStyleColor(ImGuiCol_Text, Style::Colors::WARNING);
      ImGui::TextUnformatted(Icons::WARNING);
      ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::TextUnformatted(std::format("{}?", text).c_str());
      if (Button("Confirm", ButtonKind::Primary))
      {
         clicked = true;
         ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (Button("Cancel"))
         ImGui::CloseCurrentPopup();
      WindowShadow(ImGui::GetStyle().PopupRounding);
      ImGui::EndPopup();
   }
   return clicked;
}

bool IconButton(const char* icon, std::string_view tooltip)
{
   ImGui::PushStyleColor(ImGuiCol_Button, Style::WithAlpha(Style::Colors::CONTROL, 0.f));
   const bool clicked = ImGui::Button(std::format("{}##{}", icon, tooltip).c_str());
   ImGui::PopStyleColor();
   Tooltip(tooltip);
   return clicked;
}

void ProgressBar(float fraction, std::string_view label)
{
   const std::string text = Terminated(label);
   if (fraction < 0.f)
      ImGui::ProgressBar(-float(ImGui::GetTime()), ImVec2(-1.f, 0.f), text.empty() ? nullptr : text.c_str());
   else
      ImGui::ProgressBar(std::clamp(fraction, 0.f, 1.f), ImVec2(-1.f, 0.f), text.empty() ? nullptr : text.c_str());
}

void ListDetail(std::string_view id, std::span<const ListEntry> entries, size_t& selected, const std::function<void(size_t index)>& detail, float height,
   const std::function<bool(size_t index)>& visible, const std::function<void(size_t index)>& row_action)
{
   ImGui::PushID(id.data(), id.data() + id.size());
   std::string& filter = list_filters[ImGui::GetID("Filter")];
   if (ImGui::BeginChild("List", ImVec2(Style::Scale(Style::Sizes::LIST_WIDTH), height), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX))
   {
      if (!visible)
      {
         ImGui::SetNextItemWidth(-1.f);
         ImGui::InputTextWithHint("##Filter", std::format("{} Filter", Icons::SEARCH).c_str(), &filter);
      }
      std::vector<size_t> shown;
      shown.reserve(entries.size());
      for (size_t i = 0; i < entries.size(); i++)
      {
         if (visible ? visible(i) : filter.empty() || ContainsIgnoreCase(entries[i].label, filter))
            shown.push_back(i);
      }
      // Only the visible rows are drawn: a trace has thousands
      ImGuiListClipper clipper;
      clipper.Begin(int(shown.size()));
      while (clipper.Step())
      {
         for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
         {
            const size_t i = shown[size_t(row)];
            const ListEntry& entry = entries[i];
            const bool tinted = entry.color.has_value() || entry.tone != Tone::Info;
            if (entry.color)
               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4((*entry.color)[0], (*entry.color)[1], (*entry.color)[2], (*entry.color)[3]));
            else if (entry.tone != Tone::Info)
               ImGui::PushStyleColor(ImGuiCol_Text, GetColor(entry.tone));
            const ImGuiSelectableFlags flags = row_action ? ImGuiSelectableFlags_AllowOverlap : ImGuiSelectableFlags_None;
            const float label_limit = std::max(0.f, ImGui::GetContentRegionAvail().x - (row_action ? ImGui::GetFrameHeight() * 2.f : 0.f));
            bool truncated = false;
            const std::string fitted = FitWithEllipsis(entry.label, label_limit, truncated);
            if (ImGui::Selectable(std::format("{}###{}", fitted, i).c_str(), i == selected, flags))
               selected = i;
            if (tinted)
               ImGui::PopStyleColor();
            if (truncated)
               Tooltip(entry.label);
            if (row_action)
            {
               ImGui::PushID(int(i));
               row_action(i);
               ImGui::PopID();
            }
         }
      }
   }
   ImGui::EndChild();
   ImGui::SameLine();
   if (ImGui::BeginChild("Detail", ImVec2(0.f, height), ImGuiChildFlags_Borders))
   {
      if (selected < entries.size())
         detail(selected);
      else
         TextDim("Select an entry");
   }
   ImGui::EndChild();
   ImGui::PopID();
}

void PropertyGrid(std::string_view id, std::span<const PropertyRow> rows)
{
   const std::string table = Terminated(id);
   if (!ImGui::BeginTable(table.c_str(), 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
      return;
   ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, Style::Scale(Style::Sizes::LABEL_WIDTH));
   ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
   for (size_t i = 0; i < rows.size(); i++)
   {
      const PropertyRow& row = rows[i];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      DrawTextEllipsis(row.name, 0.f, Style::Colors::TEXT_LABEL);
      ImGui::TableNextColumn();
      ImGui::PushID(int(i));
      ImGui::PushFont(Fonts::GetMonospace(), 0.f);
      if (row.tone != Tone::Info)
         ImGui::PushStyleColor(ImGuiCol_Text, GetColor(row.tone));
      const std::string_view value_text = row.value.empty() ? std::string_view("-") : std::string_view(row.value);
      bool truncated = false;
      const std::string shown = FitWithEllipsis(value_text, ImGui::GetContentRegionAvail().x, truncated);
      if (ImGui::Selectable(std::format("{}###Val", shown).c_str()))
         ImGui::SetClipboardText(row.value.c_str());
      if (row.tone != Tone::Info)
         ImGui::PopStyleColor();
      ImGui::PopFont();
      CombinedTooltip(value_text, truncated, "Click to copy");
      ImGui::PopID();
   }
   ImGui::EndTable();
}

void DataTable(std::string_view id, std::span<const TableColumn> columns, std::span<const TableRow> rows, float max_height)
{
   if (columns.empty())
      return;
   ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
   ImVec2 size(0.f, 0.f);
   const float row_height = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.f;
   if (max_height > 0.f && row_height * float(rows.size() + 1) > max_height)
   {
      flags |= ImGuiTableFlags_ScrollY;
      size.y = max_height;
   }
   if (!ImGui::BeginTable(Terminated(id).c_str(), int(columns.size()), flags, size))
      return;
   for (const TableColumn& column : columns)
      ImGui::TableSetupColumn(Terminated(column.name).c_str(), column.width > 0.f ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch,
         column.width > 0.f ? Style::Scale(column.width) : 0.f);
   ImGui::TableSetupScrollFreeze(0, 1);
   ImGui::PushStyleColor(ImGuiCol_Text, Style::Colors::TEXT_DIM);
   ImGui::TableHeadersRow();
   ImGui::PopStyleColor();
   // Only the visible rows are drawn: a cbuffer can have thousands
   ImGuiListClipper clipper;
   clipper.Begin(int(rows.size()));
   while (clipper.Step())
   {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
      {
         ImGui::TableNextRow();
         ImGui::PushID(row);
         const TableRow& cells = rows[size_t(row)];
         for (size_t column = 0; column < columns.size(); column++)
         {
            ImGui::TableNextColumn();
            if (column >= cells.size())
               continue;
            const TableCell& cell = cells[column];
            ImGui::PushID(int(column));
            if (columns[column].monospace)
               ImGui::PushFont(Fonts::GetMonospace(), 0.f);
            ImGui::PushStyleColor(ImGuiCol_Text, cell.tone != Tone::Info ? GetColor(cell.tone) : (column == 0 ? Style::Colors::TEXT_LABEL : Style::Colors::TEXT));
            const std::string_view cell_text = cell.text.empty() ? std::string_view("-") : std::string_view(cell.text);
            bool truncated = false;
            const std::string shown = FitWithEllipsis(cell_text, ImGui::GetContentRegionAvail().x, truncated);
            if (ImGui::Selectable(std::format("{}###Cell", shown).c_str()))
               ImGui::SetClipboardText(cell.text.c_str());
            ImGui::PopStyleColor();
            if (columns[column].monospace)
               ImGui::PopFont();
            CombinedTooltip(cell_text, truncated, cell.tooltip.empty() ? std::string_view("Click to copy") : std::string_view(cell.tooltip));
            ImGui::PopID();
         }
         ImGui::PopID();
      }
   }
   ImGui::EndTable();
}

void HashChip(uint32_t hash, ShaderStage stage, std::string_view name)
{
   const std::string text = std::format("0x{:08X}", hash);
   ImGui::PushFont(Fonts::GetMonospace(), 0.f);
   ImGui::PushStyleColor(ImGuiCol_Text, GetStageTint(stage));
   ImGui::PushStyleColor(ImGuiCol_Button, Style::WithAlpha(GetStageTint(stage), 0.12f));
   ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Style::Scale(Style::Sizes::UNIT * 1.5f), 0.f));
   // The same hash twice in a window shares an id: harmless, both copy the same text
   const bool clicked = ImGui::Button(text.c_str());
   ImGui::PopStyleVar();
   ImGui::PopStyleColor(2);
   ImGui::PopFont();
   if (clicked)
      ImGui::SetClipboardText(text.c_str());
   Tooltip(name.empty() ? std::format("{} shader, click to copy", GetStageName(stage)) : std::format("{} ({} shader), click to copy", name, GetStageName(stage)));
}

void Code(std::string_view text)
{
   ImGui::PushFont(Fonts::GetMonospace(), 0.f);
   ImGui::PushTextWrapPos(0.f);
   ImGui::TextUnformatted(text.data(), text.data() + text.size());
   ImGui::PopTextWrapPos();
   ImGui::PopFont();
}

void Text(std::string_view text, Tone tone)
{
   DrawTextEllipsis(text, 0.f, tone == Tone::Info ? std::nullopt : std::optional(GetColor(tone)));
}

void TextDim(std::string_view text)
{
   DrawTextEllipsis(text, 0.f, Style::Colors::TEXT_DIM);
}

void TextEllipsis(std::string_view text, float max_width, Tone tone, std::string_view tooltip)
{
   DrawTextEllipsis(text, max_width, tone == Tone::Info ? std::nullopt : std::optional(GetColor(tone)), tooltip);
}

void Toast(Tone tone, std::string message)
{
   std::scoped_lock lock(toasts_mutex);
   toasts.push_back({tone, std::move(message), Clock::now()});
   while (toasts.size() > MAX_TOASTS)
      toasts.pop_front();
}

bool HasToasts()
{
   std::scoped_lock lock(toasts_mutex);
   return !toasts.empty();
}

void DrawToasts()
{
   std::deque<ToastEntry> shown;
   {
      std::scoped_lock lock(toasts_mutex);
      const Clock::time_point now = Clock::now();
      std::erase_if(toasts, [now](const ToastEntry& toast) { return now - toast.shown > TOAST_DURATION; });
      shown = toasts;
   }
   const ImGuiViewport* viewport = ImGui::GetMainViewport();
   const float margin = Style::Scale(Style::Sizes::UNIT * 4.f);
   float y = viewport->WorkPos.y + viewport->WorkSize.y - margin;
   for (size_t i = shown.size(); i-- > 0;)
   {
      const ToastEntry& toast = shown[i];
      ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin, y), ImGuiCond_Always, ImVec2(1.f, 1.f));
      ImGui::SetNextWindowBgAlpha(0.92f);
      ImGui::PushStyleColor(ImGuiCol_Border, GetColor(toast.tone));
      const std::string name = std::format("##Toast{}", i);
      if (ImGui::Begin(name.c_str(), nullptr,
             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking))
      {
         ImGui::PushStyleColor(ImGuiCol_Text, GetColor(toast.tone));
         ImGui::TextUnformatted(GetIcon(toast.tone));
         ImGui::PopStyleColor();
         ImGui::SameLine();
         ImGui::PushTextWrapPos(Style::Scale(Style::Sizes::UNIT * 90.f));
         ImGui::TextUnformatted(toast.message.c_str());
         ImGui::PopTextWrapPos();
         WindowShadow(ImGui::GetStyle().WindowRounding);
         y -= ImGui::GetWindowHeight() + Style::Scale(Style::Sizes::UNIT * 2.f);
      }
      ImGui::End();
      ImGui::PopStyleColor();
   }
}

void StatusBar(std::span<const StatusItem> items)
{
   ImGui::PushStyleColor(ImGuiCol_ChildBg, Style::Colors::PANEL);
   const float height = ImGui::GetFrameHeight() + Style::Scale(Style::Sizes::UNIT);
   if (ImGui::BeginChild("StatusBar", ImVec2(0.f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar))
   {
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Style::Scale(Style::Sizes::UNIT * 0.5f));
      for (size_t i = 0; i < items.size(); i++)
      {
         const StatusItem& item = items[i];
         if (i > 0)
         {
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
         }
         else
         {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Style::Scale(Style::Sizes::UNIT * 2.f));
         }
         // Status icon: muted neutral for info, semantic tint for warnings/errors
         if (item.icon)
         {
            ImGui::PushStyleColor(ImGuiCol_Text, item.tone == Tone::Info ? Style::Colors::TEXT_DIM : GetColor(item.tone));
            ImGui::TextUnformatted(item.icon);
            ImGui::PopStyleColor();
            Tooltip(item.tooltip);
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
         }
         DrawTextEllipsis(item.text, 0.f, item.tone == Tone::Info ? Style::Colors::TEXT_DIM : GetColor(item.tone), item.tooltip);
         if (item.progress)
         {
            ImGui::SameLine();
            const ImVec2 size(Style::Scale(Style::Sizes::UNIT * 25.f), ImGui::GetTextLineHeight());
            const float fraction = *item.progress < 0.f ? -float(ImGui::GetTime()) : std::clamp(*item.progress, 0.f, 1.f);
            ImGui::ProgressBar(fraction, size, "");
         }
      }
   }
   ImGui::EndChild();
   ImGui::PopStyleColor();
}
} // namespace Widgets
} // namespace UI
} // namespace Luma
