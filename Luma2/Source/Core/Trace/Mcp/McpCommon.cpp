#include "Trace/Mcp/McpTools.hpp"

#if DEVELOPMENT
#include <algorithm>
#include <charconv>
#include <chrono>
#include <deque>
#include <future>
#include <memory>
#include <mutex>

#include "Utils/Json.hpp"

namespace Luma {
namespace Trace {
namespace Mcp {
// In a named namespace: glaze can't reflect types with internal linkage
namespace Replies {
struct Error
{
   bool ok = false;
   std::string error;
};
} // namespace Replies

namespace {
// Mirrored by "TIMEOUT_MS" in "Scripts/luma_mcp.py"
constexpr uint32_t DEFAULT_TIMEOUT_MS = 15000;

// Work that needs the render thread (the settings registry), run at the next present. Never held with another lock.
std::mutex jobs_mutex;
std::deque<std::shared_ptr<std::packaged_task<std::string()>>> jobs;
} // namespace

std::vector<std::string> GetIgnoredArgs(const Request& request, std::initializer_list<std::string_view> taken)
{
   std::vector<std::string> ignored;
   for (const auto& [key, value] : request.args)
   {
      // The bridge adds a timeout to every call
      if (key != "timeout_ms" && std::ranges::find(taken, key) == taken.end())
         ignored.push_back(key);
   }
   return ignored;
}

uint32_t GetTimeout(const Request& request)
{
   uint32_t timeout = DEFAULT_TIMEOUT_MS;
   if (const auto it = request.args.find("timeout_ms"); it != request.args.end())
      std::from_chars(it->second.data(), it->second.data() + it->second.size(), timeout);
   return std::clamp(timeout, 100u, 600000u);
}

std::string Error(std::string message)
{
   return Utils::Json::Write(Replies::Error{false, std::move(message)});
}

// Waits for the next present to run it
std::string RunOnRenderThread(std::function<std::string()> function, uint32_t timeout_ms)
{
   auto job = std::make_shared<std::packaged_task<std::string()>>(std::move(function));
   std::future<std::string> result = job->get_future();
   {
      std::scoped_lock lock(jobs_mutex);
      jobs.push_back(job);
   }
   if (result.wait_for(std::chrono::milliseconds(timeout_ms)) == std::future_status::ready)
      return result.get();
   {
      std::scoped_lock lock(jobs_mutex);
      std::erase(jobs, job);
   }
   // It may have run meanwhile
   if (result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
      return result.get();
   return Error(std::format("timed out after {} ms: the game isn't presenting (minimized, paused or loading)", timeout_ms));
}

bool GetBool(const Request& request, std::string_view key)
{
   const auto it = request.args.find(key);
   return it != request.args.end() && (it->second == "1" || it->second == "true");
}

uint32_t GetNumber(const Request& request, std::string_view key, uint32_t default_value, uint32_t min, uint32_t max)
{
   uint32_t value = default_value;
   if (const auto it = request.args.find(key); it != request.args.end())
      std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
   return std::clamp(value, min, max);
}

// "0x12345678" or "12345678"
std::optional<uint32_t> GetHash(const Request& request, std::string_view key)
{
   const auto it = request.args.find(key);
   if (it == request.args.end())
      return std::nullopt;
   std::string_view text = it->second;
   if (text.starts_with("0x") || text.starts_with("0X"))
      text.remove_prefix(2);
   uint32_t hash = 0;
   const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), hash, 16);
   if (error != std::errc() || end != text.data() + text.size())
      return std::nullopt;
   return hash;
}

std::string FormatHash(uint32_t hash)
{
   return std::format("0x{:08X}", hash);
}

void RunPendingJobs()
{
   std::deque<std::shared_ptr<std::packaged_task<std::string()>>> pending;
   {
      std::scoped_lock lock(jobs_mutex);
      pending.swap(jobs);
   }
   for (const auto& job : pending)
      (*job)();
}
} // namespace Mcp
} // namespace Trace
} // namespace Luma
#endif
