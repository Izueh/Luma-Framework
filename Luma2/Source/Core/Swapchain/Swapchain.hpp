#pragma once

#include <cstdint>
#include <string_view>

// The real swapchain Luma presents and the game's view of it (Guidelines.md section 12). The part every API shares: the game's
// declaration, the settings, and the decisions made from them.
//
// Mirror: the game keeps the real swapchain (Luma's hooks are on it, so NvAPI, Streamline and overlays see the real object) but its
// back buffers are Luma's textures in the size and format it asked for. Display composition is the only writer of the real buffers,
// so the output mode (format, color space) changes at any present. InPlace: 1.0's upgrade, the game gets the real buffers in the
// output mode's format, and composition reads a copy of them; the output mode changes at the game's next resize.
namespace Luma {
namespace Swapchain {
enum class Mode : uint8_t
{
   Mirror,
   InPlace,
};

enum class OutputMode : uint8_t
{
   Sdr, // 8 bit (10 if the game asked for it), sRGB encoded
   Hdr10, // R10G10B10A2, PQ BT.2020
   ScRgb, // R16G16B16A16_FLOAT, linear BT.709
};

// Game::Options::swapchain: fixed for the process
struct Options
{
   // InPlace for a game ported from 1.0 that nobody verified Mirror on yet
   Mode mode = Mode::Mirror;
   // What HDR presents as unless the user picks: most 1.0 games were made for scRGB
   OutputMode hdr_output = OutputMode::Hdr10;
   // 1.0's prevent_fullscreen_state: the game's exclusive fullscreen becomes a borderless window
   bool borderless = true;
};

// Init only (Game::SetOptions)
void SetOptions(const Options& options);
const Options& GetOptions();
// Its settings, before the game's Init
void Register();

// From the settings over the game's options, read when a swapchain is created
Mode GetMode();
bool UsesFlipModel();
bool AllowsTearing();
// Read at every fullscreen change
bool IsBorderless();
bool SetsHdrMetadata();
// The real buffers' size over the game's: 1 but in Development (Luma.Development.SwapchainScale)
float GetScale();
// The output mode the user picked, or the game's HDR one (Auto) when the display has HDR on. SDR when it hasn't, or without the
// flip model.
OutputMode GetOutputMode(bool hdr_display);
// The user picked HDR10 or scRGB (not Auto): Windows HDR is turned on for it (1.0's display mode switch)
bool RequestsHdr();

std::string_view GetName(Mode mode);
std::string_view GetName(OutputMode mode);
} // namespace Swapchain
} // namespace Luma
