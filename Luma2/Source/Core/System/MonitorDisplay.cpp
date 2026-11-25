#include "System/MonitorDisplay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <dxgi1_6.h>
#include <icm.h>
#include <wrl/client.h>
#if LUMA_USE_NVAPI
#include <nvapi.h>
#endif

#include "System/Modules.hpp"
#include "Utils/Strings.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace System {
namespace Display {
namespace {
constexpr auto DISPLAYCONFIG_DEVICE_INFO_SET_SDR_WHITE_LEVEL = DISPLAYCONFIG_DEVICE_INFO_TYPE(0xFFFFFFEE);

struct alignas(4) DisplayConfigSetSdrWhiteLevel
{
   DISPLAYCONFIG_DEVICE_INFO_HEADER header;
   ULONG sdr_white_level;
   BYTE final_value;
};

// Luma's own factory, from the system's dxgi.dll: the game's may be a proxy's
ComPtr<IDXGIFactory1> factory;
bool first_change_query = true;

IDXGIFactory1* EnsureFactory()
{
   if (factory && factory->IsCurrent())
      return factory.Get();
   using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
   static const auto CREATE = reinterpret_cast<CreateFactory>(Modules::FindExport(Modules::FindSystemModule(L"dxgi.dll"), "CreateDXGIFactory1"));
   factory.Reset();
   if (CREATE)
      CREATE(IID_PPV_ARGS(&factory));
   return factory.Get();
}

ComPtr<IDXGIOutput> FindOutput(HMONITOR monitor)
{
   if (!monitor)
      return nullptr;
   IDXGIFactory1* dxgi_factory = EnsureFactory();
   if (!dxgi_factory)
      return nullptr;
   ComPtr<IDXGIAdapter1> adapter;
   for (UINT adapter_index = 0; SUCCEEDED(dxgi_factory->EnumAdapters1(adapter_index, adapter.ReleaseAndGetAddressOf())); adapter_index++)
   {
      ComPtr<IDXGIOutput> candidate;
      for (UINT output_index = 0; SUCCEEDED(adapter->EnumOutputs(output_index, candidate.ReleaseAndGetAddressOf())); output_index++)
      {
         DXGI_OUTPUT_DESC desc = {};
         if (SUCCEEDED(candidate->GetDesc(&desc)) && desc.Monitor == monitor)
            return candidate;
      }
   }
   return nullptr;
}

// The active path whose source is on the window's monitor
std::optional<DISPLAYCONFIG_PATH_INFO> FindPath(HMONITOR monitor)
{
   UINT32 path_count = 0;
   UINT32 mode_count = 0;
   if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS)
      return std::nullopt;
   std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
   std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
   if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr) != ERROR_SUCCESS)
      return std::nullopt;
   for (UINT32 i = 0; i < path_count; i++)
   {
      const DISPLAYCONFIG_PATH_INFO& path = paths[i];
      if (!(path.flags & DISPLAYCONFIG_PATH_ACTIVE) || !(path.sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE))
         continue;
      const bool is_virtual = path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE;
      const UINT32 mode_index = is_virtual ? path.sourceInfo.sourceModeInfoIdx : path.sourceInfo.modeInfoIdx;
      if (mode_index == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || mode_index >= mode_count || modes[mode_index].infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
         continue;
      const DISPLAYCONFIG_SOURCE_MODE& source = modes[mode_index].sourceMode;
      const RECT rect = {source.position.x, source.position.y, source.position.x + LONG(source.width), source.position.y + LONG(source.height)};
      if (!IsRectEmpty(&rect) && MonitorFromRect(&rect, MONITOR_DEFAULTTONULL) == monitor)
         return path;
   }
   return std::nullopt;
}

template <typename T>
bool GetDeviceInfo(const DISPLAYCONFIG_PATH_INFO& path, DISPLAYCONFIG_DEVICE_INFO_TYPE type, T& info)
{
   info = {};
   info.header.type = type;
   info.header.size = sizeof(info);
   info.header.adapterId = path.targetInfo.adapterId;
   info.header.id = path.targetInfo.id;
   return DisplayConfigGetDeviceInfo(&info.header) == ERROR_SUCCESS;
}

