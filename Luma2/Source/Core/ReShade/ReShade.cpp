#include "ReShade/ReShade.hpp"
#include "ReShade/AddonEvents.hpp"
#include "ReShade/ReShadeABI.hpp"

#include <array>
#include <atomic>
#include <filesystem>
#include <format>
#include <mutex>
#include <string_view>
#include <system_error>
#include <vector>

#include <Windows.h>
#include <Psapi.h>
#include <dxgi.h>

#include "Game/GameRegistry.hpp"
#include "Hooks/DXGI/DXGIHooks.hpp"
#include "Hooks/DXGI/DXGISwapchainProxy.hpp"
#include "Hooks/HookManager.hpp"
#include "Hooks/InputHooks.hpp"
#include "Hooks/ThirdPartyModules.hpp"
#include "Settings/SettingKeys.hpp"
#include "Settings/SettingsRegistry.hpp"
#include "Startup.hpp"
#include "Display/DXGI/DXGISwapchain.hpp"
#include "Display/Swapchain.hpp"
#include "System/Modules.hpp"
#include "Log/Log.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace ReShade {
namespace {
#ifdef _WIN64
constexpr const wchar_t* FILE_NAME = L"ReShade64.dll";
#else
constexpr const wchar_t* FILE_NAME = L"ReShade32.dll";
#endif
// Without it, an injected ReShade's DllMain fails unless a "ReShade.ini" is in the game's folder
constexpr const wchar_t* LOADING_CHECK_VARIABLE = L"RESHADE_DISABLE_LOADING_CHECK";

constexpr std::array SETTINGS = {
   Settings::Setting{
      .key = Settings::Core::LOAD_RESHADE.key,
      .label = "Load ReShade",
      .tooltip = "Loads ReShade64.dll when it's next to Luma, so ReShade's effects and add-ons run on top of Luma.",
      .category = "ReShade",
      .visibility = Settings::Visibility::Test,
      .default_value = true,
      .requires_restart = true,
      .is_visible = [] { return Game::Registry::HasSupportedApi({Api::D3D11, Api::D3D12}); },
   },
   Settings::Setting{
      .key = Settings::Core::RESHADE_BRIDGE.key,
      .label = "ReShade Bridge",
      .tooltip = "Luma registers as a ReShade add-on, so its display composition runs before ReShade's effects and its overlay after them. "
                 "Off, ReShade's effects see the image before the composition.",
      .category = "ReShade",
      .visibility = Settings::Visibility::Test,
      .default_value = true,
      .requires_restart = true,
      .is_visible = [] { return Game::Registry::HasSupportedApi({Api::D3D11, Api::D3D12}); },
   },
};

enum class HookOrder : uint8_t
{
   Unchecked, // Before the first present
   ReShadeAboveLuma, // The intended order: ReShade's hooks outermost
   LumaAboveReShade,
   NoReShadeHooks, // ReShade hooks none of the creation exports (a proxy calls them directly)
};

struct CreationExport
{
   const wchar_t* module;
   const char* name;
};

// The ones Luma hooks and injected ReShade hooks too
constexpr std::array CREATION_EXPORTS = {
   CreationExport{L"d3d11.dll", "D3D11CreateDevice"},
   CreationExport{L"d3d11.dll", "D3D11CreateDeviceAndSwapChain"},
   CreationExport{L"d3d12.dll", "D3D12CreateDevice"},
   CreationExport{L"dxgi.dll", "CreateDXGIFactory"},
   CreationExport{L"dxgi.dll", "CreateDXGIFactory1"},
   CreationExport{L"dxgi.dll", "CreateDXGIFactory2"},
};

// Written once, before "load_state" is stored: read only after it says ReShade is loaded
std::string reshade_module_name;
uintptr_t reshade_module_base = 0;
std::atomic<LoadState> load_state = LoadState::NotLoaded;
// Written once by Load, when ReShade isn't loaded
std::string not_loaded_reason = "not loaded";
std::atomic<HookOrder> hook_order = HookOrder::Unchecked;

enum class BridgeState : uint8_t
{
   Unregistered, // No ReShade (yet)
   On,
   TurnedOff, // The setting
   MissingExport, // A ReShade without the add-on API
   Refused, // ReShadeRegisterAddon returned false: an older API version, add-ons disabled, the name taken (ReShade.log says)
};

std::atomic<BridgeState> bridge_state = BridgeState::Unregistered;
std::atomic<bool> bridge_attempted = false;
// Written on the presenting thread, read by the Info tab
std::atomic<uint64_t> present_events = 0;
std::atomic<uint64_t> present_event_at_last_effects = UINT64_MAX; // "present_events" when effects last finished
std::atomic<int64_t> effects_begin_ticks = 0;
std::atomic<float> effects_cpu_ms = 0.f;
std::atomic<bool> last_present_work_at_event = false;
std::atomic<uint32_t> live_effect_runtimes = 0;
std::atomic<uint32_t> effect_runtime_inits = 0;
std::atomic<bool> reshade_overlay_open = false;

// Before Startup::Initialize registered them, a setting is at its default (on)
bool IsSettingOnOrUnregistered(Settings::Id<bool> id)
{
   return !Settings::Find(id.key) || Settings::Get(id);
}

// As ReShade's own DllMain looks for another instance of itself
HMODULE FindLoadedReShadeModule()
{
   std::vector<HMODULE> loaded(256);
   DWORD needed = 0;
   while (EnumProcessModules(GetCurrentProcess(), loaded.data(), DWORD(loaded.size() * sizeof(HMODULE)), &needed) && needed > loaded.size() * sizeof(HMODULE))
      loaded.resize(needed / sizeof(HMODULE));
   loaded.resize(needed / sizeof(HMODULE));
   for (HMODULE module : loaded)
   {
      if (module != System::Modules::GetSelf() && GetProcAddress(module, "ReShadeVersion"))
         return module;
   }
   return nullptr;
}

// The loader notification usually registered the module as an overlay already: Add ignores a known base
void RecordReShadeModule(HMODULE module, LoadState loaded_as)
{
   MODULEINFO info = {};
   GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info));
   reshade_module_name = Utils::Strings::ToUtf8(System::Modules::GetPath(module).filename());
   reshade_module_base = reinterpret_cast<uintptr_t>(module);
   if (info.SizeOfImage)
   {
      Hooks::ThirdPartyModules::Add(reshade_module_name, module, info.SizeOfImage);
      // Its own reads of the game's swapchain are told from the game's calls by its module
      Hooks::SwapchainProxy::SetReShadeModule(module, info.SizeOfImage);
   }
   load_state.store(loaded_as, std::memory_order_release);
}

