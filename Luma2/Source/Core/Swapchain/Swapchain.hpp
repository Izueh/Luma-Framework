#pragma once

#include <cstdint>
#include <string_view>

namespace Luma {
namespace Swapchain {
enum class Mode : uint8_t
{
   Mirror, // The game draws to Luma's textures in its own size and format; composition alone writes the real buffers, so the output mode changes at any present
   InPlace, // The game draws to the real buffers in the output mode's format and composition reads a copy; the output mode changes at the game's next resize
   Native, // InPlace's presentation (flip model, tearing, borderless) with the game's (or a tool's above Luma) format and color space; nothing composed, only the overlay drawn
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
   // InPlace for a game where Mirror isn't verified yet, Native for a mod that leaves HDR to a ReShade add-on
   Mode mode = Mode::Mirror;
   // What HDR presents as unless the user picks
   OutputMode hdr_output = OutputMode::Hdr10;
   // The game's exclusive fullscreen becomes a borderless window
   bool borderless = true;
};

// Init only (Game::SetOptions)
void SetOptions(const Options& options);
const Options& GetOptions();
// Its settings, before the game's Init
void Register();

// From the settings over the game's options, read when a swapchain is created
Mode GetMode();
// Luma picks the real buffers' format, color space and output mode, and composition writes them: all but Native, where they're the game's
bool ManagesOutput(Mode mode);
bool UsesFlipModel();
bool AllowsTearing();
// The game gets a proxy of the swapchain (Luma.Swapchain.Wrap, Test builds): Luma presents before every other tool's Present hook
bool Wraps();
// Read at every fullscreen change
bool IsBorderless();
// Focus-loss window messages hidden from the game's window so Alt-Tab doesn't pause it
bool SuppressesFocusLoss();
bool SetsHdrMetadata();
// The real buffers' size over the game's: 1 but in Development (Luma.Development.SwapchainScale)
float GetScale();
// The output mode the user picked, or the game's HDR one (Auto) when the display has HDR on. SDR when it hasn't, or without the
// flip model.
OutputMode GetOutputMode(bool hdr_display);
// The user picked HDR10 or scRGB (not Auto): Windows HDR is turned on for it
bool RequestsHdr();

std::string_view GetName(Mode mode);
std::string_view GetName(OutputMode mode);
} // namespace Swapchain
} // namespace Luma
