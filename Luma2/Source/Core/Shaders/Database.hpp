#pragma once

#include <atomic>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Common/Api.hpp"
#include "Shaders/Compiler.hpp"
#include "Shaders/Shaders.hpp"
#include "Shaders/Variants.hpp"

// Every shader the game creates, by hash (CRC32 of its bytecode, as 1.0). A shader is never forgotten, so pointers to it stay
// valid; each API attaches its objects' state to them, destroyed with the object.
namespace Luma {
namespace Shaders {
namespace Database {
#if DEVELOPMENT
// The slots a shader uses (1.0's CachedShader), from its reflection data, else from its disassembly. All of them when neither
// tells (`found` false).
struct Reflection
{
   std::bitset<14> constant_buffers;
   std::bitset<16> samplers;
   std::bitset<128> shader_resources;
   std::bitset<64> unordered_access;
   std::bitset<8> render_targets;
   bool found = false;
   // Empty for stripped shaders
   std::vector<Compiler::ConstantBuffer> constant_buffer_layouts;
};
#endif

struct Shader
{
   uint32_t hash = 0;
   ShaderStage stage = ShaderStage::Pixel;
   std::string target; // "ps_5_0", empty if the bytecode doesn't say
   uint32_t size = 0;
   std::string_view name; // Game::Shaders::SetDebugName
   bool draw_callback = false; // Game::OnDraw named it
   // The game's objects with this bytecode now
   std::atomic<uint32_t> objects = 0;
#if DEVELOPMENT || TEST // For dumping
   Bytecode bytecode;
#endif
#if DEVELOPMENT
   Reflection reflection;
   // The trace's per shader actions: the variant (or the original) its binds take until cleared, its draws don't run
   std::atomic<Variants::Choice> forced_variant = Variants::Choice::Auto;
   std::atomic<bool> skip_draws = false;
#endif
};

// The dump setting (Development and Test builds)
void Register();
// Init only: names game shaders in the dev tools (Game::Shaders::SetDebugName). The span and string must outlive Luma.
void Name(std::string_view name, std::span<const uint32_t> hashes);
// Empty for a shader the game didn't name
std::string_view GetName(uint32_t hash);
// Any thread: the shader of a hash, null if the game never created it
Shader* Find(uint32_t hash);
// Any thread: adds `shader` unless another thread added its hash first, and returns the one kept
Shader& Add(std::unique_ptr<Shader> shader);
// Any thread
std::vector<const Shader*> GetShaders();

// "ps_5_0" from the program chunk of DXBC bytecode, empty if it has none
std::string GetTarget(std::span<const uint8_t> bytecode);

#if DEVELOPMENT || TEST
// Presenting thread: dumps the shaders created since the last present to Luma/Dump, once each, on the job queue
void QueueDumps();
// Writes "0x<hash>.<target>.cso" to Luma/Dump now, and returns its path. Empty if it couldn't.
std::filesystem::path Dump(const Shader& shader);
#endif
} // namespace Database
} // namespace Shaders
} // namespace Luma
