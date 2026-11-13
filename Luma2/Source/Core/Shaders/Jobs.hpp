#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Work that mustn't stall the game (compiling, dumping, later patching and pipeline variants): run one job at a time on
// Luma's worker thread, in queue order (FXC isn't thread safe), with its result handed back to the presenting thread.
namespace Luma {
namespace Shaders {
namespace Jobs {
struct Outcome
{
   bool failed = false;
   // Shown as a toast. Empty for a job that has nothing to report (a dump).
   std::string message;
};

// `work` runs on the worker thread, `done` on the presenting thread at the first present after the work finished. Any thread.
void Queue(std::string name, std::function<void()> work, std::function<Outcome()> done);
// From inside a job's work
void SetProgress(uint32_t done, uint32_t total);
// Presenting thread: the `done` of the jobs whose work finished
void RunFinished();

struct Status
{
   std::string name; // Of the running job, empty when idle
   uint32_t done = 0;
   uint32_t total = 0;
   uint32_t queued = 0; // Waiting after it
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
} // namespace Shaders
} // namespace Luma
