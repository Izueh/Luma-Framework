#include "Startup.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <thread>

#include "ReShade/ReShade.hpp"
#include "Draw/Draw.hpp"
#include "Effects/SR/SuperResolution.hpp"
#include "Game/GameRegistry.hpp"
#include "Hooks/ThirdPartyModules.hpp"
#include "Hooks/HookManager.hpp"
#include "Hooks/InputHooks.hpp"
#include "Hooks/HookScope.hpp"
#include "Luma.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Settings/SettingsStorage.hpp"
#include "Resources/ConstantCapture.hpp"
#include "Resources/LumaCBuffers.hpp"
#include "Resources/ResourceChains.hpp"
#include "Resources/ResourceDatabase.hpp"
#include "Resources/ResourceMirrors.hpp"
#include "Resources/ResourceScaling.hpp"
#include "Resources/SamplerMirrors.hpp"
#include "Resources/TextureLoader.hpp"
#include "Shaders/ShaderPatching.hpp"
#include "Shaders/Shaders.hpp"
#include "Display/DisplayComposition.hpp"
#include "Display/DisplayMode.hpp"
#include "Display/DXGI/DXGISpoofing.hpp"
#include "Swapchain/Swapchain.hpp"
#include "System/JobQueue.hpp"
#include "System/DllLoader.hpp"
#include "System/Paths.hpp"
#include "Validation/Validation.hpp"
#if DEVELOPMENT
#include "Trace/FrameCapture.hpp"
#include "Trace/D3D12/D3D12Stats.hpp"
#endif
#include "Trace/RuntimeStatus.hpp"
#include "Trace/McpServer.hpp"
#include "UI/OverlayInput.hpp"
#include "UI/Overlay.hpp"
#include "Utils/Debug.hpp"
#include "Log/Log.hpp"
#include "System/Modules.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
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

// What Core's features and the game's draw events need hooked on D3D12 this run. The game has no native D3D12 callbacks.
Hooks::D3D12::Functions GetCoreD3D12Functions()
{
   Hooks::D3D12::Functions functions = Shaders::GetD3D12Functions();
   functions |= Resources::Mirrors::GetD3D12Functions();
   functions |= Resources::Chains::GetD3D12Functions();
   functions |= Resources::Scaling::GetD3D12Functions();
   functions |= Resources::ConstantCapture::GetD3D12Functions();
   functions |= Effects::SR::GetD3D12Functions();
   functions |= Game::Registry::GetRequestedD3D12Functions();
#if DEVELOPMENT
   functions |= Trace::Capture::GetD3D12Functions();
   functions |= Trace::Stats::D3D12::GetD3D12Functions();
#endif
   return functions;
}

// The overlay, trace and the swapchain log
Hooks::DXGI::Functions GetCoreDXGIFunctions()
{
   return Hooks::DXGI::Functions().set();
}

// What Core's own features need hooked this run, each feature asked once
Hooks::D3D11::Functions GetCoreFunctions()
{
   Hooks::D3D11::Functions functions;
   functions |= Shaders::GetD3D11Functions();
   functions |= Resources::LumaCBuffers::GetD3D11Functions();
   functions |= Resources::Mirrors::GetD3D11Functions();
   functions |= Resources::Chains::GetD3D11Functions();
   functions |= Resources::Scaling::GetD3D11Functions();
   functions |= Resources::Samplers::GetD3D11Functions();
   functions |= Resources::ConstantCapture::GetD3D11Functions();
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
      Log::Error("Core: {} of the game's native callbacks are for functions Core owns, which have events instead", shared.count());
   const Hooks::D3D11::Functions functions = core | Game::Registry::GetRequestedFunctions();
   Log::Info("Core: {} of {} hookable D3D11 functions requested", functions.count(), functions.size());
   const ApiSet apis = Game::Registry::GetSupportedApis();
   Hooks::InstallDXGI(GetCoreDXGIFunctions() | Game::Registry::GetRequestedDXGIFunctions());
   if (apis.Has(Api::D3D11))
      Hooks::InstallD3D11(functions);
   if (apis.Has(Api::D3D12))
      Hooks::InstallD3D12(GetCoreD3D12Functions());
   Hooks::Input::Install();
}
} // namespace

