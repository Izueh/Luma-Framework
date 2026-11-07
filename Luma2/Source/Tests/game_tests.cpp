#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "game/game.hpp"
#include "hooks/scope.hpp"

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
   calls.push_back(std::string("device created D3D12 ") + (device ? "with a device" : "") + (Hooks::Scope::IsActive() ? " in scope" : ""));
}
void OnDeviceCreatedSecond(ID3D12Device*) { calls.push_back("device created second"); }
void OnDeviceCreated11(ID3D11Device*) { calls.push_back("device created D3D11"); }
void OnDeviceDestroyed(ID3D12Device*) { calls.push_back("device destroyed"); }
void OnSwapchainResized(IDXGISwapChain*, ID3D12CommandQueue*) { calls.push_back("swapchain resized"); }
void OnPresent(IDXGISwapChain*, ID3D11Device*) { calls.push_back("present"); }
void OnSettingChanged(const Settings::Setting& setting, const Settings::Value&) { calls.push_back("setting " + std::string(setting.key)); }

void TestInit()
{
   Game::SetInfo({.name = "Test Game", .version = "1.2", .credits = CREDITS, .links = LINKS, .api = Api::D3D12});
   Settings::Register(SETTINGS);
   Game::Callbacks::SetOnDeviceCreated(&OnDeviceCreated);
   Game::Callbacks::SetOnDeviceCreated(&OnDeviceCreated11);
   Game::Callbacks::SetOnDeviceDestroyed(&OnDeviceDestroyed);
   Game::Callbacks::SetOnSwapchainResized(&OnSwapchainResized);
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

   // Callbacks run inside a Luma scope, and only for their own API
   int storage = 0;
   auto* device = reinterpret_cast<ID3D12Device*>(&storage);
   auto* swapchain = reinterpret_cast<IDXGISwapChain*>(&storage);
   Game::CallDeviceCreated(device);
   Game::CallDeviceDestroyed(device);
   Game::CallSwapchainCreated(swapchain, reinterpret_cast<ID3D12CommandQueue*>(&storage)); // Not set
   Game::CallSwapchainResized(swapchain, reinterpret_cast<ID3D11Device*>(&storage)); // Set for D3D12 only
   Game::CallSwapchainResized(swapchain, reinterpret_cast<ID3D12CommandQueue*>(&storage));
   CHECK_FALSE(Hooks::Scope::IsActive());

   Settings::Set("GameTestValue", int32_t(5));
   CHECK(calls == std::vector<std::string>{"device created D3D12 with a device in scope", "device destroyed", "swapchain resized", "setting GameTestValue"});
}

TEST_CASE("Game: setting a callback twice replaces the first")
{
   Fixture fixture;
   TestInit();
   Game::Callbacks::SetOnDeviceCreated(&OnDeviceCreatedSecond);
   int storage = 0;
   Game::CallDeviceCreated(reinterpret_cast<ID3D12Device*>(&storage));
   CHECK(calls == std::vector<std::string>{"device created second"});
}

TEST_CASE("Game: nothing set does nothing")
{
   Fixture fixture;
   CHECK(Game::GetInfo().name.empty());
   CHECK_FALSE(Game::GetInfo().api);
   CHECK_FALSE(Game::HasPresentCallbacks());
   int storage = 0;
   Game::CallDeviceCreated(reinterpret_cast<ID3D11Device*>(&storage));
   Game::CallPresent(reinterpret_cast<IDXGISwapChain*>(&storage), reinterpret_cast<ID3D12CommandQueue*>(&storage));
   CHECK(calls.empty());

   // A present callback of either API counts
   Game::Callbacks::SetOnPresent(&OnPresent);
   CHECK(Game::HasPresentCallbacks());
   Game::CallPresent(reinterpret_cast<IDXGISwapChain*>(&storage), reinterpret_cast<ID3D11Device*>(&storage));
   CHECK(calls == std::vector<std::string>{"present"});
}