HMONITOR GetMonitor(HWND window)
{
   return MonitorFromWindow(window, window ? MONITOR_DEFAULTTONEAREST : MONITOR_DEFAULTTOPRIMARY);
}

#if LUMA_USE_NVAPI
bool HasUserHdrColorProfile(const DISPLAYCONFIG_PATH_INFO& path)
{
   using GetDisplayDefault = HRESULT(WINAPI*)(WCS_PROFILE_MANAGEMENT_SCOPE, LUID, UINT32, COLORPROFILETYPE, COLORPROFILESUBTYPE, LPWSTR*);
   using GetDisplayUserScope = HRESULT(WINAPI*)(LUID, UINT32, WCS_PROFILE_MANAGEMENT_SCOPE*);
   static const HMODULE MSCMS = LoadLibraryExW(L"mscms.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
   static const auto GET_DEFAULT = reinterpret_cast<GetDisplayDefault>(Modules::FindExport(MSCMS, "ColorProfileGetDisplayDefault"));
   static const auto GET_USER_SCOPE = reinterpret_cast<GetDisplayUserScope>(Modules::FindExport(MSCMS, "ColorProfileGetDisplayUserScope"));
   if (!GET_DEFAULT || !GET_USER_SCOPE)
      return false;
   WCS_PROFILE_MANAGEMENT_SCOPE scope = WCS_PROFILE_MANAGEMENT_SCOPE_SYSTEM_WIDE;
   if (FAILED(GET_USER_SCOPE(path.targetInfo.adapterId, path.sourceInfo.id, &scope)) || scope != WCS_PROFILE_MANAGEMENT_SCOPE_CURRENT_USER)
      return false;
   PWSTR profile_name = nullptr;
   const HRESULT result = GET_DEFAULT(WCS_PROFILE_MANAGEMENT_SCOPE_CURRENT_USER, path.targetInfo.adapterId, path.sourceInfo.id, CPT_ICC,
      CPST_EXTENDED_DISPLAY_COLOR_MODE, &profile_name);
   const bool has_profile = SUCCEEDED(result) && profile_name && profile_name[0] != L'\0';
   if (profile_name)
      LocalFree(profile_name);
   return has_profile;
}

bool EnsureNvapiInitialized()
{
   static const bool INITIALIZED = NvAPI_Initialize() == NVAPI_OK;
   return INITIALIZED;
}

NvU32 GetNvapiDisplayId(HMONITOR monitor)
{
   if (!monitor || !EnsureNvapiInitialized())
      return 0;
   MONITORINFOEXW monitor_info = {};
   monitor_info.cbSize = sizeof(monitor_info);
   if (!GetMonitorInfoW(monitor, &monitor_info))
      return 0;
   const std::string display_name = Utils::Strings::ToUtf8(monitor_info.szDevice);
   NvU32 display_id = 0;
   return NvAPI_DISP_GetDisplayIdByDisplayName(display_name.c_str(), &display_id) == NVAPI_OK ? display_id : 0;
}

bool GetNvapiHdrCapabilities(NvU32 display_id, NV_HDR_CAPABILITIES& capabilities)
{
   if (display_id == 0)
      return false;
   for (const NvU32 version : {NV_HDR_CAPABILITIES_VER3, NV_HDR_CAPABILITIES_VER2, NV_HDR_CAPABILITIES_VER1})
   {
      capabilities = {};
      capabilities.version = version;
      capabilities.driverExpandDefaultHdrParameters = 1;
      if (NvAPI_Disp_GetHdrCapabilities(display_id, &capabilities) == NVAPI_OK)
         return true;
   }
   return false;
}

void EngageHdr10PlusGaming(HMONITOR monitor, std::optional<float>& out_hdr10_plus_peak_nits)
{
   const NvU32 display_id = GetNvapiDisplayId(monitor);
   NV_HDR_CAPABILITIES capabilities = {};
   if (!GetNvapiHdrCapabilities(display_id, capabilities))
      return;
   if (capabilities.isHdr10PlusGamingSupported)
   {
      // HDR10+ Gaming index-to-nits mapping from Samsung VSVDB specification
      static constexpr std::array<float, 16> NIT_VALUES = {
         100.f, 200.f, 300.f, 400.f, 500.f, 600.f, 800.f, 1000.f, 1200.f, 1500.f, 2000.f, 2500.f, 3000.f, 4000.f, 6000.f, 8000.f};
      const size_t index = capabilities.hdr10plus_vsvdb.peak_luminance_index;
      if (index < NIT_VALUES.size() && NIT_VALUES[index] > 0.f)
         out_hdr10_plus_peak_nits = NIT_VALUES[index];
   }
   const NV_DISPLAY_OUTPUT_MODE target_mode =
      capabilities.isHdr10PlusGamingSupported ? NV_DISPLAY_OUTPUT_MODE_HDR10PLUS_GAMING : NV_DISPLAY_OUTPUT_MODE_HDR10;
   NV_DISPLAY_OUTPUT_MODE current_mode = NV_DISPLAY_OUTPUT_MODE_SDR;
   if (NvAPI_Disp_GetOutputMode(display_id, &current_mode) == NVAPI_OK && current_mode == target_mode)
      return;
   NV_DISPLAY_OUTPUT_MODE requested_mode = target_mode;
   const NvAPI_Status status = NvAPI_Disp_SetOutputMode(display_id, &requested_mode);
   if (status != NVAPI_OK && target_mode == NV_DISPLAY_OUTPUT_MODE_HDR10PLUS_GAMING && status != NVAPI_RESOURCE_IN_USE)
   {
      requested_mode = NV_DISPLAY_OUTPUT_MODE_HDR10;
      NvAPI_Disp_SetOutputMode(display_id, &requested_mode);
   }
}
#endif
} // namespace

