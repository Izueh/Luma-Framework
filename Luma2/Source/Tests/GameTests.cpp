#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "Game/Registry.hpp"
#include "Hooks/Scope.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Startup.hpp"

using namespace Luma;

namespace {
std::vector<std::string> calls;

constexpr std::array<std::string_view, 1> CREDITS = {"Someone"};
constexpr std::array<Game::Link, 1> LINKS = {{{"Site", "https://example.com"}}};
constexpr std::array SETTINGS = {
   Settings::Setting{.key = "GameTestValue", .default_value = int32_t(3), .range = Settings::Range{0, 10}},
};

void OnDeviceCreated(ID3D12Device* device)
{
   calls.push_back(std::string("device created D3D12 ") + (device ? "with a device" : ""));
}

void OnDeviceCreatedSecond(ID3D12Device*) { calls.push_back("device created second"); }

void OnDeviceCreated11(ID3D11Device*) { calls.push_back("device created D3D11"); }

void OnDeviceDestroyed(ID3D12Device*) { calls.push_back("device destroyed"); }

void OnMap(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE&) { calls.push_back("map"); }

void OnBindConstantBuffers(ID3D11DeviceContext*, ShaderStage, UINT, UINT, ID3D11Buffer* const*) {}

void OnSettingChanged(const Settings::Setting& setting, const Settings::Value&) { calls.push_back("setting " + std::string(setting.key)); }

Game::DrawResult OnDraw(const Game::D3D11::Call&) { return Game::DrawResult::None; }

Game::DrawResult OnOtherDraw(const Game::D3D11::Call&) { return Game::DrawResult::Skip; }

void TestInit()
{
   Game::SetInfo({.name = "Test Game", .version = "1.2", .credits = CREDITS, .links = LINKS, .api = Api::D3D12});
   Settings::Register(SETTINGS);
   Game::SetEvents(Game::D3D12::Events{.on_init_device = &OnDeviceCreated, .on_destroy_device = &OnDeviceDestroyed});
   Game::SetEvents(Game::D3D11::Events{.on_init_device = &OnDeviceCreated11});
   Settings::AddChangeListener(&OnSettingChanged);
}

struct Fixture
{
   Fixture()
   {
      Game::Registry::Reset();
      Settings::Clear();
      calls.clear();
   }

   ~Fixture()
   {
      Game::Registry::Reset();
      Settings::Clear();
   }
};
} // namespace

TEST_CASE("Game: Init sets the info, settings and callbacks")
{
   Fixture fixture;
   TestInit();
   CHECK(Game::Registry::GetInfo().name == "Test Game");
   CHECK(Game::Registry::GetInfo().version == "1.2");
   CHECK(Game::Registry::GetInfo().links.size() == 1);
   CHECK(Settings::Get<int32_t>("GameTestValue") == 3);
   CHECK(Game::Registry::GetInfo().api == Api::D3D12);

   int storage = 0;
   auto* device = reinterpret_cast<ID3D12Device*>(&storage);
   Game::Registry::Call(Game::Registry::GetD3D12Events().on_init_device, device);
   Game::Registry::Call(Game::Registry::GetD3D12Events().on_destroy_device, device);
   Game::Registry::Call(Game::Registry::GetD3D12Events().on_init_swapchain, reinterpret_cast<IDXGISwapChain*>(&storage), reinterpret_cast<ID3D12CommandQueue*>(&storage), false); // Not set
   CHECK_FALSE(Hooks::Scope::IsActive());

   Settings::Set("GameTestValue", int32_t(5));
   CHECK(calls == std::vector<std::string>{"device created D3D12 with a device", "device destroyed", "setting GameTestValue"});
}

TEST_CASE("Game: setting the events again replaces the first ones")
{
   Fixture fixture;
   TestInit();
   Game::SetEvents(Game::D3D12::Events{.on_init_device = &OnDeviceCreatedSecond});
   int storage = 0;
   Game::Registry::Call(Game::Registry::GetD3D12Events().on_init_device, reinterpret_cast<ID3D12Device*>(&storage));
   Game::Registry::Call(Game::Registry::GetD3D12Events().on_destroy_device, reinterpret_cast<ID3D12Device*>(&storage));
   CHECK(calls == std::vector<std::string>{"device created second"});
}

