/*
 * Input blocking while Luma's overlay is open: user32's window messages, cursor, key states, raw input and Windows hooks.
 *
 * Adapted from ReShade (https://github.com/crosire/reshade):
 *   source/input_windows.cpp, Copyright (C) 2014 Patrick Mours
 *   source/input.cpp, Copyright (C) 2014 Patrick Mours ("register_window_with_raw_input")
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Changes from ReShade: there is one overlay window instead of per window input objects, so blocking simply follows
 * whether the overlay is open (the mouse, keyboard and cursor warping are blocked together), and Luma's own calls (inside
 * a Luma scope, e.g. ImGui's backend) are never blocked; the hooks go through Luma's installer instead of ReShade's hook
 * manager; messages are handled by "Input::HandleMessage" (where ReShade's "input::handle_window_message" is adapted);
 * "GetMessage" calls the original instead of being reimplemented on "PeekMessage"; raw input registrations are tracked
 * per usage, so "RIDEV_REMOVE" and a later registration without "RIDEV_NOLEGACY" are seen, and ones without a target
 * window are kept too.
 */

#include "overlay/input_windows.hpp"

#include <Windows.h>
#include <concurrent_unordered_map.h>

#include <cassert>
#include <cstring> // std::memset
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "overlay/export_hook.hpp"
#include "overlay/input.hpp"
#include "hooks/scope.hpp"
#include "overlay/input_hooks.hpp"
#include "utils/log.hpp"

