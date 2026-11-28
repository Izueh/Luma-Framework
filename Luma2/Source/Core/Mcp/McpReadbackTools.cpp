#include "Mcp/McpTools.hpp"

#if DEVELOPMENT
#include <chrono>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Shaders/ShaderDatabase.hpp"
#include "Shaders/Shaders.hpp"
#include "Trace/CbufferTracker.hpp"
#include "Trace/FrameCapture.hpp"
#include "Trace/ResourceReadback.hpp"
#include "Utils/Format.hpp"
#include "Utils/Json.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Mcp {
// In a named namespace: glaze can't reflect types with internal linkage
namespace Replies {
// The luma_read_resource reply: where the .bin is and how the bridge decodes it
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

// The luma_read_cbuffer reply: the bound range as hex dwords, or one entry per frame
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
} // namespace Replies

namespace {
// The call a readback targets: by "index" from the last capture (its primary shader, and that shader's instance in the frame), or by
// "hash" and "instance" (the n-th call of that shader in a frame)
struct CallTarget
{
   uint32_t hash = 0;
   ShaderStage stage = ShaderStage::Pixel;
   uint32_t instance = 0;
};

std::expected<CallTarget, std::string> ResolveCall(const Request& request)
{
   if (request.args.contains("index"))
   {
      const std::shared_ptr<const Trace::Capture::Frame> frame = Trace::Capture::GetLast();
      const uint32_t index = GetNumber(request, "index", UINT32_MAX, 0, UINT32_MAX);
      if (!frame || index >= frame->events.size())
         return std::unexpected(std::format("trace index {} is out of range (captured {}), run luma_trace_capture first", index, frame ? frame->events.size() : 0));
      const Trace::Capture::Shader* primary = Trace::Capture::GetPrimaryShader(frame->events[index]);
      if (!primary || !primary->luma_shader.empty() || !primary->hash)
         return std::unexpected(std::format("trace index {} is not a call of a game shader Luma knows (Luma's passes aren't read back by index yet)", index));
      return CallTarget{primary->hash, primary->stage, Trace::Capture::GetInstance(*frame, index, primary->stage, primary->hash)};
   }
   if (const std::optional<uint32_t> hash = GetHash(request))
   {
      const Shaders::Database::Shader* shader = Shaders::Database::Find(*hash);
      if (!shader || !shader->objects)
         return std::unexpected(std::format("shader {} isn't live (never created, or released)", FormatHash(*hash)));
      return CallTarget{*hash, shader->stage, GetNumber(request, "instance", 0, 0, UINT32_MAX)};
   }
   return std::unexpected("pass either 'index' (from luma_trace_list) or 'hash' (and optionally 'instance')");
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

// `directory` is the readback root or inside it, ".." and junctions resolved
bool IsInReadbackRoot(const std::filesystem::path& directory)
{
   std::error_code error;
   const std::filesystem::path root = std::filesystem::weakly_canonical(GetReadbackRoot(), error);
   if (error || root.empty())
      return false;
   const std::filesystem::path canonical = std::filesystem::weakly_canonical(directory, error);
   return !error && std::mismatch(root.begin(), root.end(), canonical.begin(), canonical.end()).first == root.end();
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
} // namespace

namespace Tools {
std::string ReadResource(const Request& request)
{
   Replies::ReadResource reply;
   reply.ignored_args = GetIgnoredArgs(request, {"index", "hash", "instance", "view", "slot", "mip", "original", "replaced", "out_dir"});
   const auto view_argument = request.args.find("view");
   const std::string view_name = view_argument != request.args.end() ? view_argument->second : "rtv";
   const std::optional<Trace::Readback::ViewKind> view = Trace::Readback::ParseViewKind(view_name);
   if (!view)
      return Error("'view' must be rtv, srv, uav, depth, stencil, or swapchain (ui and registered:<name> are not implemented yet)");
   std::filesystem::path directory = GetReadbackRoot();
   if (const auto out_dir = request.args.find("out_dir"); out_dir != request.args.end())
   {
      directory = Utf8Path(out_dir->second);
      if (!IsInReadbackRoot(directory))
         return Error(std::format("'out_dir' must be {} or a folder inside it", Utils::Strings::ToUtf8(GetReadbackRoot())));
   }
   Trace::Readback::Target target = {.view = *view, .slot = GetNumber(request, "slot", 0, 0, 127), .mip = GetNumber(request, "mip", 0, 0, 15), .original = GetBool(request, "original")};
   std::string stem = "swapchain";
   if (*view != Trace::Readback::ViewKind::Swapchain)
   {
      const std::expected<CallTarget, std::string> call = ResolveCall(request);
      if (!call)
         return Error(call.error());
      target.hash = call->hash;
      target.stage = call->stage;
      target.instance = call->instance;
      reply.hash = FormatHash(call->hash);
      reply.stage = std::string(GetStageName(call->stage));
      reply.instance = call->instance;
      stem = std::format("{:08X}_{}_{}{}", call->hash, call->instance, view_name, target.slot);
   }
   Trace::Readback::Result result = Trace::Readback::Read(target, std::chrono::steady_clock::now() + std::chrono::milliseconds(GetTimeout(request)));
   if (!result.texture)
      return Error(result.error.empty() ? std::string("the readback failed") : std::move(result.error));
   Trace::Readback::Texture& texture = *result.texture;
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
   if (*view == Trace::Readback::ViewKind::Swapchain)
      reply.note = "Read at present, before Luma's overlay";
   return Utils::Json::Write(reply);
}

// The luma_read_cbuffer tool, through Track: the slot is tracked for as many frames as asked, then the overlay's tracking (if any) is
// put back
std::string ReadCbuffer(const Request& request)
{
   Replies::ReadCbuffer reply;
   reply.ignored_args = GetIgnoredArgs(request, {"index", "hash", "instance", "slot", "frames"});
   const std::expected<CallTarget, std::string> call = ResolveCall(request);
   if (!call)
      return Error(call.error());
   const uint32_t slot = GetNumber(request, "slot", UINT32_MAX, 0, 13);
   if (slot == UINT32_MAX)
      return Error("'slot' is the constant buffer register, 0 to 13");
   const uint32_t frames = GetNumber(request, "frames", 1, 1, 600);
   reply.hash = FormatHash(call->hash);
   reply.stage = std::string(GetStageName(call->stage));
   reply.instance = call->instance;
   reply.slot = slot;
   const std::optional<Trace::Track::Target> previous = Trace::Track::GetTarget();
   Trace::Track::SetTarget(Trace::Track::Target{.hash = call->hash, .stage = call->stage, .slot = slot, .instance = call->instance});
   // Frames without the call are misses: a few in a row and it's reported missing
   constexpr uint64_t MAX_MISSED_FRAMES = 3;
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(GetTimeout(request));
   std::vector<Trace::Track::Reading> readings;
   uint64_t last_reading = 0;
   uint64_t waited_from = Trace::Track::GetFrame();
   std::string error;
   while (readings.size() < frames)
   {
      if (std::chrono::steady_clock::now() > deadline)
      {
         error = std::format("timed out with {} of {} frames read: the game isn't presenting (minimized, paused or loading)", readings.size(), frames);
         break;
      }
      const std::optional<Trace::Track::Reading> reading = Trace::Track::GetReading();
      if (reading && !reading->stale && reading->frame > last_reading)
      {
         last_reading = reading->frame;
         waited_from = Trace::Track::GetFrame();
         readings.push_back(*reading);
         continue;
      }
      if (Trace::Track::GetFrame() > waited_from + MAX_MISSED_FRAMES)
      {
         error = std::format("the call wasn't reached, or had no constant buffer at b{}, in {} frames (check 'instance' and 'slot')", slot, MAX_MISSED_FRAMES);
         break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
   }
   Trace::Track::SetTarget(previous);
   if (readings.size() < frames)
      return Error(std::move(error));
   const Trace::Track::Reading& first = readings.front();
   reply.resource = first.buffer;
   if (frames > 1)
   {
      reply.series.emplace();
      for (const Trace::Track::Reading& reading : readings)
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
} // namespace Tools
} // namespace Mcp
} // namespace Luma
#endif
