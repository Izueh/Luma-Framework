#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Work that mustn't stall the game (compiling, dumping, later pipelines, patching and file loading): jobs on a private Win32
// thread pool, with their result handed back to the presenting thread.
namespace Luma {
namespace System {
namespace Jobs {
struct Outcome
{
   bool failed = false;
   // Posted as a notification (a toast). Empty for a job that has nothing to report (a dump).
   std::string message;
};

// Low runs only when no Normal job waits
enum class Priority : uint8_t
{
   Normal,
   Low,
};

// `work` runs on a worker, `done` on the presenting thread at the first present after the work finished. Any thread. A job that
// calls the graphics API opens a Hooks::Scope::Guard in `work`.
void Queue(std::string name, std::function<void()> work, std::function<Outcome()> done, Priority priority = Priority::Normal);
// From inside a job's work, for that job
void SetProgress(uint32_t done, uint32_t total);
// Presenting thread: the `done` of the jobs whose work finished
void RunFinished();
// Presenting thread: at most this many jobs run at once (at least 1)
void SetThreadCount(uint32_t count);

struct Status
{
   std::string name; // Of the oldest running job, empty when idle
   uint32_t done = 0; // Its progress
   uint32_t total = 0;
   uint32_t running = 0;
   uint32_t queued = 0; // Waiting for a worker
   uint32_t threads = 0; // The most that run at once
};

// Any thread
Status GetStatus();

struct Report
{
   uint64_t id = 0; // Counts the reports
   Outcome outcome;
};

// Any thread: the last outcome with a message
Report GetLastReport();
} // namespace Jobs
} // namespace System
} // namespace Luma
