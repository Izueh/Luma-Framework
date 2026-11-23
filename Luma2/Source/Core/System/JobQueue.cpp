#include "System/JobQueue.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <concurrentqueue.h>

#include "Log/Log.hpp"

namespace Luma {
namespace System {
namespace Jobs {
namespace {
struct Job
{
   std::string name;
   std::function<void()> work;
   std::function<Outcome()> done;
   std::atomic<uint32_t> progress_done = 0;
   std::atomic<uint32_t> progress_total = 0;
};

struct State
{
   std::atomic<uint32_t> queued = 0;
   // Active workers (including the brief handoff into `finished`), so a reader seeing running == 0 && queued == 0 sees every finished job
   std::atomic<uint32_t> active = 0;
   // Protects only the running list (for GetStatus's oldest-job display); never held on Queue, SetProgress, or RunFinished
   std::mutex running_mutex;
   std::vector<Job*> running;
   // Lock-free MPSC queue: workers enqueue completed jobs, the presenting thread drains them without locking
   moodycamel::ConcurrentQueue<std::unique_ptr<Job>> finished;
   std::mutex report_mutex;
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
   if (PTP_POOL pool = state.pool.load(std::memory_order_acquire))
      SetThreadpoolThreadMaximum(pool, state.threads.load(std::memory_order_relaxed));
}

void CreatePool(State& state)
{
   PTP_POOL pool = CreateThreadpool(nullptr);
   if (!pool)
   {
      Log::Warning("Jobs: the thread pool couldn't be created ({}), a thread runs each job", GetLastError());
      return;
   }
   for (TP_CALLBACK_ENVIRON& environment : state.environments)
   {
      InitializeThreadpoolEnvironment(&environment);
      SetThreadpoolCallbackPool(&environment, pool);
   }
   SetThreadpoolCallbackPriority(&state.environments[size_t(Priority::Normal)], TP_CALLBACK_PRIORITY_NORMAL);
   SetThreadpoolCallbackPriority(&state.environments[size_t(Priority::Low)], TP_CALLBACK_PRIORITY_LOW);
   SetThreadpoolThreadMaximum(pool, state.threads.load(std::memory_order_relaxed));
   state.pool.store(pool, std::memory_order_release);
   // A count set while the pool was being made
   ApplyThreadCount(state);
   Log::Info("Jobs: Win32 thread pool, up to {} threads", state.threads.load(std::memory_order_relaxed));
}

void CALLBACK Run(PTP_CALLBACK_INSTANCE, void* context)
{
   State& state = GetState();
   std::unique_ptr<Job> job(static_cast<Job*>(context));
   state.active.fetch_add(1, std::memory_order_acq_rel);
   {
      std::scoped_lock lock(state.running_mutex);
      state.running.push_back(job.get());
   }
   state.queued.fetch_sub(1, std::memory_order_release);
   // Below the game's own threads. Set at each job: the pool's threads come and go.
   SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
   current_job = job.get();
   job->work();
   current_job = nullptr;
   {
      std::scoped_lock lock(state.running_mutex);
      std::erase(state.running, job.get());
   }
   state.finished.enqueue(std::move(job));
   state.active.fetch_sub(1, std::memory_order_release);
}
} // namespace

void Queue(std::string name, std::function<void()> work, std::function<Outcome()> done, Priority priority)
{
   State& state = GetState();
   // The pool's threads start on the first job, which may be under the loader lock: they only run once it's released.
   std::call_once(state.pool_created, &CreatePool, std::ref(state));
   auto* job = new Job{std::move(name), std::move(work), std::move(done)};
   state.queued.fetch_add(1, std::memory_order_acq_rel);
   if (state.pool.load(std::memory_order_acquire) && TrySubmitThreadpoolCallback(&Run, job, &state.environments[size_t(priority)]))
      return;
   // No pool: never joined, like the MCP server's
   std::thread([job] { Run(nullptr, job); }).detach();
}

void SetProgress(uint32_t done, uint32_t total)
{
   if (!current_job)
      return;
   current_job->progress_done.store(done, std::memory_order_relaxed);
   current_job->progress_total.store(total, std::memory_order_relaxed);
}

void RunFinished()
{
   State& state = GetState();
   std::unique_ptr<Job> job;
   while (state.finished.try_dequeue(job))
   {
      Outcome outcome = job->done();
      if (outcome.message.empty())
         continue;
      std::scoped_lock lock(state.report_mutex);
      state.last_report = {state.last_report.id + 1, std::move(outcome)};
   }
}

void SetThreadCount(uint32_t count)
{
   State& state = GetState();
   state.threads.store(std::max(count, 1u), std::memory_order_relaxed);
   ApplyThreadCount(state);
}

Status GetStatus()
{
   State& state = GetState();
   Status status;
   {
      std::scoped_lock lock(state.running_mutex);
      if (!state.running.empty())
      {
         const Job& oldest = *state.running.front();
         status.name = oldest.name;
         status.done = oldest.progress_done.load(std::memory_order_relaxed);
         status.total = oldest.progress_total.load(std::memory_order_relaxed);
      }
   }
   status.running = state.active.load(std::memory_order_acquire);
   status.queued = state.queued.load(std::memory_order_acquire);
   status.threads = state.threads.load(std::memory_order_relaxed);
   return status;
}

Report GetLastReport()
{
   State& state = GetState();
   std::scoped_lock lock(state.report_mutex);
   return state.last_report;
}
} // namespace Jobs
} // namespace System
} // namespace Luma
