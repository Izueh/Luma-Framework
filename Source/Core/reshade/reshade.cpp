#include "reshade/reshade.hpp"
#include "reshade/addon_events.hpp"
#include "reshade/reshade_abi.hpp"

#include <array>
#include <atomic>
#include <filesystem>
#include <format>
#include <string_view>
#include <system_error>
#include <vector>

#include <Windows.h>
#include <Psapi.h>

#include "hooks/hooks.hpp"
#include "hooks/swapchain_proxy.hpp"
#include "hooks/third_party_modules.hpp"
#include "overlay/overlay.hpp"
#include "utils/config.hpp"
#include "utils/log.hpp"
#include "utils/modules.hpp"
#include "utils/strings.hpp"

namespace Luma {
namespace ReShade {
namespace {
#ifdef _WIN64
constexpr const wchar_t* FILE_NAME = L"ReShade64.dll";
#else
constexpr const wchar_t* FILE_NAME = L"ReShade32.dll";
#endif
constexpr const char* CONFIG_SECTION = "ReShade";
constexpr const char* LOAD_CONFIG = "Load";
constexpr const char* BRIDGE_CONFIG = "Bridge";
// Without it, an injected ReShade's DllMain fails unless a "ReShade.ini" is in the game's folder
constexpr const wchar_t* LOADING_CHECK_VARIABLE = L"RESHADE_DISABLE_LOADING_CHECK";

enum class Order : uint8_t
{
   Unchecked, // Before the first present
   ReShadeAboveLuma, // The intended order: ReShade's hooks outermost
   LumaAboveReShade,
   NoReShadeHooks, // ReShade hooks none of the creation exports (a proxy calls them directly)
};

struct Export
{
   const wchar_t* module;
   const char* name;
};

// The ones Luma hooks and injected ReShade hooks too
constexpr std::array EXPORTS = {
   Export{L"d3d11.dll", "D3D11CreateDevice"},
   Export{L"d3d11.dll", "D3D11CreateDeviceAndSwapChain"},
   Export{L"dxgi.dll", "CreateDXGIFactory"},
   Export{L"dxgi.dll", "CreateDXGIFactory1"},
   Export{L"dxgi.dll", "CreateDXGIFactory2"},
};

// Written once (by Load, or by the first present), before "arrangement" is stored: read only after it says they're set
std::string module_name;
uintptr_t module_base = 0;
std::atomic<Arrangement> arrangement = Arrangement::Absent;
std::atomic<Order> order = Order::Unchecked;

enum class Bridge : uint8_t
{
   Unregistered, // No ReShade (yet)
   On,
   TurnedOff, // The setting
   MissingExport, // A ReShade without the add-on API
   Refused, // ReShadeRegisterAddon returned false: an older API version, add-ons disabled, the name taken (ReShade.log says)
};

std::atomic<Bridge> bridge = Bridge::Unregistered;
std::atomic<bool> bridge_attempted = false;
// Written on the presenting thread, read by the Info tab
std::atomic<uint64_t> present_events = 0;
std::atomic<uint64_t> effects_after_event = UINT64_MAX; // "present_events" when effects last finished
std::atomic<int64_t> effects_begin = 0;
std::atomic<float> effects_ms = 0.f;
std::atomic<bool> last_present_work_at_event = false;
std::atomic<uint32_t> runtimes = 0;
std::atomic<uint32_t> runtime_inits = 0;
std::atomic<bool> reshade_overlay_open = false;

// As ReShade's own DllMain looks for another instance of itself
HMODULE FindLoaded()
{
   std::vector<HMODULE> loaded(256);
   DWORD needed = 0;
   while (EnumProcessModules(GetCurrentProcess(), loaded.data(), DWORD(loaded.size() * sizeof(HMODULE)), &needed) && needed > loaded.size() * sizeof(HMODULE))
      loaded.resize(needed / sizeof(HMODULE));
   loaded.resize(needed / sizeof(HMODULE));
   for (HMODULE module : loaded)
   {
      if (module != Utils::Modules::Self() && GetProcAddress(module, "ReShadeVersion"))
         return module;
   }
   return nullptr;
}

// Registers it as an overlay (the loader notification usually did already, Add ignores a known base)
void Remember(HMODULE module, Arrangement found)
{
   MODULEINFO info = {};
   GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info));
   module_name = Utils::Strings::ToUtf8(Utils::Modules::GetPath(module).filename());
   module_base = reinterpret_cast<uintptr_t>(module);
   if (info.SizeOfImage)
      Hooks::ThirdPartyModules::Add(module_name, module, info.SizeOfImage);
   arrangement.store(found, std::memory_order_release);
}

bool IsInSelf(const void* address)
{
   HMODULE module = nullptr;
   return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module) &&
      module == Utils::Modules::Self();
}

