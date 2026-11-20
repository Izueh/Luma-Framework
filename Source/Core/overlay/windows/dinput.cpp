/*
 * DirectInput 1, 2 and 7 input blocking while Luma's overlay is open.
 *
 * Adapted from ReShade (https://github.com/crosire/reshade):
 *   source/windows/dinput.cpp, Copyright (C) 2022 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Changes from ReShade: blocking follows whether the overlay is open, and Luma's own calls (inside a Luma scope) are never
 * blocked; the hooks go through Luma's installer on the "DirectInputCreateA/W/Ex" exports rather than a proxy dll, and the
 * methods are hooked by address (see "dinput_common.hpp"); the log lines are reduced to the failures.
 */

// Set version to DirectInput 7
#define DIRECTINPUT_VERSION 0x0700

#include "overlay/windows/dinput.hpp"

#include <Windows.h>
#include <dinput.h>

#include <cstring>

#include "hooks/scope.hpp"
#include "overlay/export_hook.hpp"
#include "overlay/input_hooks.hpp"
#include "overlay/windows/dinput_common.hpp"
#include "utils/log.hpp"

namespace Luma {
namespace Overlay {
namespace DInput {
namespace {
// From dinput.h, defined here so "dxguid.lib" isn't linked
constexpr GUID IID_DIRECT_INPUT_A = {0x89521360, 0xAA8A, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID IID_DIRECT_INPUT_W = {0x89521361, 0xAA8A, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID IID_DIRECT_INPUT2_A = {0x5944E662, 0xAA8A, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID IID_DIRECT_INPUT2_W = {0x5944E663, 0xAA8A, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID IID_DIRECT_INPUT7_A = {0x9A4CB684, 0x236D, 0x11D3, {0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE}};
constexpr GUID IID_DIRECT_INPUT7_W = {0x9A4CB685, 0x236D, 0x11D3, {0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE}};

// It is technically possible to associate these hooks back to a device (cooperative level), but it may not be the same window as ReShade renders on
#define IDirectInputDevice_GetDeviceState_Impl(device_interface_version, encoding) \
	HRESULT STDMETHODCALLTYPE IDirectInputDevice##device_interface_version##encoding##_GetDeviceState(IDirectInputDevice##device_interface_version##encoding *pDevice, DWORD cbData, LPVOID lpvData) \
	{ \
		const auto original = DirectInput::GetOriginal(&IDirectInputDevice##device_interface_version##encoding##_GetDeviceState); \
		if (original == nullptr) \
			return DIERR_GENERIC; \
		const HRESULT hr = original(pDevice, cbData, lpvData); \
		if (SUCCEEDED(hr)) \
		{ \
			switch (DirectInput::GetDeviceKind(pDevice)) \
			{ \
			case DirectInput::DeviceKind::Mouse: \
			case DirectInput::DeviceKind::Keyboard: \
				if (InputHooks::IsBlocking()) \
					std::memset(lpvData, 0, cbData); \
				break; \
			} \
		} \
		return hr; \
	}

IDirectInputDevice_GetDeviceState_Impl( , A)
IDirectInputDevice_GetDeviceState_Impl( , W)
IDirectInputDevice_GetDeviceState_Impl(2, A)
IDirectInputDevice_GetDeviceState_Impl(2, W)
IDirectInputDevice_GetDeviceState_Impl(7, A)
IDirectInputDevice_GetDeviceState_Impl(7, W)

#define IDirectInputDevice_GetDeviceData_Impl(device_interface_version, encoding) \
	HRESULT STDMETHODCALLTYPE IDirectInputDevice##device_interface_version##encoding##_GetDeviceData(IDirectInputDevice##device_interface_version##encoding *pDevice, DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) \
	{ \
		const auto original = DirectInput::GetOriginal(&IDirectInputDevice##device_interface_version##encoding##_GetDeviceData); \
		if (original == nullptr) \
			return DIERR_GENERIC; \
		HRESULT hr = original(pDevice, cbObjectData, rgdod, pdwInOut, dwFlags); \
		if (SUCCEEDED(hr) && \
			(dwFlags & DIGDD_PEEK) == 0 && \
			(rgdod != nullptr && pdwInOut != nullptr && *pdwInOut != 0)) \
		{ \
			switch (DirectInput::GetDeviceKind(pDevice)) \
			{ \
			case DirectInput::DeviceKind::Mouse: \
			case DirectInput::DeviceKind::Keyboard: \
				if (InputHooks::IsBlocking()) \
				{ \
					*pdwInOut = 0; \
					hr = DI_OK; /* Overwrite potential 'DI_BUFFEROVERFLOW' */ \
				} \
				break; \
			} \
		} \
		return hr; \
	}

IDirectInputDevice_GetDeviceData_Impl( , A)
IDirectInputDevice_GetDeviceData_Impl( , W)
IDirectInputDevice_GetDeviceData_Impl(2, A)
IDirectInputDevice_GetDeviceData_Impl(2, W)
IDirectInputDevice_GetDeviceData_Impl(7, A)
IDirectInputDevice_GetDeviceData_Impl(7, W)

#define IDirectInput_CreateDevice_Impl(factory_interface_version, device_interface_version, encoding) \
	HRESULT STDMETHODCALLTYPE IDirectInput##factory_interface_version##encoding##_CreateDevice(IDirectInput##factory_interface_version##encoding *pDI, REFGUID rguid, LPDIRECTINPUTDEVICE##device_interface_version##encoding *lplpDirectInputDevice, LPUNKNOWN pUnkOuter) \
	{ \
		const auto original = DirectInput::GetOriginal(&IDirectInput##factory_interface_version##encoding##_CreateDevice); \
		if (original == nullptr) \
			return DIERR_GENERIC; \
		const HRESULT hr = original(pDI, rguid, lplpDirectInputDevice, pUnkOuter); \
		if (SUCCEEDED(hr)) \
		{ \
			const LPDIRECTINPUTDEVICE##device_interface_version##encoding device = *lplpDirectInputDevice; \
			\
			const DirectInput::DeviceKind device_type = \
				(rguid == DirectInput::GUID_SYS_MOUSE) ? DirectInput::DeviceKind::Mouse : \
				(rguid == DirectInput::GUID_SYS_KEYBOARD) ? DirectInput::DeviceKind::Keyboard : DirectInput::DeviceKind::Other; \
			DirectInput::SetDeviceKind(device, device_type); \
			\
			if (device_type != DirectInput::DeviceKind::Other) \
			{ \
				DirectInput::HookMethod(device, 9, reinterpret_cast<void *>(&IDirectInputDevice##device_interface_version##encoding##_GetDeviceState), "IDirectInputDevice" #device_interface_version #encoding "::GetDeviceState"); \
				DirectInput::HookMethod(device, 10, reinterpret_cast<void *>(&IDirectInputDevice##device_interface_version##encoding##_GetDeviceData), "IDirectInputDevice" #device_interface_version #encoding "::GetDeviceData"); \
			} \
		} \
		else \
		{ \
			Utils::Log::Warning("IDirectInput" #factory_interface_version #encoding "::CreateDevice failed with error code 0x{:08X}.", static_cast<unsigned int>(hr)); \
		} \
		return hr; \
	}
#define IDirectInput_CreateDeviceEx_Impl(factory_interface_version, device_interface_version, encoding) \
	HRESULT STDMETHODCALLTYPE IDirectInput##factory_interface_version##encoding##_CreateDeviceEx(IDirectInput##factory_interface_version##encoding *pDI, REFGUID rguid, REFGUID riid, LPVOID *ppvOut, LPUNKNOWN pUnkOuter) \
	{ \
		const auto original = DirectInput::GetOriginal(&IDirectInput##factory_interface_version##encoding##_CreateDeviceEx); \
		if (original == nullptr) \
			return DIERR_GENERIC; \
		const HRESULT hr = original(pDI, rguid, riid, ppvOut, pUnkOuter); \
		if (SUCCEEDED(hr)) \
		{ \
			const LPDIRECTINPUTDEVICE##device_interface_version##encoding device = static_cast<LPDIRECTINPUTDEVICE##device_interface_version##encoding>(*ppvOut); \
			\
			const DirectInput::DeviceKind device_type = \
				(rguid == DirectInput::GUID_SYS_MOUSE) ? DirectInput::DeviceKind::Mouse : \
				(rguid == DirectInput::GUID_SYS_KEYBOARD) ? DirectInput::DeviceKind::Keyboard : DirectInput::DeviceKind::Other; \
			DirectInput::SetDeviceKind(device, device_type); \
			\
			if (device_type != DirectInput::DeviceKind::Other) \
			{ \
				DirectInput::HookMethod(device, 9, reinterpret_cast<void *>(&IDirectInputDevice##device_interface_version##encoding##_GetDeviceState), "IDirectInputDevice" #device_interface_version #encoding "::GetDeviceState"); \
				DirectInput::HookMethod(device, 10, reinterpret_cast<void *>(&IDirectInputDevice##device_interface_version##encoding##_GetDeviceData), "IDirectInputDevice" #device_interface_version #encoding "::GetDeviceData"); \
			} \
		} \
		else \
		{ \
			Utils::Log::Warning("IDirectInput" #factory_interface_version #encoding "::CreateDeviceEx failed with error code 0x{:08X}.", static_cast<unsigned int>(hr)); \
		} \
		return hr; \
	}

IDirectInput_CreateDevice_Impl( ,  , A)
IDirectInput_CreateDevice_Impl( ,  , W)
IDirectInput_CreateDevice_Impl(2, 2, A)
IDirectInput_CreateDevice_Impl(2, 2, W)
IDirectInput_CreateDevice_Impl(7, 7, A)
IDirectInput_CreateDevice_Impl(7, 7, W)
IDirectInput_CreateDeviceEx_Impl(7, 7, A)
IDirectInput_CreateDeviceEx_Impl(7, 7, W)

using DirectInputCreateAFunction = HRESULT(WINAPI *)(HINSTANCE, DWORD, LPDIRECTINPUTA *, LPUNKNOWN);
using DirectInputCreateWFunction = HRESULT(WINAPI *)(HINSTANCE, DWORD, LPDIRECTINPUTW *, LPUNKNOWN);
using DirectInputCreateExFunction = HRESULT(WINAPI *)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);

DirectInputCreateAFunction original_direct_input_create_a = nullptr;
DirectInputCreateWFunction original_direct_input_create_w = nullptr;
DirectInputCreateExFunction original_direct_input_create_ex = nullptr;

HRESULT WINAPI HookDirectInputCreateA(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA *ppDI, LPUNKNOWN punkOuter)
{
	const HRESULT hr = original_direct_input_create_a(hinst, dwVersion, ppDI, punkOuter);
	if (SUCCEEDED(hr))
	{
		if (ppDI != nullptr && *ppDI != nullptr && !Hooks::Scope::IsActive())
			DirectInput::HookMethod(*ppDI, 3, reinterpret_cast<void *>(&IDirectInputA_CreateDevice), "IDirectInputA::CreateDevice");
	}
	else
	{
		Utils::Log::Warning("DirectInputCreateA failed with error code 0x{:08X}.", static_cast<unsigned int>(hr));
	}

	return hr;
}

HRESULT WINAPI HookDirectInputCreateW(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTW *ppDI, LPUNKNOWN punkOuter)
{
	const HRESULT hr = original_direct_input_create_w(hinst, dwVersion, ppDI, punkOuter);
	if (SUCCEEDED(hr))
	{
		if (ppDI != nullptr && *ppDI != nullptr && !Hooks::Scope::IsActive())
			DirectInput::HookMethod(*ppDI, 3, reinterpret_cast<void *>(&IDirectInputW_CreateDevice), "IDirectInputW::CreateDevice");
	}
	else
	{
		Utils::Log::Warning("DirectInputCreateW failed with error code 0x{:08X}.", static_cast<unsigned int>(hr));
	}

	return hr;
}

HRESULT WINAPI HookDirectInputCreateEx(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID *ppvOut, LPUNKNOWN punkOuter)
{
	const HRESULT hr = original_direct_input_create_ex(hinst, dwVersion, riidltf, ppvOut, punkOuter);
	if (FAILED(hr))
	{
		Utils::Log::Warning("DirectInputCreateEx failed with error code 0x{:08X}.", static_cast<unsigned int>(hr));
		return hr;
	}
	if (ppvOut == nullptr || *ppvOut == nullptr || Hooks::Scope::IsActive())
		return hr;

	IUnknown *const factory = static_cast<IUnknown *>(*ppvOut);

	if (riidltf == IID_DIRECT_INPUT_A)
		DirectInput::HookMethod(factory, 3, reinterpret_cast<void *>(&IDirectInputA_CreateDevice), "IDirectInputA::CreateDevice");
	if (riidltf == IID_DIRECT_INPUT_W)
		DirectInput::HookMethod(factory, 3, reinterpret_cast<void *>(&IDirectInputW_CreateDevice), "IDirectInputW::CreateDevice");
	if (riidltf == IID_DIRECT_INPUT2_A)
		DirectInput::HookMethod(factory, 3, reinterpret_cast<void *>(&IDirectInput2A_CreateDevice), "IDirectInput2A::CreateDevice");
	if (riidltf == IID_DIRECT_INPUT2_W)
		DirectInput::HookMethod(factory, 3, reinterpret_cast<void *>(&IDirectInput2W_CreateDevice), "IDirectInput2W::CreateDevice");

	if (riidltf == IID_DIRECT_INPUT7_A)
	{
		DirectInput::HookMethod(factory, 3, reinterpret_cast<void *>(&IDirectInput7A_CreateDevice), "IDirectInput7A::CreateDevice");
		DirectInput::HookMethod(factory, 9, reinterpret_cast<void *>(&IDirectInput7A_CreateDeviceEx), "IDirectInput7A::CreateDeviceEx");
	}
	if (riidltf == IID_DIRECT_INPUT7_W)
	{
		DirectInput::HookMethod(factory, 3, reinterpret_cast<void *>(&IDirectInput7W_CreateDevice), "IDirectInput7W::CreateDevice");
		DirectInput::HookMethod(factory, 9, reinterpret_cast<void *>(&IDirectInput7W_CreateDeviceEx), "IDirectInput7W::CreateDeviceEx");
	}

	return hr;
}
} // namespace

void Install()
{
   const Hooks::Hook hooks[] = {
      MakeExportHook(L"dinput.dll", "DirectInputCreateA", &HookDirectInputCreateA, original_direct_input_create_a),
      MakeExportHook(L"dinput.dll", "DirectInputCreateW", &HookDirectInputCreateW, original_direct_input_create_w),
      MakeExportHook(L"dinput.dll", "DirectInputCreateEx", &HookDirectInputCreateEx, original_direct_input_create_ex),
   };
   Hooks::Declare(hooks);
}
} // namespace DInput
} // namespace Overlay
} // namespace Luma
