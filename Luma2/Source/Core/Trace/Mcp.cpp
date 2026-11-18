#include "Trace/Mcp.hpp"

#include <algorithm>
#include <atomic>
#include <bitset>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
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

#include "Hooks/Hooks.hpp"
#include "Settings/Registry.hpp"
#include "Trace/Info.hpp"
#include "Utils/Json.hpp"
#if DEVELOPMENT
#include <array>
#include <map>
#include <numeric>
#include <span>

#include "Common/Api.hpp"
#include "Common/ShaderStage.hpp"
#include "Shaders/Compiler.hpp"
#include "Shaders/Database.hpp"
#include "Shaders/Patching.hpp"
#include "System/Jobs.hpp"
#include "Shaders/Shaders.hpp"
#include "Shaders/Variants.hpp"
#include "System/Paths.hpp"
#include "Trace/Capture.hpp"
#include "Trace/Cbuffers.hpp"
#include "Trace/Readback.hpp"
#include "Trace/Track.hpp"
#include "Utils/Format.hpp"
#endif
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
   std::string kind; // Game, Luma, Define, Recipe, Development: the table it's in
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

#if DEVELOPMENT
struct Shader
{
   std::string hash;
   std::optional<std::string> name;
   std::string stage;
   std::optional<bool> live; // False for a shader without objects now (include_unloaded)
   uint32_t pipelines = 0; // The game's objects with this bytecode (a D3D11 pipeline is one shader)
   std::string replaced; // "file" or "none"
   std::string version;
   uint32_t bytes = 0;
   // From reflection, when it found them
   std::optional<std::vector<uint32_t>> cbs;
   std::optional<std::vector<uint32_t>> srvs;
   std::optional<std::vector<uint32_t>> uavs;
   std::optional<std::vector<uint32_t>> rtvs;
   std::optional<std::vector<uint32_t>> samplers;
};

struct ShaderList
{
   bool ok = true;
   std::vector<Shader> shaders;
   uint32_t live = 0;
   uint32_t loaded = 0;
   uint32_t matched = 0;
   uint32_t offset = 0;
   uint32_t returned = 0;
   std::vector<std::string> ignored_args;
};

struct ReplacementFile
{
   std::string file;
   bool is_hlsl = true;
   bool is_luma_native = false;
   bool compilation_failed = false;
   std::string compilation_errors;
   std::optional<std::string> disasm;
};

struct Replaced
{
   std::string hash;
   std::string stage;
   std::string replaced;
   std::string file;
};

struct ShaderStates
{
   bool ok = true;
   std::string compilation_errors;
   bool loaded = true;
   std::vector<Replaced> replaced;
   std::vector<std::string> ignored_args;
};

struct ShaderState
{
   bool ok = true;
   std::string hash;
   std::optional<std::string> name;
   std::optional<std::string> stage; // Unset if the game never created it
   uint32_t pipelines = 0;
   std::string replaced;
   // Its variants that compiled, highest priority first, and the trace's choice ("auto" when none: the highest-priority one runs)
   std::vector<std::string> variants;
   std::string forced = "auto";
   std::optional<ReplacementFile> custom_shader;
   std::vector<std::string> ignored_args;
};

struct DumpShader
{
   bool ok = true;
   std::string file;
   std::vector<std::string> ignored_args;
};

struct ReloadShaders
{
   bool ok = true;
   bool loaded = true;
   uint32_t replaced = 0; // Game shaders running a replacement now
   std::string compilation_errors;
   std::vector<std::string> ignored_args;
};

struct PatchEnv
{
   std::string name;
   std::string label;
   std::string key; // Its setting (luma_set_setting takes it too)
   std::string default_value;
   std::string value;
};

struct PatchRecipe
{
   std::string identity;
   std::string name;
   std::optional<std::string> description;
   std::optional<std::string> stage; // Its stage folder, none for any stage
   std::string key;
   bool enabled = false;
   bool registered = false; // It describes itself (`meta:`): shown to users. Otherwise Development builds only.
   uint32_t matched = 0;
   std::optional<std::string> error;
   std::vector<PatchEnv> env;
};

struct PatchBinding
{
   std::string handle;
   std::string kind;
   uint32_t slot = 0;
   uint32_t space = 0;
   bool resolved = false;
};

struct PatchShader
{
   std::string hash;
   std::optional<std::string> name;
   std::string stage;
   bool patched = false; // Its variant is published
   std::string method; // "recipe" or "callback" (the game's bytecode patch)
   bool cached = false;
   std::vector<std::string> recipes;
   std::vector<PatchBinding> bindings;
   std::optional<std::string> error;
   std::optional<std::string> report;
};

struct PatchState
{
   bool ok = true;
   bool needed = false;
   bool loaded = true;
   bool running = false;
   uint32_t patched = 0;
   uint32_t unmatched = 0;
   uint32_t failed = 0;
   uint32_t pending = 0;
   uint32_t broken_recipes = 0;
   std::vector<PatchRecipe> recipes;
   std::vector<PatchShader> shaders;
   std::vector<std::string> ignored_args;
};

struct ReloadPatches
{
   bool ok = true;
   bool loaded = true;
   uint32_t patched = 0;
   uint32_t failed = 0;
   std::vector<std::string> ignored_args;
};

struct HashCount
{
   std::string hash;
   std::string stage;
   uint32_t total = 0;
   std::optional<std::vector<uint32_t>> per_frame;
   std::optional<uint32_t> frames_seen;
   std::optional<uint32_t> min;
   std::optional<uint32_t> max;
};

struct TraceCapture
{
   bool ok = true;
   size_t count = 0; // Entries of the last capture
   uint32_t frames_captured = 0;
   std::optional<bool> trigger_found;
   std::optional<std::vector<size_t>> trigger_indices;
   std::optional<size_t> distinct_shaders;
   std::optional<std::vector<HashCount>> hash_counts;
   std::vector<std::string> ignored_args;
};

// 1.0's luma_trace_list fields, those 2.0 has
struct TraceEntry
{
   size_t index = 0;
   std::string type; // shader, custom, present, copy, clear, cpu_write
   std::optional<bool> deferred;
   std::optional<std::string> queue; // D3D12: the queue that executed it, and its command list
   std::optional<std::string> list;
   std::optional<std::string> name;
   std::optional<std::string> stage; // The primary shader's (PS, else VS, else CS)
   std::optional<std::string> stages; // Every stage of the call with a shader, "VS PS"
   std::optional<std::string> hash;
   std::optional<std::string> replaced; // none, file
   std::optional<std::string> variant; // original, file
   std::optional<bool> cancelled_or_replaced;
   std::optional<std::string> outcome; // skipped, replaced, skipped_by_trace
   std::optional<std::string> rt0;
   std::optional<std::string> depth;
   std::optional<std::string> copy_kind; // A copy other than a whole resource's: region, resolve, buffer_region
};

struct TraceList
{
   bool ok = true;
   size_t total = 0;
   std::vector<TraceEntry> entries;
   size_t matched = 0;
   size_t offset = 0;
   size_t returned = 0;
   std::vector<std::string> ignored_args;
};

struct TraceShader
{
   std::string stage;
   std::optional<std::string> hash;
   std::optional<std::string> name;
   std::optional<std::string> luma_shader;
   std::optional<std::string> replaced;
   std::optional<std::string> variant;
   std::optional<std::vector<std::string>> variants; // Those that compiled, highest priority first
   std::optional<std::string> forced; // The trace's choice, when not auto
};

