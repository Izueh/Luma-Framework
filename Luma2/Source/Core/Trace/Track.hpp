#pragma once

#if DEVELOPMENT
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Common/Api.hpp"

// 1.0's constant buffer tracking: one cbuffer slot of one shader, read again every frame after a capture, so its live values and
// what changes in them can be watched. Development builds; the draw path asks IsActive() (a relaxed load) and does nothing else
// while nothing is tracked.
namespace Luma {
namespace Trace {
namespace Track {
struct Target
{
   uint32_t hash = 0;
   ShaderStage stage = ShaderStage::Pixel;
   uint32_t slot = 0;
   // 1.0's target instance: the n-th call of the shader (at `stage`) to reach Luma's draw or dispatch hook in the frame, present to
   // present, across all contexts in the order the hooks ran. That is the order of the captured list, Luma's passes not counted; on
   // a deferred context it is the recording order, not the execution order.
   uint32_t instance = 0;

   bool operator==(const Target&) const = default;
};

struct Reading
{
   uint64_t frame = 0; // Of the tracking, counted from when it started
   std::string buffer; // The buffer's address
   uint32_t first_constant = 0; // The D3D11.1 range, 0 when the whole buffer is bound
   uint32_t constant_count = 0;
   bool deferred = false; // Copied on a deferred context: the contents are those of the last execution read
   std::vector<std::byte> contents; // The bound range
   // Per 4 bytes of `contents`: the frame they last changed, 0 if never seen changing
   std::vector<uint64_t> changed;
   uint32_t calls = 0; // The shader's calls seen in the last frame
   // The last frame didn't read the slot (the call didn't happen, or its deferred command list wasn't executed yet): these are older
   bool stale = false;
};

// Presenting thread (the overlay): tracks `target` from the next frame, or nothing
void Set(std::optional<Target> target);
std::optional<Target> Get();
// Any thread, on the draw path
bool IsActive();

// Any thread, on the draw path: the instance number of this call of the tracked shader in the frame
uint32_t CountCall();
// Presenting thread, before the overlay draws: the contents read back for this frame, or none if the call didn't happen
void SetReading(std::optional<Reading> reading);
// The last reading, empty before the first
std::optional<Reading> GetReading();
} // namespace Track
} // namespace Trace
} // namespace Luma
#endif
