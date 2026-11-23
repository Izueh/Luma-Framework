#include "System/MonitorDisplay.hpp"

#include <optional>
#include <vector>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include "System/Modules.hpp"

using Microsoft::WRL::ComPtr;

namespace Luma {
namespace System {
namespace Display {
namespace {
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
         static const bool is_wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
         if (is_wine)
         {
            info.hdr_supported = legacy.advancedColorSupported && !legacy.advancedColorForceDisabled;
            info.hdr_enabled = legacy.advancedColorEnabled;
            known = true;
         }
      }
      if (DISPLAYCONFIG_SDR_WHITE_LEVEL white = {}; GetDeviceInfo(*path, DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, white))
         info.sdr_white_nits = float(white.SDRWhiteLevel) / 1000.f * SRGB_WHITE_NITS;
   }
   ComPtr<IDXGIOutput6> output6;
   DXGI_OUTPUT_DESC1 desc = {};
   if (output && SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output6))) && SUCCEEDED(output6->GetDesc1(&desc)))
   {
      if (!known)
      {
         info.hdr_enabled = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 || desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
         info.hdr_supported |= info.hdr_enabled;
      }
      // Windows' calibration, kept between SDR and HDR. It can be stale after a display change until the next query.
      if (desc.MaxLuminance > 0.f)
         info.peak_nits = desc.MaxLuminance;
      info.min_nits = desc.MinLuminance;
      info.max_full_frame_nits = desc.MaxFullFrameLuminance > 0.f ? desc.MaxFullFrameLuminance : info.peak_nits;
   }
   return info;
}

bool HasChanged()
{
   // Luma's own factory, from the system's dxgi.dll: the game's may be a proxy's
   static ComPtr<IDXGIFactory1> factory;
   if (factory && factory->IsCurrent())
      return false;
   using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
   static const auto create = reinterpret_cast<CreateFactory>(Modules::FindExport(Modules::FindSystemModule(L"dxgi.dll"), "CreateDXGIFactory1"));
   factory.Reset();
   if (create)
      create(IID_PPV_ARGS(&factory));
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
} // namespace Display
} // namespace System
} // namespace Luma
