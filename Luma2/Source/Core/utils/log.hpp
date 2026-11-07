#pragma once

#include <filesystem>
#include <format>
#include <string_view>

namespace Luma {
namespace Utils {
namespace Log {
enum class Level
{
   Verbose,
   Info,
   Warning,
   Error,
};

// Lower levels are dropped before anything is formatted: Verbose calls compile to nothing outside Development builds
#if DEVELOPMENT
inline constexpr Level MIN_LEVEL = Level::Verbose;
#else
inline constexpr Level MIN_LEVEL = Level::Info;
#endif

// Truncates the file. Earlier lines are kept and flushed into it. Safe under the loader lock.
void Open(const std::filesystem::path& path);
void Close();

// Each line is flushed to the OS (not written through to the disk), so it survives the game crashing
void Write(Level level, std::string_view message);

template <Level level, typename... Args>
void Format(std::format_string<Args...> format, Args&&... args)
{
   if constexpr (level >= MIN_LEVEL)
      Write(level, std::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void Verbose(std::format_string<Args...> format, Args&&... args)
{
   Format<Level::Verbose>(format, std::forward<Args>(args)...);
}
template <typename... Args>
void Info(std::format_string<Args...> format, Args&&... args)
{
   Format<Level::Info>(format, std::forward<Args>(args)...);
}
template <typename... Args>
void Warning(std::format_string<Args...> format, Args&&... args)
{
   Format<Level::Warning>(format, std::forward<Args>(args)...);
}
template <typename... Args>
void Error(std::format_string<Args...> format, Args&&... args)
{
   Format<Level::Error>(format, std::forward<Args>(args)...);
}
} // namespace Log
} // namespace Utils
} // namespace Luma