bool IsAddressInLuma(const void* address)
{
   HMODULE module = nullptr;
   return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module) &&
      module == System::Modules::GetSelf();
}

bool IsReShadeLoaded(LoadState state)
{
   return state != LoadState::NotLoaded;
}

// Which hook a creation export reaches first. Not under the loader lock (module names take it).
void CheckAndLogHookOrder(LoadState state)
{
   uint32_t reshade_first = 0, luma_first = 0, other_first = 0, unhooked = 0;
   std::string details;
   for (const CreationExport& entry : CREATION_EXPORTS)
   {
      const HMODULE module = System::Modules::FindSystemModule(entry.module);
      const void* target = module ? System::Modules::FindExport(module, entry.name) : nullptr;
      if (!target)
         continue;
      const void* first = Hooks::FollowJump(target);
      std::string who;
      if (!first || System::Modules::IsInSystemModule(first))
      {
         unhooked++;
         who = "not hooked";
      }
      else if (Hooks::ThirdPartyModules::FindBase(first) == reshade_module_base)
      {
         reshade_first++;
         who = "ReShade";
      }
      else if (IsAddressInLuma(first))
      {
         luma_first++;
         who = "Luma";
      }
      else
      {
         other_first++;
         who = System::Modules::GetName(first);
      }
      details += std::format("{}{} {}", details.empty() ? "" : ", ", entry.name, who);
   }
   const Hooks::Verification verification = Hooks::GetLastVerification();
   const HookOrder found = reshade_first == 0 ? HookOrder::NoReShadeHooks : (luma_first == 0 ? HookOrder::ReShadeAboveLuma : HookOrder::LumaAboveReShade);
   hook_order.store(found, std::memory_order_relaxed);

   const std::string summary = std::format("the creation exports reach first: {} (ReShade {}, Luma {}, another hooker {}, none {}); Luma's hooks: {} first in their chain, {} chained under another hooker, {} removed by someone else",
      details, reshade_first, luma_first, other_first, unhooked, verification.intact, verification.chained, verification.restored);
   if (state != LoadState::LoadedByLuma)
      Log::Info("ReShade: at the first present, {}. ReShade wasn't loaded by Luma, so the intended order (ReShade's hooks outermost, Luma's under them) isn't expected", summary);
   else if (found == HookOrder::ReShadeAboveLuma && verification.restored == 0)
      Log::Info("ReShade: at the first present, {}. ReShade's hooks are outermost and Luma's sit under them, as intended", summary);
   else
      Log::Warning("ReShade: at the first present, {}. Luma's hooks are not under ReShade's, which is not the intended order: ReShade's hooks should be outermost, with Luma's under them", summary);
}

