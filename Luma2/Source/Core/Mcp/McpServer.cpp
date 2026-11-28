#include "Mcp/McpServer.hpp"

#if DEVELOPMENT
#include <atomic>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include <sddl.h>

#include "Hooks/HookManager.hpp"
#include "Log/Log.hpp"
#include "Mcp/McpTools.hpp"
#include "Trace/RuntimeStatus.hpp"
#include "Utils/Json.hpp"
#include "Utils/Strings.hpp"

#pragma comment(lib, "advapi32.lib")

namespace Luma {
namespace Mcp {
namespace {
constexpr uint32_t MAX_REQUEST_SIZE = 1u << 20;

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
   Log::Info("MCP: serving {}", Utils::Strings::ToUtf8(std::wstring_view(name)));
   while (!stopping)
   {
      // One listening instance at a time, each connected client keeps its own
      HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
         PIPE_UNLIMITED_INSTANCES, 1 << 16, 1 << 16, 0, security.lpSecurityDescriptor ? &security : nullptr);
      if (pipe == INVALID_HANDLE_VALUE)
      {
         Log::Error("MCP: CreateNamedPipe failed ({})", GetLastError());
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

Trace::Status GetStatusReply(const Request& request)
{
   Trace::Status status = Trace::GetStatus();
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
   {
      // What's at each hooked target now
      Hooks::Verify();
      return Utils::Json::Write(GetStatusReply(request));
   }
   if (request.tool == "get_settings")
      return RunOnRenderThread([request] { return Tools::GetSettings(request); }, GetTimeout(request));
   if (request.tool == "set_setting")
      return RunOnRenderThread([request] { return Tools::SetSetting(request); }, GetTimeout(request));
#if DEVELOPMENT
   // What these read is locked or atomic: they answer while the game isn't presenting too, but reload_shaders
   if (request.tool == "shader_list")
      return Tools::ShaderList(request);
   if (request.tool == "shader_state")
      return Tools::ShaderState(request);
   if (request.tool == "dump_shader")
      return Tools::DumpShader(request);
   if (request.tool == "reload_shaders")
      return Tools::ReloadShaders(request);
   if (request.tool == "dump_stats")
      return Tools::DumpStats(request);
   if (request.tool == "patch_state")
      return Tools::PatchState(request);
   // Wait on the pipe thread for the patch job their change starts
   if (request.tool == "set_recipe")
      return Tools::SetRecipe(request);
   if (request.tool == "reload_patches")
      return Tools::ReloadPatches(request);
   // Waits for the captures on the pipe thread
   if (request.tool == "trace_capture")
      return Tools::TraceCapture(request);
   if (request.tool == "trace_list")
      return Tools::TraceList(request);
   if (request.tool == "trace_get")
      return Tools::TraceGet(request);
   // Wait on the pipe thread for the draw path and the present
   if (request.tool == "read_resource")
      return Tools::ReadResource(request);
   if (request.tool == "read_cbuffer")
      return Tools::ReadCbuffer(request);
   if (request.tool == "sr_state")
      return Tools::SrState(request);
   if (request.tool == "sr_capture")
      return Tools::SrCapture(request);
   if (request.tool == "dev_values")
      return Tools::DevValues(request);
   if (request.tool == "set_dev_value")
      return RunOnRenderThread([request] { return Tools::SetDevValue(request); }, GetTimeout(request));
#endif
   return Error(std::format("unknown tool '{}' (Luma 2 serves status, get_settings, set_setting, shader_list, shader_state, dump_shader, reload_shaders, dump_stats, "
                            "patch_state, set_recipe, reload_patches, trace_capture, trace_list, trace_get, read_resource and read_cbuffer so far; luma_log, luma_compare"
                            " and luma_golden are the bridge's)",
      request.tool));
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
} // namespace Luma
#endif