// Which hook a creation export reaches first. Not under the loader lock (module names take it).
void CheckHookOrder(Arrangement current)
{
   uint32_t reshade_first = 0, luma_first = 0, other_first = 0, unhooked = 0;
   std::string details;
   for (const Export& entry : EXPORTS)
   {
      const HMODULE module = Utils::Modules::FindSystemModule(entry.module);
      const void* target = module ? Utils::Modules::FindExport(module, entry.name) : nullptr;
      if (!target)
         continue;
      const void* first = Hooks::FollowJump(target);
      std::string who;
      if (!first || Utils::Modules::IsInSystemModule(first))
      {
         unhooked++;
         who = "not hooked";
      }
      else if (Hooks::ThirdPartyModules::FindBase(first) == module_base)
      {
         reshade_first++;
         who = "ReShade";
      }
      else if (IsInSelf(first))
      {
         luma_first++;
         who = "Luma";
      }
      else
      {
         other_first++;
         who = Utils::Modules::GetName(first);
      }
      details += std::format("{}{} {}", details.empty() ? "" : ", ", entry.name, who);
   }
   const Hooks::Verification verification = Hooks::GetLastVerification();
   const Order found = reshade_first == 0 ? Order::NoReShadeHooks : (luma_first == 0 ? Order::ReShadeAboveLuma : Order::LumaAboveReShade);
   order.store(found, std::memory_order_relaxed);

   const std::string summary = std::format("the creation exports reach first: {} (ReShade {}, Luma {}, another hooker {}, none {}); Luma's hooks: {} first in their chain, {} chained under another hooker, {} removed by someone else",
      details, reshade_first, luma_first, other_first, unhooked, verification.intact, verification.chained, verification.restored);
   if (current != Arrangement::InjectedByLuma)
      Utils::Log::Info("ReShade: at the first present, {}. ReShade wasn't loaded by Luma, so the intended order (ReShade's hooks outermost, Luma's under them) isn't expected", summary);
   else if (found == Order::ReShadeAboveLuma && verification.restored == 0)
      Utils::Log::Info("ReShade: at the first present, {}. ReShade's hooks are outermost and Luma's sit under them, as intended", summary);
   else
      Utils::Log::Warning("ReShade: at the first present, {}. Luma's hooks are not under ReShade's, which is not the intended order: ReShade's hooks should be outermost, with Luma's under them", summary);
}
bool IsLoaded(Arrangement current)
{
   return current == Arrangement::InjectedByLuma || current == Arrangement::AlreadyLoaded;
}

int64_t Now()
{
   LARGE_INTEGER counter;
   QueryPerformanceCounter(&counter);
   return counter.QuadPart;
}

// Before ReShade's effects, after the add-ons registered before Luma: Luma's present work (up to the display composition) of the Present that follows, then the third party work window
void __cdecl OnPresentEvent(void* queue, Abi::DeviceObject* swapchain, const void* source_rect, const void* dest_rect, uint32_t dirty_rect_count, const void* dirty_rects)
{
   present_events.fetch_add(1, std::memory_order_relaxed);
   Abi::Device* device = swapchain ? swapchain->GetDevice() : nullptr;
   if (!device || device->GetApi() != Abi::DeviceApi::D3D11)
      return;
   // What ReShade's wrapper holds: Luma's proxy, or the real swapchain without one
   IDXGISwapChain* native = reinterpret_cast<IDXGISwapChain*>(static_cast<uintptr_t>(swapchain->GetNative()));
   Hooks::DXGI::PresentFromEvent(Hooks::SwapchainProxy::Unwrap(native));
}

