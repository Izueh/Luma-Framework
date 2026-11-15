#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "Shaders/Shaders.hpp"

// Luma/Cache/Shaders: compiled shaders by what they were built from, and the reflection of game shaders. Rebuilt freely,
// shipped prebuilt with releases. Any thread.
namespace Luma {
namespace Shaders {
namespace Cache {
// 1.0's check: the preprocessed text (the source with its includes and defines applied), with the target and entry point.
// Its #line directives are left out: they name files (one of them from the current directory), and don't reach bytecode
// compiled without debug information. So a cache built anywhere matches.
uint64_t GetKey(std::string_view preprocessed, std::string_view target, std::string_view entry);

// Patched bytecode (Shaders::Patching): the game shader's hash with everything its patch was made from. Separate from source keys.
uint64_t GetPatchKey(uint32_t hash, std::string_view patch);

std::optional<Bytecode> ReadShader(uint64_t key);
void WriteShader(uint64_t key, std::span<const uint8_t> bytecode);

// The reflection of a game shader, as its owner serialized it
std::optional<std::string> ReadReflection(uint32_t hash);
void WriteReflection(uint32_t hash, std::string_view text);

// The patch index: which patches matched their shader and which didn't, so a later boot patches nothing it has an answer for
std::optional<std::string> ReadPatchIndex();
void WritePatchIndex(std::string_view text);
} // namespace Cache
} // namespace Shaders
} // namespace Luma
