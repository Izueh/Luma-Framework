#include "SystemHooks.h"

#if ENABLE_SAFETYHOOK

#include <intrin.h>
#include <atomic>
#include <mutex>

namespace SystemHooks
{
   namespace
   {
      // Same layout as "D3DKMT_QUERYADAPTERINFO" (from the WDK headers)
      struct KmtQueryAdapterInfo
      {
         UINT adapter;
         UINT type;
         void* private_driver_data;
         UINT private_driver_data_size;
      };
      constexpr UINT kmt_query_umdriver_private = 0; // KMTQAITYPE_UMDRIVERPRIVATE

      std::mutex init_mutex;
      safetyhook::InlineHook kmt_query_adapter_info_hook;
      safetyhook::InlineHook get_module_handle_a_hook;
      safetyhook::InlineHook get_module_handle_w_hook;

      std::atomic<HMODULE> adapter_private_data_override_module = nullptr;
      std::atomic<AdapterPrivateDataOverride> adapter_private_data_override = nullptr;
      std::atomic<HMODULE> hidden_module_caller = nullptr;
      wchar_t hidden_module_name[MAX_PATH] = {}; // Only changed while the hooks are disabled

      LONG WINAPI HookedKmtQueryAdapterInfo(const KmtQueryAdapterInfo* info)
      {
         const LONG status = kmt_query_adapter_info_hook.stdcall<LONG>(info);
         const AdapterPrivateDataOverride data_override = adapter_private_data_override;
         if (data_override && info && info->type == kmt_query_umdriver_private && info->private_driver_data && Hooks::IsAddressInModule(_ReturnAddress(), adapter_private_data_override_module))
            return data_override(info->private_driver_data, info->private_driver_data_size, status);
         return status;
      }

      bool IsHiddenModule(const wchar_t* module_name)
      {
         const wchar_t* file_name = wcsrchr(module_name, L'\\');
         return _wcsicmp(file_name ? file_name + 1 : module_name, hidden_module_name) == 0;
      }

      HMODULE WINAPI HookedGetModuleHandleA(LPCSTR module_name)
      {
         wchar_t module_name_w[MAX_PATH] = {};
         if (module_name && Hooks::IsAddressInModule(_ReturnAddress(), hidden_module_caller) && MultiByteToWideChar(CP_ACP, 0, module_name, -1, module_name_w, MAX_PATH) && IsHiddenModule(module_name_w))
         {
            SetLastError(ERROR_MOD_NOT_FOUND);
            return nullptr;
         }
         return get_module_handle_a_hook.stdcall<HMODULE>(module_name);
      }

      HMODULE WINAPI HookedGetModuleHandleW(LPCWSTR module_name)
      {
         if (module_name && Hooks::IsAddressInModule(_ReturnAddress(), hidden_module_caller) && IsHiddenModule(module_name))
         {
            SetLastError(ERROR_MOD_NOT_FOUND);
            return nullptr;
         }
         return get_module_handle_w_hook.stdcall<HMODULE>(module_name);
      }

      bool CreateHook(safetyhook::InlineHook& hook, const wchar_t* module_name, const char* function_name, void* destination)
      {
         void* const target = GetProcAddress(GetModuleHandleW(module_name), function_name);
         if (!target)
            return false;
         auto new_hook = safetyhook::InlineHook::create(target, destination, safetyhook::InlineHook::StartDisabled);
         if (!new_hook)
            return false;
         hook = std::move(*new_hook);
         return true;
      }
   }

   bool Init()
   {
      const std::lock_guard lock(init_mutex);
      if (kmt_query_adapter_info_hook && get_module_handle_a_hook && get_module_handle_w_hook)
         return true;
      // gdi32 is always loaded with DXGI
      return (kmt_query_adapter_info_hook || CreateHook(kmt_query_adapter_info_hook, L"gdi32.dll", "D3DKMTQueryAdapterInfo", reinterpret_cast<void*>(&HookedKmtQueryAdapterInfo)))
         && (get_module_handle_a_hook || CreateHook(get_module_handle_a_hook, L"kernel32.dll", "GetModuleHandleA", reinterpret_cast<void*>(&HookedGetModuleHandleA)))
         && (get_module_handle_w_hook || CreateHook(get_module_handle_w_hook, L"kernel32.dll", "GetModuleHandleW", reinterpret_cast<void*>(&HookedGetModuleHandleW)));
   }

   bool SetAdapterPrivateDataOverride(HMODULE caller_module, AdapterPrivateDataOverride data_override)
   {
      adapter_private_data_override = data_override;
      adapter_private_data_override_module = caller_module;
      return Hooks::SetEnabled(kmt_query_adapter_info_hook, caller_module != nullptr);
   }

   bool SetHiddenModule(HMODULE caller_module, const wchar_t* module_name)
   {
      if (!caller_module)
      {
         hidden_module_caller = nullptr;
         const bool disabled = Hooks::SetEnabled(get_module_handle_a_hook, false) & Hooks::SetEnabled(get_module_handle_w_hook, false);
         hidden_module_name[0] = L'\0';
         return disabled;
      }
      wcsncpy_s(hidden_module_name, module_name ? module_name : L"", _TRUNCATE);
      hidden_module_caller = caller_module;
      return Hooks::SetEnabled(get_module_handle_a_hook, true) && Hooks::SetEnabled(get_module_handle_w_hook, true);
   }
}

#endif
