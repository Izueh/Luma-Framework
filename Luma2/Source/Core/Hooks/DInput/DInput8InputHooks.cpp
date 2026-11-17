/*
 * Adapted from ReShade (https://github.com/crosire/reshade):
 *   source/windows/dinput8.cpp, Copyright (C) 2021 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Hooks/DInput/DInput8InputHooks.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <cstddef>
#include <cstring>
#include <type_traits>

#include "Hooks/DInput/DInputDevices.hpp"
#include "Hooks/Hooks.hpp"
#include "Hooks/InputHooks.hpp"
#include "Hooks/Scope.hpp"

namespace Luma {
namespace Hooks {
namespace Input {
namespace DInput8 {
namespace {
// From dinput.h, defined here so neither "dinput8.lib" nor "dxguid.lib" is linked
constexpr GUID IID_DIRECT_INPUT8_A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
constexpr GUID IID_DIRECT_INPUT8_W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};

using DirectInput8CreateFunction = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
DirectInput8CreateFunction original_direct_input8_create = nullptr;

constexpr size_t GET_DEVICE_STATE_INDEX = 9;
constexpr size_t GET_DEVICE_DATA_INDEX = 10;
constexpr size_t CREATE_DEVICE_INDEX = 3;

template <typename Device>
HRESULT STDMETHODCALLTYPE GetDeviceState(Device* device, DWORD data_size, LPVOID data)
{
   using Function = HRESULT(STDMETHODCALLTYPE*)(Device*, DWORD, LPVOID);
   const auto original = DInput::GetOriginal<Function>(device, GET_DEVICE_STATE_INDEX);
   if (!original)
      return DIERR_GENERIC;
   const HRESULT result = original(device, data_size, data);
   if (SUCCEEDED(result) && data && IsBlocking())
   {
      switch (DInput::GetDeviceKind(device))
      {
      case DInput::DeviceKind::Mouse:
         if (data_size == sizeof(DIMOUSESTATE) || data_size == sizeof(DIMOUSESTATE2))
         {
            // Only clear the button state, to prevent the camera resetting when the overlay is opened in RoadCraft
            std::memset(static_cast<LPBYTE>(data) + offsetof(DIMOUSESTATE, rgbButtons), 0, data_size - offsetof(DIMOUSESTATE, rgbButtons));
            // Luma change: that's for absolute axes. Relative ones (most games, e.g. FFXV) are deltas, zeroing them stops the camera.
            DIPROPDWORD axis_mode = {{sizeof(DIPROPDWORD), sizeof(DIPROPHEADER), 0, DIPH_DEVICE}};
            if (SUCCEEDED(device->GetProperty(DIPROP_AXISMODE, &axis_mode.diph)) && axis_mode.dwData == DIPROPAXISMODE_REL)
            {
               auto* mouse_state = static_cast<DIMOUSESTATE*>(data);
               mouse_state->lX = 0;
               mouse_state->lY = 0;
               mouse_state->lZ = 0;
            }
         }
         break;
      case DInput::DeviceKind::Keyboard:
         std::memset(data, 0, data_size);
         break;
      }
   }
   return result;
}

// This may be called a lot (e.g. in Dungeons & Dragons Online), so it stays cheap
template <typename Device>
HRESULT STDMETHODCALLTYPE GetDeviceData(Device* device, DWORD object_data_size, LPDIDEVICEOBJECTDATA object_data, LPDWORD in_out, DWORD flags)
{
   using Function = HRESULT(STDMETHODCALLTYPE*)(Device*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
   // The function isn't cached: doing so crashes the Steam overlay
   const auto original = DInput::GetOriginal<Function>(device, GET_DEVICE_DATA_INDEX);
   if (!original)
      return DIERR_GENERIC;
   HRESULT result = original(device, object_data_size, object_data, in_out, flags);
   if (SUCCEEDED(result) && (flags & DIGDD_PEEK) == 0 && object_data && in_out && *in_out != 0 && IsBlocking())
   {
      if (DInput::GetDeviceKind(device) != DInput::DeviceKind::Other)
      {
         *in_out = 0;
         result = DI_OK; // Overwrites a potential "DI_BUFFEROVERFLOW"
      }
   }
   return result;
}

template <typename DirectInput, typename Device>
HRESULT STDMETHODCALLTYPE CreateDevice(DirectInput* direct_input, REFGUID guid, Device** device, LPUNKNOWN outer)
{
   using Function = HRESULT(STDMETHODCALLTYPE*)(DirectInput*, REFGUID, Device**, LPUNKNOWN);
   const auto original = DInput::GetOriginal<Function>(direct_input, CREATE_DEVICE_INDEX);
   if (!original)
      return DIERR_GENERIC;
   const HRESULT result = original(direct_input, guid, device, outer);
   if (FAILED(result) || !device || !*device)
      return result;
   const DInput::DeviceKind kind = guid == DInput::GUID_SYS_MOUSE ? DInput::DeviceKind::Mouse : guid == DInput::GUID_SYS_KEYBOARD ? DInput::DeviceKind::Keyboard
                                                                                                                                  : DInput::DeviceKind::Other;
   DInput::SetDeviceKind(*device, kind);
   // Only install vtable hooks for mouse and keyboard devices, since others are not all proxied by the Steam overlay and thus would hook both Steam overlay and the original DirectInput entries
   if (kind == DInput::DeviceKind::Other)
      return result;
   constexpr bool is_wide = std::is_same_v<Device, IDirectInputDevice8W>;
   DInput::HookMethod(*device, GET_DEVICE_STATE_INDEX, reinterpret_cast<void*>(&GetDeviceState<Device>), is_wide ? "IDirectInputDevice8W::GetDeviceState" : "IDirectInputDevice8A::GetDeviceState");
   DInput::HookMethod(*device, GET_DEVICE_DATA_INDEX, reinterpret_cast<void*>(&GetDeviceData<Device>), is_wide ? "IDirectInputDevice8W::GetDeviceData" : "IDirectInputDevice8A::GetDeviceData");
   return result;
}

HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer)
{
   const HRESULT result = original_direct_input8_create(instance, version, iid, out, outer);
   if (FAILED(result) || !out || !*out || Scope::IsActive())
      return result;
   if (iid == IID_DIRECT_INPUT8_W)
      DInput::HookMethod(*out, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInput8W, IDirectInputDevice8W>), "IDirectInput8W::CreateDevice");
   else if (iid == IID_DIRECT_INPUT8_A)
      DInput::HookMethod(*out, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInput8A, IDirectInputDevice8A>), "IDirectInput8A::CreateDevice");
   return result;
}
} // namespace

void Install()
{
   const Hook hooks[] = {
      ExportHook(L"dinput8.dll", "DirectInput8Create", &DirectInput8Create, original_direct_input8_create),
   };
   Declare(hooks);
}
} // namespace DInput8
} // namespace Input
} // namespace Hooks
} // namespace Luma
