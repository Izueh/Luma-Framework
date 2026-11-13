#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "Common/Api.hpp"
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
   Injected, // What Luma added, in trace
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
// A dropdown: for many options (more than about five), a Segmented control for fewer
bool Combo(std::string_view id, int32_t& index, std::span<const std::string_view> labels);
// A row of mutually exclusive options side by side, the selected one filled in the accent color: for a few options (about five or
// fewer), a Combo for more. `width` 0 sizes it to its labels, otherwise the segments share it. True when the selection changed.
bool Segmented(std::string_view id, int32_t& index, std::span<const std::string_view> labels, float width = 0.f);
// The control SettingRow uses for `count` options: Segmented up to this many, Combo above
inline constexpr size_t MAX_SEGMENTS = 5;
// A compact on/off button for list filters, filled in the accent color while on. True when toggled.
bool FilterChip(std::string_view label, bool& value, std::string_view tooltip = {});
// A small rounded label inline, tinted by `tone` (a state: "Variant", "pending")
void Tag(std::string_view text, Tone tone, std::string_view tooltip = {});
// A message across the available width, tinted by `tone`, with an optional primary action at its end. True when the action is
// clicked.
bool Banner(Tone tone, std::string_view text, std::string_view action = {}, std::string_view action_tooltip = {});
// A row with a name in the label column and a value
void Property(std::string_view name, std::string_view value);
// A dim line heading the rows or items after it
void Caption(std::string_view text);
// Text wrapped at the available width
void Paragraph(std::string_view text);
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

struct ListEntry
{
   std::string label;
   Tone tone = Tone::Info; // Info is plain text
};

// A list on the left (its width can be dragged), the selected entry's detail on the right, drawn by `detail` with the entry's index.
// `selected` indexes `entries`, kept by the caller (out of range: none). `height` is the panes', 0 for the room left. The list
// has a text filter on the labels, unless `visible` filters it (the caller draws its own filters then).
void ListDetail(std::string_view id, std::span<const ListEntry> entries, size_t& selected, const std::function<void(size_t index)>& detail,
   float height = 0.f, const std::function<bool(size_t index)>& visible = {});

struct PropertyRow
{
   std::string name;
   std::string value;
   Tone tone = Tone::Info;
};

// A key and value table for state dumps, values in monospace: a click on a row copies its value
void PropertyGrid(std::string_view id, std::span<const PropertyRow> rows);

struct TableColumn
{
   std::string_view name;
   float width = 0.f; // In pixels at the default font size, 0 to share the room left
   bool monospace = true;
};

struct TableCell
{
   std::string text;
   Tone tone = Tone::Info; // Info is plain text
   std::string tooltip;
};

using TableRow = std::vector<TableCell>;

// A table with a header row for state dumps of several columns (views, cbuffer contents): aligned columns, a click on a cell copies
// it. Above `max_height` (0: none) it scrolls on its own, only the visible rows drawn.
void DataTable(std::string_view id, std::span<const TableColumn> columns, std::span<const TableRow> rows, float max_height = 0.f);

// A shader hash: monospace "0x12345678" tinted by stage, a click copies it, hovering shows `name` and the stage
void HashChip(uint32_t hash, ShaderStage stage, std::string_view name = {});
// Monospace text, wrapped (compiler messages)
void Code(std::string_view text);
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
   // A small bar after the text, for work in progress: [0, 1], negative for unknown length
   std::optional<float> progress;
};

// The bottom strip of the window
void StatusBar(std::span<const StatusItem> items);
} // namespace Widgets
} // namespace UI
} // namespace Luma