struct TraceDraw
{
   uint32_t vertex_count = 0;
   uint32_t instance_count = 0;
   uint32_t first_vertex = 0;
   uint32_t first_instance = 0;
   uint32_t index_count = 0;
   uint32_t first_index = 0;
   int32_t vertex_offset = 0;
   bool indexed = false;
   std::array<uint32_t, 3> dispatch = {};
   bool indirect = false;
   uint32_t topology = 0;
};

struct TraceView
{
   uint32_t slot = 0;
   std::string format;
   std::string view_format;
   std::array<uint32_t, 4> size = {};
   std::array<uint32_t, 3> view_size = {};
   uint32_t mip = 0;
   std::string type;
   std::string resource;
   std::optional<std::string> debug_name;
   std::optional<uint32_t> write_mask;
   std::optional<std::string> blend;
   std::optional<uint32_t> space; // D3D12, a register space other than 0
};

struct TraceDepthStencil
{
   bool state_bound = false;
   bool depth_enable = true;
   bool depth_write = true;
   std::string depth_func;
   bool stencil_enable = false;
   std::optional<uint32_t> stencil_ref;
   std::optional<uint32_t> read_mask;
   std::optional<uint32_t> write_mask;
   std::optional<std::string> front; // func fail depth_fail pass
   std::optional<std::string> back;
};

struct TraceRasterizer
{
   bool state_bound = false;
   std::string cull;
   std::string fill;
   bool front_ccw = false;
   int32_t depth_bias = 0;
   float slope_scaled_depth_bias = 0.f;
   bool depth_clip = true;
   bool scissor_enable = false;
   bool multisample = false;
};

struct TraceField
{
   std::string name;
   std::string type;
   uint32_t offset = 0;
   std::string value;
};

struct TraceConstantBuffer
{
   uint32_t slot = 0;
   std::string stage;
   std::string resource;
   uint32_t size = 0;
   std::optional<uint32_t> first_constant;
   std::optional<uint32_t> num_constants;
   // From the captured contents: where the layout came from (reflection, replacement, raw), and the fields
   std::optional<std::string> layout;
   std::optional<std::vector<TraceField>> fields;
   std::optional<bool> fields_truncated;
   std::optional<uint32_t> space;
};

struct TraceSampler
{
   uint32_t slot = 0;
   std::string filter;
   uint32_t filter_id = 0;
   std::string address;
   std::array<uint32_t, 3> address_ids = {};
   float mip_lod_bias = 0.f;
   std::optional<uint32_t> space;
};

// A copy's, clear's or CPU write's whole resource (1.0's "source" and "target")
struct TraceResource
{
   std::string format;
   std::array<uint32_t, 4> size = {};
   std::string type;
   std::string resource;
   std::optional<std::string> debug_name;
   std::optional<std::string> mirror;
};

struct TraceCopy
{
   std::string kind;
   uint32_t source_subresource = 0;
   uint32_t dest_subresource = 0;
   std::array<uint32_t, 3> dest_offset = {};
   std::optional<std::array<uint32_t, 6>> source_box;
   std::optional<std::string> resolve_format;
};

// The entry's luma_trace_list fields, then its state
struct TraceGet
{
   bool ok = true;
   size_t index = 0;
   std::string type;
   std::optional<bool> deferred;
   std::optional<std::string> queue;
   std::optional<std::string> list;
   std::optional<std::string> name;
   std::optional<std::string> stage;
   std::optional<std::string> stages;
   std::optional<std::string> hash;
   std::optional<std::string> replaced;
   std::optional<std::string> variant;
   std::optional<bool> cancelled_or_replaced;
   std::optional<std::string> outcome;
   std::optional<std::string> rt0;
   std::optional<std::string> depth;
   std::optional<std::string> copy_kind;
   uint32_t thread = 0;
   std::optional<TraceResource> source;
   std::optional<TraceResource> target;
   std::optional<TraceCopy> copy;
   std::optional<std::array<float, 4>> clear_values;
   std::vector<TraceShader> shaders;
   TraceDraw draw;
   std::optional<TraceDepthStencil> depth_stencil;
   std::optional<TraceRasterizer> rasterizer;
   std::optional<size_t> viewport_count;
   std::optional<std::vector<std::array<float, 6>>> viewports;
   std::optional<size_t> scissor_count;
   std::optional<std::vector<std::array<int32_t, 4>>> scissor_rects;
   std::vector<TraceView> rtvs;
   bool alpha_to_coverage = false;
   std::array<float, 4> blend_factor = {};
   std::vector<TraceView> srvs;
   std::vector<TraceView> uavs;
   std::optional<TraceView> dsv;
   std::vector<TraceConstantBuffer> cbs;
   std::vector<TraceSampler> samplers;
   std::vector<std::string> ignored_args;
};

// 1.0's luma_read_resource reply: where the .bin is and how the bridge decodes it
struct ReadResource
{
   bool ok = true;
   std::optional<std::string> hash;
   std::optional<std::string> stage;
   std::optional<uint32_t> instance;
   std::string view;
   uint32_t slot = 0;
   std::string path;
   std::string dimension;
   uint32_t width = 0;
   uint32_t height = 0;
   uint32_t depth = 0;
   uint32_t slices = 0;
   uint32_t mip = 0;
   uint32_t mips = 0;
   uint32_t samples = 1;
   std::string format;
   uint32_t format_id = 0;
   std::string view_format;
   uint32_t view_format_id = 0;
   uint32_t row_pitch = 0;
   uint32_t rows = 0;
   bool block_compressed = false;
   std::string resource;
   bool mirrored = false;
   std::optional<std::string> note;
   std::vector<std::string> ignored_args;
};

// 1.0's luma_read_cbuffer reply: the bound range as hex dwords, or one entry per frame
struct ReadCbuffer
{
   bool ok = true;
   std::string hash;
   std::string stage;
   uint32_t instance = 0;
   uint32_t slot = 0;
   std::string resource;
   std::optional<size_t> bytes;
   std::optional<uint32_t> first_constant;
   std::optional<uint32_t> num_constants;
   std::optional<std::string> hex_dwords;
   std::optional<std::vector<std::string>> series;
   std::vector<std::string> ignored_args;
};
#endif
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
      info.kind = Settings::GetKindName(setting.kind);
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

#if DEVELOPMENT
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
std::optional<uint32_t> GetHash(const Request& request, std::string_view key = "hash")
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

std::string_view GetStageName(ShaderStage stage)
{
   constexpr std::string_view NAMES[] = {"VS", "HS", "DS", "GS", "PS", "CS"};
   return NAMES[size_t(stage)];
}

std::optional<std::string> GetName(const Shaders::Database::Shader& shader)
{
   return shader.name.empty() ? std::nullopt : std::optional(std::string(shader.name));
}

// Replaced while the variants are loaded, as 1.0 counts its file clones
bool IsReplaced(uint32_t hash)
{
   return Shaders::Variants::IsLoaded() && Shaders::Variants::HasVariant(hash);
}

template <size_t N>
std::vector<uint32_t> ToList(const std::bitset<N>& slots)
{
   std::vector<uint32_t> list;
   for (uint32_t i = 0; i < N; i++)
   {
      if (slots[i])
         list.push_back(i);
   }
   return list;
}

std::vector<const Shaders::Database::Shader*> GetSortedShaders()
{
   std::vector<const Shaders::Database::Shader*> shaders = Shaders::Database::GetShaders();
   std::ranges::sort(shaders, {}, &Shaders::Database::Shader::hash);
   return shaders;
}