void Initialize()
{
   if (initialized.exchange(true))
      return;
   System::Modules::Pin();
   System::Paths::Initialize();
   Log::Open(System::Paths::GetLog());
   Log::Info("Luma2 initializing in {}", Utils::Strings::ToUtf8(System::Modules::GetPath(nullptr)));
   Hooks::ThirdPartyModules::ScanLoadedModules();

   // Core's settings first: the game's Init changes their defaults (Settings::SetDefault) and adds its own
   Settings::Register(SETTINGS, Settings::Kind::Luma);
   Settings::SetDefault(Settings::Core::WORKER_THREADS, GetDefaultWorkerThreads());
   Shaders::Register();
   Resources::Mirrors::Register();
   Resources::Chains::Register();
   Resources::Scaling::Register();
   Resources::Samplers::Register();
   Effects::SR::Register();
   Swapchain::Register();
   Display::DisplayMode::Register();
   Display::Composition::RegisterSettings();
   Display::Spoofing::DXGI::Register();
   Validation::Register();
   ReShade::Register();
   Game::Init();
   Game::Registry::LogDeclarations();
#ifdef LUMA_PROJECT_NAME
   System::Paths::ResolveRoots(LUMA_PROJECT_NAME);
#else
   System::Paths::ResolveRoots(Game::Registry::GetInfo().name);
#endif
   // Named from the shader files the game's name finds
   Resources::LumaCBuffers::Register();
   Resources::Database::RegisterRuntimeDefaults();
   Display::Composition::RegisterResources();
   // After Core's cbuffers, whose names a file can't take
   Resources::Textures::Register();
   // The recipes' settings, before the saved values load
   Shaders::Patching::Register();
   const std::filesystem::path settings_path = System::Paths::GetConfig();
   UI::Input::Initialize();
   UI::Initialize();
   if (!Settings::Storage::Load(settings_path))
      Log::Warning("Core: {} couldn't be read, using default settings", Utils::Strings::ToUtf8(settings_path));
   // Written on first boot, so every setting can be edited by hand
   else if (std::error_code error; !std::filesystem::exists(settings_path, error) && !Settings::Storage::Save(settings_path))
      Log::Warning("Core: {} couldn't be created", Utils::Strings::ToUtf8(settings_path));
   UI::SaveSettingsTo(settings_path);
   // LumaSettings with what the settings bound to it loaded, for the draws before the first present
   Resources::LumaCBuffers::MarkSettingsDirty();
   System::Jobs::SetThreadCount(uint32_t(Settings::Get(Settings::Core::WORKER_THREADS)));
   Settings::AddChangeListener([](const Settings::Setting& setting, const Settings::Value& value) {
      if (setting.key == Settings::Core::WORKER_THREADS.key)
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
           // Shaders: the database, the variants
           CreateVertexShader, CreateHullShader, CreateDomainShader, CreateGeometryShader, CreateGeometryShaderWithStreamOutput, CreatePixelShader,
           CreateComputeShader, VSSetShader, HSSetShader, DSSetShader, GSSetShader, PSSetShader, CSSetShader,
           // Luma's cbuffers: the game's buffers at their slots
           VSSetConstantBuffers, HSSetConstantBuffers, DSSetConstantBuffers, GSSetConstantBuffers, PSSetConstantBuffers, CSSetConstantBuffers,
           // Draw's bound-state cache
           RSSetViewports, RSSetScissorRects, OMSetBlendState, OMSetDepthStencilState,
           // What resets a context's state
           ClearState, FinishCommandList, ExecuteCommandList,
           // Resources: mirrors (creation, views, binds, writes, copies, clears) and sampler mirrors, and trace's copy, clear and write
           // events
           CreateBuffer, CreateTexture1D, CreateTexture2D, CreateTexture3D, OpenSharedResource, CreateShaderResourceView, CreateRenderTargetView,
           CreateUnorderedAccessView, CreateDepthStencilView, CreateSamplerState, VSSetShaderResources, HSSetShaderResources, DSSetShaderResources,
           GSSetShaderResources, PSSetShaderResources, CSSetShaderResources, VSSetSamplers, HSSetSamplers, DSSetSamplers, GSSetSamplers, PSSetSamplers,
           CSSetSamplers, CSSetUnorderedAccessViews, OMSetRenderTargets, OMSetRenderTargetsAndUnorderedAccessViews, Map, Unmap, UpdateSubresource,
           CopyResource, CopySubresourceRegion, ResolveSubresource, ClearRenderTargetView, ClearDepthStencilView, ClearUnorderedAccessViewUint,
           ClearUnorderedAccessViewFloat, ClearView,
           // The variants' requests, the draw callbacks, trace
           Draw, DrawInstanced, DrawIndexed, DrawIndexedInstanced, DrawAuto, DrawInstancedIndirect, DrawIndexedInstancedIndirect, Dispatch, DispatchIndirect})
      functions.set(size_t(function));
   return functions;
}

bool IsInitialized()
{
   return initialized;
}

void Unload()
{
   // Initialize pins Luma's dll: only a Luma that never initialized gets here, with just the creation exports hooked
   Hooks::Scope::SetBypass(true);
   if (Hooks::UninstallAll(false).NeedsModule())
      Log::Error("Core: too late to keep Luma's dll loaded");
   Log::Info("Luma2 unloaded");
   Log::Close();
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
