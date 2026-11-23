#pragma once

#include <IconsFontAwesome6.h>
#include <IconsFontAwesome6Brands.h>

struct ImFont;

// Font Awesome Free glyphs (UTF-8, merged into the overlay's font from the dll's resources, UI/Fonts.rc). Any codepoint of the
// Solid and Brands fonts works: name it here from IconFontCppHeaders' headers. An icon-only button always has a tooltip.
namespace Luma {
namespace UI {
namespace Icons {
inline constexpr const char* SETTINGS = ICON_FA_GEAR;
inline constexpr const char* INFO = ICON_FA_CIRCLE_INFO;
inline constexpr const char* HELP = ICON_FA_CIRCLE_QUESTION;
inline constexpr const char* RESET = ICON_FA_ROTATE_LEFT;
inline constexpr const char* RELOAD = ICON_FA_ARROWS_ROTATE;
inline constexpr const char* RESTART = ICON_FA_ROTATE_RIGHT;
inline constexpr const char* SUCCESS = ICON_FA_CIRCLE_CHECK;
inline constexpr const char* WARNING = ICON_FA_TRIANGLE_EXCLAMATION;
inline constexpr const char* ERROR_ = ICON_FA_CIRCLE_XMARK; // ERROR is a Windows macro
inline constexpr const char* CLOSE = ICON_FA_XMARK;
inline constexpr const char* KEYBOARD = ICON_FA_KEYBOARD;
inline constexpr const char* SEARCH = ICON_FA_MAGNIFYING_GLASS;
inline constexpr const char* COPY = ICON_FA_COPY;
inline constexpr const char* DELETE_ = ICON_FA_TRASH; // DELETE is a Windows macro
inline constexpr const char* FOLDER = ICON_FA_FOLDER;
inline constexpr const char* CODE = ICON_FA_CODE;
inline constexpr const char* BUG = ICON_FA_BUG;
inline constexpr const char* LIST = ICON_FA_LIST;
inline constexpr const char* CAPTURE = ICON_FA_CAMERA;
inline constexpr const char* DISPLAY = ICON_FA_DISPLAY;
inline constexpr const char* SLIDERS = ICON_FA_SLIDERS;
inline constexpr const char* TOOLS = ICON_FA_WRENCH;
inline constexpr const char* POWER = ICON_FA_POWER_OFF;
inline constexpr const char* LINK = ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE;
inline constexpr const char* HEART = ICON_FA_HEART;
inline constexpr const char* EXPANDED = ICON_FA_CHEVRON_DOWN;
inline constexpr const char* COLLAPSED = ICON_FA_CHEVRON_RIGHT;
inline constexpr const char* FILTER = ICON_FA_FILTER;
inline constexpr const char* TRACK = ICON_FA_CROSSHAIRS;
inline constexpr const char* VIEW = ICON_FA_EYE;
inline constexpr const char* STOP = ICON_FA_STOP;
inline constexpr const char* VARIANT = ICON_FA_CODE_BRANCH;
inline constexpr const char* PATCH = ICON_FA_BANDAGE;

// Category headers
inline constexpr const char* OVERLAY = ICON_FA_WINDOW_MAXIMIZE;
inline constexpr const char* UPSCALING = ICON_FA_EXPAND;
inline constexpr const char* POST_PROCESSING = ICON_FA_WAND_MAGIC_SPARKLES;
inline constexpr const char* ADVANCED = ICON_FA_FLASK;
inline constexpr const char* CONSOLE = ICON_FA_TERMINAL;
inline constexpr const char* CREDITS = ICON_FA_USERS;
inline constexpr const char* GAME = ICON_FA_GAMEPAD;
inline constexpr const char* COLOR = ICON_FA_PALETTE;
inline constexpr const char* HDR = ICON_FA_SUN;
inline constexpr const char* PERFORMANCE = ICON_FA_GAUGE;
inline constexpr const char* LAYERS = ICON_FA_LAYER_GROUP;

// Link buttons. Font Awesome Free 6.7.2's Brands font has no Ko-fi and no Buy Me a Coffee mark: Solid mugs stand in.
inline constexpr const char* KOFI = ICON_FA_MUG_HOT;
inline constexpr const char* BUY_ME_A_COFFEE = ICON_FA_MUG_SAUCER;
inline constexpr const char* DISCORD = ICON_FA_DISCORD;
inline constexpr const char* GITHUB = ICON_FA_GITHUB;
} // namespace Icons

namespace Fonts {
// The text font (Segoe UI, or ImGui's own without it) with Font Awesome's Solid and Brands merged in, and the monospace font
// (Consolas, else the text font). After the context is created.
void Load();
ImFont* GetMonospace();
} // namespace Fonts
} // namespace UI
} // namespace Luma
