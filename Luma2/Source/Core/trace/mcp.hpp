#pragma once

#include <map>
#include <string>
#include <string_view>

namespace Luma {
namespace Trace {
namespace Mcp {
struct Request
{
   std::string tool;
   std::map<std::string, std::string, std::less<>> args;
};

// "status\ntimeout_ms=1000" -> {"status", {{"timeout_ms", "1000"}}}
Request ParseRequest(std::string_view text);
std::string Answer(const Request& request);

// Starts the server thread. Clients connect once the loader lock is released.
void Start();
// Stops accepting clients and cancels their reads. Threads aren't joined: the loader lock would keep them from exiting.
void Stop();
std::wstring GetPipeName();
} // namespace Mcp
} // namespace Trace
} // namespace Luma
