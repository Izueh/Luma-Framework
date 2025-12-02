#pragma once

#include "globals.h"

#include <Windows.h>
#include <string>
#include <fstream>

// DEFINE_NAME_AS_STRING
#define _STRINGIZE(x) _STRINGIZE2(x)
// DEFINE_VALUE_AS_STRING
#define _STRINGIZE2(x) #x

#if DEVELOPMENT || TEST
#define PUBLISHING_CONSTEXPR
#else
#define PUBLISHING_CONSTEXPR constexpr
#endif

// In non debug builds, replace asserts with a message box
#if defined(NDEBUG) && (DEVELOPMENT || TEST)
#define ASSERT(expression) ((void)(                                                       \
            (!!(expression)) ||                                                           \
            (MessageBoxA(NULL, "Assertion failed: " #expression "\nFile: " __FILE__ "\nLine: " _STRINGIZE(__LINE__), Globals::MOD_NAME, MB_SETFOREGROUND | MB_OK))) \
        )
#undef assert
#define assert(expression) ASSERT(expression)
#else
#define ASSERT(expression) assert(expression)
#endif

#if DEVELOPMENT || TEST || _DEBUG
// "do while" is to avoid some edge cases with indentation
#define ASSERT_MSG(expression, msg)                                     \
    do {                                                                \
        if (!(expression)) {                                            \
            std::string full_msg = std::string("Assertion failed:\n\n") \
                + #expression + "\n\n"                                  \
                + msg + "\n\n"                                          \
                + "File: " + __FILE__ + "\n"                            \
                + "Line: " + std::to_string(__LINE__) + "\n\n"          \
                + "Press Yes to break into debugger.\n"                 \
                + "Press No to continue.";                              \
                                                                        \
            int result = MessageBoxA(nullptr,                           \
                full_msg.c_str(),                                       \
                "Assertion Failed",                                     \
                MB_ICONERROR | MB_YESNO);                               \
                                                                        \
            if (result == IDYES) { __debugbreak(); }                    \
        }                                                               \
    } while (false)
#define ASSERT_ONCE(expression) do { { static bool asserted_once = false; \
if (!asserted_once && !(expression)) { ASSERT(expression); asserted_once = true; } } } while (false)
#define ASSERT_ONCE_MSG(expression, msg) do { { static bool asserted_once = false; \
if (!asserted_once && !(expression)) { ASSERT_MSG(expression, msg); asserted_once = true; } } } while (false)

#else
#define ASSERT_MSG(expression, msg) ((void)0)
#define ASSERT_ONCE(expression) ((void)0)
#define ASSERT_ONCE_MSG(expression, msg) ((void)0)
#endif

namespace
{
#if DEVELOPMENT || _DEBUG
   // Returns true if it vaguely succeeded (definition of success in unclear)
   bool LaunchDebugger(const char* name, const DWORD unique_random_handle = 0)
   {
#if 0 // Non stopping optional debugger
      // Get System directory, typically c:\windows\system32
      std::wstring systemDir(MAX_PATH + 1, '\0');
      UINT nChars = GetSystemDirectoryW(&systemDir[0], systemDir.length());
      if (nChars == 0) return false; // failed to get system directory
      systemDir.resize(nChars);

      // Get process ID and create the command line
      DWORD pid = GetCurrentProcessId();
      std::wostringstream s;
      s << systemDir << L"\\vsjitdebugger.exe -p " << pid;
      std::wstring cmdLine = s.str();

      // Start debugger process
      STARTUPINFOW si;
      ZeroMemory(&si, sizeof(si));
      si.cb = sizeof(si);

      PROCESS_INFORMATION pi;
      ZeroMemory(&pi, sizeof(pi));

      if (!CreateProcessW(NULL, &cmdLine[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return false;

      // Close debugger process handles to eliminate resource leak
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
#else // Stop execution until the debugger is attached or skipped

#if 1
		// Note: the process ID is unique within a session, but not across sessions so it could repeat itself (though unlikely), we currently have no better solution to have a unique identifier unique across dll loads and process runs
		DWORD hProcessId = unique_random_handle != 0 ? unique_random_handle : GetCurrentProcessId();
      std::ifstream fileRead("Luma-Debug-Cache"); // Implies "Globals::MOD_NAME"
      if (fileRead)
      {
         DWORD hProcessIdRead;
         fileRead >> hProcessIdRead;
         fileRead.close();
         if (hProcessIdRead == hProcessId)
         {
            return true;
         }
      }

      if (!IsDebuggerPresent())
      {
			// TODO: Add a way to skip this dialog for x minutes or until we change compilation mode. Maybe we should only show it if the build was made with debug symbols/information, however there's no way to know at runtime AFAIK
			auto ret = MessageBoxA(NULL, "Loaded. You can now attach the debugger or continue execution (press \"Yes\").\nPress \"No\" to skip this message for this session.\nPress \"Cancel\" to close the application.", name, MB_SETFOREGROUND | MB_YESNOCANCEL);
         if (ret == IDABORT || ret == IDCANCEL)
         {
            exit(0);
         }
         // Write a file on disk so we can avoid re-opening the debugger dialog (which can be annoying) if a program loaded and unloaded multiple times in a row (it can happen on boot)
         // It'd be nice to delete this file when luma closes, but that's not possible as it closes many times.
         else if (ret == IDNO)
         {
            std::ofstream fileWrite("Luma-Debug-Cache"); // Implies "Globals::MOD_NAME"
            if (fileWrite)
            {
               fileWrite << hProcessId;
               fileWrite.close();
            }
         }
      }
#else
      // Wait for the debugger to attach
      while (!IsDebuggerPresent()) Sleep(100);
#endif

#endif

#if 0
      // Stop execution so the debugger can take over
      DebugBreak();
#endif

      return true;
   }
#endif // DEVELOPMENT || _DEBUG
}

// A macro wraper for the assert macro.
// Example usage            : ensure(device->CreateTexture2D(&desc, nullptr, &tex), == S_OK);
// In debug it expands to   : assert(device->CreateTexture2D(&desc, nullptr, &tex) == S_OK);
// In release it expands to : device->CreateTexture2D(&desc, nullptr, &tex);
#ifndef _DEBUG
#define ensure(always_keep, discard_if_ndebug) always_keep
#else
#define ensure(always_keep, discard_if_ndebug) (assert(always_keep discard_if_ndebug))
#endif