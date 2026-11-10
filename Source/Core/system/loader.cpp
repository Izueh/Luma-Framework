#include "system/loader.hpp"

#include <atomic>
#include <string_view>

#include <winternl.h>

#include "hooks/hooks.hpp"
#include "utils/log.hpp"
#include "utils/modules.hpp"
#include "utils/strings.hpp"

namespace Luma {
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

constexpr std::wstring_view GRAPHICS_MODULE = L"d3d11.dll";

void* notification_cookie = nullptr;
void (*on_initialize)() = nullptr;
std::atomic<bool> initialized = false;

void OnGraphicsModule(void* base, bool was_already_loaded)
{
   if (initialized.exchange(true))
      return;
   Utils::Log::Info("Loader: d3d11.dll {} at {}", was_already_loaded ? "was already loaded" : "loaded", base);
   on_initialize();
}

VOID CALLBACK OnDllNotification(ULONG reason, const LdrDllNotificationData* data, PVOID context)
{
   (void)context;
   if (reason != LDR_DLL_NOTIFICATION_REASON_LOADED || !data || !data->base_dll_name)
      return;
   const std::wstring_view name(data->base_dll_name->Buffer, data->base_dll_name->Length / sizeof(wchar_t));
   if (Utils::Strings::EqualsIgnoreCase(name, GRAPHICS_MODULE))
      OnGraphicsModule(data->dll_base, false);
   if (initialized)
      Hooks::OnModuleLoaded(name);
}
} // namespace

void Attach(HMODULE module, void (*initialize)())
{
   Utils::Modules::SetSelf(module);
   on_initialize = initialize;

   HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
   const auto register_notification = reinterpret_cast<LdrRegisterDllNotificationFunction>(GetProcAddress(ntdll, "LdrRegisterDllNotification"));
   if (!register_notification || register_notification(0, &OnDllNotification, nullptr, &notification_cookie) < 0)
      Utils::Log::Error("Loader: LdrRegisterDllNotification failed, only an already loaded d3d11.dll will be detected");

   if (HMODULE graphics_module = Utils::Modules::FindSystemModule(GRAPHICS_MODULE))
      OnGraphicsModule(graphics_module, true);
}

void Detach()
{
   if (notification_cookie)
   {
      HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
      if (const auto unregister_notification = reinterpret_cast<LdrUnregisterDllNotificationFunction>(GetProcAddress(ntdll, "LdrUnregisterDllNotification")))
         unregister_notification(notification_cookie);
      notification_cookie = nullptr;
   }
   Hooks::UninstallAll();
}
} // namespace Loader
} // namespace Luma