Info Query(HWND window, IDXGIOutput* output)
{
   Info info;
   info.monitor = GetMonitor(window);
   const std::optional<DISPLAYCONFIG_PATH_INFO> path = FindPath(info.monitor);
   bool known = false;
   if (path)
   {
      // From Windows 11 24H2. WCG ("Advanced Color" in SDR) isn't HDR.
      if (DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 color = {}; GetDeviceInfo(*path, DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2, color))
      {
         info.hdr_enabled = color.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR;
         info.hdr_supported = info.hdr_enabled || (color.highDynamicRangeSupported && !color.advancedColorLimitedByPolicy);
         known = true;
      }
      // Wine and Proton only have this legacy query. On Windows 11 22H2-23H2 it reports SDR WCG ("Automatically Manage Colors") as
      // advanced color, so Windows leaves HDR detection to IDXGIOutput6::GetDesc1 when the 24H2 query isn't available.
      else if (DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO legacy = {}; GetDeviceInfo(*path, DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, legacy))
      {
         static const bool IS_WINE = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
         if (IS_WINE)
         {
            info.hdr_supported = legacy.advancedColorSupported && !legacy.advancedColorForceDisabled;
            info.hdr_enabled = legacy.advancedColorEnabled;
            known = true;
         }
      }
      if (DISPLAYCONFIG_SDR_WHITE_LEVEL white = {}; GetDeviceInfo(*path, DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, white))
         info.sdr_white_nits = float(white.SDRWhiteLevel) / 1000.f * SRGB_WHITE_NITS;
   }
   ComPtr<IDXGIOutput> resolved_output = output;
   if (!resolved_output)
      resolved_output = FindOutput(info.monitor);
   ComPtr<IDXGIOutput6> output6;
   DXGI_OUTPUT_DESC1 desc = {};
   if (resolved_output && SUCCEEDED(resolved_output->QueryInterface(IID_PPV_ARGS(&output6))) && SUCCEEDED(output6->GetDesc1(&desc)))
   {
      if (!known)
      {
         info.hdr_enabled = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 || desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
         info.hdr_supported |= info.hdr_enabled;
      }
      // SDR mode reports ~270 nits: only use MaxLuminance when it reflects an HDR peak
      if (desc.MaxLuminance >= MIN_PEAK_NITS)
         info.peak_nits = desc.MaxLuminance;
      info.min_nits = desc.MinLuminance;
      info.max_full_frame_nits = desc.MaxFullFrameLuminance > 0.f ? desc.MaxFullFrameLuminance : info.peak_nits;
   }
#if LUMA_USE_NVAPI
   // Engage HDR10+ Gaming when HDR is on, and use its VSVDB peak brightness unless the user calibrated a custom HDR ICC profile in Windows
   if (info.hdr_enabled)
   {
      std::optional<float> hdr10_plus_peak_nits;
      EngageHdr10PlusGaming(info.monitor, hdr10_plus_peak_nits);
      if (hdr10_plus_peak_nits && *hdr10_plus_peak_nits >= MIN_PEAK_NITS && (!path || !HasUserHdrColorProfile(*path)))
         info.peak_nits = *hdr10_plus_peak_nits;
   }
#endif
   return info;
}

