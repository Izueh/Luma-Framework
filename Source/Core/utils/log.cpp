#include "utils/log.hpp"

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include <Windows.h>

namespace Luma {
namespace Utils {
namespace Log {
namespace {
std::mutex mutex;
std::FILE* file = nullptr;
std::vector<std::string> pending_lines;

constexpr std::string_view LevelName(Level level)
{
   switch (level)
   {
   case Level::Verbose: return "VERBOSE";
   case Level::Info: return "INFO";
   case Level::Warning: return "WARNING";
   case Level::Error: return "ERROR";
   }
   return "?";
}
} // namespace

void Open(const std::filesystem::path& path)
{
   std::scoped_lock lock(mutex);
   if (file)
      return;
   file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
   if (!file)
      return;
   for (const std::string& line : pending_lines)
      std::fputs(line.c_str(), file);
   pending_lines.clear();
   pending_lines.shrink_to_fit();
   std::fflush(file);
}

void Close()
{
   std::scoped_lock lock(mutex);
   if (file)
   {
      std::fclose(file);
      file = nullptr;
   }
}

void Write(Level level, std::string_view message)
{
   if (level < MIN_LEVEL)
      return;
   // Not std::chrono: the time zone database would load a dll, and this can run under the loader lock
   SYSTEMTIME time;
   GetLocalTime(&time);
   const std::string line = std::format("{:02}:{:02}:{:02}.{:03} [{}] [{}] {}\n", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
      GetCurrentThreadId(), LevelName(level), message);

#if _DEBUG
   OutputDebugStringA(line.c_str());
#endif

   std::scoped_lock lock(mutex);
   if (!file)
   {
      pending_lines.push_back(line);
      return;
   }
   std::fputs(line.c_str(), file);
   std::fflush(file);
}
} // namespace Log
} // namespace Utils
} // namespace Luma
