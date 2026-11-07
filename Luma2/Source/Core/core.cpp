#include "core.hpp"

#include <array>
#include <atomic>
#include <optional>
#include <string_view>

#include <winternl.h>

#include "game/game.hpp"
#include "hooks/hooks.hpp"
#include "settings/storage.hpp"
#include "trace/mcp.hpp"
#include "ui/input.hpp"
#include "ui/overlay.hpp"
#include "utils/log.hpp"
#include "utils/modules.hpp"
#include "utils/strings.hpp"

namespace Luma {
namespace Core {
namespace {
// Documented, but not in the Windows SDK headers
struct LdrDllNotificationData
{
   ULONG flags;
   const UNICODE_STRING* full_dll_name;
   const UNICODE_STRING* base_dll_name;
   void* dll_base;
   ULONG size_of_image;
};
constexpr ULONG LDR_DLL_NOTIFICATION_REASON_LOADED = 1;
using LdrDllNotificationFunction = VOID(CALLBACK*)(ULONG reason, const LdrDllNotificationData* data, PVOID context);
using LdrRegisterDllNotificationFunction = NTSTATUS(NTAPI*)(ULONG flags, LdrDllNotificationFunction callback, PVOID context, PVOID* cookie);
using LdrUnregisterDllNotificationFunction = NTSTATUS(NTAPI*)(PVOID cookie);

constexpr std::array<std::wstring_view, 2> GRAPHICS_MODULES = {L"d3d11.dll", L"d3d12.dll"};

void* notification_cookie = nullptr;
std::atomic<bool> initialized = false;

bool IsGraphicsModule(std::wstring_view name)
{
   for (std::wstring_view graphics_module : GRAPHICS_MODULES)
   {
      if (Utils::Strings::EqualsIgnoreCase(name, graphics_module))
         return true;
   }
   return false;
}

// After the game's Init and its saved settings, so what's declared and enabled can decide
void InstallHooks()
{
   const std::optional<Api> api = Game::GetInfo().api;
   Hooks::InstallDXGI();
   if (!api || *api == Api::D3D11)
      Hooks::InstallD3D11();
   if (!api || *api == Api::D3D12)
      Hooks::InstallD3D12();
}

// Runs once, under the loader lock
void Initialize()
{
   if (initialized.exchange(true))
      return;
   Utils::Log::Open(Utils::Modules::SelfDirectory() / "Luma.log");
   Utils::Log::Info("Luma2 initializing in {}", Utils::Strings::ToUtf8(Utils::Modules::GetPath(nullptr)));

   Game::Init();
   Game::LogDeclarations();
   const std::filesystem::path settings_path = Utils::Modules::SelfDirectory() / "Luma.toml";
   UI::Input::Initialize();
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

void OnGraphicsModule(std::wstring_view name, void* base, bool was_already_loaded)
{
   Initialize();
   Utils::Log::Info("Core: {} {} at {}", Utils::Strings::ToUtf8(name), was_already_loaded ? "was already loaded" : "loaded", base);
}

VOID CALLBACK OnDllNotification(ULONG reason, const LdrDllNotificationData* data, PVOID context)
{
   (void)context;
   if (reason != LDR_DLL_NOTIFICATION_REASON_LOADED || !data || !data->base_dll_name)
      return;
   const std::wstring_view name(data->base_dll_name->Buffer, data->base_dll_name->Length / sizeof(wchar_t));
   if (IsGraphicsModule(name))
      OnGraphicsModule(name, data->dll_base, false);
   if (initialized)
      Hooks::OnModuleLoaded(name);
}

void Attach(HMODULE module)
{
   Utils::Modules::SetSelf(module);
   DisableThreadLibraryCalls(module);
   Utils::Log::Info("Luma2 loaded from {}", Utils::Strings::ToUtf8(Utils::Modules::GetPath(module)));

   HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
   const auto register_notification = reinterpret_cast<LdrRegisterDllNotificationFunction>(GetProcAddress(ntdll, "LdrRegisterDllNotification"));
   if (!register_notification || register_notification(0, &OnDllNotification, nullptr, &notification_cookie) < 0)
      Utils::Log::Error("Core: LdrRegisterDllNotification failed, only modules already loaded will be detected");

   // Loaded before Luma (d3d11 and d3d12 import dxgi)
   for (std::wstring_view name : GRAPHICS_MODULES)
   {
      if (HMODULE graphics_module = GetModuleHandleW(name.data()))
         OnGraphicsModule(name, graphics_module, true);
   }
}

void Detach(bool is_process_terminating)
{
   if (is_process_terminating)
   {
      Utils::Log::Info("Process exiting");
      return;
   }
   if (notification_cookie)
   {
      HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
      if (const auto unregister_notification = reinterpret_cast<LdrUnregisterDllNotificationFunction>(GetProcAddress(ntdll, "LdrUnregisterDllNotification")))
         unregister_notification(notification_cookie);
      notification_cookie = nullptr;
   }
   Trace::Mcp::Stop();
   Hooks::UninstallAll();
   Utils::Log::Info("Luma2 unloaded");
   Utils::Log::Close();
}
} // namespace

BOOL DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
   switch (reason)
   {
   case DLL_PROCESS_ATTACH: Attach(module); break;
   case DLL_PROCESS_DETACH: Detach(reserved != nullptr); break;
   }
   return TRUE;
}
} // namespace Core
} // namespace Luma