int64_t Now()
{
   LARGE_INTEGER counter;
   QueryPerformanceCounter(&counter);
   return counter.QuadPart;
}

// Luma's present work (up to the display composition) for the Present that follows, then the window for ReShade's work
void __cdecl OnPresentEvent(void*, ABI::DeviceObject* swapchain, const void*, const void*, uint32_t, const void*)
{
   present_events.fetch_add(1, std::memory_order_relaxed);
   ABI::Device* device = swapchain ? swapchain->GetDevice() : nullptr;
   if (!device)
      return;
   if (const ABI::DeviceApi api = device->GetApi(); api != ABI::DeviceApi::D3D11 && api != ABI::DeviceApi::D3D12)
      return;
   // What ReShade's wrapper holds: Luma's proxy, or the real swapchain without one
   IDXGISwapChain* native = reinterpret_cast<IDXGISwapChain*>(static_cast<uintptr_t>(swapchain->GetNative()));
   Hooks::DXGI::RunPresentWorkAtEvent(Hooks::SwapchainProxy::Unwrap(native));
}

// After ReShade's Present: Luma's Present closed the window already, unless it never came
void __cdecl OnFinishPresentEvent(void*, ABI::DeviceObject*)
{
   static std::atomic<bool> warned = false;
   if (Hooks::DXGI::CloseThirdPartyWorkWindowAtFinishPresent() && !warned.exchange(true))
      Log::Warning("ReShade bridge: Luma's Present didn't follow ReShade's present event: Luma's present work (up to the display composition) ran at the event, but Luma's overlay wasn't drawn for that frame; "
                   "the window for ReShade's work was closed when ReShade finished presenting. Logged once.");
}

void __cdecl OnBeginEffects(void*, void*, ABI::Handle, ABI::Handle)
{
   effects_begin_ticks.store(Now(), std::memory_order_relaxed);
}

