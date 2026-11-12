#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "Game/Game.hpp"
#include "Hooks/Scope.hpp"

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

bool OnSetConstantBuffers(ID3D11DeviceContext*, ShaderStage, UINT, UINT, ID3D11Buffer**, const UINT*, const UINT*) { return false; }

void OnSettingChanged(const Settings::Setting& setting, const Settings::Value&) { calls.push_back("setting " + std::string(setting.key)); }

void TestInit()
{
   Game::SetInfo({.name = "Test Game", .version = "1.2", .credits = CREDITS, .links = LINKS, .api = Api::D3D12});
   Settings::Register(SETTINGS);
   Game::SetCallbacks(Game::D3D12::Callbacks{.post_create_device = &OnDeviceCreated, .on_device_destroyed = &OnDeviceDestroyed});
   Game::SetCallbacks(Game::D3D11::Callbacks{.post_create_device = &OnDeviceCreated11});
   Settings::AddChangeListener(&OnSettingChanged);
}

struct Fixture
{
   Fixture()
   {
      Game::Reset();
      Settings::Clear();
      calls.clear();
   }

   ~Fixture()
   {
      Game::Reset();
      Settings::Clear();
   }
};
} // namespace

TEST_CASE("Game: Init sets the info, settings and callbacks")
{
   Fixture fixture;
   TestInit();
   CHECK(Game::GetInfo().name == "Test Game");
   CHECK(Game::GetInfo().version == "1.2");
   CHECK(Game::GetInfo().links.size() == 1);
   CHECK(Settings::Get<int32_t>("GameTestValue") == 3);
   CHECK(Game::GetInfo().api == Api::D3D12);

   int storage = 0;
   auto* device = reinterpret_cast<ID3D12Device*>(&storage);
   Game::Call(Game::GetD3D12().post_create_device, device);
   Game::Call(Game::GetD3D12().on_device_destroyed, device);
   Game::Call(Game::GetD3D12().post_create_swapchain, reinterpret_cast<IDXGISwapChain*>(&storage), reinterpret_cast<ID3D12CommandQueue*>(&storage)); // Not set
   CHECK_FALSE(Hooks::Scope::IsActive());

   Settings::Set("GameTestValue", int32_t(5));
   CHECK(calls == std::vector<std::string>{"device created D3D12 with a device", "device destroyed", "setting GameTestValue"});
}

TEST_CASE("Game: setting the callbacks again replaces the first ones")
{
   Fixture fixture;
   TestInit();
   Game::SetCallbacks(Game::D3D12::Callbacks{.post_create_device = &OnDeviceCreatedSecond});
   int storage = 0;
   Game::Call(Game::GetD3D12().post_create_device, reinterpret_cast<ID3D12Device*>(&storage));
   Game::Call(Game::GetD3D12().on_device_destroyed, reinterpret_cast<ID3D12Device*>(&storage));
   CHECK(calls == std::vector<std::string>{"device created second"});
}

TEST_CASE("Game: a callback requests the hooks of its native functions, and only those")
{
   Fixture fixture;
   CHECK(Game::GetRequestedFunctions().none());
   Game::SetCallbacks(Game::D3D11::Callbacks{.post_create_device = &OnDeviceCreated11, .pre_set_constant_buffers = &OnSetConstantBuffers, .post_map = &OnMap});
   const Hooks::D3D11::Functions functions = Game::GetRequestedFunctions();
   using enum Hooks::D3D11::Function;
   CHECK(functions.count() == 7);
   CHECK(functions.test(size_t(Map)));
   CHECK(functions.test(size_t(VSSetConstantBuffers)));
   CHECK(functions.test(size_t(CSSetConstantBuffers)));
   CHECK_FALSE(functions.test(size_t(Unmap)));
}
