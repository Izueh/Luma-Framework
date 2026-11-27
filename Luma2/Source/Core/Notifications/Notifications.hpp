#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// What Luma tells the person playing or developing, from any layer and the game. Toasts are queued here and the overlay only draws
// them; asserts are delivered as 1.0 did (a message box, or a toast in silent mode). Warnings, errors and asserts are also logged.
namespace Luma {
namespace Notifications {
enum class Kind : uint8_t
{
   Info,
   Success,
   Warning,
   Error,
};

using Duration = std::chrono::milliseconds;
// Info and Success
inline constexpr Duration SHORT = Duration{4000};
// Warning and Error: long enough to read a sentence or two
inline constexpr Duration LONG = Duration{10000};
// The oldest go first past this
inline constexpr size_t MAX_SHOWN = 5;

// Any thread, also under the loader lock. A duration of zero takes the kind's.
void Post(Kind kind, std::string message, Duration duration = Duration::zero());

// 1.0's AVOID_INPUT_LOSS / PREFER_OVERLAY_MESSAGE_ASSERTS: asserts show as error toasts instead of a message box, for games that
// lose input or crash when a window takes the focus
struct Options
{
   bool silent_asserts = false;
};

void SetSilentAsserts(bool silent);
bool AreAssertsSilent();
// Tests and other unattended runs: silent from now on, whatever the options and settings say (no box waits for a click)
void ForceSilentAsserts();

// The assert macros (Utils/Debug.hpp). Logged, then: silent, an error toast; otherwise 1.0's message box, Yes breaks into the
// debugger (with a message), or OK only (without one; the CRT's assert dialog in _DEBUG builds).
void Assert(std::string_view expression, std::string_view message, const char* file, int line);

struct Toast
{
   uint64_t id = 0;
   Kind kind = Kind::Info;
   std::string message;
   // Since it was posted, or last hovered
   std::chrono::steady_clock::time_point since;
   Duration duration = SHORT;
   bool copied = false;
};

// For the overlay, each frame: the toasts still up, oldest first (expired ones are dropped here)
std::vector<Toast> GetToasts();
bool HasToasts();
// The pointer is over it: it stays up for its whole duration after the pointer leaves
void Hold(uint64_t id);
// Its text was copied to the clipboard
void MarkCopied(uint64_t id);
// Tests
void ClearToasts();
} // namespace Notifications
} // namespace Luma
