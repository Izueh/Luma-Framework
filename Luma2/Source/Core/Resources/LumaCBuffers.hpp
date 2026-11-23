#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Common/Api.hpp"
#include "Common/Math.hpp"
#include "Frame/FrameState.hpp"
#include "Hooks/HookFunctions.hpp"
#include "Shaders/ShaderCompiler.hpp"

// The HLSL types the cbuffer structs below are written with, for their C++ side: Core's job, so the game's file holds only its structs
namespace CB {
using namespace Luma::Math;
using uint = uint32_t;
} // namespace CB

// The structs of the cbuffers, shared with the shaders: the game's parts from its Shaders folder, when it has
// them (CBuffers.hlsl defines empty ones otherwise, as in HLSL), then Luma's with the game's parts in them
#if __has_include("Game/GameCBuffers.hlsl")
#include "Game/GameCBuffers.hlsl"
#endif
#include "Includes/Core/CBuffers.hlsl"

// HLSL starts a struct on a new 16 byte register, C++ after the member before it: what comes before a game's part is a multiple
// of 16 bytes on both sides (CBuffers.hlsl), and a game's part is made of 32 bit scalars and vectors (Game/GameCBuffers.hlsl)
static_assert(std::is_standard_layout_v<CB::DisplayCompositionSettings> && std::is_trivially_copyable_v<CB::DisplayCompositionSettings>);
static_assert(sizeof(CB::DisplayCompositionSettings) % 16 == 0, "DisplayCompositionSettings must be a multiple of 16 bytes");
static_assert(std::is_standard_layout_v<CB::LumaGlobalSettings> && std::is_trivially_copyable_v<CB::LumaGlobalSettings>);
static_assert(std::is_standard_layout_v<CB::LumaInstanceData> && std::is_trivially_copyable_v<CB::LumaInstanceData>);
static_assert(offsetof(CB::LumaGlobalSettings, DisplayComposition) % 16 == 0, "LumaSettings' part before DisplayComposition must be a multiple of 16 bytes");
static_assert(offsetof(CB::LumaGlobalSettings, GameSettings) % 16 == 0, "LumaSettings' part before GameSettings must be a multiple of 16 bytes");
static_assert(offsetof(CB::LumaInstanceData, GameData) % 16 == 0, "LumaData's part before GameData must be a multiple of 16 bytes");
static_assert(alignof(CB::LumaGameSettings) == 4 && alignof(CB::LumaGameData) == 4, "A game's cbuffer part has 32 bit members only");

// The LumaSettings and LumaData cbuffers: two Resources::Database entries, which every Luma shader (a replacement, one of Luma's
// own) gets bound at the game's slots wherever it runs. LumaSettings is
// uploaded by a context when it's dirty (its stamp moved: every present, a device's resolutions changed, MarkSettingsDirty),
// LumaData written at every bind and uploaded when it differs from that context's last upload.
namespace Luma {
namespace Game {
// Defined by a game that has per-device data (Luma.hpp)
struct GameDeviceData;
} // namespace Game

namespace Resources {
namespace Database {
struct CustomData;
struct WriteTarget;
} // namespace Database

namespace LumaCBuffers {
// Core's part and the game's (GameSettings), which the settings registry's bindings write. Written
// on the presenting thread only (Init, the present and swapchain callbacks, the settings registry and the overlay): uploads don't
// read it, they copy what MarkSettingsDirty published, at the end of every present and when the game calls it after a change
// within a frame. Its sizes and FrameIndex are written per upload, from the uploading context's device.
extern CB::LumaGlobalSettings cb_luma_global_settings;

// The game's part of LumaData (data.GameData), written at every draw with a Luma shader, from any
// thread, into a LumaData whose Luma part is filled and whose game part is zero. It gets the drawing context's device data and
// resolutions, found once per context. Not called for a device without GameDeviceData.
using UpdateLumaInstanceData = void (*)(CB::LumaInstanceData& data, const Game::GameDeviceData& game_device_data, const Frame::Resolutions& resolutions);

// Game::Options::cbuffers: fixed for the process, they shape the cbuffers and are compiled into every shader
struct Options
{
   // Two different ones of 0 to 13
   uint32_t settings_slot = 13;
   uint32_t data_slot = 12;
   UpdateLumaInstanceData update_luma_instance_data = nullptr;
   // After a draw that bound Luma's cbuffers, the game's buffers at the two slots are bound
   // again, for games that bind theirs there once and expect them to stay. Hooks every *SetConstantBuffers.
   bool restore_after_draw = false;
};

// Init only (Game::SetOptions)
void SetOptions(const Options& options);
const Options& GetOptions();
// After the game's Init and the roots: the two database entries, and the dev sliders (DevSetting01 to 10 of LumaSettings) as
// Development settings named from CBuffers.hlsl
void Register();
// The native functions to hook: the constant buffer binds of every stage when the game's are restored after draws
Hooks::D3D11::Functions GetD3D11Functions();

// Presenting thread (the thread that wrote cb_luma_global_settings): publishes it for the uploads, and every context uploads
// LumaSettings again at its next draw with a Luma shader (Game::MarkLumaSettingsDirty, and Core at the end of every present)
void MarkSettingsDirty();
// Any thread: LumaSettings' stamp, which moves at every present, with a device's resolutions and when it's marked dirty. Read
// before the contents are copied, so an upload's contents are never older than the stamp it records.
uint64_t GetSettingsStamp();

// The entries at the game's slots, which every Luma shader requests
std::vector<Shaders::Request> GetRequests();
// LUMA_SETTINGS_CB_INDEX, LUMA_DATA_CB_INDEX and LUMA_UI_DATA_CB_INDEX ("b13", "b13, space100" for D3D12)
std::vector<Shaders::Compiler::Define> GetDefines(Api api = Api::D3D11);
// LumaSettings 0, LumaData 1, other entries none: D3D12 binds them at Luma's root parameters, in this order
std::optional<uint32_t> GetRootIndex(uint32_t entry);

// The buffers' sizes, padded to 16 bytes
constexpr uint32_t SETTINGS_SIZE = (sizeof(CB::LumaGlobalSettings) + 15) / 16 * 16;
constexpr uint32_t DATA_SIZE = (sizeof(CB::LumaInstanceData) + 15) / 16 * 16;

// Their contents now for the context's device: the published LumaSettings, LumaData with a pass's custom values and the game's
// part. Any thread.
void WriteSettings(std::span<std::byte> destination, const Database::CustomData& custom, const Database::WriteTarget& target);
void WriteData(std::span<std::byte> destination, const Database::CustomData& custom, const Database::WriteTarget& target);

struct DevSetting
{
   float default_value = 0.f;
   float min = 0.f;
   float max = 1.f;
   std::string name;
};

// The "float DevSetting01; // Default, Min, Max, Name" lines of CBuffers.hlsl, each part optional
std::vector<DevSetting> ParseDevSettings(std::string_view text);
} // namespace LumaCBuffers
} // namespace Resources
} // namespace Luma
