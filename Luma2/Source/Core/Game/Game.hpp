#pragma once

#include <concepts>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <Windows.h>

#include "Common/Api.hpp"
#include "Common/Device.hpp"
#include "Common/Math.hpp"
#include "Common/ShaderStage.hpp"
#include "Display/DisplayComposition.hpp"
#include "Draw/Draw.hpp"
#include "Frame/FrameState.hpp"
#include "Game/GameEvents.hpp"
#include "Resources/ResourceBindings.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Resources/TextureLoader.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Settings/SettingKeys.hpp"
#include "Swapchain/Swapchain.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Shaders/ShaderDefines.hpp"
#include "Shaders/ShaderPatching.hpp"
// "name"_h, how Luma's resources and shaders are named in the calls below
#include "Utils/Hash.hpp"

namespace Luma {
namespace Game {
struct Link
{
   std::string_view label;
   std::string_view url;
};

struct Info
{
   // Shown in the overlay's footer. Must outlive Luma (string literals, static arrays).
   std::string_view name;
   std::string_view version;
   // What the mod does and who made it, at the top of the About section
   std::string_view description;
   std::span<const std::string_view> credits;
   std::span<const Link> links;
   // The only API the game uses: the other one's hooks are never installed. Unset, Luma handles both.
   std::optional<Api> api;
};

// Implemented by the game. Under the loader lock: declarations only (no dll loads, threads or API objects).
void Init();

void SetInfo(const Info& info);

// Shaders. The spans and strings must outlive Luma (static arrays, literals), but AddDefine's.
namespace Shaders {
namespace Defines = Luma::Shaders::Defines;

// In the dev tools, and for a draw callback's hash list
void SetDebugName(std::string_view name, std::span<const uint32_t> hashes);
// The hashes a "<file_name>_0x########.ps_5_0.hlsl" replacement file stands for
void RedirectHashes(std::string_view file_name, std::span<const uint32_t> hashes);

// As the define's default, which users can still change. False, logged once, for a define this build doesn't have.
template <typename T, typename U>
   requires std::same_as<T, U>
bool SetDefine(const Luma::Shaders::Defines::Id<T>& define, U value)
{
   return Luma::Shaders::Defines::SetDefault(define.key, define.name, Settings::Value(std::move(value)));
}

// A name already registered is refused. The strings are copied.
void AddDefine(const Luma::Shaders::Defines::Definition& definition);
} // namespace Shaders

// For a game that defines `struct Luma::Game::GameDeviceData`. Created with each device before Core's objects, destroyed after
// on_destroy_device.
struct GameDeviceDataType
{
   GameDeviceData* (*create)() = nullptr;
   void (*destroy)(GameDeviceData* data) = nullptr;

   // A template, so it's compiled where the game's definition is visible
   template <typename T>
      requires std::same_as<T, GameDeviceData>
   static GameDeviceDataType Of()
   {
      return {[]() -> GameDeviceData* { return new T(); }, [](GameDeviceData* data) { delete static_cast<T*>(data); }};
   }
};

// Fixed for the process: anything changed at runtime is a setting instead
struct Options
{
   // The LumaData writer runs at every draw with a Luma shader; LumaData uploads only when its bytes changed
   Luma::Resources::LumaCBuffers::Options cbuffers;
   // The bound-state cache's groups
   Draw::Options draw;
   // Where Luma's own bindings go (D3D12's register space), and its D3D12 static samplers
   Luma::Resources::Bindings::Options bindings;
   // The .dds files of the Textures folders, as Resources::Database entries
   Luma::Resources::Textures::Options textures;
   // The game's anisotropic samplers get Luma's mirrors (16x, SR's LOD bias)
   Luma::Resources::Samplers::Options samplers;
   // The game's own bytecode patches (recipes are files in the Recipes folder)
   Luma::Shaders::Patching::Options patching;
   // How Luma presents: Mirror (the game draws to Luma's buffers) or in place, the HDR output mode, borderless
   Luma::Swapchain::Options swapchain;
   // Default display composition pipeline space and gamma/gamut/UI parameters (written to LumaSettings.DisplayComposition)
   Luma::Display::Composition::Options composition;
   GameDeviceDataType device_data;
};

// Once, in Init. A second call is ignored.
void SetOptions(const Options& options);

// The game's textures Luma upgrades through mirrors. Rules name DXGI formats shared by D3D11 and D3D12. Called in Init.
namespace Resources {
namespace Upgrade = Luma::Resources::Upgrade;

void SetUpgrades(const Luma::Resources::Upgrade::Rules& rules);
} // namespace Resources

// Drawn with UI::Widgets. Plain settings go through the registry instead.
struct Panels
{
   void (*settings)() = nullptr; // Below the registry's settings
   void (*info)() = nullptr; // In the Info tab (Development) or the settings view
   void (*about)() = nullptr; // Above the credits
   void (*development)() = nullptr; // Development builds only
};

void SetPanels(const Panels& panels);

// The game's part is its GameSettings member. Written on the presenting thread only; contexts upload what was last published.
using Luma::Resources::LumaCBuffers::cb_luma_global_settings;

// Core publishes at every present: this shows a change made elsewhere (on_init_swapchain) sooner. Presenting thread only.
void MarkLumaSettingsDirty();

// Null for a device Luma doesn't know. Any thread, the device or the game's proxy of it.
GameDeviceData* GetGameDeviceData(Device device);

// What the game's image is: its own HDR option. Any thread, applied at the next present:
// LumaSettings' display mode and white levels follow (SDR at 80 nits), the output mode doesn't.
namespace Display {
void SetHdr(bool hdr);
} // namespace Display

// The device's resolutions (Luma::Frame). Any thread, the device or the game's proxy of it.
namespace Frame {
Luma::Frame::Resolutions Get(Device device);
// What the game renders the scene at, from its SR or TAA path: the output's until set, and again after a resize
void SetRenderResolution(Device device, Math::uint2 size);
} // namespace Frame

// For the draw events (Events::on_variant_draw, on_draw_or_dispatch, OnShaderDraw), inside a Luma scope
namespace D3D11 {
// The game's call, with the bindings now on the context
void RunOriginal(const Call& call);
// Until the game binds another shader on `stage`. Nothing on a stage without a variant.
void UseOriginal(const Call& call, ShaderStage stage);
// With the resources the shader requests. From a draw event, Core then binds no variant's requests for that call.
void BindLumaShader(ID3D11DeviceContext* context, uint64_t name_hash, const Luma::Resources::Database::CustomData& custom = {});
// A texture of the Textures folders (Options::textures), by its file's name: "fast_noise"_h. Null if it didn't load. Lives as long
// as the device.
ID3D11ShaderResourceView* GetTexture(ID3D11Device* device, uint64_t name_hash);
} // namespace D3D11

// For the D3D12 draw events, inside a Luma scope. A draw event can't bind Luma's own shaders on D3D12 yet.
namespace D3D12 {
// The game's call on its list, with what the list has bound now (Luma's cbuffers for the variant bound included)
void RunOriginal(const Call& call);
// D3D12 can't put one stage back: the game's whole pipeline is bound, every stage of the call runs the game's shader, until the
// game binds another pipeline
void UseOriginal(const Call& call);
} // namespace D3D12
} // namespace Game
} // namespace Luma
