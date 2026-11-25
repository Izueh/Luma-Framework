#include "Trace/Mcp/McpTools.hpp"

#if DEVELOPMENT
#include <array>
#include <charconv>
#include <string>
#include <vector>

#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Utils/Json.hpp"

namespace Luma {
namespace Trace {
namespace Mcp {
namespace Tools {
struct DevValuesReply
{
   bool ok = true;
   std::array<float, 10> values = {};
   std::vector<std::string> ignored_args;
};

struct SetDevValueReply
{
   bool ok = true;
   uint32_t index = 0;
   float value = 0.f;
   std::vector<std::string> ignored_args;
};

std::string DevValues(const Request& request)
{
   DevValuesReply reply;
   for (size_t i = 0; i < Settings::Core::DEV_SETTINGS.size(); i++)
      reply.values[i] = Settings::Get(Settings::Core::DEV_SETTINGS[i]);
   reply.ignored_args = GetIgnoredArgs(request, {});
   return Utils::Json::Write(reply);
}

std::string SetDevValue(const Request& request)
{
   const uint32_t index = GetNumber(request, "index", 0, 1, 10);
   if (index == 0)
      return Error("index (1..10) is required");
   const auto it = request.args.find("value");
   if (it == request.args.end())
      return Error("value is required");
   float value = 0.f;
   const auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
   if (ec != std::errc{})
      return Error("value must be a number");
   Settings::Set(Settings::Core::DEV_SETTINGS[index - 1], value);
   SetDevValueReply reply;
   reply.index = index;
   reply.value = value;
   reply.ignored_args = GetIgnoredArgs(request, {"index", "value"});
   return Utils::Json::Write(reply);
}
} // namespace Tools
} // namespace Mcp
} // namespace Trace
} // namespace Luma
#endif
