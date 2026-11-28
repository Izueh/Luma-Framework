#include "Mcp/McpTools.hpp"

#if DEVELOPMENT
#include <string>
#include <vector>

#include "Effects/SR/SuperResolution.hpp"
#include "Utils/Json.hpp"

namespace Luma {
namespace Mcp {
namespace Tools {
struct BackendReply
{
   std::string name;
   bool initialized = false;
   bool supported = false;
   bool auto_selectable = false;
   std::string provider_version;
};

struct SrStateReply
{
   bool ok = true;
   bool enabled = false;
   std::string user_type;
   std::string active_type;
   bool drew_this_frame = false;
   uint32_t output_width = 0;
   uint32_t output_height = 0;
   uint32_t render_width = 0;
   uint32_t render_height = 0;
   bool dynamic_resolution = false;
   bool hdr = false;
   bool inverted_depth = false;
   float jitter_x = 0.f;
   float jitter_y = 0.f;
   float vert_fov = 0.f;
   float near_plane = 0.f;
   float far_plane = 0.f;
   bool reset = false;
   int jitter_phases = 0;
   std::vector<BackendReply> backends;
   std::vector<std::string> ignored_args;
};

struct SrCaptureReply
{
   bool ok = true;
   std::string active_type;
   uint32_t render_width = 0;
   uint32_t render_height = 0;
   uint32_t output_width = 0;
   uint32_t output_height = 0;
   float jitter_x = 0.f;
   float jitter_y = 0.f;
   std::string note;
   std::vector<std::string> ignored_args;
};

namespace {
std::string UserTypeName(Effects::SR::UserType type)
{
   switch (type)
   {
   case Effects::SR::UserType::None: return "Off";
   case Effects::SR::UserType::Auto: return "Auto";
   case Effects::SR::UserType::Dlss: return "DLSS";
   case Effects::SR::UserType::Fsr3: return "FSR 3";
   case Effects::SR::UserType::Fsr4: return "FSR 4";
   case Effects::SR::UserType::XeSS: return "XeSS";
   default: return "Unknown";
   }
}
} // namespace

std::string SrState(const Request& request)
{
   const Effects::SR::RuntimeInfo info = Effects::SR::GetRuntimeInfo();
   SrStateReply reply;
   reply.enabled = info.enabled;
   reply.user_type = UserTypeName(info.user_type);
   reply.active_type = std::string(Effects::SR::GetTypeName(info.active_type));
   reply.drew_this_frame = info.drew_this_frame;
   reply.output_width = info.settings.output_width;
   reply.output_height = info.settings.output_height;
   reply.render_width = info.last_draw.render_width ? info.last_draw.render_width : info.settings.render_width;
   reply.render_height = info.last_draw.render_height ? info.last_draw.render_height : info.settings.render_height;
   reply.dynamic_resolution = info.settings.dynamic_resolution;
   reply.hdr = info.settings.hdr;
   reply.inverted_depth = info.settings.inverted_depth;
   reply.jitter_x = info.last_draw.jitter_x;
   reply.jitter_y = info.last_draw.jitter_y;
   reply.vert_fov = info.last_draw.vert_fov;
   reply.near_plane = info.last_draw.near_plane;
   reply.far_plane = info.last_draw.far_plane;
   reply.reset = info.last_draw.reset;
   reply.jitter_phases = info.jitter_phases;
   for (size_t i = 0; i < Effects::SR::TYPE_COUNT; i++)
   {
      const Effects::SR::Capabilities& cap = info.backends[i];
      reply.backends.push_back({std::string(Effects::SR::GetTypeName(Effects::SR::Type(i))), cap.initialized, cap.supported, cap.auto_selectable,
         cap.provider_version});
   }
   reply.ignored_args = GetIgnoredArgs(request, {});
   return Utils::Json::Write(reply);
}

std::string SrCapture(const Request& request)
{
   const Effects::SR::RuntimeInfo info = Effects::SR::GetRuntimeInfo();
   if (info.active_type == Effects::SR::Type::None)
      return Error("SR is not active");
   SrCaptureReply reply;
   reply.active_type = std::string(Effects::SR::GetTypeName(info.active_type));
   reply.render_width = info.last_draw.render_width ? info.last_draw.render_width : info.settings.render_width;
   reply.render_height = info.last_draw.render_height ? info.last_draw.render_height : info.settings.render_height;
   reply.output_width = info.settings.output_width;
   reply.output_height = info.settings.output_height;
   reply.jitter_x = info.last_draw.jitter_x;
   reply.jitter_y = info.last_draw.jitter_y;
   reply.note = "Use luma_read_resource on TAA draw slots (t1=depth, t2=color, rtv0=output) for pixel readback.";
   reply.ignored_args = GetIgnoredArgs(request, {"out_dir", "format", "buffers"});
   return Utils::Json::Write(reply);
}
} // namespace Tools
} // namespace Mcp
} // namespace Luma
#endif
