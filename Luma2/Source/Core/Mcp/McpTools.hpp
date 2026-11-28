#pragma once

#if DEVELOPMENT
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Common/ShaderStage.hpp"
#include "Mcp/McpServer.hpp"

// What the MCP tools share. Each family of tools is a file of its own (Mcp<Family>Tools.cpp) with its reply structs, so a change to
// one doesn't rebuild the others (their replies are serialized by glaze, the slowest thing to compile in Core). McpServer.cpp
// serves the pipe and dispatches to them.
namespace Luma {
namespace Mcp {
// Request arguments. The bridge adds a "timeout_ms" to every call: it is never reported as ignored.
std::vector<std::string> GetIgnoredArgs(const Request& request, std::initializer_list<std::string_view> taken);
uint32_t GetTimeout(const Request& request);
bool GetBool(const Request& request, std::string_view key);
uint32_t GetNumber(const Request& request, std::string_view key, uint32_t default_value, uint32_t min, uint32_t max);
// "0x12345678" or "12345678"
std::optional<uint32_t> GetHash(const Request& request, std::string_view key = "hash");
std::string FormatHash(uint32_t hash);

inline std::string_view GetStageName(ShaderStage stage)
{
   constexpr std::string_view NAMES[] = {"VS", "HS", "DS", "GS", "PS", "CS", "AS", "MS"};
   return NAMES[size_t(stage)];
}

// The reply of a failed call
std::string Error(std::string message);
// Runs the function at the next present (the settings registry and the render thread's state), and waits for it
std::string RunOnRenderThread(std::function<std::string()> function, uint32_t timeout_ms);

// One function per tool, each answering with its JSON reply
namespace Tools {
// McpSettingsTools.cpp
std::string GetSettings(const Request& request);
std::string SetSetting(const Request& request);
// McpShaderTools.cpp
std::string ShaderList(const Request& request);
std::string ShaderState(const Request& request);
std::string DumpShader(const Request& request);
std::string DumpStats(const Request& request);
std::string ReloadShaders(const Request& request);
// McpPatchTools.cpp
std::string PatchState(const Request& request);
std::string SetRecipe(const Request& request);
std::string ReloadPatches(const Request& request);
// McpTraceTools.cpp
std::string TraceCapture(const Request& request);
std::string TraceList(const Request& request);
std::string TraceGet(const Request& request);
// McpReadbackTools.cpp
std::string ReadResource(const Request& request);
std::string ReadCbuffer(const Request& request);
// McpSrTools.cpp
std::string SrState(const Request& request);
std::string SrCapture(const Request& request);
// McpDevTools.cpp
std::string DevValues(const Request& request);
std::string SetDevValue(const Request& request);
} // namespace Tools
} // namespace Mcp
} // namespace Luma
#endif
