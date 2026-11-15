#include "Trace/Track.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

namespace Luma {
namespace Trace {
namespace Track {
namespace {
std::atomic<bool> active = false;
std::atomic<uint32_t> calls = 0;
// Never held with another lock
std::mutex mutex;
std::optional<Target> target;
std::optional<Reading> last;
uint64_t frame = 0;
} // namespace

void Set(std::optional<Target> value)
{
   std::scoped_lock lock(mutex);
   if (target == value)
      return;
   target = value;
   last.reset();
   frame = 0;
   calls = 0;
   active = target.has_value();
}

std::optional<Target> Get()
{
   std::scoped_lock lock(mutex);
   return target;
}

bool IsActive()
{
   return active.load(std::memory_order_relaxed);
}

uint32_t CountCall()
{
   return calls.fetch_add(1, std::memory_order_relaxed);
}

void SetReading(std::optional<Reading> reading)
{
   const uint32_t counted = calls.exchange(0, std::memory_order_relaxed);
   std::scoped_lock lock(mutex);
   if (!target)
      return;
   frame++;
   if (!reading)
   {
      if (last)
      {
         last->calls = counted;
         last->stale = true;
      }
      return;
   }
   reading->frame = frame;
   reading->calls = counted;
   reading->changed.assign((reading->contents.size() + 3) / 4, 0);
   // A word that differs from the last reading changed this frame, the others keep when they last did
   if (last && last->contents.size() == reading->contents.size())
   {
      for (size_t word = 0; word < reading->changed.size(); word++)
      {
         const size_t offset = word * 4;
         const size_t size = std::min<size_t>(4, reading->contents.size() - offset);
         reading->changed[word] = std::memcmp(reading->contents.data() + offset, last->contents.data() + offset, size) != 0 ? frame : last->changed[word];
      }
   }
   last = std::move(reading);
}

std::optional<Reading> GetReading()
{
   std::scoped_lock lock(mutex);
   return last;
}

uint64_t GetFrame()
{
   std::scoped_lock lock(mutex);
   return frame;
}
} // namespace Track
} // namespace Trace
} // namespace Luma
#endif
