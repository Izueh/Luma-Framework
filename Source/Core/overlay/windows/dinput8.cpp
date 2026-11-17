/*
 * DirectInput 8 input blocking while Luma's overlay is open.
 *
 * Adapted from ReShade (https://github.com/crosire/reshade):
 *   source/windows/dinput8.cpp, Copyright (C) 2021 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Changes from ReShade: blocking follows whether the overlay is open, and Luma's own calls (inside a Luma scope) are never
 * blocked; the hooks go through Luma's installer on the "DirectInput8Create" export rather than a proxy dll, and the
 * methods are hooked by address (see "dinput_common.hpp"); the macros are templates; relative mouse axes are zeroed too,
 * not just the buttons (ReShade only clears the buttons, to prevent the camera resetting when the overlay opens in
 * RoadCraft, which uses absolute axes; Final Fantasy XV reads relative deltas from "GetDeviceState", so its camera kept
 * turning).
 */

#include "overlay/windows/dinput8.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <cstddef>
#include <cstring>
#include <type_traits>

#include "hooks/scope.hpp"
#include "overlay/export_hook.hpp"
#include "overlay/input_hooks.hpp"
#include "overlay/windows/dinput_common.hpp"

namespace Luma {
namespace Overlay {
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
   const auto original = DirectInput::GetOriginal<Function>(device, GET_DEVICE_STATE_INDEX);
   if (!original)
      return DIERR_GENERIC;
   const HRESULT result = original(device, data_size, data);
   if (SUCCEEDED(result) && data && InputHooks::IsBlocking())
   {
      switch (DirectInput::GetDeviceKind(device))
      {
      case DirectInput::DeviceKind::Mouse:
         if (data_size == sizeof(DIMOUSESTATE) || data_size == sizeof(DIMOUSESTATE2))
         {
            std::memset(static_cast<LPBYTE>(data) + offsetof(DIMOUSESTATE, rgbButtons), 0, data_size - offsetof(DIMOUSESTATE, rgbButtons));
            // Luma change: ReShade only clears the buttons, so the camera doesn't reset when the overlay opens (RoadCraft).
            // That's for absolute axes: relative ones (most games, e.g. FFXV) are deltas, zeroing them stops the camera.
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
      case DirectInput::DeviceKind::Keyboard:
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
   // Not cached: doing so crashes the Steam overlay
   const auto original = DirectInput::GetOriginal<Function>(device, GET_DEVICE_DATA_INDEX);
   if (!original)
      return DIERR_GENERIC;
   HRESULT result = original(device, object_data_size, object_data, in_out, flags);
   if (SUCCEEDED(result) && (flags & DIGDD_PEEK) == 0 && object_data && in_out && *in_out != 0 && InputHooks::IsBlocking())
   {
      if (DirectInput::GetDeviceKind(device) != DirectInput::DeviceKind::Other)
      {
         *in_out = 0;
         result = DI_OK; // Overwrites a potential "DI_BUFFEROVERFLOW"
      }
   }
   return result;
}

template <typename DirectInput8, typename Device>
HRESULT STDMETHODCALLTYPE CreateDevice(DirectInput8* direct_input, REFGUID guid, Device** device, LPUNKNOWN outer)
{
   using Function = HRESULT(STDMETHODCALLTYPE*)(DirectInput8*, REFGUID, Device**, LPUNKNOWN);
   const auto original = DirectInput::GetOriginal<Function>(direct_input, CREATE_DEVICE_INDEX);
   if (!original)
      return DIERR_GENERIC;
   const HRESULT result = original(direct_input, guid, device, outer);
   if (FAILED(result) || !device || !*device)
      return result;
   const DirectInput::DeviceKind kind = guid == DirectInput::GUID_SYS_MOUSE ? DirectInput::DeviceKind::Mouse :
      guid == DirectInput::GUID_SYS_KEYBOARD                                ? DirectInput::DeviceKind::Keyboard :
                                                                              DirectInput::DeviceKind::Other;
   DirectInput::SetDeviceKind(*device, kind);
   // Only install vtable hooks for mouse and keyboard devices, since others are not all proxied by the Steam overlay and thus would hook both Steam overlay and the original DirectInput entries
   if (kind == DirectInput::DeviceKind::Other)
      return result;
   constexpr bool is_wide = std::is_same_v<Device, IDirectInputDevice8W>;
   DirectInput::HookMethod(*device, GET_DEVICE_STATE_INDEX, reinterpret_cast<void*>(&GetDeviceState<Device>), is_wide ? "IDirectInputDevice8W::GetDeviceState" : "IDirectInputDevice8A::GetDeviceState");
   DirectInput::HookMethod(*device, GET_DEVICE_DATA_INDEX, reinterpret_cast<void*>(&GetDeviceData<Device>), is_wide ? "IDirectInputDevice8W::GetDeviceData" : "IDirectInputDevice8A::GetDeviceData");
   return result;
}

HRESULT WINAPI HookDirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer)
{
   const HRESULT result = original_direct_input8_create(instance, version, iid, out, outer);
   if (FAILED(result) || !out || !*out || Hooks::Scope::IsActive())
      return result;
   if (iid == IID_DIRECT_INPUT8_W)
      DirectInput::HookMethod(*out, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInput8W, IDirectInputDevice8W>), "IDirectInput8W::CreateDevice");
   else if (iid == IID_DIRECT_INPUT8_A)
      DirectInput::HookMethod(*out, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInput8A, IDirectInputDevice8A>), "IDirectInput8A::CreateDevice");
   return result;
}
} // namespace

void Install()
{
   const Hooks::Hook hooks[] = {
      MakeExportHook(L"dinput8.dll", "DirectInput8Create", &HookDirectInput8Create, original_direct_input8_create),
   };
   Hooks::Declare(hooks);
}
} // namespace DInput8
} // namespace Overlay
} // namespace Luma