std::string ShaderList(const Request& request)
{
   const auto stage = request.args.find("stage");
   const bool replaced_only = GetBool(request, "replaced_only");
   const bool include_unloaded = GetBool(request, "include_unloaded");
   // ~150 bytes per entry: the default stays under an agent's tool output cap (1.0)
   const uint32_t offset = GetNumber(request, "offset", 0, 0, UINT32_MAX);
   const uint32_t limit = GetNumber(request, "limit", 200, 1, 100000);
   Replies::ShaderList reply;
   reply.ignored_args = GetIgnoredArgs(request, {"stage", "replaced_only", "include_unloaded", "offset", "limit"});
   for (const Shaders::Database::Shader* shader : GetSortedShaders())
   {
      reply.loaded++;
      const bool live = shader->objects != 0;
      reply.live += live;
      const bool replaced = live && IsReplaced(shader->hash);
      if ((!live && (!include_unloaded || replaced_only)) || (replaced_only && !replaced) ||
         (stage != request.args.end() && _stricmp(stage->second.c_str(), std::string(GetStageName(shader->stage)).c_str()) != 0))
         continue;
      if (reply.matched++ < offset || reply.shaders.size() >= limit)
         continue;
      Replies::Shader& entry = reply.shaders.emplace_back();
      entry.hash = FormatHash(shader->hash);
      entry.name = GetName(*shader);
      entry.stage = GetStageName(shader->stage);
      if (!live)
         entry.live = false;
      entry.pipelines = shader->objects;
      entry.replaced = replaced ? "file" : "none";
      entry.version = shader->target;
      entry.bytes = shader->size;
      if (shader->reflection.found)
      {
         entry.cbs = ToList(shader->reflection.constant_buffers);
         entry.srvs = ToList(shader->reflection.shader_resources);
         entry.uavs = ToList(shader->reflection.unordered_access);
         entry.rtvs = ToList(shader->reflection.render_targets);
         entry.samplers = ToList(shader->reflection.samplers);
      }
   }
   reply.offset = offset;
   reply.returned = uint32_t(reply.shaders.size());
   return Utils::Json::Write(reply);
}

std::string ShaderState(const Request& request)
{
   const std::vector<Shaders::Variants::Variant> replacements = Shaders::Variants::GetAll();
   const auto find_replacement = [&](uint32_t hash) { return std::ranges::find(replacements, hash, &Shaders::Variants::Variant::hash); };
   if (request.args.contains("hash"))
   {
      const std::optional<uint32_t> hash = GetHash(request);
      if (!hash)
         return Error("'hash' is a shader hash, like 0x12345678");
      Replies::ShaderState reply;
      reply.ignored_args = GetIgnoredArgs(request, {"hash", "disasm", "variant"});
      reply.hash = FormatHash(*hash);
      Shaders::Database::Shader* shader = Shaders::Database::Find(*hash);
      // The trace's variant picker: what the hash's binds take from now on, "auto" to clear it
      if (const auto variant = request.args.find("variant"); variant != request.args.end())
      {
         if (!shader)
            return Error(std::format("shader {} was never created by the game", FormatHash(*hash)));
         constexpr std::array CHOICES = {Shaders::Variants::Choice::Auto, Shaders::Variants::Choice::Original, Shaders::Variants::Choice::File,
            Shaders::Variants::Choice::Patch};
         const auto choice = std::ranges::find(CHOICES, variant->second, [](Shaders::Variants::Choice choice) { return Shaders::Variants::GetName(choice); });
         if (choice == CHOICES.end())
            return Error("'variant' is auto, original, file or patch");
         shader->forced_variant = *choice;
         Shaders::Variants::MarkChoiceChanged();
      }
      if (shader)
      {
         reply.name = GetName(*shader);
         reply.stage = std::string(GetStageName(shader->stage));
         reply.pipelines = shader->objects;
         reply.forced = Shaders::Variants::GetName(shader->forced_variant.load());
      }
      for (const Shaders::Variants::Variant& variant : Shaders::Variants::GetVariants(*hash))
      {
         if (variant.compiled)
            reply.variants.emplace_back(Shaders::Variants::GetName(variant.source));
      }
      reply.replaced = reply.pipelines && IsReplaced(*hash) ? "file" : "none";
      if (const auto replacement = find_replacement(*hash); replacement != replacements.end())
      {
         Replies::ReplacementFile& file = reply.custom_shader.emplace();
         file.file = replacement->file;
         file.compilation_failed = replacement->failed;
         file.compilation_errors = replacement->messages;
         if (GetBool(request, "disasm") && replacement->compiled)
            file.disasm = Shaders::Compiler::Disassemble(replacement->compiled->bytecode).value_or("disassembly failed");
      }
      return Utils::Json::Write(reply);
   }
   Replies::ShaderStates reply;
   reply.ignored_args = GetIgnoredArgs(request, {"hash", "disasm", "variant"});
   reply.compilation_errors = Shaders::GetLastBuild().errors;
   reply.loaded = Shaders::Variants::IsLoaded();
   for (const Shaders::Database::Shader* shader : GetSortedShaders())
   {
      if (!shader->objects || !IsReplaced(shader->hash))
         continue;
      const auto replacement = find_replacement(shader->hash);
      reply.replaced.push_back({FormatHash(shader->hash), std::string(GetStageName(shader->stage)), replacement != replacements.end() ? std::string(Shaders::Variants::GetName(replacement->source)) : std::string("file"), replacement != replacements.end() ? replacement->file : std::string()});
   }
   return Utils::Json::Write(reply);
}

std::string DumpShader(const Request& request)
{
   const std::optional<uint32_t> hash = GetHash(request);
   if (!hash)
      return Error("dump_shader takes a 'hash', like 0x12345678");
   const Shaders::Database::Shader* shader = Shaders::Database::Find(*hash);
   if (!shader)
      return Error(std::format("shader {} was never created by the game", FormatHash(*hash)));
   const std::filesystem::path file = Shaders::Database::Dump(*shader);
   if (file.empty())
      return Error(std::format("couldn't write {} to {}", FormatHash(*hash), Utils::Strings::ToUtf8(System::Paths::GetDump())));
   Replies::DumpShader reply;
   reply.file = Utils::Strings::ToUtf8(file);
   reply.ignored_args = GetIgnoredArgs(request, {"hash"});
   return Utils::Json::Write(reply);
}

// Waits on the pipe thread for the build the reload starts, like 1.0's job waiting for its load
std::string ReloadShaders(const Request& request)
{
   const bool unload = GetBool(request, "unload");
   const uint32_t timeout = GetTimeout(request);
   const uint64_t previous_build = Shaders::GetLastBuild().id;
   const std::string requested = RunOnRenderThread(
      [unload] {
         unload ? Shaders::Unload() : Shaders::Load();
         return std::string();
      },
      timeout);
   if (!requested.empty())
      return requested;
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
   const auto is_busy = [] {
      const System::Jobs::Status status = System::Jobs::GetStatus();
      return status.running != 0 || status.queued != 0;
   };
   while (!unload && (Shaders::GetLastBuild().id == previous_build || is_busy()))
   {
      if (std::chrono::steady_clock::now() > deadline)
         return Error(std::format("the shaders didn't finish compiling in {} ms (see luma_status)", timeout));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
   }
   Replies::ReloadShaders reply;
   reply.loaded = Shaders::Variants::IsLoaded();
   for (const Shaders::Database::Shader* shader : Shaders::Database::GetShaders())
      reply.replaced += shader->objects && IsReplaced(shader->hash);
   reply.compilation_errors = Shaders::GetLastBuild().errors;
   reply.ignored_args = GetIgnoredArgs(request, {"unload"});
   return Utils::Json::Write(reply);
}