bool HasChanged()
{
   const bool first = std::exchange(first_change_query, false);
   if (!first && factory && factory->IsCurrent())
      return false;
   EnsureFactory();
   return true;
}

bool SetHdrEnabled(HWND window, bool enabled)
{
   const std::optional<DISPLAYCONFIG_PATH_INFO> path = FindPath(GetMonitor(window));
   if (!path)
      return false;
   if (DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 color = {}; GetDeviceInfo(*path, DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2, color))
   {
      const bool is_enabled = color.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR;
      if (is_enabled == enabled)
         return true;
      if (!color.highDynamicRangeSupported || (enabled && color.advancedColorLimitedByPolicy))
         return false;
      DISPLAYCONFIG_SET_HDR_STATE state = {};
      state.header = {DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE, sizeof(state), color.header.adapterId, color.header.id};
      state.enableHdr = enabled;
      return DisplayConfigSetDeviceInfo(&state.header) == ERROR_SUCCESS;
   }
   // Before Windows 11 24H2 HDR and advanced color are one switch
   if (DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO legacy = {}; GetDeviceInfo(*path, DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, legacy))
   {
      if (bool(legacy.advancedColorEnabled) == enabled)
         return true;
      if (!legacy.advancedColorSupported || (enabled && legacy.advancedColorForceDisabled))
         return false;
      DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE state = {};
      state.header = {DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE, sizeof(state), legacy.header.adapterId, legacy.header.id};
      state.enableAdvancedColor = enabled;
      return DisplayConfigSetDeviceInfo(&state.header) == ERROR_SUCCESS;
   }
   return false;
}

bool SetSdrWhiteLevel(HWND window, float nits)
{
   const std::optional<DISPLAYCONFIG_PATH_INFO> path = FindPath(GetMonitor(window));
   if (!path)
      return false;
   DisplayConfigSetSdrWhiteLevel state = {};
   state.header = {DISPLAYCONFIG_DEVICE_INFO_SET_SDR_WHITE_LEVEL, sizeof(state), path->targetInfo.adapterId, path->targetInfo.id};
   state.sdr_white_level = static_cast<ULONG>(std::round(1000.f * std::clamp(nits, SRGB_WHITE_NITS, MAX_OS_SDR_WHITE_NITS) / SRGB_WHITE_NITS));
   state.final_value = TRUE;
   return DisplayConfigSetDeviceInfo(&state.header) == ERROR_SUCCESS;
}

std::string GetAdapterName(LUID luid)
{
   // A factory of its own: callers aren't on the presenting thread, which owns the shared one
   using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
   static const auto CREATE = reinterpret_cast<CreateFactory>(Modules::FindExport(Modules::FindSystemModule(L"dxgi.dll"), "CreateDXGIFactory1"));
   ComPtr<IDXGIFactory4> adapters;
   ComPtr<IDXGIAdapter1> adapter;
   DXGI_ADAPTER_DESC1 desc = {};
   if (!CREATE || FAILED(CREATE(IID_PPV_ARGS(&adapters))) || FAILED(adapters->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))) || FAILED(adapter->GetDesc1(&desc)))
      return "an unknown adapter";
   return std::format("{} (vendor 0x{:04X})", Utils::Strings::ToUtf8(desc.Description), desc.VendorId);
}
} // namespace Display
} // namespace System
} // namespace Luma
