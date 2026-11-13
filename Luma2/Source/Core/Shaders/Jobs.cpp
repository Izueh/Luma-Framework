#include "Shaders/Jobs.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace Luma {
namespace Shaders {
namespace Jobs {
namespace {
struct Job
{
   std::string name;
   std::function<void()> work;
   std::function<Outcome()> done;
};

struct State
{
   // Never held with another lock: work and done run without it
   std::mutex mutex;
   std::condition_variable wake;
   std::deque<Job> queued;
   std::vector<Job> finished;
   Status status;
   Report last_report;
   bool worker_started = false;
};

// Leaked: the worker may still wait on it while the dll unloads
State& GetState()
{
   static State* state = new State();
   return *state;
}

void Work()
{
   State& state = GetState();
   for (;;)
   {
      Job job;
      {
         std::unique_lock lock(state.mutex);
         state.wake.wait(lock, [&] { return !state.queued.empty(); });
         job = std::move(state.queued.front());
         state.queued.pop_front();
         state.status = {.name = job.name};
      }
      job.work();
      std::scoped_lock lock(state.mutex);
      state.status = {};
      state.finished.push_back(std::move(job));
   }
}
} // namespace

void Queue(std::string name, std::function<void()> work, std::function<Outcome()> done)
{
   State& state = GetState();
   std::scoped_lock lock(state.mutex);
   state.queued.push_back({std::move(name), std::move(work), std::move(done)});
   // Started on first use, which may be under the loader lock: the thread only runs once it's released. Never joined, like
   // the MCP server's.
   if (!state.worker_started)
   {
      state.worker_started = true;
      std::thread(&Work).detach();
   }
   state.wake.notify_one();
}

void SetProgress(uint32_t done, uint32_t total)
{
   State& state = GetState();
   std::scoped_lock lock(state.mutex);
   state.status.done = done;
   state.status.total = total;
}

void RunFinished()
{
   State& state = GetState();
   std::vector<Job> jobs;
   {
      std::scoped_lock lock(state.mutex);
      if (state.finished.empty())
         return;
      jobs.swap(state.finished);
   }
   for (Job& job : jobs)
   {
      Outcome outcome = job.done();
      if (outcome.message.empty())
         continue;
      std::scoped_lock lock(state.mutex);
      state.last_report = {state.last_report.id + 1, std::move(outcome)};
   }
}

Status GetStatus()
{
   State& state = GetState();
   std::scoped_lock lock(state.mutex);
   Status status = state.status;
   status.queued = uint32_t(state.queued.size());
   return status;
}

Report GetLastReport()
{
   State& state = GetState();
   std::scoped_lock lock(state.mutex);
   return state.last_report;
}
} // namespace Jobs
} // namespace Shaders
} // namespace Luma
