#include "Trace/Mcp.hpp"

#include <algorithm>
#include <atomic>
#include <bitset>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
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
#if DEVELOPMENT
#include <array>
#include <map>
#include <numeric>
#include <span>

#include "Common/Api.hpp"
#include "Common/ShaderStage.hpp"
#include "Shaders/Compiler.hpp"
#include "Shaders/Database.hpp"
#include "System/Jobs.hpp"
#include "Shaders/Shaders.hpp"
#include "Shaders/Variants.hpp"
#include "System/Paths.hpp"
#include "Trace/Capture.hpp"
#include "Trace/Cbuffers.hpp"
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
   std::string type; // shader, custom, present
   std::optional<bool> deferred;
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
};

struct TraceSampler
{
   uint32_t slot = 0;
   std::string filter;
   uint32_t filter_id = 0;
   std::string address;
   std::array<uint32_t, 3> address_ids = {};
   float mip_lod_bias = 0.f;
};

// The entry's luma_trace_list fields, then its state
struct TraceGet
{
   bool ok = true;
   size_t index = 0;
   std::string type;
   std::optional<bool> deferred;
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
   uint32_t thread = 0;
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
   if (event.type == EventType::LumaPass || event.type == EventType::Present)
      reply.name = event.name;
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
      std::ranges::any_of(event.constant_buffers, [&](const Capture::ConstantBuffer& buffer) { return buffer.id == id; });
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
   };
   if (const std::optional<std::string> stage = arg("stage"); stage && !(filter.stage = ParseStage(*stage)))
      return Error("'stage' is VS, HS, DS, GS, PS or CS");
   if (request.args.contains("hash") && !(filter.hash = GetHash(request)))
      return Error("'hash' is a shader hash, like 0x12345678");
   const uint32_t offset = GetNumber(request, "offset", 0, 0, UINT32_MAX);
   const uint32_t limit = GetNumber(request, "limit", 300, 1, 5000);
   Replies::TraceList reply;
   // 2.0 records no binding or buffer write entries yet: those switches have nothing to include
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
      if (buffer.contents.empty())
         continue;
      std::span<const std::byte> contents = buffer.contents;
      if (buffer.constant_count)
      {
         const size_t first = std::min<size_t>(size_t(buffer.first_constant) * 16, contents.size());
         contents = contents.subspan(first, std::min<size_t>(size_t(buffer.constant_count) * 16, contents.size() - first));
      }
      uint32_t hash = 0;
      for (const Capture::Shader& shader : event.shaders)
         hash = shader.stage == buffer.stage ? shader.hash : hash;
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
      reply.samplers.push_back({sampler.slot, sampler.filter, sampler.filter_id, sampler.address_names, sampler.address, sampler.mip_lod_bias});
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
      return Utils::Json::Write(GetStatusReply(request));
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
   // Waits for the captures on the pipe thread
   if (request.tool == "trace_capture")
      return TraceCapture(request);
   if (request.tool == "trace_list")
      return TraceList(request);
   if (request.tool == "trace_get")
      return TraceGet(request);
#endif
   return Error(std::format("unknown tool '{}' (Luma 2 serves status, get_settings, set_setting, shader_list, shader_state, dump_shader, reload_shaders, "
                            "trace_capture, trace_list and trace_get so far; luma_log is the bridge's)",
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
