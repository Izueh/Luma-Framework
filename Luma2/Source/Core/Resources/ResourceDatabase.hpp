#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Common/ShaderStage.hpp"
#include "Shaders/Shaders.hpp"

struct IUnknown;

namespace Luma::Frame {
struct DeviceState;
}

namespace Luma::Game {
struct GameDeviceData;
}

// Luma's named resources, which any module resolves by "name"_h and the framework creates and releases on every device (nothing
// of Luma's keeps a device alive): Luma's shaders, found in the files, and its cbuffers. Everything Luma ships is registered by
// Core at init from what's on disk; a game registers only what it captures from the game at runtime. Textures from disk and
// invalidation (a resize) come with Step 4.
namespace Luma {
namespace Resources {
namespace Database {
enum class Kind : uint8_t
{
   Shader,
   ConstantBuffer,
};

// Core's entries are read-only: a game registering one of their names is refused
enum class Owner : uint8_t
{
   Core,
   Game,
};

// A pass's values for LumaData (1.0's CustomData1 to 4), given to the writers of the entries it requests
struct CustomData
{
   uint32_t custom_data_1 = 0;
   uint32_t custom_data_2 = 0;
   float custom_data_3 = 0.f;
   float custom_data_4 = 0.f;

   friend bool operator==(const CustomData&, const CustomData&) = default;
};

// What a constant buffer's contents are written for: the device of the context uploading them and its state, which the context
// finds once (a write, at every draw with a Luma shader, looks nothing up)
struct WriteTarget
{
   IUnknown* device = nullptr;
   const Frame::DeviceState* frame = nullptr; // Its resolutions (Frame::Read)
   Game::GameDeviceData* game_device_data = nullptr; // Null without Game::Options::device_data
};

struct Entry
{
   uint64_t name_hash = 0;
   std::string name;
   Kind kind = Kind::Shader;
   Owner owner = Owner::Core;
   // Shaders: their stage, compiled by Shaders and published here
   ShaderStage stage = ShaderStage::Pixel;
   std::string file; // From the Shaders root
   // Constant buffers: their size and their contents now for a pass's custom values and the context's device. With a stamp, which
   // moves whenever their contents may have changed (1.0's dirty flag), a context writes and uploads them again only when it moved
   // since its last upload (LumaSettings). Without, a context writes them at every bind and uploads them when the bytes differ from
   // its last upload (LumaData, 1.0's compare).
   uint32_t size = 0;
   void (*write)(std::span<std::byte> destination, const CustomData& custom, const WriteTarget& target) = nullptr;
   uint64_t (*get_stamp)() = nullptr;
};

// What a context last uploaded of a constant buffer entry: its stamp, or its bytes for an entry without one
struct Upload
{
   uint64_t stamp = 0;
   std::vector<std::byte> bytes;
   bool done = false;
};

// An entry with a stamp: whether the context uploads it again, the stamp now being `stamp`
bool NeedsUpload(const Upload& upload, uint64_t stamp);
// An entry without: `written` holds its contents now. When they differ from the last upload they become it (`written` gets the
// old bytes, its storage reused at the next write) and the context uploads them.
bool ReplaceIfChanged(Upload& upload, std::vector<std::byte>& written);

// Init only, before any device: entries are fixed from then on (a shader file added while the game runs needs a restart).
// Null for a name already registered. A game's entry with the name of one of Core's is refused, or replaced when it came first.
std::optional<uint32_t> Register(Entry entry);
std::optional<uint32_t> Find(uint64_t name_hash);
const Entry& Get(uint32_t index);
uint32_t GetCount();

struct ShaderState
{
   // The last version that compiled: a failed build keeps it
   std::shared_ptr<const Shaders::CompiledShader> compiled;
   std::string messages;
   bool failed = false;
};

// Presenting thread: a build's shaders, by entry. Equal bytes keep the compiled shader they had, so no object is recreated.
void PublishShaders(std::vector<std::pair<uint32_t, ShaderState>> shaders);
// Any thread
ShaderState GetShader(uint32_t index);
// Any thread: changes with every publication
uint64_t GetGeneration();
} // namespace Database
} // namespace Resources
} // namespace Luma