Replies::PatchShader ToReply(const Shaders::Patching::PatchInfo& info, bool detail)
{
   Replies::PatchShader shader;
   shader.hash = FormatHash(info.hash);
   if (const std::string_view name = Shaders::Database::GetName(info.hash); !name.empty())
      shader.name = std::string(name);
   shader.stage = GetStageName(info.stage);
   shader.patched = info.patched;
   shader.method = info.callback ? "callback" : "recipe";
   shader.cached = info.cached;
   shader.recipes = info.recipes;
   for (const Shaders::Patching::BindingInfo& binding : info.bindings)
      shader.bindings.push_back({binding.handle, binding.kind, binding.slot, binding.space, binding.resolved});
   if (!info.error.empty())
      shader.error = info.error;
   if (detail && !info.report.empty())
      shader.report = info.report;
   return shader;
}

// The recipes, and the shaders a patch changed or failed on (`include_unmatched` adds those a recipe ran on and left as they were)
std::string PatchState(const Request& request)
{
   Replies::PatchState reply;
   reply.ignored_args = GetIgnoredArgs(request, {"hash", "include_unmatched"});
   const Shaders::Patching::Status status = Shaders::Patching::GetStatus();
   reply.needed = status.needed;
   reply.loaded = status.loaded;
   reply.running = status.running;
   reply.patched = status.patched;
   reply.unmatched = status.unmatched;
   reply.failed = status.failed;
   reply.pending = status.pending;
   reply.broken_recipes = status.broken_recipes;
   for (Shaders::Patching::RecipeInfo& recipe : Shaders::Patching::GetRecipes())
   {
      Replies::PatchRecipe& info = reply.recipes.emplace_back();
      info.identity = std::move(recipe.identity);
      info.name = std::move(recipe.name);
      if (!recipe.description.empty())
         info.description = std::move(recipe.description);
      if (!recipe.stage.empty())
         info.stage = std::move(recipe.stage);
      info.key = std::move(recipe.key);
      info.enabled = recipe.enabled;
      info.registered = recipe.registered;
      info.matched = recipe.matched;
      if (!recipe.error.empty())
         info.error = std::move(recipe.error);
      for (Shaders::Patching::EnvValue& env : recipe.env)
         info.env.push_back({std::move(env.name), std::move(env.label), std::move(env.key), std::move(env.default_value), std::move(env.value)});
   }
   if (request.args.contains("hash"))
   {
      const std::optional<uint32_t> hash = GetHash(request);
      if (!hash)
         return Error("'hash' is a shader hash, like 0x12345678");
      const std::optional<Shaders::Patching::PatchInfo> info = Shaders::Patching::GetPatch(*hash);
      if (!info)
         return Error(std::format("no patch was tried on {} (no recipe for its stage, or the game didn't create it)", FormatHash(*hash)));
      reply.shaders.push_back(ToReply(*info, true));
      return Utils::Json::Write(reply);
   }
   const bool include_unmatched = GetBool(request, "include_unmatched");
   for (const Shaders::Patching::PatchInfo& info : Shaders::Patching::GetPatches())
   {
      if (info.patched || !info.error.empty() || include_unmatched)
         reply.shaders.push_back(ToReply(info, false));
   }
   return Utils::Json::Write(reply);
}

// Waits on the pipe thread for the patch job a change starts (the next present queues it)
std::optional<std::string> WaitForPatches(uint64_t previous_generation, uint32_t timeout)
{
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
   while (true)
   {
      const Shaders::Patching::Status status = Shaders::Patching::GetStatus();
      if (status.generation != previous_generation && !status.running)
         return std::nullopt;
      if (std::chrono::steady_clock::now() > deadline)
         return Error(std::format("the shaders weren't patched in {} ms (see luma_status)", timeout));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
   }
}

// 1.0's set_recipe: a recipe's switch, or one of its env values set or reset, through their settings (saved like the overlay's)
std::string SetRecipe(const Request& request)
{
   const auto identity = request.args.find("identity");
   if (identity == request.args.end())
      return Error("set_recipe takes the 'identity' of a recipe (see luma_patch_state)");
   const uint64_t previous_generation = Shaders::Patching::GetStatus().generation;
   const uint32_t timeout = GetTimeout(request);
   constexpr std::string_view UNCHANGED = "unchanged";
   const std::string answer = RunOnRenderThread(
      [request, identity = identity->second, UNCHANGED]() -> std::string {
         const std::vector<Shaders::Patching::RecipeInfo> recipes = Shaders::Patching::GetRecipes();
         const auto recipe = std::ranges::find(recipes, identity, &Shaders::Patching::RecipeInfo::identity);
         if (recipe == recipes.end())
            return Error(std::format("no recipe '{}' (see luma_patch_state)", identity));
         bool changed = false;
         if (const auto enabled = request.args.find("enabled"); enabled != request.args.end())
         {
            const bool value = enabled->second == "1" || enabled->second == "true";
            changed |= Settings::Get<bool>(recipe->key) != value;
            Settings::Set(recipe->key, value);
         }
         if (const auto env = request.args.find("env"); env != request.args.end())
         {
            const auto variable = std::ranges::find(recipe->env, env->second, &Shaders::Patching::EnvValue::name);
            if (variable == recipe->env.end())
               return Error(std::format("recipe '{}' has no env '{}'", identity, env->second));
            const Settings::Setting* setting = Settings::Find(variable->key);
            if (!setting)
               return Error(std::format("recipe '{}' has no env '{}'", identity, env->second));
            const std::optional<Settings::Value> before = Settings::GetValue(variable->key);
            if (GetBool(request, "reset"))
               Settings::Reset(variable->key);
            else if (const auto text = request.args.find("value"); text != request.args.end())
            {
               const std::optional<Settings::Value> value = Settings::Parse(*setting, text->second);
               if (!value)
                  return Error(std::format("'{}' isn't a {} value", text->second, Settings::GetTypeName(setting->default_value)));
               Settings::Set(variable->key, *value);
            }
            else
               return Error("'env' takes a 'value', or 'reset'");
            changed |= Settings::GetValue(variable->key) != before;
         }
         return changed ? std::string() : std::string(UNCHANGED);
      },
      timeout);
   if (!answer.empty() && answer != UNCHANGED)
      return answer;
   if (answer.empty())
   {
      if (std::optional<std::string> error = WaitForPatches(previous_generation, timeout))
         return *error;
   }
   return PatchState(Request{.tool = "patch_state"});
}

std::string ReloadPatches(const Request& request)
{
   const bool unload = GetBool(request, "unload");
   const uint32_t timeout = GetTimeout(request);
   const uint64_t previous_generation = Shaders::Patching::GetStatus().generation;
   const std::string requested = RunOnRenderThread(
      [unload] {
         if (unload)
            Shaders::Patching::SetLoaded(false);
         else
         {
            Shaders::Patching::SetLoaded(true);
            Shaders::Patching::Reload();
         }
         return std::string();
      },
      timeout);
   if (!requested.empty())
      return requested;
   if (!unload)
   {
      if (std::optional<std::string> error = WaitForPatches(previous_generation, timeout))
         return *error;
   }
   const Shaders::Patching::Status status = Shaders::Patching::GetStatus();
   Replies::ReloadPatches reply;
   reply.loaded = status.loaded;
   reply.patched = status.patched;
   reply.failed = status.failed;
   reply.ignored_args = GetIgnoredArgs(request, {"unload"});
   return Utils::Json::Write(reply);
}

using Capture::Event;
using Capture::EventType;