// After ReShade's Present: Luma's Present closed the window already, unless it never came
void __cdecl OnFinishPresentEvent(void* queue, Abi::DeviceObject* swapchain)
{
   static std::atomic<bool> warned = false;
   if (Hooks::DXGI::AbandonPresentFromEvent() && !warned.exchange(true))
      Utils::Log::Warning("ReShade bridge: Luma's Present didn't follow ReShade's present event: Luma's present work (up to the display composition) ran at the event, but Luma's overlay wasn't drawn for that frame; "
         "the window for other overlays' work was closed when ReShade finished presenting. Logged once.");
}

void __cdecl OnBeginEffects(void* runtime, void* command_list, Abi::Handle rtv, Abi::Handle rtv_srgb)
{
   effects_begin.store(Now(), std::memory_order_relaxed);
}

void __cdecl OnFinishEffects(void* runtime, void* command_list, Abi::Handle rtv, Abi::Handle rtv_srgb)
{
   LARGE_INTEGER frequency;
   QueryPerformanceFrequency(&frequency);
   effects_ms.store(float(double(Now() - effects_begin.load(std::memory_order_relaxed)) * 1000.0 / double(frequency.QuadPart)), std::memory_order_relaxed);
   effects_after_event.store(present_events.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void __cdecl OnInitEffectRuntime(void* runtime)
{
   runtimes.fetch_add(1, std::memory_order_relaxed);
   runtime_inits.fetch_add(1, std::memory_order_relaxed);
}

void __cdecl OnDestroyEffectRuntime(void* runtime)
{
   if (runtimes.load(std::memory_order_relaxed) != 0)
      runtimes.fetch_sub(1, std::memory_order_relaxed);
   reshade_overlay_open.store(false, std::memory_order_relaxed);
}

// One overlay open at a time: returning true keeps ReShade's closed
bool __cdecl OnOpenOverlay(void* runtime, bool open, int32_t source)
{
   if (open && Overlay::IsOpen())
   {
      Utils::Log::Info("ReShade bridge: ReShade's overlay kept closed, Luma's is open");
      return true;
   }
   reshade_overlay_open.store(open, std::memory_order_relaxed);
   return false;
}

// From Load, before ReShade loads its add-ons (at its first device creation), so Luma's present handler runs before theirs
void RegisterBridge()
{
   const Arrangement current = arrangement.load(std::memory_order_acquire);
   if (!IsLoaded(current) || bridge_attempted.exchange(true))
      return;
   AddonEvents::Install(reinterpret_cast<HMODULE>(module_base), current == Arrangement::AlreadyLoaded);
   constexpr const char* FALLBACK = "Luma's present work runs at the proxy's Present: ReShade's effects and add-ons see the image before Luma's display composition";
   bool enabled = true;
   Config::Get(CONFIG_SECTION, BRIDGE_CONFIG, enabled);
   if (!enabled)
   {
      bridge.store(Bridge::TurnedOff, std::memory_order_relaxed);
      Utils::Log::Info("ReShade bridge: off, turned off in Luma.toml ([{}] {} = false). {}", CONFIG_SECTION, BRIDGE_CONFIG, FALLBACK);
      return;
   }
   const HMODULE module = reinterpret_cast<HMODULE>(module_base);
   const auto register_addon = reinterpret_cast<Abi::RegisterAddonFunction>(GetProcAddress(module, "ReShadeRegisterAddon"));
   const auto register_event = reinterpret_cast<Abi::RegisterEventFunction>(GetProcAddress(module, "ReShadeRegisterEvent"));
   if (!register_addon || !register_event)
   {
      bridge.store(Bridge::MissingExport, std::memory_order_relaxed);
      Utils::Log::Warning("ReShade bridge: off, {} has no add-on API (no ReShadeRegisterAddon or ReShadeRegisterEvent). {}", module_name, FALLBACK);
      return;
   }
   if (!register_addon(Utils::Modules::Self(), Abi::API_VERSION))
   {
      bridge.store(Bridge::Refused, std::memory_order_relaxed);
      Utils::Log::Warning("ReShade bridge: off, {} refused Luma as an add-on with API version {} (ReShade.log says why: an older ReShade, add-ons disabled, the name taken). {}",
         module_name, Abi::API_VERSION, FALLBACK);
      return;
   }
   register_event(Abi::Event::Present, reinterpret_cast<void*>(&OnPresentEvent));
   register_event(Abi::Event::FinishPresent, reinterpret_cast<void*>(&OnFinishPresentEvent));
   register_event(Abi::Event::ReShadeBeginEffects, reinterpret_cast<void*>(&OnBeginEffects));
   register_event(Abi::Event::ReShadeFinishEffects, reinterpret_cast<void*>(&OnFinishEffects));
   register_event(Abi::Event::InitEffectRuntime, reinterpret_cast<void*>(&OnInitEffectRuntime));
   register_event(Abi::Event::DestroyEffectRuntime, reinterpret_cast<void*>(&OnDestroyEffectRuntime));
   register_event(Abi::Event::ReShadeOpenOverlay, reinterpret_cast<void*>(&OnOpenOverlay));
   bridge.store(Bridge::On, std::memory_order_relaxed);
   Utils::Log::Info("ReShade bridge: on, Luma registered as an add-on of {} (API version {}, from ReShade v6.8.0's headers)", module_name, Abi::API_VERSION);
}
} // namespace

void Load()
{
   if (const HMODULE loaded = FindLoaded())
   {
      Remember(loaded, Arrangement::AlreadyLoaded);
      Utils::Log::Info("ReShade: already loaded as {} ({}), Luma doesn't load another: it runs as a proxy or was injected by something else",
         module_name, Utils::Strings::ToUtf8(Utils::Modules::GetPath(loaded)));
      RegisterBridge();
      return;
   }

   const std::filesystem::path path = Utils::Modules::SelfDirectory() / FILE_NAME;
   std::error_code error;
   if (!std::filesystem::is_regular_file(path, error))
   {
      Utils::Log::Info("ReShade: absent (no {} next to Luma)", Utils::Strings::ToUtf8(FILE_NAME));
      return;
   }
   bool load = true;
   Config::Get(CONFIG_SECTION, LOAD_CONFIG, load);
   if (!load)
   {
      arrangement.store(Arrangement::Disabled, std::memory_order_release);
      Utils::Log::Info("ReShade: {} not loaded, turned off in Luma.toml ([{}] {} = false)", Utils::Strings::ToUtf8(path), CONFIG_SECTION, LOAD_CONFIG);
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
      arrangement.store(Arrangement::LoadFailed, std::memory_order_release);
      Utils::Log::Error("ReShade: loading {} failed (error {})", Utils::Strings::ToUtf8(path), load_error);
      return;
   }
   Remember(module, Arrangement::InjectedByLuma);
   Utils::Log::Info("ReShade: injected by Luma, {} loaded at {}, after Luma's loader notification so its hooks go in on top of Luma's", Utils::Strings::ToUtf8(path),
      static_cast<void*>(module));
   // Still in DllMain, as the load: the registration reads ReShade's config and Luma's version resource, nothing waits
   RegisterBridge();
}

void OnPresent()
{
   static std::atomic<bool> checked = false;
   if (checked.load(std::memory_order_relaxed) || checked.exchange(true))
      return;
   Arrangement current = arrangement.load(std::memory_order_acquire);
   // ReShade as the game's dxgi.dll, with Luma under another dll name, can load after Luma
   if (current == Arrangement::Absent)
   {
      if (const HMODULE loaded = FindLoaded())
      {
         Remember(loaded, Arrangement::AlreadyLoaded);
         current = Arrangement::AlreadyLoaded;
         Utils::Log::Info("ReShade: loaded after Luma as {} ({}), not by Luma", module_name, Utils::Strings::ToUtf8(Utils::Modules::GetPath(loaded)));
         // Its add-ons may have registered before: their present handlers run before Luma's
         RegisterBridge();
      }
   }
   if (current == Arrangement::InjectedByLuma || current == Arrangement::AlreadyLoaded)
      CheckHookOrder(current);
}

void OnPresentOverlay(bool after_third_party_work)
{
   last_present_work_at_event.store(after_third_party_work, std::memory_order_relaxed);
   if (after_third_party_work)
   {
      static std::atomic<bool> logged = false;
      if (!logged.exchange(true))
         Utils::Log::Info("ReShade bridge: Luma's present work runs at ReShade's present event: ReShade's effects and the add-ons registered after Luma "
            "see Luma's composed image, Luma's overlay draws at the proxy's Present after them");
      return;
   }
   static std::atomic<bool> logged = false;
   if (!IsLoaded(arrangement.load(std::memory_order_acquire)) || logged.exchange(true))
      return;
   if (bridge.load(std::memory_order_relaxed) == Bridge::On)
      Utils::Log::Info("ReShade bridge: a present without ReShade's present event before it (ReShade skipped the event, or turned its add-ons off): "
         "Luma's present work ran at the proxy's Present, before ReShade's effects, for that frame. Logged once.");
   else
      Utils::Log::Info("ReShade: Luma's present work runs at the proxy's Present (the bridge is off): ReShade's effects run before Luma's display composition, "
         "Luma's overlay after them");
}

bool IsOverlayOpen()
{
   return reshade_overlay_open.load(std::memory_order_relaxed);
}

Arrangement GetArrangement()
{
   return arrangement.load(std::memory_order_acquire);
}

std::string Describe()
{
   const Arrangement current = arrangement.load(std::memory_order_acquire);
   std::string_view order_text;
   switch (order.load(std::memory_order_relaxed))
   {
   case Order::Unchecked: order_text = "hook order not checked yet"; break;
   case Order::ReShadeAboveLuma: order_text = "its hooks above Luma's"; break;
   case Order::LumaAboveReShade: order_text = "Luma's hooks above its (not the intended order)"; break;
   case Order::NoReShadeHooks: order_text = "it hooks none of the creation exports"; break;
   }
   switch (current)
   {
   case Arrangement::Absent: return "none";
   case Arrangement::Disabled: return std::format("{} next to Luma, turned off in Luma.toml", Utils::Strings::ToUtf8(FILE_NAME));
   case Arrangement::InjectedByLuma: return std::format("{}, injected by Luma, {}; {}", module_name, order_text, AddonEvents::Describe());
   case Arrangement::AlreadyLoaded: return std::format("{}, not loaded by Luma (a proxy, or injected by something else), {}; {}", module_name, order_text, AddonEvents::Describe());
   case Arrangement::LoadFailed: return std::format("{} failed to load (see Luma.log)", Utils::Strings::ToUtf8(FILE_NAME));
   }
   return {};
}

std::string DescribeBridge()
{
   if (!IsLoaded(arrangement.load(std::memory_order_acquire)))
      return "no ReShade";
   std::string_view off_reason;
   switch (bridge.load(std::memory_order_relaxed))
   {
   case Bridge::Unregistered: off_reason = "not registered"; break;
   case Bridge::TurnedOff: off_reason = "turned off in Luma.toml"; break;
   case Bridge::MissingExport: off_reason = "ReShade has no add-on API"; break;
   case Bridge::Refused: off_reason = "ReShade refused the registration, see ReShade.log"; break;
   case Bridge::On: break;
   }
   if (!off_reason.empty())
      return std::format("off ({}), Luma's present work runs at the proxy's Present: ReShade's effects run before Luma's composition", off_reason);
   const bool effects_ran = effects_after_event.load(std::memory_order_relaxed) == present_events.load(std::memory_order_relaxed);
   return std::format("on (API {}), Luma's present work runs at {}; effects {}; effect runtime {} ({} inits); ReShade's overlay {}", Abi::API_VERSION,
      last_present_work_at_event.load(std::memory_order_relaxed) ? "ReShade's present event" : "the proxy's Present (no event before the last present)",
      effects_ran ? std::format("ran in the last frame, {:.2f} ms CPU", effects_ms.load(std::memory_order_relaxed)) : std::string("didn't run in the last frame"),
      runtimes.load(std::memory_order_relaxed) != 0 ? "ready" : "not initialized", runtime_inits.load(std::memory_order_relaxed),
      reshade_overlay_open.load(std::memory_order_relaxed) ? "open" : "closed");
}
} // namespace ReShade
} // namespace Luma
