#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Common/Device.hpp"
#include "Common/ShaderStage.hpp"
#include "Shaders/Shaders.hpp"
#include "Utils/Hash.hpp"

namespace Luma::FrameState {
struct DeviceState;
}

namespace Luma::Game {
struct GameDeviceData;
}

// Luma's named resources, which any module resolves by "name"_h and the framework creates and releases on every device (nothing
// of Luma's keeps a device alive): Luma's shaders, found in the files, its cbuffers, and the textures in the Textures folders
// (Resources::Textures). Everything Luma ships is registered by Core at init from what's on disk; a game registers only what it
// captures from the game at runtime. Nothing is invalidated on a resize yet.
namespace Luma {
namespace Resources {
namespace Database {
enum class Kind : uint8_t
{
   Shader,
   ConstantBuffer,
   Texture, // Loaded from disk (.dds), bound as a shader resource view
   RuntimeTexture, // Tagged from a game pass or lazily created/copied at runtime
};

enum class Invalidation : uint8_t
{
   None = 0,
   SwapchainResize = 1 << 0,
   RenderResolutionChange = 1 << 1,
};

constexpr Invalidation operator|(Invalidation a, Invalidation b)
{
   return Invalidation(uint8_t(a) | uint8_t(b));
}

constexpr Invalidation operator&(Invalidation a, Invalidation b)
{
   return Invalidation(uint8_t(a) & uint8_t(b));
}

constexpr bool HasInvalidation(Invalidation value, Invalidation flag)
{
   return (uint8_t(value) & uint8_t(flag)) != 0;
}

// Well-known RuntimeTexture entry hashes used by Effects::SR and game mods
namespace Sr {
inline constexpr uint64_t COLOR = Utils::Hash::Name("sr_color");
inline constexpr uint64_t DEPTH = Utils::Hash::Name("sr_depth");
inline constexpr uint64_t MOTION_VECTORS = Utils::Hash::Name("sr_motion_vectors");
inline constexpr uint64_t OUTPUT = Utils::Hash::Name("sr_output");
inline constexpr uint64_t EXPOSURE = Utils::Hash::Name("sr_exposure");
inline constexpr uint64_t REACTIVE = Utils::Hash::Name("sr_reactive");
inline constexpr uint64_t TRANSPARENCY = Utils::Hash::Name("sr_transparency");
} // namespace Sr

// Core's entries are read-only: a game registering one of their names is refused
enum class Owner : uint8_t
{
   Core,
   Game,
};

// A pass's values for LumaData (CustomData1 to 4), given to the writers of the entries it requests
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
   Device device;
   const FrameState::DeviceState* frame = nullptr; // Its resolutions (FrameState::Read)
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
   std::string file; // Shaders: from the Shaders root. Textures: from the Textures root.
   // Constant buffers: their size and their contents now for a pass's custom values and the context's device. With a stamp, which
   // moves whenever their contents may have changed, a context writes and uploads them again only when it moved
   // since its last upload (LumaSettings). Without, a context writes them at every bind and uploads them when the bytes differ from
   // its last upload (LumaData).
   uint32_t size = 0;
   void (*write)(std::span<std::byte> destination, const CustomData& custom, const WriteTarget& target) = nullptr;
   uint64_t (*get_stamp)() = nullptr;
   // RuntimeTexture: when Core retires its managed GPU texture
   Invalidation invalidation = Invalidation::None;
};

// What a context last uploaded of a constant buffer entry: its stamp, or its bytes for an entry without one
struct Upload
{
   uint64_t stamp = 0;
   std::vector<std::byte> bytes;
   bool done = false;
};

bool NeedsUpload(const Upload& upload, uint64_t stamp);
bool ReplaceIfChanged(Upload& upload, std::vector<std::byte>& written);

template <typename EmitFn>
void UploadIfDirty(const Entry& entry, Upload& upload, std::vector<std::byte>& scratch, const CustomData& custom, const WriteTarget& target, EmitFn&& emit)
{
   if (entry.get_stamp)
   {
      const uint64_t stamp = entry.get_stamp();
      if (!NeedsUpload(upload, stamp))
         return;
      upload.done = emit([&](std::span<std::byte> destination) { entry.write(destination, custom, target); });
      upload.stamp = stamp;
      return;
   }
   scratch.resize(entry.size);
   entry.write(scratch, custom, target);
   if (ReplaceIfChanged(upload, scratch))
      upload.done = emit([&](std::span<std::byte> destination) { std::ranges::copy(upload.bytes, destination.begin()); });
}

// Registers Core's built-in RuntimeTexture entries (Sr::COLOR, DEPTH, MOTION_VECTORS, OUTPUT, ...)
void RegisterRuntimeDefaults();
// Registers a game-owned RuntimeTexture entry in Init()
std::optional<uint32_t> RegisterRuntimeTexture(std::string_view name, Invalidation invalidation = Invalidation::SwapchainResize | Invalidation::RenderResolutionChange);
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

// A device's compiled Luma shaders (Shaders::Store), by entry. Never held with another lock.
struct ShaderStates
{
   mutable std::shared_mutex mutex;
   std::vector<ShaderState> by_entry = std::vector<ShaderState>(GetCount());
   std::atomic<uint64_t> generation = 0;
};

// Presenting thread: a build's shaders, by entry. Equal bytes keep the compiled shader they had, so no object is recreated.
void PublishShaders(Shaders::Store& store, std::vector<std::pair<uint32_t, ShaderState>> shaders);
// Any thread
ShaderState GetShader(const Shaders::Store& store, uint32_t index);
// Any thread: changes with every publication to the store
uint64_t GetGeneration(const Shaders::Store& store);
} // namespace Database
} // namespace Resources
} // namespace Luma