// "PS", " vs "...
std::optional<ShaderStage> ParseStage(std::string_view name)
{
   while (!name.empty() && std::isspace(uint8_t(name.front())))
      name.remove_prefix(1);
   while (!name.empty() && std::isspace(uint8_t(name.back())))
      name.remove_suffix(1);
   for (uint32_t i = 0; i <= uint32_t(ShaderStage::Compute); i++)
   {
      const std::string_view stage = GetStageName(ShaderStage(i));
      if (name.size() == stage.size() && _strnicmp(name.data(), stage.data(), stage.size()) == 0)
         return ShaderStage(i);
   }
   return std::nullopt;
}

// 1.0's "replaced": the source of the hash's first variant, or "none"
std::string GetReplacedName(const Capture::Shader& shader)
{
   return shader.variants.empty() ? "none" : std::string(Shaders::Variants::GetName(shader.variants.front()));
}

// 1.0's "variant": the source of the variant that ran, or "original" for a shader with variants that ran the game's
std::optional<std::string> GetVariantName(const Capture::Shader& shader)
{
   if (shader.variant)
      return std::string(Shaders::Variants::GetName(*shader.variant));
   return shader.variants.empty() ? std::nullopt : std::optional<std::string>("original");
}

std::string_view GetTypeName(EventType type)
{
   switch (type)
   {
   case EventType::LumaPass: return "custom";
   case EventType::Present: return "present";
   case EventType::Copy: return "copy";
   case EventType::Clear: return "clear";
   case EventType::CpuWrite: return "cpu_write";
   default: return "shader";
   }
}

// 1.0's luma_trace_list fields of an entry
template <typename Reply>
void WriteSummary(Reply& reply, const Event& event, size_t index)
{
   reply.index = index;
   reply.type = GetTypeName(event.type);
   if (event.deferred)
      reply.deferred = true;
   if (!event.queue.empty())
      reply.queue = event.queue;
   if (!event.list.empty())
      reply.list = event.list;
   if (event.type == EventType::LumaPass || event.type == EventType::Present || Capture::IsResourceEvent(event))
      reply.name = event.name;
   // A copy's, clear's or write's target, as 1.0 listed it
   if (event.target)
      reply.rt0 = std::format("{} {}x{}", Utils::Format::GetName(event.target->format), event.target->size[0], event.target->size[1]);
   if (event.copy && event.copy->kind != Capture::CopyKind::Resource)
   {
      constexpr std::string_view KINDS[] = {"resource", "region", "resolve", "buffer_region"};
      reply.copy_kind = std::string(KINDS[size_t(event.copy->kind)]);
   }
   // 1.0 listed an entry per stage; 2.0 lists the call once, by its primary shader, with its stages
   if (!event.shaders.empty())
   {
      std::string stages;
      for (const Capture::Shader& shader : event.shaders)
         stages += std::format("{}{}", stages.empty() ? "" : " ", GetStageName(shader.stage));
      reply.stages = std::move(stages);
   }
   if (const Capture::Shader* shader = Capture::GetPrimaryShader(event))
   {
      reply.stage = std::string(GetStageName(shader->stage));
      if (event.type != EventType::LumaPass)
      {
         reply.hash = FormatHash(shader->hash);
         reply.replaced = GetReplacedName(*shader);
         if (!shader->name.empty())
            reply.name = shader->name;
         reply.variant = GetVariantName(*shader);
      }
   }
   if (event.outcome != Capture::Outcome::Ran)
   {
      constexpr std::string_view OUTCOMES[] = {"", "skipped", "replaced", "skipped_by_trace"};
      reply.cancelled_or_replaced = true;
      reply.outcome = std::string(OUTCOMES[size_t(event.outcome)]);
   }
   if (!event.render_targets.empty() && event.render_targets.front().slot == 0)
   {
      const Capture::View& target = event.render_targets.front();
      reply.rt0 = std::format("{} {}x{}", Utils::Format::GetName(target.format), target.size[0], target.size[1]);
   }
   if (event.depth_stencil_view && event.depth_stencil.depth_enable)
      reply.depth = std::format("{}{}", event.depth_stencil.depth_func, event.depth_stencil.depth_write ? " write" : "");
}

bool HasResource(const Event& event, std::string_view id)
{
   const auto in = [&](const std::vector<Capture::View>& views) { return std::ranges::any_of(views, [&](const Capture::View& view) { return view.resource.id == id; }); };
   return in(event.render_targets) || in(event.shader_resources) || in(event.unordered_access) || (event.depth_stencil_view && event.depth_stencil_view->resource.id == id) ||
      std::ranges::any_of(event.constant_buffers, [&](const Capture::ConstantBuffer& buffer) { return buffer.id == id; }) ||
      (event.source && event.source->id == id) || (event.target && event.target->id == id);
}

// Waits on the pipe thread for the capture of the next frame
std::shared_ptr<const Capture::Frame> CaptureFrame(std::chrono::steady_clock::time_point deadline)
{
   const std::shared_ptr<const Capture::Frame> previous = Capture::GetLast();
   Capture::Request();
   while (Capture::GetLast() == previous)
   {
      if (std::chrono::steady_clock::now() > deadline)
         return nullptr;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
   }
   return Capture::GetLast();
}

std::string TraceCapture(const Request& request)
{
   std::optional<uint32_t> trigger;
   if (request.args.contains("trigger") && !(trigger = GetHash(request, "trigger")))
      return Error("'trigger' is a shader hash, like 0x12345678");
   const uint32_t max_frames = GetNumber(request, "max_frames", 120, 1, 3600);
   const uint32_t frames = trigger ? 1 : GetNumber(request, "frames", 1, 1, 600);
   const uint32_t timeout = GetTimeout(request);
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
   Replies::TraceCapture reply;
   reply.ignored_args = GetIgnoredArgs(request, {"trigger", "max_frames", "frames", "top", "summary"});
   // Draws per shader hash, per captured frame
   std::map<uint32_t, std::pair<ShaderStage, std::vector<uint32_t>>> counts;
   while (true)
   {
      const std::shared_ptr<const Capture::Frame> frame = CaptureFrame(deadline);
      if (!frame)
         return Error(std::format("timed out after {} ms with {} frames captured: the game isn't presenting (minimized, paused or loading)", timeout, reply.frames_captured));
      reply.frames_captured++;
      reply.count = frame->events.size();
      if (trigger)
      {
         std::vector<size_t> indices;
         for (size_t i = 0; i < frame->events.size() && indices.size() < 32; i++)
         {
            if (std::ranges::any_of(frame->events[i].shaders, [&](const Capture::Shader& shader) { return shader.hash == *trigger; }))
               indices.push_back(i);
         }
         if (indices.empty() && reply.frames_captured < max_frames)
            continue;
         reply.trigger_found = !indices.empty();
         reply.trigger_indices = std::move(indices);
         return Utils::Json::Write(reply);
      }
      if (frames == 1)
         return Utils::Json::Write(reply);
      for (const Event& event : frame->events)
      {
         for (const Capture::Shader& shader : event.shaders)
         {
            if (event.type == EventType::LumaPass || !shader.hash)
               continue;
            auto& [stage, per_frame] = counts.try_emplace(shader.hash, shader.stage, std::vector<uint32_t>()).first->second;
            per_frame.resize(reply.frames_captured, 0);
            per_frame.back()++;
         }
      }
      if (reply.frames_captured >= frames)
         break;
   }
   std::vector<std::pair<uint32_t, uint32_t>> totals; // Hash, draws
   for (auto& [hash, count] : counts)
   {
      count.second.resize(reply.frames_captured, 0);
      totals.emplace_back(hash, std::accumulate(count.second.begin(), count.second.end(), 0u));
   }
   std::ranges::sort(totals, std::greater(), &std::pair<uint32_t, uint32_t>::second);
   const uint32_t top = GetNumber(request, "top", 64, 1, 5000);
   const bool summary = GetBool(request, "summary");
   reply.distinct_shaders = totals.size();
   reply.hash_counts.emplace();
   for (size_t i = 0; i < std::min<size_t>(top, totals.size()); i++)
   {
      const auto& [stage, per_frame] = counts[totals[i].first];
      Replies::HashCount& entry = reply.hash_counts->emplace_back(FormatHash(totals[i].first), std::string(GetStageName(stage)), totals[i].second);
      if (summary)
      {
         entry.frames_seen = uint32_t(std::ranges::count_if(per_frame, [](uint32_t count) { return count != 0; }));
         entry.min = std::ranges::min(per_frame);
         entry.max = std::ranges::max(per_frame);
      }
      else
         entry.per_frame = per_frame;
   }
   return Utils::Json::Write(reply);
}

