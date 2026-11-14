#include "Startup.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <thread>

#include "Draw/Draw.hpp"
#include "Game/Registry.hpp"
#include "Hooks/Hooks.hpp"
#include "Hooks/InputHooks.hpp"
#include "Luma.hpp"
#include "Settings/Keys.hpp"
#include "Settings/Registry.hpp"
#include "Settings/Storage.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Shaders/Shaders.hpp"
#include "System/Jobs.hpp"
#include "System/Loader.hpp"
#include "System/Paths.hpp"
#if DEVELOPMENT
#include "Trace/Capture.hpp"
#endif
#include "Trace/Info.hpp"
#include "Trace/Mcp.hpp"
#include "UI/Input.hpp"
#include "UI/Overlay.hpp"
#include "Utils/Debug.hpp"
#include "Utils/Log.hpp"
#include "System/Modules.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace {
// Settings::Core::ENABLED, read at every bind and draw, set from the setting once it's loaded
std::atomic<bool> enabled = false;
} // namespace

bool IsEnabled()
{
   return enabled.load(std::memory_order_relaxed);
}

namespace Startup {
namespace {
std::atomic<bool> initialized = false;

constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::ENABLED.key,
      .label = "Enabled",
      .tooltip = "Luma's changes to the image. Off shows the game's own image, everything Luma tracks keeps running.",
      .category = "Luma",
      .default_value = true,
   },
   Settings::Setting{
      .key = Settings::Core::WORKER_THREADS.key,
      .label = "Worker Threads",
      .tooltip = "How many background jobs run at once: compiling shaders, dumping, loading files. Fewer leaves more of the CPU to the game.",
      .category = "Advanced",
      // Replaced by GetDefaultWorkerThreads
      .default_value = int32_t(1),
      .range = Settings::Range{1, 16},
   },
};

// A quarter of the CPU's threads, 1 to 4: the game comes first
int32_t GetDefaultWorkerThreads()
{
   return std::clamp(int32_t(std::thread::hardware_concurrency() / 4), 1, 4);
}

// What Core's own features need hooked this run, each feature asked once
Hooks::D3D11::Functions GetCoreFunctions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   // The overlay, trace and the swapchain log
   for (const Hooks::D3D11::Function function : {Present, ResizeBuffers, SetFullscreenState})
      functions.set(size_t(function));
   functions |= Shaders::GetD3D11Functions();
   functions |= Resources::LumaCBuffers::GetD3D11Functions();
   functions |= Luma::Draw::GetD3D11Functions();
#if DEVELOPMENT
   functions |= Trace::Capture::GetD3D11Functions();
#endif
   return functions;
}

// After the game's Init and its saved settings, so what's declared and enabled can decide. Exactly the requested set.
void InstallHooks()
{
   const Hooks::D3D11::Functions core = GetCoreFunctions();
   // A feature hooking a function the game may have a native callback for would run in the middle of the game's work on it
   ASSERT_ONCE_MSG((core & ~GetOwnedFunctions()).none(), "A Core feature hooks a function missing from Startup::GetOwnedFunctions");
   if (const Hooks::D3D11::Functions shared = Game::Registry::GetCallbackFunctions() & GetOwnedFunctions(); shared.any())
      Utils::Log::Error("Core: {} of the game's native callbacks are for functions Core owns, which have events instead", shared.count());
   const Hooks::D3D11::Functions functions = core | Game::Registry::GetRequestedFunctions();
   Utils::Log::Info("Core: {} of {} hookable D3D11 and DXGI functions requested", functions.count(), functions.size());
   const std::optional<Api> api = Game::Registry::GetInfo().api;
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
   Utils::Log::Info("Luma2 initializing in {}", Utils::Strings::ToUtf8(System::Modules::GetPath(nullptr)));

   // Core's settings first: the game's Init changes their defaults (Settings::SetDefault) and adds its own
   Settings::Register(SETTINGS, Settings::Kind::Luma);
   Settings::SetDefault(Settings::Core::WORKER_THREADS, GetDefaultWorkerThreads());
   Shaders::Register();
   Game::Init();
   Game::Registry::LogDeclarations();
   System::Paths::ResolveRoots();
   // Named from the shader files the game's name finds
   Resources::LumaCBuffers::Register();
   const std::filesystem::path settings_path = System::Paths::GetConfig();
   UI::Input::Initialize();
   UI::Initialize();
   if (!Settings::Storage::Load(settings_path))
      Utils::Log::Warning("Core: {} couldn't be read, using default settings", Utils::Strings::ToUtf8(settings_path));
   // Written on first boot, so every setting can be edited by hand
   else if (std::error_code error; !std::filesystem::exists(settings_path, error) && !Settings::Storage::Save(settings_path))
      Utils::Log::Warning("Core: {} couldn't be created", Utils::Strings::ToUtf8(settings_path));
   UI::SaveSettingsTo(settings_path);
   // LumaSettings with what the settings bound to it loaded, for the draws before the first present
   Resources::LumaCBuffers::MarkSettingsDirty();
   enabled = Settings::Get(Settings::Core::ENABLED);
   System::Jobs::SetThreadCount(uint32_t(Settings::Get(Settings::Core::WORKER_THREADS)));
   Settings::AddChangeListener([](const Settings::Setting& setting, const Settings::Value& value) {
      if (setting.key == Settings::Core::ENABLED.key)
         enabled = std::get<bool>(value);
      else if (setting.key == Settings::Core::WORKER_THREADS.key)
         System::Jobs::SetThreadCount(uint32_t(std::get<int32_t>(value)));
   });

   Shaders::Start();
   InstallHooks();
#if DEVELOPMENT
   Trace::Mcp::Start();
#endif
}

Hooks::D3D11::Functions GetOwnedFunctions()
{
   using enum Hooks::D3D11::Function;
   Hooks::D3D11::Functions functions;
   for (const Hooks::D3D11::Function function : {
           // The overlay, trace and the swapchain log
           Present, ResizeBuffers, SetFullscreenState,
           // Shaders: the database, the variants
           CreateVertexShader, CreateHullShader, CreateDomainShader, CreateGeometryShader, CreateGeometryShaderWithStreamOutput, CreatePixelShader,
           CreateComputeShader, VSSetShader, HSSetShader, DSSetShader, GSSetShader, PSSetShader, CSSetShader,
           // Luma's cbuffers: the game's buffers at their slots
           VSSetConstantBuffers, HSSetConstantBuffers, DSSetConstantBuffers, GSSetConstantBuffers, PSSetConstantBuffers, CSSetConstantBuffers,
           // Draw's bound-state cache
           RSSetViewports, RSSetScissorRects, OMSetBlendState, OMSetDepthStencilState,
           // What resets a context's state
           ClearState, FinishCommandList, ExecuteCommandList,
           // The variants' requests, the draw callbacks, trace
           Draw, DrawInstanced, DrawIndexed, DrawIndexedInstanced, DrawAuto, DrawInstancedIndirect, DrawIndexedInstancedIndirect, Dispatch, DispatchIndirect})
      functions.set(size_t(function));
   return functions;
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

} // namespace Startup

BOOL DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
   switch (reason)
   {
   case DLL_PROCESS_ATTACH: System::Loader::Attach(module); break;
   case DLL_PROCESS_DETACH: System::Loader::Detach(reserved != nullptr); break;
   }
   return TRUE;
}
} // namespace Luma
