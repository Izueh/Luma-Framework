#include "UI/Widgets.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <variant>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "UI/Icons.hpp"
#include "UI/Input.hpp"
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

// Sliders, drags and text apply when the edit ends; toggles, combos and keys at once
bool IsAppliedOnRelease(const Settings::Setting& setting)
{
   if (std::holds_alternative<std::string>(setting.default_value) || std::holds_alternative<float>(setting.default_value))
      return true;
   return std::holds_alternative<int32_t>(setting.default_value) && setting.range;
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
   default: return Style::Colors::ACCENT;
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

// A small rounded label after a row's control
void Badge(std::string_view text, ImVec4 color)
{
   ImGui::SameLine();
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

float GetControlWidth()
{
   // Room for the reset icon and a badge
   return std::max(ImGui::GetContentRegionAvail().x - Style::Scale(Style::Sizes::UNIT * 22.f), Style::Scale(Style::Sizes::UNIT * 30.f));
}

bool DrawControl(const Settings::Setting& setting, Settings::Value& value)
{
   const std::string id(setting.key);
   ImGui::SetNextItemWidth(GetControlWidth());
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
         return Combo(id, typed.value, setting.enum_labels);
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
   // Every category header, whatever it holds, has this look and height
   const Style::Category category = Style::GetCategory(label);
   if (!icon)
      icon = category.icon;
   const float unit = Style::Scale(Style::Sizes::UNIT);
   const ImVec2 padding(unit * 2.f, unit * 1.5f);
   const float rounding = Style::Scale(Style::Sizes::CONTROL_ROUNDING);
   ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
   ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
   ImGui::PushStyleColor(ImGuiCol_Header, Style::WithAlpha(Style::Colors::PANEL, 0.f));
   ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Style::Colors::HOVER_OVERLAY);
   ImGui::PushStyleColor(ImGuiCol_HeaderActive, Style::Colors::HIGHLIGHT);

   // The header on top, its raised background and accent bar under it once its rectangle is known
   ImDrawList* draw_list = ImGui::GetWindowDrawList();
   draw_list->ChannelsSplit(2);
   draw_list->ChannelsSetCurrent(1);
   const float text_x = ImGui::GetCursorScreenPos().x + ImGui::GetFontSize() + padding.x * 3.f;
   const std::string text = std::format("{}{}###{}", IconRoom(icon), label, label);
   const bool open = ImGui::CollapsingHeader(text.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | (default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0));
   const ImVec2 min = ImGui::GetItemRectMin();
   const ImVec2 max = ImGui::GetItemRectMax();
   DrawIcon(ImVec2(text_x, min.y + padding.y), icon, category.tint);
   draw_list->ChannelsSetCurrent(0);
   Style::DrawRaised(draw_list, min, max, rounding);
   draw_list->AddRectFilled(min, ImVec2(min.x + Style::Scale(Style::Sizes::ACCENT_BAR), max.y), ImGui::GetColorU32(category.tint), rounding,
      ImDrawFlags_RoundCornersLeft);
   draw_list->ChannelsMerge();
   ImGui::PopStyleColor(3);
   ImGui::PopStyleVar(2);
   if (open)
   {
      ImGui::Indent(unit * 2.f);
      ImGui::Dummy(ImVec2(0.f, unit * 0.5f));
   }
   return open;
}

void EndSection()
{
   ImGui::Unindent(Style::Scale(Style::Sizes::UNIT) * 2.f);
   ImGui::Dummy(ImVec2(0.f, Style::Scale(Style::Sizes::UNIT)));
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
   draw_list->AddText(ImVec2(min.x + padding.x, min.y + padding.y), ImGui::GetColorU32(selected ? category.tint : Style::Colors::ICON_DISABLED), icon);
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
   ImGui::TextUnformatted(value.data(), value.data() + value.size());
}

void Caption(std::string_view text)
{
   ImGui::AlignTextToFramePadding();
   TextDim(text);
}

void ListItem(std::string_view text)
{
   ImGui::Bullet();
   ImGui::TextUnformatted(text.data(), text.data() + text.size());
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
   ImGui::PushStyleColor(ImGuiCol_Text, emphasized ? Style::Colors::TEXT_BRIGHT : Style::Colors::TEXT_LABEL);
   ImGui::TextUnformatted(label.data(), label.data() + label.size());
   ImGui::PopStyleColor();
   Tooltip(tooltip);
   ImGui::SameLine(start + Style::Scale(Style::Sizes::LABEL_WIDTH));
}

bool SettingRow(const Settings::Setting& setting)
{
   const std::optional<Settings::Value> current = Settings::GetValue(setting.key);
   if (!current)
      return false;
   ImGui::PushID(Terminated(setting.key).c_str());
   const ImGuiID row_id = ImGui::GetID("Row");
   const bool is_default = *current == Settings::Clamp(setting, setting.default_value);
   RowLabel(setting.label.empty() ? setting.key : setting.label, setting.tooltip, !is_default);
   const bool enabled = !setting.is_enabled || setting.is_enabled();
   ImGui::BeginDisabled(!enabled);
   // A drag or a typed edit shows its own value until it ends, and only then reaches the registry
   Settings::Value edited = editing_id == row_id ? editing_value : *current;
   const bool control_changed = DrawControl(setting, edited);
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
   if (*Settings::GetValue(setting.key) != Settings::Clamp(setting, setting.default_value))
   {
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, Style::Colors::ACCENT);
      if (IconButton(Icons::RESET, "Reset to default"))
         changed |= Settings::Reset(setting.key);
      ImGui::PopStyleColor();
   }
   ImGui::EndDisabled();
   if (std::holds_alternative<Settings::Sources::Define>(setting.source))
      Badge("recompile", Style::Colors::WARNING);
   else if (std::holds_alternative<Settings::Sources::Recipe>(setting.source))
      Badge("re-patch", Style::Colors::WARNING);
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

bool Combo(std::string_view id, int32_t& index, std::span<const std::string_view> labels)
{
   const std::string preview = index >= 0 && size_t(index) < labels.size() ? Terminated(labels[index]) : std::to_string(index);
   bool changed = false;
   if (ImGui::BeginCombo(HiddenLabel(id).c_str(), preview.c_str()))
   {
      for (int32_t i = 0; i < int32_t(labels.size()); i++)
      {
         if (ImGui::Selectable(Terminated(labels[i]).c_str(), i == index))
         {
            changed = i != index;
            index = i;
         }
      }
      ImGui::EndCombo();
   }
   return changed;
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

void Text(std::string_view text, Tone tone)
{
   if (tone == Tone::Info)
   {
      ImGui::TextUnformatted(text.data(), text.data() + text.size());
      return;
   }
   ImGui::PushStyleColor(ImGuiCol_Text, GetColor(tone));
   ImGui::TextUnformatted(text.data(), text.data() + text.size());
   ImGui::PopStyleColor();
}

void TextDim(std::string_view text)
{
   ImGui::PushStyleColor(ImGuiCol_Text, Style::Colors::TEXT_DIM);
   ImGui::TextUnformatted(text.data(), text.data() + text.size());
   ImGui::PopStyleColor();
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
         // The icon tinted (the accent, or its state's color), the text dim unless it reports a state
         if (item.icon)
         {
            ImGui::PushStyleColor(ImGuiCol_Text, GetColor(item.tone));
            ImGui::TextUnformatted(item.icon);
            ImGui::PopStyleColor();
            Tooltip(item.tooltip);
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
         }
         if (item.tone == Tone::Info)
            TextDim(item.text);
         else
            Text(item.text, item.tone);
         Tooltip(item.tooltip);
      }
   }
   ImGui::EndChild();
   ImGui::PopStyleColor();
}
} // namespace Widgets
} // namespace UI
} // namespace Luma