std::string TraceList(const Request& request)
{
   const std::shared_ptr<const Capture::Frame> frame = Capture::GetLast();
   if (!frame)
      return Error("nothing captured yet, run luma_trace_capture first");
   const auto arg = [&](std::string_view key) {
      const auto it = request.args.find(key);
      return it != request.args.end() ? std::optional(it->second) : std::nullopt;
   };
   const std::optional<std::string> type = arg("type");
   const std::optional<std::string> resource = arg("resource");
   const std::optional<std::string> variant = arg("variant");
   // The overlay's filter: the shader conditions match any stage of a call
   Capture::Filter filter = {
      .include_vertex_only = GetBool(request, "include_vs"),
      .text = arg("filter").value_or(std::string()),
      .replaced_only = GetBool(request, "replaced_only"),
      .cancelled_only = GetBool(request, "cancelled_only"),
      .buffer_writes = GetBool(request, "include_buffer_writes"),
   };
   if (const std::optional<std::string> stage = arg("stage"); stage && !(filter.stage = ParseStage(*stage)))
      return Error("'stage' is VS, HS, DS, GS, PS or CS");
   if (request.args.contains("hash") && !(filter.hash = GetHash(request)))
      return Error("'hash' is a shader hash, like 0x12345678");
   const uint32_t offset = GetNumber(request, "offset", 0, 0, UINT32_MAX);
   const uint32_t limit = GetNumber(request, "limit", 300, 1, 5000);
   Replies::TraceList reply;
   // 2.0 records no binding entries yet: include_bindings has nothing to include
   reply.ignored_args = GetIgnoredArgs(request,
      {"type", "stage", "resource", "variant", "hash", "filter", "include_vs", "include_bindings", "include_buffer_writes", "replaced_only", "cancelled_only", "offset",
         "limit"});
   reply.total = frame->events.size();
   for (size_t i = 0; i < frame->events.size(); i++)
   {
      const Event& event = frame->events[i];
      if (!Capture::Matches(event, filter) || (type && *type != GetTypeName(event.type)))
         continue;
      const Capture::Shader* main = Capture::GetPrimaryShader(event);
      if (variant && (!main || GetVariantName(*main) != *variant))
         continue;
      if (resource && !HasResource(event, *resource))
         continue;
      if (reply.matched++ < offset || reply.entries.size() >= limit)
         continue;
      WriteSummary(reply.entries.emplace_back(), event, i);
   }
   reply.offset = offset;
   reply.returned = reply.entries.size();
   return Utils::Json::Write(reply);
}

Replies::TraceView ToReply(const Capture::View& view)
{
   const Capture::Resource& resource = view.resource;
   Replies::TraceView reply = {view.slot, std::string(Utils::Format::GetName(resource.format)), std::string(Utils::Format::GetName(view.format)), resource.size,
      view.size, view.mip, resource.type, resource.id};
   if (!resource.debug_name.empty())
      reply.debug_name = resource.debug_name;
   if (view.space != 0)
      reply.space = view.space;
   return reply;
}

