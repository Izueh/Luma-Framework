#include "System/Jobs.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "Utils/Log.hpp"

namespace Luma {
namespace System {
namespace Jobs {
namespace {
struct Job
{
   std::string name;
   std::function<void()> work;
   std::function<Outcome()> done;
   uint32_t progress_done = 0;
   uint32_t progress_total = 0;
};

struct State
{
   // Never held with another lock: work and done run without it
   std::mutex mutex;
   uint32_t queued = 0;
   // In the order they started
   std::vector<Job*> running;
   std::vector<std::unique_ptr<Job>> finished;
   Report last_report;

   std::once_flag pool_created;
   std::atomic<PTP_POOL> pool = nullptr;
   // One environment per priority, the Priority's value indexing them
   TP_CALLBACK_ENVIRON environments[2] = {};
   std::atomic<uint32_t> threads = 1;
};

// Leaked: a worker may still be running while the dll unloads
State& GetState()
{
   static State* state = new State();
   return *state;
}

// The job this worker is running
thread_local Job* current_job = nullptr;

void ApplyThreadCount(State& state)
{
   if (PTP_POOL pool = state.pool.load())
      SetThreadpoolThreadMaximum(pool, state.threads.load());
}

void CreatePool(State& state)
{
   PTP_POOL pool = CreateThreadpool(nullptr);
   if (!pool)
   {
      Utils::Log::Warning("Jobs: the thread pool couldn't be created ({}), a thread runs each job", GetLastError());
      return;
   }
   for (TP_CALLBACK_ENVIRON& environment : state.environments)
   {
      InitializeThreadpoolEnvironment(&environment);
      SetThreadpoolCallbackPool(&environment, pool);
   }
   SetThreadpoolCallbackPriority(&state.environments[size_t(Priority::Normal)], TP_CALLBACK_PRIORITY_NORMAL);
   SetThreadpoolCallbackPriority(&state.environments[size_t(Priority::Low)], TP_CALLBACK_PRIORITY_LOW);
   SetThreadpoolThreadMaximum(pool, state.threads.load());
   state.pool = pool;
   // A count set while the pool was being made
   ApplyThreadCount(state);
   Utils::Log::Info("Jobs: Win32 thread pool, up to {} threads", state.threads.load());
}

void CALLBACK Run(PTP_CALLBACK_INSTANCE, void* context)
{
   State& state = GetState();
   std::unique_ptr<Job> job(static_cast<Job*>(context));
   {
      std::scoped_lock lock(state.mutex);
      state.queued--;
      state.running.push_back(job.get());
   }
   // Below the game's own threads. Set at each job: the pool's threads come and go.
   SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
   current_job = job.get();
   job->work();
   current_job = nullptr;
   std::scoped_lock lock(state.mutex);
   std::erase(state.running, job.get());
   state.finished.push_back(std::move(job));
}
} // namespace

void Queue(std::string name, std::function<void()> work, std::function<Outcome()> done, Priority priority)
{
   State& state = GetState();
   // The pool's threads start on the first job, which may be under the loader lock: they only run once it's released.
   std::call_once(state.pool_created, &CreatePool, std::ref(state));
   auto* job = new Job{std::move(name), std::move(work), std::move(done)};
   {
      std::scoped_lock lock(state.mutex);
      state.queued++;
   }
   if (state.pool.load() && TrySubmitThreadpoolCallback(&Run, job, &state.environments[size_t(priority)]))
      return;
   // No pool: never joined, like the MCP server's
   std::thread([job] { Run(nullptr, job); }).detach();
}

void SetProgress(uint32_t done, uint32_t total)
{
   if (!current_job)
      return;
   std::scoped_lock lock(GetState().mutex);
   current_job->progress_done = done;
   current_job->progress_total = total;
}

void RunFinished()
{
   State& state = GetState();
   std::vector<std::unique_ptr<Job>> jobs;
   {
      std::scoped_lock lock(state.mutex);
      if (state.finished.empty())
         return;
      jobs.swap(state.finished);
   }
   for (const std::unique_ptr<Job>& job : jobs)
   {
      Outcome outcome = job->done();
      if (outcome.message.empty())
         continue;
      std::scoped_lock lock(state.mutex);
      state.last_report = {state.last_report.id + 1, std::move(outcome)};
   }
}

void SetThreadCount(uint32_t count)
{
   State& state = GetState();
   state.threads = std::max(count, 1u);
   ApplyThreadCount(state);
}

Status GetStatus()
{
   State& state = GetState();
   std::scoped_lock lock(state.mutex);
   Status status;
   if (!state.running.empty())
   {
      const Job& oldest = *state.running.front();
      status.name = oldest.name;
      status.done = oldest.progress_done;
      status.total = oldest.progress_total;
   }
   status.running = uint32_t(state.running.size());
   status.queued = state.queued;
   status.threads = state.threads;
   return status;
}

Report GetLastReport()
{
   State& state = GetState();
   std::scoped_lock lock(state.mutex);
   return state.last_report;
}
} // namespace Jobs
} // namespace System
} // namespace Luma
