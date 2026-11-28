#pragma once

#include <concepts>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <Windows.h>

#include "Common/Api.hpp"
#include "Common/Device.hpp"
#include "Common/Math.hpp"
#include "Common/ShaderStage.hpp"
#include "Display/DisplayComposition.hpp"
#include "Draw/Draw.hpp"
#include "Effects/SR/SuperResolution.hpp"
#include "FrameState/FrameState.hpp"
#include "Game/GameEvents.hpp"
#include "Resources/ConstantCapture.hpp"
#include "Resources/ResourceBindings.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Resources/TextureLoader.hpp"
#include "Resources/ResourceUpgrades.hpp"
#include "Notifications/Notifications.hpp"
#include "Settings/SettingKeys.hpp"
#include "Display/Swapchain.hpp"
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

// How far the mod is, shown in About. Publishing builds warn about the last two at start.
enum class DevelopmentState : uint8_t
{
   Finished,
   Playable,
   WorkInProgress,
   NonFunctional,
};

constexpr std::string_view GetName(DevelopmentState state)
{
   switch (state)
   {
   case DevelopmentState::Finished: return "Finished";
   case DevelopmentState::Playable: return "Playable";
   case DevelopmentState::WorkInProgress: return "Work in Progress";
   case DevelopmentState::NonFunctional: return "Non Functional";
   }
   return "?";
}

struct Info
{
   // Shown in the overlay's footer. Must outlive Luma (string literals, static arrays).
   std::string_view name;
   std::string_view version;
   // What the mod does and who made it, at the top of the About section
   std::string_view description;
   std::span<const std::string_view> credits;
   std::span<const Link> links;
   DevelopmentState state = DevelopmentState::Finished;
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
   // Indirect mirror propagation across copies, draws and shader-hash seeds
   Luma::Resources::Chains::Options chains;
   // Resolution scaling for mirrors, viewports, scissors and copies
   Luma::Resources::Scaling::Options scaling;
   // The game's own bytecode patches (recipes are files in the Recipes folder)
   Luma::Shaders::Patching::Options patching;
   // How Luma presents: Mirror (the game draws to Luma's buffers) or in place, the HDR output mode, borderless
   Luma::Display::Swapchain::Options swapchain;
   // Default display composition pipeline space and gamma/gamut/UI parameters (written to LumaSettings.DisplayComposition)
   Luma::Display::Composition::Options composition;
   // Super Resolution (DLSS, FSR 3, FSR 4, XeSS)
   Luma::Effects::SR::Options sr;
   // Constant buffer capture (D3D11 Map/Unmap/UpdateSubresource, D3D12 upload heap read)
   Luma::Resources::ConstantCapture::Options constant_capture;
   GameDeviceDataType device_data;
   // 1.0's AVOID_INPUT_LOSS: asserts as toasts, for a game that loses input or crashes when a message box takes the focus. The default
   // of the Luma.Advanced.SilentAsserts setting.
   Luma::Notifications::Options notifications;
   // Only these APIs are hooked: a device or swapchain of another is left to the game
   ApiSet supported_apis = {Api::D3D11, Api::D3D12};
};

// Once, in Init. A second call is ignored.
void SetOptions(const Options& options);

// The game's textures Luma upgrades through mirrors. Rules name DXGI formats shared by D3D11 and D3D12. Called in Init.
namespace Resources {
namespace Upgrade = Luma::Resources::Upgrade;
namespace Chains = Luma::Resources::Chains;

void SetUpgrades(const Luma::Resources::Upgrade::Rules& rules);

namespace Scaling {
using Mode = Luma::Resources::Scaling::Mode;
using Options = Luma::Resources::Scaling::Options;

void Begin(Device device);
void End(Device device);
bool IsActive(Device device);
void SetTargetResolution(Device device, Math::uint2 size);
} // namespace Scaling

namespace Database {
namespace Sr = Luma::Resources::Database::Sr;
using Invalidation = Luma::Resources::Database::Invalidation;

std::optional<uint32_t> RegisterRuntimeTexture(std::string_view name,
   Invalidation invalidation = Invalidation::SwapchainResize | Invalidation::RenderResolutionChange);
} // namespace Database

namespace ConstantCapture {
using BufferWriteCallback = Luma::Resources::ConstantCapture::BufferWriteCallback;
void RegisterWriteCallback(uint32_t byte_size, BufferWriteCallback callback);

inline void PauseForFrame() { Luma::Resources::ConstantCapture::PauseForFrame(); }
} // namespace ConstantCapture
} // namespace Resources