std::string TraceGet(const Request& request)
{
   const std::shared_ptr<const Capture::Frame> frame = Capture::GetLast();
   const uint32_t index = GetNumber(request, "index", UINT32_MAX, 0, UINT32_MAX);
   if (!frame || index >= frame->events.size())
      return Error(std::format("trace index {} is out of range (captured {})", index, frame ? frame->events.size() : 0));
   const Event& event = frame->events[index];
   Replies::TraceGet reply;
   reply.ignored_args = GetIgnoredArgs(request, {"index"});
   WriteSummary(reply, event, index);
   reply.thread = event.thread;
   if (Capture::IsResourceEvent(event))
   {
      const auto to_reply = [](const Capture::Resource& resource) {
         return Replies::TraceResource{std::string(Utils::Format::GetName(resource.format)), resource.size, resource.type, resource.id,
            resource.debug_name.empty() ? std::nullopt : std::optional(resource.debug_name), resource.mirror};
      };
      if (event.source)
         reply.source = to_reply(*event.source);
      if (event.target)
         reply.target = to_reply(*event.target);
      if (const std::optional<Capture::Copy>& copy = event.copy)
      {
         constexpr std::string_view KINDS[] = {"resource", "region", "resolve", "buffer_region"};
         reply.copy = Replies::TraceCopy{std::string(KINDS[size_t(copy->kind)]), copy->source_subresource, copy->destination_subresource, copy->destination_offset,
            copy->source_box, copy->kind == Capture::CopyKind::Resolve ? std::optional(std::string(Utils::Format::GetName(copy->resolve_format))) : std::nullopt};
      }
      if (event.type == EventType::Clear)
         reply.clear_values = event.clear_values;
      return Utils::Json::Write(reply);
   }
   for (const Capture::Shader& shader : event.shaders)
   {
      Replies::TraceShader& entry = reply.shaders.emplace_back();
      entry.stage = std::string(GetStageName(shader.stage));
      if (!shader.luma_shader.empty())
      {
         entry.luma_shader = shader.luma_shader;
         continue;
      }
      entry.hash = FormatHash(shader.hash);
      if (!shader.name.empty())
         entry.name = shader.name;
      entry.replaced = GetReplacedName(shader);
      entry.variant = GetVariantName(shader);
      if (!shader.variants.empty())
      {
         entry.variants.emplace();
         for (const Shaders::Variants::Source source : shader.variants)
            entry.variants->emplace_back(Shaders::Variants::GetName(source));
      }
      if (shader.forced != Shaders::Variants::Choice::Auto)
         entry.forced = std::string(Shaders::Variants::GetName(shader.forced));
   }
   reply.draw = {.vertex_count = event.indexed ? 0 : event.vertex_or_index_count,
      .instance_count = event.instance_count,
      .first_vertex = event.indexed ? 0 : event.start_vertex_or_index,
      .first_instance = event.start_instance,
      .index_count = event.indexed ? event.vertex_or_index_count : 0,
      .first_index = event.indexed ? event.start_vertex_or_index : 0,
      .vertex_offset = event.base_vertex,
      .indexed = event.indexed,
      .dispatch = event.dispatch,
      .indirect = event.indirect,
      .topology = event.topology};
   if (event.type == EventType::Draw || event.type == EventType::LumaPass)
   {
      const Capture::DepthStencil& depth = event.depth_stencil;
      Replies::TraceDepthStencil& depth_reply = reply.depth_stencil.emplace(depth.state_bound, depth.depth_enable, depth.depth_write, depth.depth_func, depth.stencil_enable);
      if (depth.stencil_enable)
      {
         depth_reply.stencil_ref = depth.stencil_ref;
         depth_reply.read_mask = depth.read_mask;
         depth_reply.write_mask = depth.write_mask;
         depth_reply.front = depth.front;
         depth_reply.back = depth.back;
      }
      const Capture::Rasterizer& rasterizer = event.rasterizer;
      reply.rasterizer = Replies::TraceRasterizer{rasterizer.state_bound, rasterizer.cull, rasterizer.fill, rasterizer.front_ccw, rasterizer.depth_bias,
         rasterizer.slope_scaled_depth_bias, rasterizer.depth_clip, rasterizer.scissor_enable, rasterizer.multisample};
      reply.viewport_count = event.viewports.size();
      reply.viewports = event.viewports;
      reply.scissor_count = event.scissor_rects.size();
      reply.scissor_rects = event.scissor_rects;
   }
   for (const Capture::View& view : event.render_targets)
   {
      Replies::TraceView& target = reply.rtvs.emplace_back(ToReply(view));
      target.write_mask = view.write_mask;
      if (!view.blend.empty())
         target.blend = view.blend;
   }
   reply.alpha_to_coverage = event.alpha_to_coverage;
   reply.blend_factor = event.blend_factor;
   for (const Capture::View& view : event.shader_resources)
      reply.srvs.push_back(ToReply(view));
   for (const Capture::View& view : event.unordered_access)
      reply.uavs.push_back(ToReply(view));
   if (event.depth_stencil_view)
      reply.dsv = ToReply(*event.depth_stencil_view);
   // Typed fields, a few hundred at most per buffer
   constexpr size_t MAX_FIELDS = 256;
   for (const Capture::ConstantBuffer& buffer : event.constant_buffers)
   {
      Replies::TraceConstantBuffer& entry = reply.cbs.emplace_back(buffer.slot, std::string(GetStageName(buffer.stage)), buffer.id, buffer.size);
      if (buffer.constant_count)
      {
         entry.first_constant = buffer.first_constant;
         entry.num_constants = buffer.constant_count;
      }
      if (buffer.space != 0)
         entry.space = buffer.space;
      if (buffer.contents.empty())
         continue;
      std::span<const std::byte> contents = buffer.contents;
      if (buffer.constant_count && !buffer.contents_ranged)
      {
         const size_t first = std::min<size_t>(size_t(buffer.first_constant) * 16, contents.size());
         contents = contents.subspan(first, std::min<size_t>(size_t(buffer.constant_count) * 16, contents.size() - first));
      }
      uint32_t hash = 0;
      // The shader's layouts are by space 0 slot: another space's buffer is shown raw
      for (const Capture::Shader& shader : event.shaders)
         hash = shader.stage == buffer.stage && buffer.space == 0 ? shader.hash : hash;
      const Cbuffers::Description description = Cbuffers::Describe(hash, buffer.slot, contents);
      entry.layout = description.source;
      entry.fields.emplace();
      for (size_t i = 0; i < std::min(description.fields.size(), MAX_FIELDS); i++)
      {
         const Cbuffers::Field& field = description.fields[i];
         entry.fields->push_back({field.name, field.type, field.offset, field.value});
      }
      if (description.fields.size() > MAX_FIELDS)
         entry.fields_truncated = true;
   }
   for (const Capture::Sampler& sampler : event.samplers)
      reply.samplers.push_back({sampler.slot, sampler.filter, sampler.filter_id, sampler.address_names, sampler.address, sampler.mip_lod_bias,
         sampler.space != 0 ? std::optional<uint32_t>(sampler.space) : std::nullopt});
   return Utils::Json::Write(reply);
}

// The call a readback targets: by "index" from the last capture (its primary shader, and that shader's instance in the frame), or by
// "hash" and "instance" (the n-th call of that shader in a frame)
struct CallTarget
{
   uint32_t hash = 0;
   ShaderStage stage = ShaderStage::Pixel;
   uint32_t instance = 0;
};

std::optional<CallTarget> ResolveCall(const Request& request, std::string* error)
{
   if (request.args.contains("index"))
   {
      const std::shared_ptr<const Capture::Frame> frame = Capture::GetLast();
      const uint32_t index = GetNumber(request, "index", UINT32_MAX, 0, UINT32_MAX);
      if (!frame || index >= frame->events.size())
      {
         *error = std::format("trace index {} is out of range (captured {}), run luma_trace_capture first", index, frame ? frame->events.size() : 0);
         return std::nullopt;
      }
      const Capture::Shader* primary = Capture::GetPrimaryShader(frame->events[index]);
      if (!primary || !primary->luma_shader.empty() || !primary->hash)
      {
         *error = std::format("trace index {} is not a call of a game shader Luma knows (Luma's passes aren't read back by index yet)", index);
         return std::nullopt;
      }
      return CallTarget{primary->hash, primary->stage, Capture::GetInstance(*frame, index, primary->stage, primary->hash)};
   }
   if (const std::optional<uint32_t> hash = GetHash(request))
   {
      const Shaders::Database::Shader* shader = Shaders::Database::Find(*hash);
      if (!shader || !shader->objects)
      {
         *error = std::format("shader {} isn't live (never created, or released)", FormatHash(*hash));
         return std::nullopt;
      }
      return CallTarget{*hash, shader->stage, GetNumber(request, "instance", 0, 0, UINT32_MAX)};
   }
   *error = "pass either 'index' (from luma_trace_list) or 'hash' (and optionally 'instance')";
   return std::nullopt;
}

// Where readbacks go ("out_dir" only picks a subfolder): an elevated game must not write where a non elevated client asks. Not under
// the game's Luma folder, whose roots hold sources only.
std::filesystem::path GetReadbackRoot()
{
   return std::filesystem::temp_directory_path() / "luma-mcp";
}

// Request arguments are UTF-8
std::filesystem::path Utf8Path(std::string_view utf8)
{
   return std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
}

// `directory` is the readback root or inside it, ".." and junctions resolved (1.0's check)
bool IsInReadbackRoot(const std::filesystem::path& directory)
{
   std::error_code error;
   const std::filesystem::path root = std::filesystem::weakly_canonical(GetReadbackRoot(), error);
   if (error || root.empty())
      return false;
   const std::filesystem::path canonical = std::filesystem::weakly_canonical(directory, error);
   return !error && std::mismatch(root.begin(), root.end(), canonical.begin(), canonical.end()).first == root.end();
}

