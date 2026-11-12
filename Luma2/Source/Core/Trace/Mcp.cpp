#include "Trace/Mcp.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <future>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <Windows.h>
#include <sddl.h>

#include "Settings/Registry.hpp"
#include "Trace/Info.hpp"
#include "Utils/Json.hpp"
#include "Utils/Log.hpp"
#include "Utils/Strings.hpp"

#pragma comment(lib, "advapi32.lib")

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

struct Setting
{
   std::string key;
   std::string label;
   std::string tooltip;
   std::string category;
   std::string type;
   std::string visibility;
   std::string source;
   Utils::Json::Value value;
   Utils::Json::Value default_value;
   std::optional<Utils::Json::Value> min;
   std::optional<Utils::Json::Value> max;
   std::vector<std::string> options; // Enum labels
};

struct Settings
{
   bool ok = true;
   std::vector<Setting> settings;
   std::vector<std::string> ignored_args;
};

struct SetSetting
{
   bool ok = true;
   std::string key;
   Utils::Json::Value value; // After clamping
   std::vector<std::string> ignored_args;
};
} // namespace Replies

namespace {
constexpr uint32_t MAX_REQUEST_SIZE = 1u << 20;
// Mirrored by "TIMEOUT_MS" in "Scripts/luma_mcp.py"
constexpr uint32_t DEFAULT_TIMEOUT_MS = 15000;

// Work that needs the render thread (the settings registry), run at the next present. Never held with another lock.
std::mutex jobs_mutex;
std::deque<std::shared_ptr<std::packaged_task<std::string()>>> jobs;

std::atomic<bool> stopping = false;
std::atomic<bool> started = false;
// Client threads, to cancel their blocking reads on Stop. Never held with another lock.
std::mutex clients_mutex;
std::vector<HANDLE> client_threads;

bool ReadExact(HANDLE pipe, void* data, DWORD size)
{
   auto* bytes = static_cast<uint8_t*>(data);
   while (size > 0)
   {
      DWORD read = 0;
      if (!ReadFile(pipe, bytes, size, &read, nullptr) || read == 0)
         return false;
      bytes += read;
      size -= read;
   }
   return true;
}

bool WriteReply(HANDLE pipe, const std::string& reply)
{
   const uint32_t size = uint32_t(reply.size());
   DWORD written = 0;
   return WriteFile(pipe, &size, sizeof(size), &written, nullptr) && WriteFile(pipe, reply.data(), size, &written, nullptr) && written == size;
}

void ServeClient(HANDLE pipe)
{
   uint32_t size = 0;
   while (!stopping && ReadExact(pipe, &size, sizeof(size)) && size <= MAX_REQUEST_SIZE)
   {
      std::string text(size, '\0');
      if (!ReadExact(pipe, text.data(), size) || !WriteReply(pipe, Answer(ParseRequest(text))))
         break;
   }
   FlushFileBuffers(pipe);
   DisconnectNamedPipe(pipe);
   CloseHandle(pipe);
}

// So a bridge that isn't elevated can talk to an elevated game: grants its user access, with a medium integrity label
std::wstring GetSecurityDescriptor()
{
   std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";
   HANDLE token = nullptr;
   if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
   {
      alignas(TOKEN_USER) BYTE user[256];
      DWORD size = 0;
      LPWSTR sid = nullptr;
      if (GetTokenInformation(token, TokenUser, user, sizeof(user), &size) && ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user)->User.Sid, &sid))
      {
         sddl += std::format(L"(A;;GA;;;{})", sid);
         LocalFree(sid);
      }
      CloseHandle(token);
   }
   return sddl + L"S:(ML;;NW;;;ME)";
}

void ServerThread()
{
   const std::wstring name = GetPipeName();
   SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES)};
   if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(GetSecurityDescriptor().c_str(), SDDL_REVISION_1, &security.lpSecurityDescriptor, nullptr))
      security.lpSecurityDescriptor = nullptr;
   Utils::Log::Info("MCP: serving {}", Utils::Strings::ToUtf8(std::wstring_view(name)));
   while (!stopping)
   {
      // One listening instance at a time, each connected client keeps its own
      HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
         PIPE_UNLIMITED_INSTANCES, 1 << 16, 1 << 16, 0, security.lpSecurityDescriptor ? &security : nullptr);
      if (pipe == INVALID_HANDLE_VALUE)
      {
         Utils::Log::Error("MCP: CreateNamedPipe failed ({})", GetLastError());
         Sleep(1000);
         continue;
      }
      if (!(ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) || stopping)
      {
         DisconnectNamedPipe(pipe);
         CloseHandle(pipe);
         continue;
      }
      std::thread client([pipe] { ServeClient(pipe); });
      {
         std::scoped_lock lock(clients_mutex);
         HANDLE thread = nullptr;
         DuplicateHandle(GetCurrentProcess(), client.native_handle(), GetCurrentProcess(), &thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
         std::erase_if(client_threads, [](HANDLE handle) {
            if (WaitForSingleObject(handle, 0) != WAIT_OBJECT_0)
               return false;
            CloseHandle(handle);
            return true;
         });
         if (thread)
            client_threads.push_back(thread);
      }
      client.detach();
   }
   if (security.lpSecurityDescriptor)
      LocalFree(security.lpSecurityDescriptor);
}

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