void __cdecl OnFinishEffects(void*, void*, ABI::Handle, ABI::Handle)
{
   LARGE_INTEGER frequency;
   QueryPerformanceFrequency(&frequency);
   effects_cpu_ms.store(float(double(Now() - effects_begin_ticks.load(std::memory_order_relaxed)) * 1000.0 / double(frequency.QuadPart)), std::memory_order_relaxed);
   present_event_at_last_effects.store(present_events.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

// ReShade's runtime has read the swapchain (after a creation, resize or color space change)
void __cdecl OnInitEffectRuntime(void*)
{
   Hooks::SwapchainProxy::CloseReShadeInitWindow();
   live_effect_runtimes.fetch_add(1, std::memory_order_relaxed);
   effect_runtime_inits.fetch_add(1, std::memory_order_relaxed);
}

void __cdecl OnDestroyEffectRuntime(void*)
{
   if (live_effect_runtimes.load(std::memory_order_relaxed) != 0)
      live_effect_runtimes.fetch_sub(1, std::memory_order_relaxed);
   reshade_overlay_open.store(false, std::memory_order_relaxed);
}

// One overlay open at a time: returning true keeps ReShade's closed
bool __cdecl OnOpenOverlay(void*, bool open, int32_t)
{
   if (open && Hooks::Input::IsOverlayOpen())
   {
      Log::Info("ReShade bridge: ReShade's overlay kept closed, Luma's is open");
      return true;
   }
   reshade_overlay_open.store(open, std::memory_order_relaxed);
   return false;
}

// Before ReShade loads its add-ons, so Luma's present handler runs before theirs
void RegisterBridge()
{
   if (!IsReShadeLoaded(load_state.load(std::memory_order_acquire)) || bridge_attempted.exchange(true))
      return;
   AddonEvents::Install(reinterpret_cast<HMODULE>(reshade_module_base), load_state.load(std::memory_order_acquire) != LoadState::LoadedByLuma);
   constexpr const char* FALLBACK = "Luma's present work runs at the proxy's Present: ReShade's effects and add-ons see the image before Luma's display composition";
   if (!IsSettingOnOrUnregistered(Settings::Core::RESHADE_BRIDGE))
   {
      bridge_state.store(BridgeState::TurnedOff, std::memory_order_relaxed);
      Log::Info("ReShade bridge: off, turned off in Luma.toml ({} = false). {}", Settings::Core::RESHADE_BRIDGE.key, FALLBACK);
      return;
   }
   const HMODULE module = reinterpret_cast<HMODULE>(reshade_module_base);
   const auto register_addon = reinterpret_cast<ABI::RegisterAddonFunction>(GetProcAddress(module, "ReShadeRegisterAddon"));
   const auto register_event = reinterpret_cast<ABI::RegisterEventFunction>(GetProcAddress(module, "ReShadeRegisterEvent"));
   if (!register_addon || !register_event)
   {
      bridge_state.store(BridgeState::MissingExport, std::memory_order_relaxed);
      Log::Warning("ReShade bridge: off, {} has no add-on API (no ReShadeRegisterAddon or ReShadeRegisterEvent). {}", reshade_module_name, FALLBACK);
      return;
   }
   if (!register_addon(System::Modules::GetSelf(), ABI::API_VERSION))
   {
      bridge_state.store(BridgeState::Refused, std::memory_order_relaxed);
      Log::Warning("ReShade bridge: off, {} refused Luma as an add-on with API version {} (ReShade.log says why: an older ReShade, add-ons disabled, the name taken). {}",
         reshade_module_name, ABI::API_VERSION, FALLBACK);
      return;
   }
   register_event(ABI::Event::Present, reinterpret_cast<void*>(&OnPresentEvent));
   register_event(ABI::Event::FinishPresent, reinterpret_cast<void*>(&OnFinishPresentEvent));
   register_event(ABI::Event::ReShadeBeginEffects, reinterpret_cast<void*>(&OnBeginEffects));
   register_event(ABI::Event::ReShadeFinishEffects, reinterpret_cast<void*>(&OnFinishEffects));
   register_event(ABI::Event::InitEffectRuntime, reinterpret_cast<void*>(&OnInitEffectRuntime));
   register_event(ABI::Event::DestroyEffectRuntime, reinterpret_cast<void*>(&OnDestroyEffectRuntime));
   register_event(ABI::Event::ReShadeOpenOverlay, reinterpret_cast<void*>(&OnOpenOverlay));
   bridge_state.store(BridgeState::On, std::memory_order_relaxed);
   // Its events open and close the windows in which its reads of a Mirror swapchain get the real one
   Hooks::SwapchainProxy::SetReShadeBridged(true);
   Log::Info("ReShade bridge: on, Luma registered as an add-on of {} (API version {}, from ReShade v6.8.0's headers)", reshade_module_name, ABI::API_VERSION);
}
} // namespace

void Register()
{
   Settings::Register(SETTINGS, Settings::Kind::Luma);
}

void Load()
{
   if (const HMODULE loaded = FindLoadedReShadeModule())
   {
      RecordReShadeModule(loaded, LoadState::LoadedByAnotherModule);
      Log::Info("ReShade: already loaded as {} ({}), Luma doesn't load another: it runs as a proxy or was injected by something else", reshade_module_name,
         Utils::Strings::ToUtf8(System::Modules::GetPath(loaded)));
      RegisterBridge();
      return;
   }

   const std::filesystem::path path = System::Modules::GetSelfDirectory() / FILE_NAME;
   std::error_code error;
   const std::string file = Utils::Strings::ToUtf8(FILE_NAME);
   if (!std::filesystem::is_regular_file(path, error))
   {
      not_loaded_reason = std::format("no {} next to Luma", file);
      Log::Info("ReShade: absent (no {} next to Luma)", file);
      return;
   }
   // The mode comes from the game's Init and the saved settings, which Startup::Initialize reads when the graphics dll loads. A ReShade
   // loaded once that happened, from the loader notification or the game's first call, would put its hooks under Luma's.
   if (!Startup::IsInitialized())
   {
      not_loaded_reason = std::format("{} next to Luma, not loaded: Luma loaded before the game's graphics dll, the swapchain mode wasn't known", file);
      Log::Warning("ReShade: {} not loaded, Luma's dll loaded before the game's graphics dll, so the swapchain mode and Luma's settings aren't "
                   "known yet, and ReShade can't be loaded in Mirror mode without them",
         Utils::Strings::ToUtf8(path));
      return;
   }
   if (!Settings::Get(Settings::Core::LOAD_RESHADE))
   {
      not_loaded_reason = std::format("{} next to Luma, not loaded: turned off in Luma.toml", file);
      Log::Info("ReShade: {} not loaded, turned off in Luma.toml ({} = false)", Utils::Strings::ToUtf8(path), Settings::Core::LOAD_RESHADE.key);
      return;
   }
   // Mirror tells ReShade's own reads from the game's in the proxy the game holds
   if (Display::Swapchain::GetMode() == Display::Swapchain::Mode::Mirror && !Display::Swapchain::Wraps())
   {
      not_loaded_reason = std::format("{} next to Luma, not loaded: the swapchain is in Mirror mode without the swapchain wrapper", file);
      Log::Info("ReShade: {} not loaded, the game's swapchain is in Mirror mode without the swapchain wrapper ({} = false), where ReShade would "
                "hand the game Luma's real buffers",
         Utils::Strings::ToUtf8(path), Settings::Core::SWAPCHAIN_WRAP.key);
      return;
   }

   // Only for ReShade's DllMain, so the game's child processes don't inherit it. A value the user set is left alone.
   const bool set_variable = GetEnvironmentVariableW(LOADING_CHECK_VARIABLE, nullptr, 0) == 0;
   if (set_variable)
      SetEnvironmentVariableW(LOADING_CHECK_VARIABLE, L"1");
   // Never freed: the game ends up holding ReShade's wrappers, whose code is in that dll
   const HMODULE module = LoadLibraryW(path.c_str());
   const DWORD load_error = GetLastError();
   if (set_variable)
      SetEnvironmentVariableW(LOADING_CHECK_VARIABLE, nullptr);
   if (!module)
   {
      not_loaded_reason = std::format("{} next to Luma failed to load (error {}), see Luma.log", file, load_error);
      Log::Error("ReShade: loading {} failed (error {})", Utils::Strings::ToUtf8(path), load_error);
      return;
   }
   RecordReShadeModule(module, LoadState::LoadedByLuma);
   Log::Info("ReShade: injected by Luma, {} loaded at {}, after Luma's loader notification so its hooks go in on top of Luma's", Utils::Strings::ToUtf8(path),
      static_cast<void*>(module));
   // Still in DllMain, as the load: the registration reads ReShade's config and Luma's version resource, nothing waits
   RegisterBridge();
}

void FindReShadeLoadedAfterLuma()
{
   // ReShade as the game's dxgi.dll, with Luma under another dll name, can load after Luma
   if (load_state.load(std::memory_order_acquire) != LoadState::NotLoaded)
      return;
   static std::mutex mutex;
   std::scoped_lock lock(mutex);
   if (load_state.load(std::memory_order_acquire) != LoadState::NotLoaded)
      return;
   const HMODULE loaded = FindLoadedReShadeModule();
   if (!loaded)
      return;
   RecordReShadeModule(loaded, LoadState::LoadedByAnotherModule);
   Log::Info("ReShade: loaded after Luma as {} ({}), not by Luma", reshade_module_name, Utils::Strings::ToUtf8(System::Modules::GetPath(loaded)));
   // Its add-ons may have registered before: their present handlers run before Luma's
   RegisterBridge();
}

void CheckHookOrderAtFirstPresent()
{
   static std::atomic<bool> checked = false;
   if (checked.load(std::memory_order_relaxed) || checked.exchange(true))
      return;
   FindReShadeLoadedAfterLuma();
   const LoadState state = load_state.load(std::memory_order_acquire);
   if (!IsReShadeLoaded(state))
      return;
   CheckAndLogHookOrder(state);
   if (state != LoadState::LoadedByAnotherModule || Display::Swapchain::GetMode() != Display::Swapchain::Mode::Mirror)
      return;
   if (!Display::Swapchain::Wraps())
      Log::Warning("ReShade: the game's swapchain is in Mirror mode without the swapchain wrapper ({} = false): ReShade's calls can't be told from "
                   "the game's, so the game may get Luma's real buffers",
         Settings::Core::SWAPCHAIN_WRAP.key);
   else if (bridge_state.load(std::memory_order_relaxed) != BridgeState::On)
      Log::Warning("ReShade: the game's swapchain is in Mirror mode and the bridge is off: ReShade's effects draw on the game's image, before Luma's display composition");
   else
      Log::Info("ReShade: the game's swapchain is in Mirror mode: ReShade's own reads of it, from {}, get the real buffers, so its effects draw on the composed image", reshade_module_name);
}

void RecordWherePresentWorkRan(bool at_event)
{
   last_present_work_at_event.store(at_event, std::memory_order_relaxed);
   if (at_event)
   {
      static std::atomic<bool> logged = false;
      if (!logged.exchange(true))
         Log::Info("ReShade bridge: Luma's present work runs at ReShade's present event: ReShade's effects and the add-ons registered after Luma "
                   "see Luma's composed image, Luma's overlay draws at the proxy's Present after them");
      return;
   }
   static std::atomic<bool> logged = false;
   if (!IsReShadeLoaded(load_state.load(std::memory_order_acquire)) || logged.load(std::memory_order_relaxed) || logged.exchange(true))
      return;
   if (bridge_state.load(std::memory_order_relaxed) == BridgeState::On)
      Log::Info("ReShade bridge: a present without ReShade's present event before it (ReShade skipped the event, or turned its add-ons off): "
                "Luma's present work ran at the proxy's Present, before ReShade's effects, for that frame. Logged once.");
   else
      Log::Info("ReShade: Luma's present work runs at the proxy's Present (the bridge is off): ReShade's effects run before Luma's display composition, "
                "Luma's overlay after them");
}

bool IsOverlayOpen()
{
   return reshade_overlay_open.load(std::memory_order_relaxed);
}

LoadState GetLoadState()
{
   return load_state.load(std::memory_order_acquire);
}

std::string DescribeLoadState()
{
   const LoadState state = load_state.load(std::memory_order_acquire);
   if (state == LoadState::NotLoaded)
      return not_loaded_reason;
   std::string_view hook_order_text;
   switch (hook_order.load(std::memory_order_relaxed))
   {
   case HookOrder::Unchecked: hook_order_text = "hook order not checked yet"; break;
   case HookOrder::ReShadeAboveLuma: hook_order_text = "its hooks above Luma's"; break;
   case HookOrder::LumaAboveReShade: hook_order_text = "Luma's hooks above its (not the intended order)"; break;
   case HookOrder::NoReShadeHooks: hook_order_text = "it hooks none of the creation exports"; break;
   }
   return state == LoadState::LoadedByLuma ? std::format("{}, injected by Luma, {}", reshade_module_name, hook_order_text)
                                           : std::format("{}, not loaded by Luma (a proxy, or injected by something else), {}", reshade_module_name, hook_order_text);
}

std::string DescribeMirrorSwapchainView()
{
   if (!IsReShadeLoaded(load_state.load(std::memory_order_acquire)))
      return "no ReShade";
   const Display::Swapchain::DXGI::Status status = Display::Swapchain::DXGI::GetStatus();
   if (!status.known)
      return "no swapchain yet";
   if (status.mode != Display::Swapchain::GetName(Display::Swapchain::Mode::Mirror))
      return std::format("not needed ({}: ReShade holds the real buffers like the game)", status.mode);
   if (!status.wrapped)
      return "off, the game holds the real swapchain (no wrapper): ReShade's calls can't be told from the game's";
   if (!status.reshade_reads_real_swapchain)
      return "off, the bridge is off: ReShade gets the game's buffers, its effects draw before Luma's composition";
   return std::format("active: ReShade's own reads get the real buffers, its effects draw on the composed image; {} output switches through ReShade's wrapper{}",
      status.reshade_switches, status.reshade_switch_stopped ? ", then one couldn't: output switching stopped while ReShade is loaded" : "");
}

std::string DescribeBridge()
{
   if (!IsReShadeLoaded(load_state.load(std::memory_order_acquire)))
      return "no ReShade";
   std::string_view off_reason;
   switch (bridge_state.load(std::memory_order_relaxed))
   {
   case BridgeState::Unregistered: off_reason = "not registered"; break;
   case BridgeState::TurnedOff: off_reason = "turned off in Luma.toml"; break;
   case BridgeState::MissingExport: off_reason = "ReShade has no add-on API"; break;
   case BridgeState::Refused: off_reason = "ReShade refused the registration, see ReShade.log"; break;
   case BridgeState::On: break;
   }
   if (!off_reason.empty())
      return std::format("off ({}), Luma's present work runs at the proxy's Present: ReShade's effects run before Luma's composition; {}", off_reason, AddonEvents::Describe());
   const bool effects_ran = present_event_at_last_effects.load(std::memory_order_relaxed) == present_events.load(std::memory_order_relaxed);
   return std::format("on (API {}), Luma's present work runs at {}; effects {}; effect runtime {} ({} inits); ReShade's overlay {}; {}", ABI::API_VERSION,
      last_present_work_at_event.load(std::memory_order_relaxed) ? "ReShade's present event" : "the proxy's Present (no event before the last present)",
      effects_ran ? std::format("ran in the last frame, {:.2f} ms CPU", effects_cpu_ms.load(std::memory_order_relaxed)) : std::string("didn't run in the last frame"),
      live_effect_runtimes.load(std::memory_order_relaxed) != 0 ? "ready" : "not initialized", effect_runtime_inits.load(std::memory_order_relaxed),
      reshade_overlay_open.load(std::memory_order_relaxed) ? "open" : "closed", AddonEvents::Describe());
}
} // namespace ReShade
} // namespace Luma