std::string ReadResource(const Request& request)
{
   Replies::ReadResource reply;
   reply.ignored_args = GetIgnoredArgs(request, {"index", "hash", "instance", "view", "slot", "mip", "original", "replaced", "out_dir"});
   const auto view_argument = request.args.find("view");
   const std::string view_name = view_argument != request.args.end() ? view_argument->second : "rtv";
   const std::optional<Readback::ViewKind> view = Readback::ParseViewKind(view_name);
   if (!view)
      return Error("'view' is rtv, srv, uav, depth, stencil or swapchain (ui and registered:<name> come with display composition, Step 5)");
   std::filesystem::path directory = GetReadbackRoot();
   if (const auto out_dir = request.args.find("out_dir"); out_dir != request.args.end())
   {
      directory = Utf8Path(out_dir->second);
      if (!IsInReadbackRoot(directory))
         return Error(std::format("'out_dir' must be {} or a folder inside it", Utils::Strings::ToUtf8(GetReadbackRoot())));
   }
   Readback::Target target = {.view = *view, .slot = GetNumber(request, "slot", 0, 0, 127), .mip = GetNumber(request, "mip", 0, 0, 15), .original = GetBool(request, "original")};
   std::string stem = "swapchain";
   if (*view != Readback::ViewKind::Swapchain)
   {
      std::string error;
      const std::optional<CallTarget> call = ResolveCall(request, &error);
      if (!call)
         return Error(std::move(error));
      target.hash = call->hash;
      target.stage = call->stage;
      target.instance = call->instance;
      reply.hash = FormatHash(call->hash);
      reply.stage = std::string(GetStageName(call->stage));
      reply.instance = call->instance;
      stem = std::format("{:08X}_{}_{}{}", call->hash, call->instance, view_name, target.slot);
   }
   Readback::Result result = Readback::Read(target, std::chrono::steady_clock::now() + std::chrono::milliseconds(GetTimeout(request)));
   if (!result.texture)
      return Error(result.error.empty() ? std::string("the readback failed") : std::move(result.error));
   Readback::Texture& texture = *result.texture;
   const std::filesystem::path path = directory / std::format("{}_f{}.bin", stem, Shaders::GetFrame());
   {
      std::error_code error;
      std::filesystem::create_directories(directory, error);
      std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(texture.bytes.data()), std::streamsize(texture.bytes.size()));
      if (!file)
         return Error(std::format("{} couldn't be written", Utils::Strings::ToUtf8(path)));
   }
   reply.view = view_name;
   reply.slot = target.slot;
   reply.path = Utils::Strings::ToUtf8(path);
   reply.dimension = texture.dimension;
   reply.width = texture.width;
   reply.height = texture.height;
   reply.depth = texture.depth;
   reply.slices = texture.slices;
   reply.mip = texture.mip;
   reply.mips = texture.mips;
   reply.samples = texture.samples;
   reply.format = std::string(Utils::Format::GetName(texture.format));
   reply.format_id = texture.format;
   reply.view_format = std::string(Utils::Format::GetName(texture.view_format));
   reply.view_format_id = texture.view_format;
   reply.row_pitch = texture.row_pitch;
   reply.rows = texture.rows;
   reply.block_compressed = texture.block_compressed;
   reply.resource = std::move(texture.resource);
   reply.mirrored = texture.mirrored;
   if (*view == Readback::ViewKind::Swapchain)
      reply.note = "Read at present, before Luma's overlay";
   return Utils::Json::Write(reply);
}

std::string HexDwords(const std::vector<std::byte>& contents)
{
   std::string hex;
   hex.reserve(contents.size() * 2);
   for (size_t offset = 0; offset + 4 <= contents.size(); offset += 4)
   {
      uint32_t value = 0;
      std::memcpy(&value, contents.data() + offset, 4);
      std::format_to(std::back_inserter(hex), "{:08X}", value);
   }
   return hex;
}

// 1.0's luma_read_cbuffer, through Track: the slot is tracked for as many frames as asked, then the overlay's tracking (if any) is
// put back
std::string ReadCbuffer(const Request& request)
{
   Replies::ReadCbuffer reply;
   reply.ignored_args = GetIgnoredArgs(request, {"index", "hash", "instance", "slot", "frames"});
   std::string error;
   const std::optional<CallTarget> call = ResolveCall(request, &error);
   if (!call)
      return Error(std::move(error));
   const uint32_t slot = GetNumber(request, "slot", UINT32_MAX, 0, 13);
   if (slot == UINT32_MAX)
      return Error("'slot' is the constant buffer register, 0 to 13");
   const uint32_t frames = GetNumber(request, "frames", 1, 1, 600);
   reply.hash = FormatHash(call->hash);
   reply.stage = std::string(GetStageName(call->stage));
   reply.instance = call->instance;
   reply.slot = slot;
   const std::optional<Track::Target> previous = Track::Get();
   Track::Set(Track::Target{.hash = call->hash, .stage = call->stage, .slot = slot, .instance = call->instance});
   // Frames without the call are misses: a few in a row and it's reported missing (1.0's budget)
   constexpr uint64_t MAX_MISSED_FRAMES = 3;
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(GetTimeout(request));
   std::vector<Track::Reading> readings;
   uint64_t last_reading = 0;
   uint64_t waited_from = Track::GetFrame();
   while (readings.size() < frames)
   {
      if (std::chrono::steady_clock::now() > deadline)
      {
         error = std::format("timed out with {} of {} frames read: the game isn't presenting (minimized, paused or loading)", readings.size(), frames);
         break;
      }
      const std::optional<Track::Reading> reading = Track::GetReading();
      if (reading && !reading->stale && reading->frame > last_reading)
      {
         last_reading = reading->frame;
         waited_from = Track::GetFrame();
         readings.push_back(*reading);
         continue;
      }
      if (Track::GetFrame() > waited_from + MAX_MISSED_FRAMES)
      {
         error = std::format("the call wasn't reached, or had no constant buffer at b{}, in {} frames (check 'instance' and 'slot')", slot, MAX_MISSED_FRAMES);
         break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
   }
   Track::Set(previous);
   if (readings.size() < frames)
      return Error(std::move(error));
   const Track::Reading& first = readings.front();
   reply.resource = first.buffer;
   if (frames > 1)
   {
      reply.series.emplace();
      for (const Track::Reading& reading : readings)
         reply.series->push_back(std::format("{}:{}", reading.frame, HexDwords(reading.contents)));
      return Utils::Json::Write(reply);
   }
   reply.bytes = first.contents.size();
   if (first.constant_count)
   {
      reply.first_constant = first.first_constant;
      reply.num_constants = first.constant_count;
   }
   reply.hex_dwords = HexDwords(first.contents);
   return Utils::Json::Write(reply);
}
#endif

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
   {
      // What's at each hooked target now
      Hooks::Verify();
      return Utils::Json::Write(GetStatusReply(request));
   }
   if (request.tool == "get_settings")
      return RunOnRenderThread([request] { return GetSettings(request); }, GetTimeout(request));
   if (request.tool == "set_setting")
      return RunOnRenderThread([request] { return SetSetting(request); }, GetTimeout(request));
#if DEVELOPMENT
   // What these read is locked or atomic: they answer while the game isn't presenting too, but reload_shaders
   if (request.tool == "shader_list")
      return ShaderList(request);
   if (request.tool == "shader_state")
      return ShaderState(request);
   if (request.tool == "dump_shader")
      return DumpShader(request);
   if (request.tool == "reload_shaders")
      return ReloadShaders(request);
   if (request.tool == "patch_state")
      return PatchState(request);
   // Wait on the pipe thread for the patch job their change starts
   if (request.tool == "set_recipe")
      return SetRecipe(request);
   if (request.tool == "reload_patches")
      return ReloadPatches(request);
   // Waits for the captures on the pipe thread
   if (request.tool == "trace_capture")
      return TraceCapture(request);
   if (request.tool == "trace_list")
      return TraceList(request);
   if (request.tool == "trace_get")
      return TraceGet(request);
   // Wait on the pipe thread for the draw path and the present
   if (request.tool == "read_resource")
      return ReadResource(request);
   if (request.tool == "read_cbuffer")
      return ReadCbuffer(request);
#endif
   return Error(std::format("unknown tool '{}' (Luma 2 serves status, get_settings, set_setting, shader_list, shader_state, dump_shader, reload_shaders, "
                            "patch_state, set_recipe, reload_patches, trace_capture, trace_list, trace_get, read_resource and read_cbuffer so far; luma_log, luma_compare"
                            " and luma_golden are the bridge's)",
      request.tool));
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
