#pragma once

#if DEVELOPMENT
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Shaders/Shaders.hpp"

// Asynchronous HLSL decompilation for Development builds using 3DMigoto cmd_Decompiler.exe (DXBC)
// and RenoDX decomp.exe --flatten --do-while (DXIL), cached in Luma/Cache/Decomp/.
namespace Luma {
namespace Shaders {
namespace Decompiler {
enum class State : uint8_t
{
   Idle,
   Running,
   Ready,
   Failed,
};

struct Result
{
   State state = State::Idle;
   bool cached = false;
   std::string tool; // "cmd_Decompiler.exe" or "decomp.exe"
   std::string hlsl;
   std::string error;
};

// Returns current cached/in-progress result for (hash, tag) where tag is e.g. "original" or "patched"
Result GetResult(uint32_t hash, std::string_view tag);
// Starts async decompilation on System::Jobs if not already cached or running
void Request(uint32_t hash, std::string_view tag, Bytecode bytecode);
// Reads a replacement HLSL file from the shader roots (optionally preprocessed with #includes inlined)
std::optional<std::string> ReadReplacementSource(std::string_view file, bool inline_includes);
} // namespace Decompiler
} // namespace Shaders
} // namespace Luma
#endif
