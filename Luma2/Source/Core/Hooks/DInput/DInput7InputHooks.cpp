/*
 * Adapted from ReShade (https://github.com/crosire/reshade):
 *   source/windows/dinput.cpp, Copyright (C) 2022 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Set version to DirectInput 7
#define DIRECTINPUT_VERSION 0x0700

#include "Hooks/DInput/DInput7InputHooks.hpp"

#include <Windows.h>
#include <dinput.h>

#include <cstring>
#include <string>

#include "Hooks/DInput/DInputDevices.hpp"
#include "Hooks/HookManager.hpp"
#include "Hooks/InputHooks.hpp"
#include "Hooks/HookScope.hpp"
#include "Log/Log.hpp"

namespace Luma {
namespace Hooks {
namespace Input {
namespace DInput7 {
namespace {
// From dinput.h, defined here so "dxguid.lib" isn't linked
constexpr GUID IID_DIRECT_INPUT_A = {0x89521360, 0xAA8A, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID IID_DIRECT_INPUT_W = {0x89521361, 0xAA8A, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID IID_DIRECT_INPUT2_A = {0x5944E662, 0xAA8A, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID IID_DIRECT_INPUT2_W = {0x5944E663, 0xAA8A, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID IID_DIRECT_INPUT7_A = {0x9A4CB684, 0x236D, 0x11D3, {0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE}};
constexpr GUID IID_DIRECT_INPUT7_W = {0x9A4CB685, 0x236D, 0x11D3, {0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE}};

constexpr size_t CREATE_DEVICE_INDEX = 3;
constexpr size_t CREATE_DEVICE_EX_INDEX = 9;
constexpr size_t GET_DEVICE_STATE_INDEX = 9;
constexpr size_t GET_DEVICE_DATA_INDEX = 10;

using DirectInputCreateAFunction = HRESULT(WINAPI*)(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);
using DirectInputCreateWFunction = HRESULT(WINAPI*)(HINSTANCE, DWORD, LPDIRECTINPUTW*, LPUNKNOWN);
using DirectInputCreateExFunction = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

DirectInputCreateAFunction original_direct_input_create_a = nullptr;
DirectInputCreateWFunction original_direct_input_create_w = nullptr;
DirectInputCreateExFunction original_direct_input_create_ex = nullptr;

template <typename Device>
struct DeviceName;

template <>
struct DeviceName<IDirectInputDeviceA>
{
   static constexpr const char* VALUE = "IDirectInputDeviceA";
};

template <>
struct DeviceName<IDirectInputDeviceW>
{
   static constexpr const char* VALUE = "IDirectInputDeviceW";
};

template <>
struct DeviceName<IDirectInputDevice2A>
{
   static constexpr const char* VALUE = "IDirectInputDevice2A";
};

template <>
struct DeviceName<IDirectInputDevice2W>
{
   static constexpr const char* VALUE = "IDirectInputDevice2W";
};

template <>
struct DeviceName<IDirectInputDevice7A>
{
   static constexpr const char* VALUE = "IDirectInputDevice7A";
};

template <>
struct DeviceName<IDirectInputDevice7W>
{
   static constexpr const char* VALUE = "IDirectInputDevice7W";
};

// It is technically possible to associate these hooks back to a device (cooperative level), but it may not be the same window as the overlay is drawn on
template <typename Device>
HRESULT STDMETHODCALLTYPE GetDeviceState(Device* device, DWORD data_size, LPVOID data)
{
   const auto original = DInput::GetOriginal(&GetDeviceState<Device>);
   if (!original)
      return DIERR_GENERIC;
   const HRESULT hr = original(device, data_size, data);
   if (SUCCEEDED(hr) && data)
   {
      switch (DInput::GetDeviceKind(device))
      {
      case DInput::DeviceKind::Mouse:
      case DInput::DeviceKind::Keyboard:
         if (IsBlocking())
            std::memset(data, 0, data_size);
         break;
      }
   }
   return hr;
}

template <typename Device>
HRESULT STDMETHODCALLTYPE GetDeviceData(Device* device, DWORD object_data_size, LPDIDEVICEOBJECTDATA object_data, LPDWORD in_out, DWORD flags)
{
   const auto original = DInput::GetOriginal(&GetDeviceData<Device>);
   if (!original)
      return DIERR_GENERIC;
   HRESULT hr = original(device, object_data_size, object_data, in_out, flags);
   if (SUCCEEDED(hr) && (flags & DIGDD_PEEK) == 0 && (object_data != nullptr && in_out != nullptr && *in_out != 0))
   {
      switch (DInput::GetDeviceKind(device))
      {
      case DInput::DeviceKind::Mouse:
      case DInput::DeviceKind::Keyboard:
         if (IsBlocking())
         {
            *in_out = 0;
            hr = DI_OK; // Overwrite potential "DI_BUFFEROVERFLOW"
         }
         break;
      }
   }
   return hr;
}

template <typename Device>
void HookDevice(Device* device, REFGUID guid)
{
   const DInput::DeviceKind kind = guid == DInput::GUID_SYS_MOUSE ? DInput::DeviceKind::Mouse : guid == DInput::GUID_SYS_KEYBOARD ? DInput::DeviceKind::Keyboard
                                                                                                                                  : DInput::DeviceKind::Other;
   DInput::SetDeviceKind(device, kind);
   if (kind == DInput::DeviceKind::Other)
      return;
   const std::string name = DeviceName<Device>::VALUE;
   DInput::HookMethod(device, GET_DEVICE_STATE_INDEX, reinterpret_cast<void*>(&GetDeviceState<Device>), (name + "::GetDeviceState").c_str());
   DInput::HookMethod(device, GET_DEVICE_DATA_INDEX, reinterpret_cast<void*>(&GetDeviceData<Device>), (name + "::GetDeviceData").c_str());
}

template <typename DirectInput, typename Device>
HRESULT STDMETHODCALLTYPE CreateDevice(DirectInput* direct_input, REFGUID guid, Device** device, LPUNKNOWN outer)
{
   const auto original = DInput::GetOriginal(&CreateDevice<DirectInput, Device>);
   if (!original)
      return DIERR_GENERIC;
   const HRESULT hr = original(direct_input, guid, device, outer);
   if (SUCCEEDED(hr))
   {
      if (device && *device)
         HookDevice(*device, guid);
   }
   else
   {
      Log::Warning("Hooks: IDirectInput::CreateDevice failed with error code 0x{:08X}", static_cast<unsigned int>(hr));
   }
   return hr;
}

template <typename DirectInput, typename Device>
HRESULT STDMETHODCALLTYPE CreateDeviceEx(DirectInput* direct_input, REFGUID guid, REFGUID iid, LPVOID* out, LPUNKNOWN outer)
{
   const auto original = DInput::GetOriginal(&CreateDeviceEx<DirectInput, Device>);
   if (!original)
      return DIERR_GENERIC;
   const HRESULT hr = original(direct_input, guid, iid, out, outer);
   if (SUCCEEDED(hr))
   {
      if (out && *out)
         HookDevice(static_cast<Device*>(*out), guid);
   }
   else
   {
      Log::Warning("Hooks: IDirectInput7::CreateDeviceEx failed with error code 0x{:08X}", static_cast<unsigned int>(hr));
   }
   return hr;
}

void HookFactory(const void* factory, REFIID iid)
{
   if (iid == IID_DIRECT_INPUT_A)
      DInput::HookMethod(factory, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInputA, IDirectInputDeviceA>), "IDirectInputA::CreateDevice");
   if (iid == IID_DIRECT_INPUT_W)
      DInput::HookMethod(factory, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInputW, IDirectInputDeviceW>), "IDirectInputW::CreateDevice");
   if (iid == IID_DIRECT_INPUT2_A)
      DInput::HookMethod(factory, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInput2A, IDirectInputDevice2A>), "IDirectInput2A::CreateDevice");
   if (iid == IID_DIRECT_INPUT2_W)
      DInput::HookMethod(factory, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInput2W, IDirectInputDevice2W>), "IDirectInput2W::CreateDevice");

   if (iid == IID_DIRECT_INPUT7_A)
   {
      DInput::HookMethod(factory, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInput7A, IDirectInputDevice7A>), "IDirectInput7A::CreateDevice");
      DInput::HookMethod(factory, CREATE_DEVICE_EX_INDEX, reinterpret_cast<void*>(&CreateDeviceEx<IDirectInput7A, IDirectInputDevice7A>), "IDirectInput7A::CreateDeviceEx");
   }
   if (iid == IID_DIRECT_INPUT7_W)
   {
      DInput::HookMethod(factory, CREATE_DEVICE_INDEX, reinterpret_cast<void*>(&CreateDevice<IDirectInput7W, IDirectInputDevice7W>), "IDirectInput7W::CreateDevice");
      DInput::HookMethod(factory, CREATE_DEVICE_EX_INDEX, reinterpret_cast<void*>(&CreateDeviceEx<IDirectInput7W, IDirectInputDevice7W>), "IDirectInput7W::CreateDeviceEx");
   }
}

HRESULT WINAPI DirectInputCreateA(HINSTANCE instance, DWORD version, LPDIRECTINPUTA* out, LPUNKNOWN outer)
{
   const HRESULT hr = original_direct_input_create_a(instance, version, out, outer);
   if (SUCCEEDED(hr))
   {
      if (out && *out && !Scope::IsActive())
         HookFactory(*out, IID_DIRECT_INPUT_A);
   }
   else
   {
      Log::Warning("Hooks: DirectInputCreateA failed with error code 0x{:08X}", static_cast<unsigned int>(hr));
   }
   return hr;
}

HRESULT WINAPI DirectInputCreateW(HINSTANCE instance, DWORD version, LPDIRECTINPUTW* out, LPUNKNOWN outer)
{
   const HRESULT hr = original_direct_input_create_w(instance, version, out, outer);
   if (SUCCEEDED(hr))
   {
      if (out && *out && !Scope::IsActive())
         HookFactory(*out, IID_DIRECT_INPUT_W);
   }
   else
   {
      Log::Warning("Hooks: DirectInputCreateW failed with error code 0x{:08X}", static_cast<unsigned int>(hr));
   }
   return hr;
}

HRESULT WINAPI DirectInputCreateEx(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer)
{
   const HRESULT hr = original_direct_input_create_ex(instance, version, iid, out, outer);
   if (FAILED(hr))
   {
      Log::Warning("Hooks: DirectInputCreateEx failed with error code 0x{:08X}", static_cast<unsigned int>(hr));
      return hr;
   }
   if (out && *out && !Scope::IsActive())
      HookFactory(*out, iid);
   return hr;
}
} // namespace

void Install()
{
   const Hook hooks[] = {
      ExportHook(L"dinput.dll", "DirectInputCreateA", &DirectInputCreateA, original_direct_input_create_a),
      ExportHook(L"dinput.dll", "DirectInputCreateW", &DirectInputCreateW, original_direct_input_create_w),
      ExportHook(L"dinput.dll", "DirectInputCreateEx", &DirectInputCreateEx, original_direct_input_create_ex),
   };
   Declare(hooks);
}
} // namespace DInput7
} // namespace Input
} // namespace Hooks
} // namespace Luma