namespace Effects {
namespace SR {
using DrawOverrides = Luma::Effects::SR::DrawOverrides;
using Type = Luma::Effects::SR::Type;

bool IsActive();
bool Draw(const Game::D3D11::Call& call, const DrawOverrides& overrides = {});
bool Draw(const Game::D3D12::Call& call, const DrawOverrides& overrides = {});
Math::uint2 FindClosestIntegerResolutionForAspectRatio(double target_width, double target_height, double aspect_ratio);
} // namespace SR
} // namespace Effects

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
namespace Swapchain = Luma::Display::Swapchain;
void SetHdr(bool hdr);
} // namespace Display

// The device's resolutions, camera, and pass markers (Luma::FrameState). Any thread, the device or the game's proxy of it.
namespace FrameState {
using Camera = Luma::FrameState::Camera;
using PassMarker = Luma::FrameState::PassMarker;

Luma::FrameState::Resolutions Get(Device device);
// What the game renders the scene at, from its SR or TAA path: the output's until set, and again after a resize
void SetRenderResolution(Device device, Math::uint2 size);
void SetCamera(Device device, const Camera& camera);
Camera GetCamera(Device device);
void MarkPass(Device device, PassMarker marker);
bool HasPass(Device device, PassMarker marker);
void MarkPass(Device device, uint64_t custom_hash);
bool HasPass(Device device, uint64_t custom_hash);
} // namespace FrameState

// For the draw events (Events::on_variant_draw, on_draw_or_dispatch, OnShaderDraw), inside a Luma scope
namespace D3D11 {
// The game's call, with the bindings now on the context
void RunOriginal(const Call& call);
// Until the game binds another shader on `stage`. Nothing on a stage without a variant.
void UseOriginal(const Call& call, ShaderStage stage);
// With the resources the shader requests. From a draw event, Core then binds no variant's requests for that call.
void BindLumaShader(ID3D11DeviceContext* context, uint64_t name_hash, const Luma::Resources::Database::CustomData& custom = {});
bool HasLumaShader(ID3D11Device* device, uint64_t name_hash);
// A texture of the Textures folders (Options::textures), by its file's name: "fast_noise"_h. Null if it didn't load. Lives as long
// as the device.
ID3D11ShaderResourceView* GetTexture(ID3D11Device* device, uint64_t name_hash);
// RuntimeTexture Resource Database helpers
ID3D11Resource* TagBoundSRV(const Call& call, ShaderStage stage, UINT slot, uint64_t name_hash);
ID3D11Resource* TagBoundRTV(const Call& call, UINT slot, uint64_t name_hash);
ID3D11Resource* TagBoundDSV(const Call& call, uint64_t name_hash);
ID3D11Resource* TagBoundUAV(const Call& call, UINT slot, uint64_t name_hash);
ID3D11Texture2D* CopyBoundSRV(const Call& call, ShaderStage stage, UINT slot, uint64_t name_hash, DXGI_FORMAT format_override = DXGI_FORMAT_UNKNOWN,
   UINT extra_bind_flags = 0);
ID3D11Texture2D* CopyBoundRTV(const Call& call, UINT slot, uint64_t name_hash, DXGI_FORMAT format_override = DXGI_FORMAT_UNKNOWN,
   UINT extra_bind_flags = 0);
ID3D11Texture2D* EnsureTexture(ID3D11Device* device, uint64_t name_hash, UINT width, UINT height, DXGI_FORMAT format, UINT bind_flags);
ID3D11Resource* GetResource(ID3D11Device* device, uint64_t name_hash);
ID3D11RenderTargetView* GetRTV(ID3D11Device* device, uint64_t name_hash);
ID3D11ShaderResourceView* GetSRV(ID3D11Device* device, uint64_t name_hash);
ID3D11UnorderedAccessView* GetUAV(ID3D11Device* device, uint64_t name_hash);
// Mirror/original and view/resource helpers (null if the object has no mirror or is not a mirror)
ID3D11Resource* FindMirror(ID3D11Resource* resource);
ID3D11Resource* FindOriginal(ID3D11Resource* mirror);
ID3D11View* FindMirrorView(ID3D11View* view);
ID3D11View* FindOriginalView(ID3D11View* mirror_view);

template <typename View>
   requires std::derived_from<View, ID3D11View>
View* FindMirror(View* view)
{
   return static_cast<View*>(FindMirrorView(view));
}

template <typename View>
   requires std::derived_from<View, ID3D11View>
View* FindOriginal(View* mirror_view)
{
   return static_cast<View*>(FindOriginalView(mirror_view));
}

ID3D11Resource* GetViewResource(ID3D11View* view);
std::vector<ID3D11View*> GetResourceViews(ID3D11Resource* resource);
} // namespace D3D11

