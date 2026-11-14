#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "System/Jobs.hpp"

namespace Jobs = Luma::System::Jobs;

namespace {
template <typename Predicate>
bool WaitFor(Predicate predicate)
{
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
   while (!predicate())
   {
      if (std::chrono::steady_clock::now() > deadline)
         return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   return true;
}

// Every job finished and its done run, so the next test starts clean
void Drain()
{
   REQUIRE(WaitFor([] {
      const Jobs::Status status = Jobs::GetStatus();
      return status.running == 0 && status.queued == 0;
   }));
   Jobs::RunFinished();
}
} // namespace

TEST_CASE("Jobs: work runs on a worker, done on the thread that calls RunFinished")
{
   Jobs::SetThreadCount(2);
   std::atomic<bool> worked = false;
   std::thread::id worker;
   std::thread::id finisher;
   bool done_ran = false;
   Jobs::Queue(
      "Test",
      [&] {
         worker = std::this_thread::get_id();
         worked = true;
      },
      [&] {
         finisher = std::this_thread::get_id();
         done_ran = true;
         return Jobs::Outcome{false, "finished"};
      });
   REQUIRE(WaitFor([&] { return worked.load(); }));
   CHECK_FALSE(done_ran);
   // The work may not have returned yet
   REQUIRE(WaitFor([&] {
      Jobs::RunFinished();
      return done_ran;
   }));
   CHECK(worker != std::this_thread::get_id());
   CHECK(finisher == std::this_thread::get_id());
   const Jobs::Report report = Jobs::GetLastReport();
   CHECK(report.outcome.message == "finished");
   CHECK_FALSE(report.outcome.failed);
   Drain();
}

TEST_CASE("Jobs: a job with nothing to report leaves the last report alone")
{
   Jobs::SetThreadCount(1);
   const uint64_t before = Jobs::GetLastReport().id;
   bool done_ran = false;
   Jobs::Queue(
      "Quiet", [] {},
      [&] {
         done_ran = true;
         return Jobs::Outcome{};
      });
   REQUIRE(WaitFor([&] {
      Jobs::RunFinished();
      return done_ran;
   }));
   CHECK(Jobs::GetLastReport().id == before);
   Drain();
}

TEST_CASE("Jobs: a low priority job waits for the normal ones")
{
   Jobs::SetThreadCount(1);
   std::atomic<bool> release = false;
   std::atomic<bool> blocking = false;
   Jobs::Queue(
      "Blocker",
      [&] {
         blocking = true;
         while (!release)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
      },
      [] { return Jobs::Outcome{}; });
   REQUIRE(WaitFor([&] { return blocking.load(); }));

   std::mutex mutex;
   std::vector<std::string> order;
   const auto record = [&](std::string name) {
      return [&, name = std::move(name)] {
         std::scoped_lock lock(mutex);
         order.push_back(name);
      };
   };
   Jobs::Queue("Low", record("low"), [] { return Jobs::Outcome{}; }, Jobs::Priority::Low);
   Jobs::Queue("Normal", record("normal"), [] { return Jobs::Outcome{}; });

   const Jobs::Status status = Jobs::GetStatus();
   CHECK(status.name == "Blocker");
   CHECK(status.running == 1);
   CHECK(status.queued == 2);
   CHECK(status.threads == 1);

   release = true;
   Drain();
   std::scoped_lock lock(mutex);
   REQUIRE(order.size() == 2);
   CHECK(order[0] == "normal");
   CHECK(order[1] == "low");
}

TEST_CASE("Jobs: no more run at once than the thread count")
{
   Jobs::SetThreadCount(2);
   std::atomic<uint32_t> running = 0;
   std::atomic<uint32_t> peak = 0;
   for (uint32_t i = 0; i < 8; i++)
   {
      Jobs::Queue(
         "Count",
         [&] {
            const uint32_t now = ++running;
            uint32_t seen = peak;
            while (now > seen && !peak.compare_exchange_weak(seen, now))
            {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            --running;
         },
         [] { return Jobs::Outcome{}; });
   }
   Drain();
   CHECK(peak == 2);
}

TEST_CASE("Jobs: each job has its own progress")
{
   Jobs::SetThreadCount(2);
   std::atomic<bool> release = false;
   std::atomic<bool> first_set = false;
   std::atomic<bool> second_set = false;
   Jobs::Queue(
      "First",
      [&] {
         Jobs::SetProgress(1, 10);
         first_set = true;
         while (!release)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
      },
      [] { return Jobs::Outcome{}; });
   REQUIRE(WaitFor([&] { return first_set.load(); }));
   Jobs::Queue(
      "Second",
      [&] {
         Jobs::SetProgress(5, 6);
         second_set = true;
         while (!release)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
      },
      [] { return Jobs::Outcome{}; });
   REQUIRE(WaitFor([&] { return second_set.load(); }));

   // The oldest running job's, not the last one to report
   const Jobs::Status status = Jobs::GetStatus();
   CHECK(status.name == "First");
   CHECK(status.done == 1);
   CHECK(status.total == 10);
   CHECK(status.running == 2);

   // Not from a job: nothing to attach it to
   Jobs::SetProgress(9, 9);
   CHECK(Jobs::GetStatus().done == 1);

   release = true;
   Drain();
}