namespace Luma {
namespace Overlay {
namespace InputWindows {
namespace {
std::shared_mutex s_windows_mutex;
std::unordered_map<HWND, unsigned int> s_raw_input_windows; // The null window has the registrations without a target
RECT s_last_clip_cursor = {};
POINT s_last_cursor_position = {};

decltype(&::PeekMessageA) original_peek_message_a = nullptr;
decltype(&::PeekMessageW) original_peek_message_w = nullptr;
decltype(&::GetMessageA) original_get_message_a = nullptr;
decltype(&::GetMessageW) original_get_message_w = nullptr;
decltype(&::PostMessageA) original_post_message_a = nullptr;
decltype(&::PostMessageW) original_post_message_w = nullptr;
decltype(&::RegisterRawInputDevices) original_register_raw_input_devices = nullptr;
decltype(&::ClipCursor) original_clip_cursor = nullptr;
decltype(&::SetCursorPos) original_set_cursor_pos = nullptr;
decltype(&::GetCursorPos) original_get_cursor_pos = nullptr;
decltype(&::GetKeyState) original_get_key_state = nullptr;
decltype(&::GetAsyncKeyState) original_get_async_key_state = nullptr;
decltype(&::GetKeyboardState) original_get_keyboard_state = nullptr;
decltype(&::GetRawInputBuffer) original_get_raw_input_buffer = nullptr;
decltype(&::SetWindowsHookExA) original_set_windows_hook_ex_a = nullptr;
decltype(&::SetWindowsHookExW) original_set_windows_hook_ex_w = nullptr;
decltype(&::UnhookWindowsHookEx) original_unhook_windows_hook_ex = nullptr;

BOOL WINAPI HookGetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
	const BOOL result = original_get_message_a(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
	if (result < 0) // If there is an error, the return value is negative (https://docs.microsoft.com/windows/win32/api/winuser/nf-winuser-getmessage)
		return result;

	assert(lpMsg != nullptr);

	if (lpMsg->hwnd != nullptr && Input::HandleMessage(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
	{
		// We still want 'WM_CHAR' messages, so translate message
		TranslateMessage(lpMsg);

		// Change message so it is ignored by the recipient window
		lpMsg->message = WM_NULL;
	}

	return result;
}
BOOL WINAPI HookGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
	const BOOL result = original_get_message_w(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
	if (result < 0)
		return result;

	assert(lpMsg != nullptr);

	if (lpMsg->hwnd != nullptr && Input::HandleMessage(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
	{
		// We still want 'WM_CHAR' messages, so translate message
		TranslateMessage(lpMsg);

		// Change message so it is ignored by the recipient window
		lpMsg->message = WM_NULL;
	}

	return result;
}
BOOL WINAPI HookPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
	if (!original_peek_message_a(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg) || lpMsg == nullptr)
		return FALSE;

	if (lpMsg->hwnd != nullptr && (wRemoveMsg & PM_REMOVE) != 0 && Input::HandleMessage(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
	{
		// We still want 'WM_CHAR' messages, so translate message
		TranslateMessage(lpMsg);

		// Change message so it is ignored by the recipient window
		lpMsg->message = WM_NULL;
	}

	return TRUE;
}
BOOL WINAPI HookPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
	if (!original_peek_message_w(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg) || lpMsg == nullptr)
		return FALSE;

	if (lpMsg->hwnd != nullptr && (wRemoveMsg & PM_REMOVE) != 0 && Input::HandleMessage(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
	{
		// We still want 'WM_CHAR' messages, so translate message
		TranslateMessage(lpMsg);

		// Change message so it is ignored by the recipient window
		lpMsg->message = WM_NULL;
	}

	return TRUE;
}

BOOL WINAPI HookPostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	// Do not allow mouse movement simulation while we block input
	if (InputHooks::IsBlocking() && Msg == WM_MOUSEMOVE)
		return TRUE;

	return original_post_message_a(hWnd, Msg, wParam, lParam);
}
BOOL WINAPI HookPostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (InputHooks::IsBlocking() && Msg == WM_MOUSEMOVE)
		return TRUE;

	return original_post_message_w(hWnd, Msg, wParam, lParam);
}

// Sets the bits of `mask` to the matching ones of `flags`
void register_window_with_raw_input(HWND window, unsigned int mask, unsigned int flags)
{
	const std::unique_lock<std::shared_mutex> lock(s_windows_mutex);

	const auto insert = s_raw_input_windows.emplace(window, 0u);

	insert.first->second = (insert.first->second & ~mask) | flags;
}

BOOL WINAPI HookRegisterRawInputDevices(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize)
{
	for (UINT i = 0; pRawInputDevices != nullptr && i < uiNumDevices; ++i)
	{
		const RAWINPUTDEVICE &device = pRawInputDevices[i];

		if (device.usUsagePage != 1)
			continue;

		const unsigned int mask = device.usUsage == 0x06 ? RAW_INPUT_NO_LEGACY_KEYBOARD : device.usUsage == 0x02 ? RAW_INPUT_NO_LEGACY_MOUSE : 0u;
		const unsigned int flags = (device.dwFlags & (RIDEV_NOLEGACY | RIDEV_REMOVE)) == RIDEV_NOLEGACY ? mask : 0u;

		if ((device.dwFlags & RIDEV_REMOVE) != 0)
		{
			// Removals have no target window: the usage is no longer registered for any
			const std::unique_lock<std::shared_mutex> lock(s_windows_mutex);

			for (auto &window : s_raw_input_windows)
				window.second &= ~mask;
		}
		else if (device.hwndTarget != nullptr || mask != 0)
		{
			register_window_with_raw_input(device.hwndTarget, mask, flags);
		}
	}

	if (!original_register_raw_input_devices(pRawInputDevices, uiNumDevices, cbSize))
	{
		Utils::Log::Warning("RegisterRawInputDevices failed with error code {}.", GetLastError());
		return FALSE;
	}

	return TRUE;
}

BOOL WINAPI HookClipCursor(const RECT *lpRect)
{
	s_last_clip_cursor = (lpRect != nullptr) ? *lpRect : RECT {};

	if (InputHooks::IsBlocking())
		// Some applications clip the mouse cursor, so disable that while we want full control over mouse input
		lpRect = nullptr;

	return original_clip_cursor(lpRect);
}

BOOL WINAPI HookSetCursorPosition(int X, int Y)
{
	s_last_cursor_position.x = X;
	s_last_cursor_position.y = Y;

	if (InputHooks::IsBlocking())
		return TRUE;

	return original_set_cursor_pos(X, Y);
}
BOOL WINAPI HookGetCursorPosition(LPPOINT lpPoint)
{
	if (InputHooks::IsBlocking())
	{
		assert(lpPoint != nullptr);

		*lpPoint = s_last_cursor_position;

		return TRUE;
	}

	const BOOL result = original_get_cursor_pos(lpPoint);
	if (result)
	{
		assert(lpPoint != nullptr);

		// Update position in case it is not overridden via 'SetCursorPos'
		s_last_cursor_position = *lpPoint;
	}

	return result;
}

SHORT WINAPI HookGetKeyState(int vKey)
{
	// Valid keyboard keys are between 8 and 255
	if ((vKey & 0xF8) != 0)
	{
		if (InputHooks::IsBlocking())
			return 0;
	}
	else if (vKey >= VK_LBUTTON && vKey <= VK_XBUTTON2)
	{
		// Some games use this API for mouse buttons
		if (InputHooks::IsBlocking())
			return 0;
	}

	return original_get_key_state(vKey);
}
SHORT WINAPI HookGetAsyncKeyState(int vKey)
{
	// Valid keyboard keys are between 8 and 255
	if ((vKey & 0xF8) != 0)
	{
		if (InputHooks::IsBlocking())
			return 0;
	}
	else if (vKey >= VK_LBUTTON && vKey <= VK_XBUTTON2)
	{
		// Some games use this API for mouse buttons
		if (InputHooks::IsBlocking())
			return 0;
	}

	return original_get_async_key_state(vKey);
}
BOOL WINAPI HookGetKeyboardState(PBYTE lpKeyState)
{
	const BOOL result = original_get_keyboard_state(lpKeyState);
	if (result)
	{
		if (InputHooks::IsBlocking())
			std::memset(lpKeyState, 0, 7); // Mouse buttons
		if (InputHooks::IsBlocking())
			std::memset(lpKeyState + 7, 0, 256 - 7); // Keyboard keys
	}

	return result;
}

// This variant of raw input does not use 'WM_INPUT' messages, so a hook is necessary to block these inputs
UINT WINAPI HookGetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader)
{
	const UINT result = original_get_raw_input_buffer(pData, pcbSize, cbSizeHeader);
	// This is a high throughput API (i.e. 8 kHz mouse polling), so need a fast path to exit
	if (result == static_cast<UINT>(-1) || pData == nullptr || *pcbSize == 0 || !InputHooks::IsBlocking())
		return result;

	using QWORD = UINT64;

	for (UINT i = 0; i < result; ++i, pData = NEXTRAWINPUTBLOCK(pData))
	{
		switch (pData->header.dwType)
		{
		case RIM_TYPEMOUSE:
			pData->header.hDevice = nullptr;
			pData->header.wParam = RIM_INPUTSINK;

			// Zero mouse data, to ensure ignore input is ignored even if 'RIM_INPUTSINK' is not respected
			std::memset(&pData->data.mouse, 0, pData->header.dwSize - sizeof(RAWINPUTHEADER));
			break;
		case RIM_TYPEKEYBOARD:
			// Supplying an invalid device will early-out SDL before it calls HID APIs to try and get an input report that we don't want it to see
			pData->header.hDevice = nullptr;
			// Most engines will honor the input sink state
			pData->header.wParam = RIM_INPUTSINK;

			if (0 == (pData->data.keyboard.Flags & RI_KEY_BREAK))
			{
				// Fake a key up message
				pData->data.keyboard.Flags |= RI_KEY_BREAK;
				pData->data.keyboard.VKey = 0;
			}
			break;
		}
	}

	return result;
}

concurrency::concurrent_unordered_map<UINT64, std::pair<HHOOK, HOOKPROC>> s_windows_hooks;

template <DWORD hook_id>
LRESULT CALLBACK handle_windows_hook(int nCode, WPARAM wParam, LPARAM lParam)
{
	if ((nCode == HC_ACTION || nCode == HC_NOREMOVE) && !InputHooks::IsBlocking())
	{
		const UINT64 global_lookup_key = (static_cast<UINT64>(hook_id) << 32);
		const UINT64 thread_lookup_key = GetCurrentThreadId() | global_lookup_key;

		// Look up original hook function for the current thread and hook type
		if (const auto it = s_windows_hooks.find(thread_lookup_key);
			it != s_windows_hooks.end() && it->second.second != nullptr)
		{
			const HOOKPROC orig_hook_proc = it->second.second;
			// A non-zero result value prevents the system from passing the message to the target window procedure
			// In that case our 'GetMessage' and 'PeekMessage' hooks will not receive them, so ignore that result and continue with the hook chain instead below
			if (orig_hook_proc(nCode, wParam, lParam) == 0)
				return 0;
		}

		// Alternatively look up global hook function for this hook type (registered for thread ID zero)
		if (const auto it = s_windows_hooks.find(global_lookup_key);
			it != s_windows_hooks.end() && it->second.second != nullptr)
		{
			const HOOKPROC orig_hook_proc = it->second.second;
			if (orig_hook_proc(nCode, wParam, lParam) == 0)
				return 0;
		}
	}

	// Bypass the original hook function and continue with the hook chain
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

HHOOK WINAPI HookSetWindowsHookExA(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId)
{
	// Unhooking: a wrapper would keep Windows calling into Luma's code
	if (Luma::Hooks::Scope::IsBypassed())
		return original_set_windows_hook_ex_a(idHook, lpfn, hmod, dwThreadId);
	HOOKPROC orig_hook_proc = lpfn;
	const UINT64 thread_lookup_key = dwThreadId | (static_cast<UINT64>(idHook) << 32);

	if (const auto it = s_windows_hooks.find(thread_lookup_key);
		it == s_windows_hooks.end() || it->second.first == nullptr)
	{
		switch (idHook)
		{
		case WH_MOUSE:
			lpfn = &handle_windows_hook<WH_MOUSE>;
			break;
		case WH_MOUSE_LL:
			lpfn = &handle_windows_hook<WH_MOUSE_LL>;
			break;
		case WH_KEYBOARD:
			lpfn = &handle_windows_hook<WH_KEYBOARD>;
			break;
		case WH_KEYBOARD_LL:
			lpfn = &handle_windows_hook<WH_KEYBOARD_LL>;
			break;
		}
	}
	else
	{
		Utils::Log::Verbose("Windows hook function of type {} was already registered for thread {}.", idHook, dwThreadId);
	}

	const HHOOK result = original_set_windows_hook_ex_a(idHook, lpfn, hmod, dwThreadId);

	if (result != nullptr && lpfn != orig_hook_proc)
	{
		Utils::Log::Verbose("Windows hook function of type {} registered for thread {}.", idHook, dwThreadId);

		s_windows_hooks[thread_lookup_key] = std::make_pair(result, orig_hook_proc);
	}

	return result;
}
HHOOK WINAPI HookSetWindowsHookExW(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId)
{
	// Unhooking: a wrapper would keep Windows calling into Luma's code
	if (Luma::Hooks::Scope::IsBypassed())
		return original_set_windows_hook_ex_w(idHook, lpfn, hmod, dwThreadId);
	HOOKPROC orig_hook_proc = lpfn;
	const UINT64 thread_lookup_key = dwThreadId | (static_cast<UINT64>(idHook) << 32);

	if (const auto it = s_windows_hooks.find(thread_lookup_key);
		it == s_windows_hooks.end() || it->second.first == nullptr)
	{
		switch (idHook)
		{
		case WH_MOUSE:
			lpfn = &handle_windows_hook<WH_MOUSE>;
			break;
		case WH_MOUSE_LL:
			lpfn = &handle_windows_hook<WH_MOUSE_LL>;
			break;
		case WH_KEYBOARD:
			lpfn = &handle_windows_hook<WH_KEYBOARD>;
			break;
		case WH_KEYBOARD_LL:
			lpfn = &handle_windows_hook<WH_KEYBOARD_LL>;
			break;
		}
	}
	else
	{
		Utils::Log::Verbose("Windows hook function of type {} was already registered for thread {}.", idHook, dwThreadId);
	}

	const HHOOK result = original_set_windows_hook_ex_w(idHook, lpfn, hmod, dwThreadId);

	if (result != nullptr && lpfn != orig_hook_proc)
	{
		Utils::Log::Verbose("Windows hook function of type {} registered for thread {}.", idHook, dwThreadId);

		s_windows_hooks[thread_lookup_key] = std::make_pair(result, orig_hook_proc);
	}

	return result;
}
BOOL WINAPI HookUnhookWindowsHookEx(HHOOK hhk)
{
	for (const std::pair<const UINT64, std::pair<HHOOK, HOOKPROC>> &hook : s_windows_hooks)
	{
		if (hook.second.first == hhk)
		{
			Utils::Log::Verbose("Windows hook function of type {} unregistered for thread {}.", static_cast<int>(hook.first >> 32), static_cast<DWORD>(hook.first & 0xFFFFFFFF));

			s_windows_hooks[hook.first] = std::make_pair(nullptr, nullptr);
			break;
		}
	}

	return original_unhook_windows_hook_ex(hhk);
}
} // namespace

void Install()
{
	const Hooks::Hook hooks[] = {
		MakeExportHook(L"user32.dll", "PeekMessageA", &HookPeekMessageA, original_peek_message_a),
		MakeExportHook(L"user32.dll", "PeekMessageW", &HookPeekMessageW, original_peek_message_w),
		MakeExportHook(L"user32.dll", "GetMessageA", &HookGetMessageA, original_get_message_a),
		MakeExportHook(L"user32.dll", "GetMessageW", &HookGetMessageW, original_get_message_w),
		MakeExportHook(L"user32.dll", "PostMessageA", &HookPostMessageA, original_post_message_a),
		MakeExportHook(L"user32.dll", "PostMessageW", &HookPostMessageW, original_post_message_w),
		MakeExportHook(L"user32.dll", "RegisterRawInputDevices", &HookRegisterRawInputDevices, original_register_raw_input_devices),
		MakeExportHook(L"user32.dll", "ClipCursor", &HookClipCursor, original_clip_cursor),
		MakeExportHook(L"user32.dll", "SetCursorPos", &HookSetCursorPosition, original_set_cursor_pos),
		MakeExportHook(L"user32.dll", "GetCursorPos", &HookGetCursorPosition, original_get_cursor_pos),
		MakeExportHook(L"user32.dll", "GetKeyState", &HookGetKeyState, original_get_key_state),
		MakeExportHook(L"user32.dll", "GetAsyncKeyState", &HookGetAsyncKeyState, original_get_async_key_state),
		MakeExportHook(L"user32.dll", "GetKeyboardState", &HookGetKeyboardState, original_get_keyboard_state),
		MakeExportHook(L"user32.dll", "GetRawInputBuffer", &HookGetRawInputBuffer, original_get_raw_input_buffer),
		MakeExportHook(L"user32.dll", "SetWindowsHookExA", &HookSetWindowsHookExA, original_set_windows_hook_ex_a),
		MakeExportHook(L"user32.dll", "SetWindowsHookExW", &HookSetWindowsHookExW, original_set_windows_hook_ex_w),
		MakeExportHook(L"user32.dll", "UnhookWindowsHookEx", &HookUnhookWindowsHookEx, original_unhook_windows_hook_ex),
	};
	Hooks::Declare(hooks);
}

void OnOpenChanged(bool open)
{
	// ReShade's "input::block_mouse_cursor_warping"
	if (!original_clip_cursor)
		return;
	// Some games setup ClipCursor with a tiny area which could make the cursor stay in that area instead of the whole window
	if (open)
	{
		original_clip_cursor(nullptr);
	}
	else if ((s_last_clip_cursor.right - s_last_clip_cursor.left) != 0 && (s_last_clip_cursor.bottom - s_last_clip_cursor.top) != 0)
	{
		// Restore previous clipping rectangle when not blocking mouse input
		original_clip_cursor(&s_last_clip_cursor);
	}
}

bool IsRawInputWindow(HWND window)
{
	if (window == nullptr)
		return false;

	const std::shared_lock<std::shared_mutex> lock(s_windows_mutex);

	return s_raw_input_windows.contains(window);
}

unsigned int GetRawInputFlags(HWND window)
{
	const std::shared_lock<std::shared_mutex> lock(s_windows_mutex);

	unsigned int flags = 0;
	for (const HWND key : {window, HWND(nullptr)})
		if (const auto it = s_raw_input_windows.find(key); it != s_raw_input_windows.end())
			flags |= it->second;
	return flags;
}
} // namespace InputWindows
} // namespace Overlay
} // namespace Luma
