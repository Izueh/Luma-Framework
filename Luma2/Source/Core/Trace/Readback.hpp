#pragma once

#if DEVELOPMENT
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Common/ShaderStage.hpp"

// 1.0's luma_read_resource: one view of one call read back right after the call ran, or the back buffer at present. One readback
// at a time: the pipe thread arms a target and waits, the draw path copies the view at the target call (D3D11's and D3D12's parts),
// the present reads the copy and finishes it. Development builds; the draw path asks IsArmed() (a relaxed load) and does nothing
// else while nothing is armed.
namespace Luma {
namespace Trace {
namespace Readback {
enum class ViewKind : uint8_t
{
   RenderTarget,
   ShaderResource,
   UnorderedAccess,
   Depth,
   Stencil,
   Swapchain, // The back buffer at present, before Luma's overlay
};

struct Target
{
   ViewKind view = ViewKind::RenderTarget;
   // The call: the n-th call of the shader at `stage` in the frame (Track's instance), unused for the swapchain
   uint32_t hash = 0;
   ShaderStage stage = ShaderStage::Pixel;
   uint32_t instance = 0;
   uint32_t slot = 0;
   uint32_t mip = 0;
   // A mirrored view: read the game's resource instead of Luma's mirror of it
   bool original = false;
};

// One mip of a texture, every array or depth slice, as rows of `row_pitch` bytes ordered [slice][z][row] (1.0's .bin layout)
struct Texture
{
   std::string dimension; // "1d", "2d", "3d"
   uint32_t width = 1;
   uint32_t height = 1;
   uint32_t depth = 1;
   uint32_t slices = 1;
   uint32_t mip = 0;
   uint32_t mips = 1;
   uint32_t samples = 1;
   uint32_t format = 0; // DXGI_FORMAT of the resource read
   uint32_t view_format = 0; // How to interpret the data
   uint32_t row_pitch = 0;
   uint32_t rows = 0;
   bool block_compressed = false;
   std::string resource; // The game's resource the view names
   bool mirrored = false; // Read from Luma's mirror of it
   std::vector<std::byte> bytes;
};

struct Result
{
   std::optional<Texture> texture;
   std::string error;
};

// Pipe thread: arms `target`, waits for the result until `deadline`, disarms
Result Read(const Target& target, std::chrono::steady_clock::time_point deadline);

// Any thread, on the draw path
bool IsArmed();
// The armed target, if any
std::optional<Target> GetTarget();
// Any thread, on the draw path, for a call of the armed shader at its stage: whether it is the armed instance
bool CountCall();
// Any thread: the API part copied the view at the call, the result comes with a later present
void MarkCopied();
// Presenting thread: the result of the armed readback
void Finish(Result result);
// Presenting thread, after the API part read its copies: a pass not reached in a few frames is reported missing
void OnPresent();

// The view kind's name as luma_read_resource takes it ("rtv", "srv", "uav", "depth", "stencil", "swapchain")
std::string_view GetName(ViewKind view);
std::optional<ViewKind> ParseViewKind(std::string_view name);
} // namespace Readback
} // namespace Trace
} // namespace Luma
#endif