// For the D3D12 draw events, inside a Luma scope.
namespace D3D12 {
// The game's call on its list, with what the list has bound now (Luma's cbuffers for the variant bound included)
void RunOriginal(const Call& call);
// D3D12 can't put one stage back: the game's whole pipeline is bound, every stage of the call runs the game's shader, until the
// game binds another pipeline
void UseOriginal(const Call& call);
// The viewport the game last bound at `index` on `list` (requires RSSetViewports hook, e.g. Scaling enabled)
std::optional<D3D12_VIEWPORT> GetViewport(ID3D12GraphicsCommandList* list, UINT index = 0);
// RuntimeTexture Resource Database helpers
ID3D12Resource* TagBoundSRV(const Call& call, ShaderStage stage, UINT slot, uint64_t name_hash);
ID3D12Resource* TagBoundRTV(const Call& call, UINT slot, uint64_t name_hash);
ID3D12Resource* TagBoundDSV(const Call& call, uint64_t name_hash);
ID3D12Resource* EnsureTexture(ID3D12Device* device, uint64_t name_hash, UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags);
ID3D12Resource* GetResource(ID3D12Device* device, uint64_t name_hash);
D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(ID3D12Device* device, uint64_t name_hash);
// Reads a constant buffer bound at `slot` (space 0) of `stage` directly from its mapped upload heap
bool ReadBoundCBuffer(const Call& call, ShaderStage stage, UINT slot, std::span<std::byte> destination);
// Draws a fullscreen quad using Luma's compiled VS/PS into a RuntimeTexture entry (using the game's bound extended root signature and bindings)
bool DrawFullscreenToEntry(const Call& call, uint64_t vertex_shader_hash, uint64_t pixel_shader_hash, uint64_t target_entry_hash, DXGI_FORMAT target_format,
   Math::uint2 viewport_size);
// Mirror/original and descriptor/resource helpers (null / {0} if the object has no mirror or is not tracked)
ID3D12Resource* FindMirror(ID3D12Resource* resource);
ID3D12Resource* FindOriginal(ID3D12Resource* mirror);
D3D12_CPU_DESCRIPTOR_HANDLE FindMirrorHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle);
D3D12_CPU_DESCRIPTOR_HANDLE FindOriginalHandle(D3D12_CPU_DESCRIPTOR_HANDLE mirror_handle);
ID3D12Resource* GetDescriptorResource(D3D12_CPU_DESCRIPTOR_HANDLE handle);
std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> GetResourceDescriptors(ID3D12Resource* resource);
} // namespace D3D12
} // namespace Game
} // namespace Luma
