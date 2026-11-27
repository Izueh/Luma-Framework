#include "Notifications/Notifications.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <format>
#include <mutex>

#include <Windows.h>
#if _DEBUG
#include <crtdbg.h>
#endif

#include "Log/Log.hpp"

namespace Luma {
namespace Notifications {
namespace {
using Clock = std::chrono::steady_clock;

std::atomic<bool> silent_asserts = false;
std::atomic<bool> forced_silent = false;

// Never held with another lock
std::mutex mutex;
std::deque<Toast> toasts;
uint64_t next_id = 1;

Toast* Find(uint64_t id)
{
   const auto it = std::ranges::find(toasts, id, &Toast::id);
   return it != toasts.end() ? &*it : nullptr;
}

void Queue(Kind kind, std::string message, Duration duration)
{
   if (duration == Duration::zero())
      duration = kind == Kind::Warning || kind == Kind::Error ? LONG : SHORT;
   std::scoped_lock lock(mutex);
   toasts.push_back({.id = next_id++, .kind = kind, .message = std::move(message), .since = Clock::now(), .duration = duration});
   while (toasts.size() > MAX_SHOWN)
      toasts.pop_front();
}
} // namespace

void Post(Kind kind, std::string message, Duration duration)
{
   if (kind == Kind::Warning)
      Log::Warning("{}", message);
   else if (kind == Kind::Error)
      Log::Error("{}", message);
   Queue(kind, std::move(message), duration);
}

void SetSilentAsserts(bool silent)
{
   silent_asserts.store(silent, std::memory_order_relaxed);
}

bool AreAssertsSilent()
{
   return forced_silent.load(std::memory_order_relaxed) || silent_asserts.load(std::memory_order_relaxed);
}

void ForceSilentAsserts()
{
   forced_silent.store(true, std::memory_order_relaxed);
}

void Assert(std::string_view expression, std::string_view message, const char* file, int line)
{
   Log::Error("Assert: {} ({}) at {}:{}", message.empty() ? expression : message, expression, file, line);
   if (AreAssertsSilent())
   {
      Queue(Kind::Error, std::format("Assertion failed: {}\n{}{}File: {}\nLine: {}", expression, message, message.empty() ? "" : "\n", file, line), LONG);
      return;
   }
   // As 1.0's ASSERT: the CRT's dialog in debug builds (Retry breaks), an OK box otherwise
   if (message.empty())
   {
#if _DEBUG
      if (_CrtDbgReport(_CRT_ASSERT, file, line, nullptr, "%.*s", int(expression.size()), expression.data()) == 1)
         __debugbreak();
#else
      const std::string text = std::format("Assertion failed: {}\nFile: {}\nLine: {}", expression, file, line);
      MessageBoxA(nullptr, text.c_str(), "Luma", MB_SETFOREGROUND | MB_OK);
#endif
      return;
   }
   // As 1.0's ASSERT_MSG
   const std::string text = std::format("Assertion failed:\n\n{}\n\n{}\n\nFile: {}\nLine: {}\n\nPress Yes to break into debugger.\nPress No to continue.",
      expression, message, file, line);
   if (MessageBoxA(nullptr, text.c_str(), "Assertion Failed", MB_ICONERROR | MB_YESNO) == IDYES)
      __debugbreak();
}

std::vector<Toast> GetToasts()
{
   std::scoped_lock lock(mutex);
   const Clock::time_point now = Clock::now();
   std::erase_if(toasts, [now](const Toast& toast) { return now - toast.since > toast.duration; });
   return {toasts.begin(), toasts.end()};
}

bool HasToasts()
{
   std::scoped_lock lock(mutex);
   return !toasts.empty();
}

void Hold(uint64_t id)
{
   std::scoped_lock lock(mutex);
   if (Toast* toast = Find(id))
      toast->since = Clock::now();
}

void MarkCopied(uint64_t id)
{
   std::scoped_lock lock(mutex);
   if (Toast* toast = Find(id))
      toast->copied = true;
}

void ClearToasts()
{
   std::scoped_lock lock(mutex);
   toasts.clear();
}
} // namespace Notifications
} // namespace Luma
