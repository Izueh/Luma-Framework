#include "Core.hpp"

#include <array>
#include <atomic>
#include <optional>

#include "Game/Game.hpp"
#include "Hooks/Hooks.hpp"
#include "Hooks/InputHooks.hpp"
#include "Luma.hpp"
#include "Settings/Registry.hpp"
#include "Settings/Storage.hpp"
#include "System/Loader.hpp"
#include "System/Paths.hpp"
#include "Trace/Mcp.hpp"
#include "UI/Input.hpp"
#include "UI/Overlay.hpp"
#include "Utils/Log.hpp"
#include "Utils/Modules.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace Core {
namespace {
std::atomic<bool> initialized = false;

constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = ENABLED_SETTING,
      .label = "Enabled",
      .tooltip = "Luma's changes to the image. Off shows the game's own image, everything Luma tracks keeps running.",
      .category = "Luma",
      .default_value = true,
   },
};

// What Core's own features need hooked
Hooks::D3D11::Functions GetCoreFunctions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   // The overlay, trace and the swapchain log
   for (const Hooks::D3D11::Function function : {Present, ResizeBuffers, SetFullscreenState})
      functions.set(size_t(function));
   return functions;
}

// After the game's Init and its saved settings, so what's declared and enabled can decide. Exactly the requested set.
void InstallHooks()
{
   const Hooks::D3D11::Functions functions = GetCoreFunctions() | Game::GetRequestedFunctions();
   Utils::Log::Info("Core: {} of {} hookable D3D11 and DXGI functions requested", functions.count(), functions.size());
   const std::optional<Api> api = Game::GetInfo().api;
   Hooks::InstallDXGI(functions);
   if (!api || *api == Api::D3D11)
      Hooks::InstallD3D11(functions);
   if (!api || *api == Api::D3D12)
      Hooks::InstallD3D12();
   Hooks::Input::Install();
}
} // namespace

void Initialize()
{
   if (initialized.exchange(true))
      return;
   System::Paths::Initialize();
   Utils::Log::Open(System::Paths::GetLog());
   Utils::Log::Info("Luma2 initializing in {}", Utils::Strings::ToUtf8(Utils::Modules::GetPath(nullptr)));

   Game::Init();
   Game::LogDeclarations();
   System::Paths::ResolveRoots();
   const std::filesystem::path settings_path = System::Paths::GetConfig();
   Settings::Register(SETTINGS);
   UI::Input::Initialize();
   UI::Initialize();
   if (!Settings::Storage::Load(settings_path))
      Utils::Log::Warning("Core: {} couldn't be read, using default settings", Utils::Strings::ToUtf8(settings_path));
   // Written on first boot, so every setting can be edited by hand
   else if (std::error_code error; !std::filesystem::exists(settings_path, error) && !Settings::Storage::Save(settings_path))
      Utils::Log::Warning("Core: {} couldn't be created", Utils::Strings::ToUtf8(settings_path));
   UI::SaveSettingsTo(settings_path);

   InstallHooks();
#if DEVELOPMENT
   Trace::Mcp::Start();
#endif
}

bool IsInitialized()
{
   return initialized;
}

void Shutdown()
{
   Trace::Mcp::Stop();
   Hooks::UninstallAll();
   Utils::Log::Info("Luma2 unloaded");
   Utils::Log::Close();
}

BOOL DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
   switch (reason)
   {
   case DLL_PROCESS_ATTACH: System::Loader::Attach(module); break;
   case DLL_PROCESS_DETACH: System::Loader::Detach(reserved != nullptr); break;
   }
   return TRUE;
}
} // namespace Core
} // namespace Luma
