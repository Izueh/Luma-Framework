#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "Settings/Registry.hpp"

// The only place that calls raw ImGui controls for settings, so every panel looks and behaves the same. Games use these in
// their panels too. `id`s are unique within the current window or section.
namespace Luma {
namespace UI {
namespace Widgets {
enum class ButtonKind : uint8_t
{
   Primary,
   Secondary,
   Danger, // Asks to confirm
};

enum class Tone : uint8_t
{
   Info,
   Success,
   Warning,
   Error,
};

// A collapsible category header, raised, with an accent bar and an icon tinted by category (chosen from the label, or
// `icon`). True while open: draw its rows, then call EndSection().
bool Section(std::string_view label, bool default_open = true, const char* icon = nullptr);
void EndSection();

// A tab of the overlay's tab bar, underlined in the accent color while selected. True while selected: call EndTab().
bool Tab(std::string_view label, const char* icon = nullptr);
void EndTab();

// One registry setting: its label at a fixed width, the control for its value type, a reset icon when it isn't the default,
// a badge for what a change triggers. Sliders and text apply when the edit ends, unless the setting is live. True if the
// value changed (it's already set in the registry).
bool SettingRow(const Settings::Setting& setting);
// The label column of a row with a custom control, which follows on the same line. `emphasized` for a changed value.
void RowLabel(std::string_view label, std::string_view tooltip = {}, bool emphasized = false);

bool Toggle(std::string_view id, bool& value);
bool Slider(std::string_view id, float& value, float min, float max, std::string_view format = "%.3f");
bool Slider(std::string_view id, int32_t& value, int32_t min, int32_t max);
bool Combo(std::string_view id, int32_t& index, std::span<const std::string_view> labels);
// A row with a name in the label column and a value
void Property(std::string_view name, std::string_view value);
// A dim line heading the rows or items after it
void Caption(std::string_view text);
// One item of a bulleted list
void ListItem(std::string_view text);
// A button opening `url` in the browser, with the service's mark in its color (Ko-fi, Buy Me a Coffee, Discord, GitHub) or a
// generic link icon
bool Link(std::string_view label, std::string_view url);
// The width Link() will take
float GetLinkWidth(std::string_view label, std::string_view url);
// Lays the next item out on the same line if `width` still fits, otherwise on the next one. Returns the item's width.
float WrapSameLine(float line_used, float width);

// Click, then press a key (Escape cancels). `key` is a Windows virtual key code.
bool KeyBind(std::string_view id, uint32_t& key);
// True on every edit (SettingRow applies the text when the edit ends)
bool TextInput(std::string_view id, std::string& text);

// True when clicked (for Danger, when the confirmation is accepted)
bool Button(std::string_view label, ButtonKind kind = ButtonKind::Secondary, std::string_view tooltip = {});
bool IconButton(const char* icon, std::string_view tooltip);

// `fraction` in [0, 1], negative for work of unknown length
void ProgressBar(float fraction, std::string_view label = {});
void Text(std::string_view text, Tone tone = Tone::Info);
void TextDim(std::string_view text);

// A transient message in the corner of the screen, also shown while the overlay is closed. Any thread.
void Toast(Tone tone, std::string message);
// For the overlay, each frame
bool HasToasts();
void DrawToasts();

struct StatusItem
{
   const char* icon = nullptr;
   std::string text;
   Tone tone = Tone::Info;
   std::string tooltip;
};

// The bottom strip of the window
void StatusBar(std::span<const StatusItem> items);
} // namespace Widgets
} // namespace UI
} // namespace Luma