TEST_CASE("Game: events and callbacks request the hooks of their native functions, and only those")
{
   Fixture fixture;
   CHECK(Game::Registry::GetRequestedFunctions().none());
   Game::SetEvents(Game::D3D11::Events{.on_init_device = &OnDeviceCreated11, .on_bind_constant_buffers = &OnBindConstantBuffers});
   Game::SetCallbacks(Game::D3D11::Callbacks{.post_map = &OnMap});
   const Hooks::D3D11::Functions functions = Game::Registry::GetRequestedFunctions();
   using enum Hooks::D3D11::Function;
   CHECK(functions.count() == 7);
   CHECK(functions.test(size_t(Map)));
   CHECK(functions.test(size_t(VSSetConstantBuffers)));
   CHECK(functions.test(size_t(CSSetConstantBuffers)));
   CHECK_FALSE(functions.test(size_t(Unmap)));
   // The event's functions are Core's, the callback's aren't
   CHECK(Game::Registry::GetCallbackFunctions().count() == 1);
}

TEST_CASE("Game: no native callback is for a function Core owns, and every event is")
{
   Fixture fixture;
   Game::D3D11::Callbacks callbacks;
   // Every field is a function pointer
   auto* fields = reinterpret_cast<void**>(&callbacks);
   for (size_t i = 0; i < sizeof(callbacks) / sizeof(void*); i++)
      fields[i] = reinterpret_cast<void*>(&OnMap);
   Game::SetCallbacks(callbacks);
   const Hooks::D3D11::Functions native = Game::Registry::GetCallbackFunctions();
   CHECK(native.any());
   CHECK((native & Startup::GetOwnedFunctions()).none());

   Game::Registry::Reset();
   Game::D3D11::Events events;
   auto* event_fields = reinterpret_cast<void**>(&events);
   for (size_t i = 0; i < sizeof(events) / sizeof(void*); i++)
      event_fields[i] = reinterpret_cast<void*>(&OnMap);
   Game::SetEvents(events);
   const Hooks::D3D11::Functions requested = Game::Registry::GetRequestedFunctions();
   CHECK(requested.any());
   CHECK((requested & ~Startup::GetOwnedFunctions()).none());
}

TEST_CASE("Game: draw callbacks are found by any of their hashes, the last set wins")
{
   Fixture fixture;
   CHECK_FALSE(Game::Registry::HasDrawCallbacks());
   constexpr std::array<uint32_t, 2> HASHES = {0x11111111, 0x22222222};
   constexpr std::array<uint32_t, 1> OTHER = {0x22222222};
   Game::D3D11::OnShaderDraw(HASHES, &OnDraw);
   Game::D3D11::OnShaderDraw(OTHER, &OnOtherDraw);
   CHECK(Game::Registry::HasDrawCallbacks());
   CHECK(Game::Registry::FindDrawCallback(0x11111111) == &OnDraw);
   CHECK(Game::Registry::FindDrawCallback(0x22222222) == &OnOtherDraw);
   CHECK(Game::Registry::FindDrawCallback(0x33333333) == nullptr);
   CHECK(Game::Registry::GetD3D11Events().on_draw_or_dispatch == nullptr);
}

TEST_CASE("Game: any draw event requests the draw and dispatch hooks")
{
   Fixture fixture;
   CHECK_FALSE(Game::Registry::GetRequestedFunctions().test(size_t(Hooks::D3D11::Function::Draw)));
   Game::SetEvents(Game::D3D11::Events{.on_draw_or_dispatch = &OnDraw});
   CHECK(Game::Registry::HasDrawCallbacks());
   const Hooks::D3D11::Functions functions = Game::Registry::GetRequestedFunctions();
   CHECK(functions.count() == 9);
   CHECK(functions.test(size_t(Hooks::D3D11::Function::Draw)));
   CHECK(functions.test(size_t(Hooks::D3D11::Function::DispatchIndirect)));

   Game::Registry::Reset();
   Game::SetEvents(Game::D3D11::Events{.on_variant_draw = &OnDraw});
   CHECK(Game::Registry::GetRequestedFunctions().count() == 9);
}

TEST_CASE("Game: a second SetOptions is ignored, and Reset forgets the options")
{
   Fixture fixture;
   Game::SetOptions({.cbuffers = {.settings_slot = 5, .data_slot = 6, .restore_after_draw = true}});
   Game::SetOptions({.cbuffers = {.settings_slot = 7, .data_slot = 8}});
   CHECK(Resources::LumaCBuffers::GetOptions().settings_slot == 5);
   CHECK(Resources::LumaCBuffers::GetOptions().restore_after_draw);

   Game::Registry::Reset();
   CHECK(Resources::LumaCBuffers::GetOptions().settings_slot == 13);
   CHECK_FALSE(Resources::LumaCBuffers::GetOptions().restore_after_draw);
   // Once more after a reset
   Game::SetOptions({.cbuffers = {.settings_slot = 7, .data_slot = 8}});
   CHECK(Resources::LumaCBuffers::GetOptions().settings_slot == 7);
}
