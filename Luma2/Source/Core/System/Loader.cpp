#include "System/Loader.hpp"

#include <array>
#include <string_view>

#include <winternl.h>

#include "Core.hpp"
#include "Hooks/Hooks.hpp"
#include "Utils/Log.hpp"
#include "Utils/Modules.hpp"
#include "Utils/Strings.hpp"

namespace Luma {
namespace System {
namespace Loader {
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

bool IsGraphicsModule(std::wstring_view name)
{
   for (std::wstring_view graphics_module : GRAPHICS_MODULES)
   {
      if (Utils::Strings::EqualsIgnoreCase(name, graphics_module))
         return true;
   }
   return false;
}

void OnGraphicsModule(std::wstring_view name, void* base, bool was_already_loaded)
{
   Core::Initialize();
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
   if (Core::IsInitialized())
      Hooks::OnModuleLoaded(name);
}
} // namespace

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
   Core::Shutdown();
}
} // namespace Loader
} // namespace System
} // namespace Luma