Utils::Json::Value ToJson(const Settings::Setting& setting, const Settings::Value& value)
{
   Utils::Json::Value json;
   std::visit([&]<typename T>(const T& typed) {
      if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string>)
         json.data = typed;
      else if constexpr (std::is_same_v<T, int32_t>)
         json.data = int64_t(typed);
      else if constexpr (std::is_same_v<T, float>)
         json.data = double(typed);
      else if constexpr (std::is_same_v<T, Settings::Key>)
         json.data = int64_t(typed.code);
      else if (typed.value >= 0 && size_t(typed.value) < setting.enum_labels.size())
         json.data = std::string(setting.enum_labels[typed.value]);
      else
         json.data = int64_t(typed.value);
   },
      value);
   return json;
}

std::string_view GetVisibilityName(Settings::Visibility visibility)
{
   switch (visibility)
   {
   case Settings::Visibility::Test: return "test";
   case Settings::Visibility::Development: return "development";
   default: return "release";
   }
}

std::string_view GetSourceName(const Settings::Source& source)
{
   constexpr std::string_view NAMES[] = {"value", "binding", "define", "recipe"};
   return NAMES[source.index()];
}

std::string GetSettings(const Request& request)
{
   Replies::Settings reply;
   reply.ignored_args = GetIgnoredArgs(request, {});
   Settings::ForEach([&](const Settings::Setting& setting, const Settings::Value& value) {
      if (!Settings::IsVisibleInBuild(setting.visibility))
         return;
      Replies::Setting& info = reply.settings.emplace_back();
      info.key = setting.key;
      info.label = setting.label;
      info.tooltip = setting.tooltip;
      info.category = Settings::GetCategory(setting);
      info.type = Settings::GetTypeName(setting.default_value);
      info.visibility = GetVisibilityName(setting.visibility);
      info.source = GetSourceName(setting.source);
      info.value = ToJson(setting, value);
      info.default_value = ToJson(setting, setting.default_value);
      if (setting.range)
      {
         info.min = ToJson(setting, setting.range->min);
         info.max = ToJson(setting, setting.range->max);
      }
      for (const std::string_view label : setting.enum_labels)
         info.options.emplace_back(label);
   });
   return Utils::Json::Write(reply);
}

std::string SetSetting(const Request& request)
{
   const auto name = request.args.find("name");
   const auto text = request.args.find("value");
   if (name == request.args.end() || text == request.args.end())
      return Error("set_setting takes a 'name' (the setting's key, see get_settings) and a 'value'");
   const Settings::Setting* setting = Settings::Find(name->second);
   if (!setting || !Settings::IsVisibleInBuild(setting->visibility))
      return Error(std::format("no setting '{}' (see get_settings for the keys)", name->second));
   const std::optional<Settings::Value> value = Settings::Parse(*setting, text->second);
   if (!value)
      return Error(std::format("'{}' isn't a {} value", text->second, Settings::GetTypeName(setting->default_value)));
   Settings::Set(setting->key, *value);
   Replies::SetSetting reply;
   reply.key = setting->key;
   reply.value = ToJson(*setting, *Settings::GetValue(setting->key));
   reply.ignored_args = GetIgnoredArgs(request, {"name", "value"});
   return Utils::Json::Write(reply);
}

Status GetStatusReply(const Request& request)
{
   Status status = GetStatus();
   // The bridge adds a timeout to every call
   for (const auto& [key, value] : request.args)
   {
      if (key != "timeout_ms")
         status.ignored_args.push_back(key);
   }
   return status;
}
} // namespace

Request ParseRequest(std::string_view text)
{
   Request request;
   size_t start = 0;
   bool first = true;
   while (start <= text.size())
   {
      size_t end = text.find('\n', start);
      if (end == std::string_view::npos)
         end = text.size();
      std::string_view line = text.substr(start, end - start);
      if (line.ends_with('\r'))
         line.remove_suffix(1);
      if (first)
         request.tool = std::string(line);
      else if (const size_t equals = line.find('='); equals != std::string_view::npos)
         request.args[std::string(line.substr(0, equals))] = std::string(line.substr(equals + 1));
      first = false;
      start = end + 1;
   }
   return request;
}

std::string Answer(const Request& request)
{
   if (request.tool == "status")
      return Utils::Json::Write(GetStatusReply(request));
   if (request.tool == "get_settings")
      return RunOnRenderThread([request] { return GetSettings(request); }, GetTimeout(request));
   if (request.tool == "set_setting")
      return RunOnRenderThread([request] { return SetSetting(request); }, GetTimeout(request));
   return Error(std::format("unknown tool '{}' (Luma 2 serves status, get_settings and set_setting so far)", request.tool));
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

void Start()
{
   if (started.exchange(true))
      return;
   stopping = false;
   std::thread(&ServerThread).detach();
}

void Stop()
{
   if (!started.exchange(false))
      return;
   stopping = true;
   // Wakes the server thread up from ConnectNamedPipe
   HANDLE self = CreateFileW(GetPipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
   if (self != INVALID_HANDLE_VALUE)
      CloseHandle(self);
   std::scoped_lock lock(clients_mutex);
   for (HANDLE thread : client_threads)
   {
      CancelSynchronousIo(thread);
      CloseHandle(thread);
   }
   client_threads.clear();
}

std::wstring GetPipeName()
{
   return std::format(L"\\\\.\\pipe\\luma-mcp-{}", GetCurrentProcessId());
}
} // namespace Mcp
} // namespace Trace
} // namespace Luma
