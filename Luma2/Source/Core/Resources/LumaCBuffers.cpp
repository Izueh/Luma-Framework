#include "Resources/LumaCBuffers.hpp"

#include "Resources/Bindings.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <unordered_set>

#include "Common/Math.hpp"
#include "Frame/Frame.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Shaders/Shaders.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "System/Paths.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Hash.hpp"
#include "Utils/Log.hpp"

namespace Luma {
namespace Resources {
namespace LumaCBuffers {
// Display mode and white levels are 1.0's values before its display code updates them, which Step 5 ports. Sizes and the frame
// index are the uploading device's and frame's.
CB::LumaGlobalSettings cb_luma_global_settings = {
   .DisplayMode = 1, // HDR
   .PeakWhiteNits = 1000.f,
   .GamePaperWhiteNits = 203.f,
   .UIPaperWhiteNits = 203.f,
};

namespace {
constexpr uint32_t SLOT_COUNT = 14; // D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
constexpr size_t DEV_SETTING_COUNT = 10;

// What the game set, written while Init runs
Options options;
// Their database entries, registered after Init
uint32_t settings_entry = 0;
uint32_t data_entry = 0;

// cb_luma_global_settings as MarkSettingsDirty last published it, which uploads copy (1.0 held s_mutex_reshade while copying the
// settings and the UI while changing them). Never held with another lock.
std::mutex published_mutex;
CB::LumaGlobalSettings published = cb_luma_global_settings;
// Bumped after every publication
std::atomic<uint64_t> settings_generation = 0;

#if DEVELOPMENT
static_assert(Settings::Core::DEV_SETTINGS.size() == DEV_SETTING_COUNT);
constexpr std::array<float CB::LumaGlobalSettings::*, DEV_SETTING_COUNT> DEV_SETTING_FIELDS = {&CB::LumaGlobalSettings::DevSetting01,
   &CB::LumaGlobalSettings::DevSetting02, &CB::LumaGlobalSettings::DevSetting03, &CB::LumaGlobalSettings::DevSetting04, &CB::LumaGlobalSettings::DevSetting05,
   &CB::LumaGlobalSettings::DevSetting06, &CB::LumaGlobalSettings::DevSetting07, &CB::LumaGlobalSettings::DevSetting08, &CB::LumaGlobalSettings::DevSetting09,
   &CB::LumaGlobalSettings::DevSetting10};
// The registry keeps views of these
std::array<std::string, DEV_SETTING_COUNT> dev_setting_labels;
#endif

Math::float2 ToFloat(Math::uint2 size)
{
   return {float(size.x), float(size.y)};
}

// 1.0's FindNextUniqueNumberInRange: from `value` towards the side of the range with more room, the first one not excluded
uint32_t FindFreeSlot(uint32_t value, const std::unordered_set<uint32_t>& excluded)
{
   const bool forward = value < (SLOT_COUNT - 1) / 2;
   while (excluded.contains(value))
      value = forward ? value + 1 : value - 1;
   return value;
}

// Skips the spaces and commas, and returns the number the text starts with if it starts with a digit (as 1.0)
std::optional<float> ReadNumber(std::string_view& text)
{
   while (!text.empty() && (text.front() == ' ' || text.front() == ','))
      text.remove_prefix(1);
   if (text.empty() || text.front() < '0' || text.front() > '9')
      return std::nullopt;
   float value = 0.f;
   const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
   if (error != std::errc())
      return std::nullopt;
   text.remove_prefix(size_t(end - text.data()));
   return value;
}
} // namespace

void SetOptions(const Options& set)
{
   options = set;
}

const Options& GetOptions()
{
   return options;
}

void Register()
{
   // 1.0's flow: LumaSettings is uploaded when dirty (its stamp), LumaData's bytes are compared at every bind
   settings_entry = Database::Register({.name_hash = Utils::Hash::Name("LumaSettings"),
                                          .name = "LumaSettings",
                                          .kind = Database::Kind::ConstantBuffer,
                                          .size = SETTINGS_SIZE,
                                          .write = &WriteSettings,
                                          .get_stamp = &GetSettingsStamp})
                       .value_or(0);
   data_entry = Database::Register({.name_hash = Utils::Hash::Name("LumaData"), .name = "LumaData", .kind = Database::Kind::ConstantBuffer, .size = DATA_SIZE, .write = &WriteData})
                   .value_or(0);
#if DEVELOPMENT
   const std::filesystem::path path = System::Paths::Find(System::Paths::GetShaderRoots(), L"Includes/Core/CBuffers.hlsl");
   std::ifstream stream(path, std::ios::binary);
   const std::vector<DevSetting> parsed = ParseDevSettings(std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()));
   if (parsed.empty())
      Utils::Log::Warning("Shaders: no dev settings in Includes/Core/CBuffers.hlsl, the dev sliders are unnamed");
   std::array<Settings::Setting, DEV_SETTING_COUNT> dev_settings;
   for (size_t i = 0; i < DEV_SETTING_COUNT; i++)
   {
      const DevSetting dev_setting = i < parsed.size() ? parsed[i] : DevSetting{};
      dev_setting_labels[i] = dev_setting.name.empty() ? std::format("Dev Setting {:02}", i + 1) : dev_setting.name;
      dev_settings[i] = {
         .key = Settings::Core::DEV_SETTINGS[i].key,
         .label = dev_setting_labels[i],
         .tooltip = std::string_view("DevSetting01 to 10 of the LumaSettings cbuffer (DVS1 to DVS10). Named in Includes/Core/CBuffers.hlsl."),
         .category = "Development",
         .visibility = Settings::Visibility::Development,
         .default_value = dev_setting.default_value,
         .range = Settings::Range{dev_setting.min, dev_setting.max},
         .source = Settings::Sources::Binding{&(cb_luma_global_settings.*DEV_SETTING_FIELDS[i])},
         .live = true,
      };
   }
   Settings::Register(dev_settings, Settings::Kind::Development);
#endif
}

Hooks::D3D11::Functions GetD3D11Functions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   if (!options.restore_after_draw)
      return functions;
   // The binds record the game's buffers, the resets forget them
   for (const Hooks::D3D11::Function function : {VSSetConstantBuffers, HSSetConstantBuffers, DSSetConstantBuffers, GSSetConstantBuffers, PSSetConstantBuffers,
           CSSetConstantBuffers, ClearState, FinishCommandList, ExecuteCommandList})
      functions.set(size_t(function));
   return functions;
}

void MarkSettingsDirty()
{
   {
      std::scoped_lock lock(published_mutex);
      published = cb_luma_global_settings;
   }
   // After the contents: a stamp read before them never goes with older ones
   settings_generation.fetch_add(1, std::memory_order_release);
}

uint64_t GetSettingsStamp()
{
   // 1.0's cb_luma_global_settings_dirty, per context: set at every present (the frame), when a device's resolutions change (a
   // resize) and when marked dirty. The three only grow, so their sum moves whenever one does.
   return Shaders::GetFrame() + Frame::GetGeneration() + settings_generation.load(std::memory_order_acquire);
}

std::vector<Shaders::Request> GetRequests()
{
   return {{settings_entry, options.settings_slot}, {data_entry, options.data_slot}};
}

std::optional<uint32_t> GetRootIndex(uint32_t entry)
{
   if (entry == settings_entry)
      return 0;
   if (entry == data_entry)
      return 1;
   return std::nullopt;
}

std::vector<Shaders::Compiler::Define> GetDefines(Api api)
{
   const uint32_t settings_slot = options.settings_slot;
   const uint32_t data_slot = options.data_slot;
   ASSERT_ONCE_MSG(settings_slot < SLOT_COUNT && data_slot < SLOT_COUNT && settings_slot != data_slot, "The Luma cbuffer slots must be two different ones of 0 to 13");
   // 1.0 gives the UI cbuffer (unused yet) a free slot too: the compiler fails on two cbuffers with the same one
   const uint32_t ui_slot = FindFreeSlot(SLOT_COUNT / 2 - 2, {settings_slot, data_slot});
   const std::string space = api == Api::D3D12 ? std::format(", space{}", Bindings::GetOptions().d3d12_space) : std::string();
   return {
      {"LUMA_SETTINGS_CB_INDEX", std::format("b{}{}", settings_slot, space)},
      {"LUMA_DATA_CB_INDEX", std::format("b{}{}", data_slot, space)},
      {"LUMA_UI_DATA_CB_INDEX", std::format("b{}{}", ui_slot, space)},
   };
}

// LumaSettings has no per pass values
void WriteSettings(std::span<std::byte> destination, const Database::CustomData&, const Database::WriteTarget& target)
{
   ASSERT_ONCE(destination.size() >= SETTINGS_SIZE);
   CB::LumaGlobalSettings settings;
   {
      std::scoped_lock lock(published_mutex);
      settings = published;
   }
   const Frame::Resolutions resolutions = Frame::Read(target.frame);
   settings.SwapchainSize = ToFloat(resolutions.output);
   settings.SwapchainInvSize = {1.f / settings.SwapchainSize.x, 1.f / settings.SwapchainSize.y};
   settings.RenderSize = ToFloat(resolutions.render);
   settings.RenderInvSize = {1.f / settings.RenderSize.x, 1.f / settings.RenderSize.y};
   settings.FrameIndex = uint32_t(Shaders::GetFrame());
   std::ranges::fill(destination, std::byte{0});
   std::memcpy(destination.data(), &settings, sizeof(settings));
}

// At every bind (1.0): the caller's context state holds the device's, nothing is looked up
void WriteData(std::span<std::byte> destination, const Database::CustomData& custom, const Database::WriteTarget& target)
{
   ASSERT_ONCE(destination.size() >= DATA_SIZE);
   const Frame::Resolutions resolutions = Frame::Read(target.frame);
   const Math::float2 output = ToFloat(resolutions.output);
   const Math::float2 render = ToFloat(resolutions.render);
   const Math::float2 previous_render = ToFloat(resolutions.previous_render);
   // render_scale_active stays 0 until Step 7 (1.0 derives it from the render target bound). The game's part starts zeroed.
   CB::LumaInstanceData data = {
      .CustomData1 = custom.custom_data_1,
      .CustomData2 = custom.custom_data_2,
      .CustomData3 = custom.custom_data_3,
      .CustomData4 = custom.custom_data_4,
      .RenderResolutionScale = {render.x / output.x, render.y / output.y},
      .PreviousRenderResolutionScale = {previous_render.x / output.x, previous_render.y / output.y},
   };
   if (options.update_luma_instance_data && target.game_device_data)
      options.update_luma_instance_data(data, *target.game_device_data, resolutions);
   std::ranges::fill(destination, std::byte{0});
   std::memcpy(destination.data(), &data, sizeof(data));
}

std::vector<DevSetting> ParseDevSettings(std::string_view text)
{
   std::vector<DevSetting> dev_settings;
   size_t start = 0;
   while (start < text.size() && dev_settings.size() < DEV_SETTING_COUNT)
   {
      size_t end = text.find('\n', start);
      if (end == std::string_view::npos)
         end = text.size();
      const std::string_view line = text.substr(start, end - start);
      start = end + 1;
      const size_t declaration = line.find("float DevSetting");
      if (declaration == std::string_view::npos)
         continue;
      DevSetting& dev_setting = dev_settings.emplace_back();
      const size_t comment = line.find("//");
      if (comment == std::string_view::npos || comment < declaration)
         continue;
      std::string_view rest = line.substr(comment + 2);
      float* values[] = {&dev_setting.default_value, &dev_setting.min, &dev_setting.max};
      for (float* value : values)
      {
         const std::optional<float> number = ReadNumber(rest);
         if (!number)
            break;
         *value = *number;
      }
      const size_t first = rest.find_first_not_of(" ,\t");
      const size_t last = rest.find_last_not_of(" \t\r");
      if (first != std::string_view::npos && last != std::string_view::npos && last >= first)
         dev_setting.name = std::string(rest.substr(first, last - first + 1));
   }
   return dev_settings;
}
} // namespace LumaCBuffers
} // namespace Resources

} // namespace Luma
