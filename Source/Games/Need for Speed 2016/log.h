#pragma once

using LogLevel = Luma::Utils::Log::Level;

// Logs to Luma.log in Development and Test builds. Publishing builds neither format nor keep the messages.
template <typename... Args>
void LogDiagnostic(LogLevel level, std::format_string<Args...> format, Args&&... args)
{
#if DEVELOPMENT || TEST
   Luma::Utils::Log::Write(level, "NFS: " + std::format(format, std::forward<Args>(args)...));
#endif
}
