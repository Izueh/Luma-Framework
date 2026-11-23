#pragma once

#include <string_view>

#include <imgui.h>

// Every color, size and spacing of the overlay. Panels use these, never literal colors or pixel sizes.
namespace Luma {
namespace UI {
namespace Style {
namespace Colors {
// Surfaces, darkest to lightest
inline constexpr ImVec4 WINDOW = {0.067f, 0.071f, 0.082f, 1.f};
inline constexpr ImVec4 PANEL = {0.098f, 0.106f, 0.122f, 1.f};
inline constexpr ImVec4 CONTROL = {0.153f, 0.165f, 0.188f, 1.f};
inline constexpr ImVec4 CONTROL_HOVERED = {0.204f, 0.220f, 0.251f, 1.f};
inline constexpr ImVec4 CONTROL_ACTIVE = {0.243f, 0.263f, 0.302f, 1.f};
inline constexpr ImVec4 BORDER = {0.224f, 0.239f, 0.271f, 1.f};
inline constexpr ImVec4 SHADOW = {0.f, 0.f, 0.f, 0.45f};

inline constexpr ImVec4 TEXT = {0.902f, 0.914f, 0.933f, 1.f};
inline constexpr ImVec4 TEXT_DIM = {0.557f, 0.580f, 0.624f, 1.f};
// Setting labels, and a step brighter for a changed setting
inline constexpr ImVec4 TEXT_LABEL = {0.776f, 0.792f, 0.827f, 1.f};
inline constexpr ImVec4 TEXT_BRIGHT = {1.f, 1.f, 1.f, 1.f};
inline constexpr ImVec4 ICON_DISABLED = {0.420f, 0.439f, 0.478f, 1.f};

// Raised surfaces (section headers, the title bar): a vertical gradient and a highlight on the top edge
inline constexpr ImVec4 RAISED_TOP = {0.149f, 0.161f, 0.192f, 1.f};
inline constexpr ImVec4 RAISED_BOTTOM = {0.110f, 0.118f, 0.141f, 1.f};
inline constexpr ImVec4 HIGHLIGHT = {1.f, 1.f, 1.f, 0.10f};
inline constexpr ImVec4 HOVER_OVERLAY = {1.f, 1.f, 1.f, 0.05f};

// Luma blue: selection, focus and primary actions
inline constexpr ImVec4 ACCENT = {0.247f, 0.557f, 0.988f, 1.f};
inline constexpr ImVec4 ACCENT_HOVERED = {0.384f, 0.647f, 1.f, 1.f};
inline constexpr ImVec4 ACCENT_ACTIVE = {0.180f, 0.447f, 0.835f, 1.f};

inline constexpr ImVec4 SUCCESS = {0.298f, 0.733f, 0.427f, 1.f};
inline constexpr ImVec4 WARNING = {0.961f, 0.678f, 0.212f, 1.f};
inline constexpr ImVec4 DANGER = {0.898f, 0.282f, 0.302f, 1.f};
inline constexpr ImVec4 DANGER_HOVERED = {0.957f, 0.392f, 0.408f, 1.f};
// What Luma injected, in trace
inline constexpr ImVec4 INJECTED = {0.631f, 0.475f, 0.961f, 1.f};

// Hash chips by shader stage
namespace Stages {
inline constexpr ImVec4 VERTEX = {0.353f, 0.784f, 0.980f, 1.f};
inline constexpr ImVec4 PIXEL = {0.957f, 0.451f, 0.710f, 1.f};
inline constexpr ImVec4 COMPUTE = {0.243f, 0.835f, 0.710f, 1.f};
// Hull, domain and geometry
inline constexpr ImVec4 OTHER = {0.984f, 0.663f, 0.243f, 1.f};
} // namespace Stages

// Semantic trace list row colors
namespace Trace {
inline constexpr ImVec4 VERTEX = {0.580f, 0.820f, 0.960f, 1.f};
inline constexpr ImVec4 PIXEL = {0.902f, 0.914f, 0.933f, 1.f};
inline constexpr ImVec4 COMPUTE = {0.420f, 0.855f, 0.765f, 1.f};
inline constexpr ImVec4 REPLACED = {0.340f, 0.800f, 0.470f, 1.f};
inline constexpr ImVec4 RESOURCE_OP = {0.940f, 0.760f, 0.400f, 1.f};
inline constexpr ImVec4 PRESENT = {0.820f, 0.880f, 1.000f, 1.f};
inline constexpr ImVec4 LUMA_PASS = {0.690f, 0.560f, 0.980f, 1.f};
inline constexpr ImVec4 CANCELLED = {0.880f, 0.480f, 0.360f, 1.f};
inline constexpr ImVec4 HIGHLIGHT_RW_SINGLE = {0.957f, 0.451f, 0.710f, 1.f}; // Pink: read or write
inline constexpr ImVec4 HIGHLIGHT_RW_BOTH = {0.960f, 0.290f, 0.310f, 1.f}; // Red: read and write
} // namespace Trace

// The services' own colors, for their marks on link buttons (GitHub's is its dark-theme white)
namespace Brands {
inline constexpr ImVec4 KOFI = {1.f, 0.369f, 0.357f, 1.f};
inline constexpr ImVec4 BUY_ME_A_COFFEE = {1.f, 0.867f, 0.f, 1.f};
inline constexpr ImVec4 DISCORD = {0.345f, 0.396f, 0.949f, 1.f};
inline constexpr ImVec4 GITHUB = {0.941f, 0.965f, 0.988f, 1.f};
} // namespace Brands
} // namespace Colors

// In pixels at the default font size. Scale() adapts them to the font size setting.
namespace Sizes {
inline constexpr float UNIT = 4.f;
inline constexpr float CONTROL_ROUNDING = 4.f;
inline constexpr float PANEL_ROUNDING = 6.f;
inline constexpr float LABEL_WIDTH = 200.f;
inline constexpr float LIST_WIDTH = 240.f;
inline constexpr float DEFAULT_FONT = 16.f;
inline constexpr float ACCENT_BAR = 3.f;
inline constexpr float SHADOW = 10.f;
} // namespace Sizes

// A category header's icon and tint, chosen by its name ("Display", "Overlay", "Credits"...)
struct Category
{
   const char* icon;
   ImVec4 tint;
};

Category GetCategory(std::string_view name);
// Registers a custom icon for a category name (matched case-insensitively before built-in keywords)
void RegisterCategoryIcon(std::string_view category, const char* icon);

// The gradient and top highlight of raised surfaces
void DrawRaised(ImDrawList* draw_list, ImVec2 min, ImVec2 max, float rounding);
// A soft shadow around (outside) a popup, tooltip or toast. From inside its window.
void DrawShadow(ImDrawList* draw_list, ImVec2 min, ImVec2 max, float rounding);

// `pixels` at the current font size (and DPI)
float Scale(float pixels);
// Every frame before NewFrame: the tokens at `font_size` (unscaled) and the window's DPI scale, the window background at `opacity` (0 to 1)
void Apply(float font_size, float dpi_scale, float opacity = 1.f);

ImVec4 WithAlpha(ImVec4 color, float alpha);
} // namespace Style
} // namespace UI
} // namespace Luma
